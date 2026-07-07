# metl — Audit Findings & Backlog (2026-07-07)

Read-only correctness audit. Verified baseline: clean build, **ctest 45/45**, all 51
headers compile standalone, umbrella `metl.hpp` complete. The library is solid on the
**default configuration**; most High-severity exposure is *conditional* on a user
installing a returning assert handler — one fix neutralizes that whole class.

## Design Principles (library) — governing constraint

metl has no UI, so "usability" = **API clarity & contract honesty**:
- **No silent surprises** — an operation either does the obvious `std::`-like thing or
  fails loudly; never silently truncate/mislabel (see `flat_map::operator[]`, `fixed_string`).
- **Contracts are documented and enforced** — every precondition asserts by default and
  the assert path is UB-safe.
- **Don't claim what isn't real** — `constexpr`/`noexcept`/"benchmarks"/"TSAN clean" must
  be true, not aspirational.
- **Zero-dependency, header-only, deterministic** stays inviolable.

## Section A — Correctness findings

**Two cross-cutting facts drive most findings:**
- **① assert handler is not `[[noreturn]]`** (`assert.hpp:7`, `set_assert_handler` `:40`).
  Default handler aborts (safe), but a user-installed *returning* handler turns every
  checked precondition library-wide into fall-through UB. **Fix (highest leverage): mark
  the handler type `[[noreturn]]` or `std::abort()`/`__builtin_unreachable()` at call sites.**
  ✅ **DONE** — `detail::assertion_failed`/`detail::panic_failed`/`panic` are now
  `[[noreturn]]` and unconditionally `std::abort()` after invoking the customization-point
  handler, so control can never continue past a failed assert even with a returning handler.
- **② TSAN CI exercises zero concurrency** (`.github/workflows/ci.yml:94`) — no test spawns
  a thread, examples default OFF. `spsc_queue`/`atomic_ref`/`static_message_queue` ordering
  is never validated; a regression would pass green.
  ✅ **DONE** — added `tests/spsc_queue_threaded_test.cpp`, a bounded/deterministic
  producer-consumer + atomic_ref stress test that builds and runs in every config
  (including the TSAN job).

| Sev | Issue | Location |
|---|---|---|
| HIGH ✅ DONE | `function_ref` non-`explicit` ctor binds rvalue temporaries → dangling (P0792 deletes this) — now lvalue-only ctor + deleted rvalue overload | `function_ref.hpp:37` |
| HIGH ✅ DONE | `scheduler::run_once` not reentrancy-safe: a task detaching during `poll` shifts the vector → stale index OOB — now snapshots the attached set and skips detached tasks (`is_attached`) | `coro/scheduler.hpp:103` |
| HIGH ✅ DONE | `static_unordered_map::emplace` doesn't check existing key → duplicate double-constructs + `++size_` twice — now finds-existing-first (no-op, no overwrite) | `static_unordered_map.hpp:374` |
| HIGH ✅ DONE | full-table `emplace`/`operator[]` reach `construct_at(npos,…)` wild OOB if handler returns — `construct_at` now hard-guards `index < bucket_count` | `static_unordered_map.hpp:527` |
| MED ✅ DONE | `variant`/`expected` assignment destroys active member then constructs w/o rollback → double-destroy if ctor throws — now exception-safe (valueless window / reinit-into-temp / swap rollback) | `expected.hpp:545,342`, `variant.hpp:396,297` |
| MED ✅ DONE | `variant` comparisons use `get<T>` by type → fail to compile for duplicate alt types — now compare by index | `variant.hpp:649` |
| MED ✅ DONE (documented) | `flat_map/set::operator[]`/`at` are **positional** index accessors, not key lookups (opposite of `std::map`) — documented + `nth()` alias added; signatures unchanged (no break) | `flat_map.hpp:113` |
| MED ✅ DONE (overflow) | `fixed_string(const char*)` silently yields empty string on overflow (discards `assign` failure); non-`explicit` — now asserts on overflow (still non-`explicit`) | `fixed_string.hpp:25` |
| MED ✅ DONE | `mmio_ptr(uintptr_t)` constexpr ctor always `reinterpret_cast`s → IFNDR; no alignment enforcement (UB) — dropped `constexpr`, added alignment static_assert (register) + runtime assert (ptr) | `mmio.hpp:47,21` |
| MED ✅ DONE | `arena_allocator`/`static_allocator` size math can integer-overflow **before** the bounds check → OOB — now overflow-safe (subtractive checks / division guard) | `arena_allocator.hpp:101`, `static_allocator.hpp:27` |
| MED ✅ DONE | `fnv1a_hash` hashes raw object representation (padding/pointers) → breaks hash/equality invariant — now `static_assert(has_unique_object_representations)` | `hash.hpp:107` |
| MED ✅ DONE | `fixed_function::operator()` is `const` but `const_cast`s storage → UB mutating a const instance — storage is now `mutable` (well-defined) | `fixed_function.hpp:267` |
| MED ✅ DONE | `intrusive_ptr` destroys via CRTP base cast → non-virtual base of deeper hierarchy = UB/leak — now `static_assert(final || has_virtual_destructor)` | `intrusive_ptr.hpp:85` |
| MED ✅ DONE (documented) | `static_message_queue` filed under "Concurrency" but uses plain non-atomic indices — single-threaded only — documented as single-threaded/non-ISR-safe FIFO | `static_message_queue.hpp:158` |
| MED ✅ DONE | `fsm::dispatch` updates state *after* action → reentrant dispatch re-fires same transition — now commits state before the action | `fsm.hpp:61` |
| LOW ✅ DONE (optional) / documented (rest) | Pervasive non-functional `constexpr` labels (placement-new/launder not constant-evaluable in C++17) — `optional` is now GENUINELY constexpr on C++20 via a union + `metl/detail/construct.hpp` (`construct_at`/`destroy_at`, constexpr since C++20; placement-new fallback on C++17, behavior unchanged); the remaining laundered-storage types carry an honest source note + are deferred (see below) | optional/expected/variant/fixed_vector/flat_map |
| — | **Clean:** `spsc_queue` fences correct, `intrusive_ptr` refcount ordering correct, hash probes bounded (no infinite loop), bit/bitfield/crc all correct | — |

### `constexpr` honesty (Section A LOW)

Placement-`new` and `std::launder`/`reinterpret_cast` are **never** usable in
constant evaluation (C++17 *or* C++20), so a `constexpr` label on a
constructor/accessor that routes through them is non-functional — the type
cannot actually be constant-initialized. `std::construct_at`/`std::destroy_at`,
by contrast, are `constexpr` since C++20.

- **New helper** `metl/detail/construct.hpp` — `metl::detail::construct_at` /
  `destroy_at` forward to the `std::` facilities (constant-evaluable) under
  C++20 and fall back to placement-new / explicit destructor on C++17
  (byte-for-byte the previous behavior). `METL_CONSTEXPR20` expands to
  `constexpr` only when that C++20 path is active. Wired into the umbrella and
  the self-containment checks.
- **`optional` is now genuinely constexpr on C++20** — its storage is a union
  (active member named directly, no launder) and its lifetime runs through the
  helper. `constexpr metl::optional<int> o{42}; static_assert(*o == 42);` is a
  real constant expression on a C++20 toolchain; on C++17 the code is unchanged
  (same size/alignment, same placement-new path). Proven by
  `tests/optional_constexpr_test.cpp`, whose `static_assert`s are compiled only
  under `#if __cplusplus >= 202002L` (a no-op on the C++17 CI matrix) while its
  runtime smoke runs everywhere. The `const`-qualified `value()` overloads were
  also made `constexpr` (they were previously unlabeled).
- **`expected` / `variant` / `fixed_vector` / `flat_map` / `flat_set` —
  documented, deferred.** Each carries an inline source note that its
  laundered-storage `constexpr` labels are effective only outside constant
  evaluation. A genuine conversion needs a union-of-alternatives rewrite that
  also has to reconcile with delicate paths (`expected`'s exception-safe
  reinit/swap, `variant`'s recursive union + visitation, `fixed_vector`'s ASan
  tail-poisoning, `flat_map`/`flat_set` built atop that). That was judged too
  risky to land reliably-green across the full cross/sanitizer matrix in this
  pass (no local gcc to validate C++17 codegen), so it is deferred rather than
  half-done. The helper is in place to make each migration mechanical later.

## Section C — Hardening & codegen (abseil-derived techniques applied)

Portable, empty-fallback macro layers modeled on abseil, plus their
applications. All gated on `__has_cpp_attribute`/`__has_attribute`/`__has_*` so
they are honored or no-ops — never a build break — and gcc/MSVC-clean by
construction.

- ✅ **Attribute layer** `metl/attributes.hpp` (abseil `attributes.h`) —
  consolidates `METL_NODISCARD`; adds `METL_NORETURN`, `METL_ALWAYS_INLINE`,
  `METL_MAYBE_UNUSED`, `METL_DEPRECATED`, `METL_LIFETIME_BOUND`,
  `METL_CONST_INIT`, `METL_ATTRIBUTE_TRIVIAL_ABI`.
- ✅ **Optimization layer** `metl/optimization.hpp` (abseil `optimization.h`) —
  `METL_PREDICT_TRUE/FALSE`, `METL_ASSUME`, `METL_CACHELINE_SIZE`,
  `METL_CACHELINE_ALIGNED`.
- ✅ **Feature detection** in `compiler.hpp` (abseil `config.h`) —
  `METL_HAVE_BUILTIN/FEATURE/INCLUDE`.
- ✅ **`METL_LIFETIME_BOUND`** on `function_ref` (callable) and `span`
  (container/array) constructors — clang diagnoses a view outliving its
  referent, complementing the deleted rvalue-binding overloads.
- ✅ **`METL_CONST_INIT`** on the assert/panic handler storage — makes constant
  initialization explicit; guards against a future static-init-order hazard.
- ✅ **`METL_ATTRIBUTE_TRIVIAL_ABI`** on `intrusive_ptr` — register-passed and
  callee-destroyed like a raw pointer; behavior unchanged (verified under
  ASan/UBSan).
- ✅ **`METL_PREDICT_FALSE`** on the failed-`METL_ASSERT` branch;
  **`METL_CACHELINE_ALIGNED`** replaces the hand-rolled `alignas(64)` in
  `spsc_queue` (identical layout).
- ✅ **`METL_DASSERT`** — debug-only DCHECK alongside the always-on, hardened
  `METL_ASSERT`. Existing `METL_ASSERT` sites are unchanged (never downgraded).
- ✅ **ASan tail poisoning for `fixed_vector`** (à la `absl::InlinedVector`) —
  the unused-capacity tail `[size(), capacity())` is poisoned so OOB past
  `size()` is trapped; unpoison-during-mutation / re-poison-tail-on-exit, and the
  destructor unpoisons everything so no stale poison outlives the storage. Gated
  on ASan; a `constexpr`-safe no-op otherwise. Covered by
  `tests/fixed_vector_asan_test.cpp` (boundary asserts + a forked OOB death test).

## Section D — Embedded & environment validation (CI)

Portability is a load-bearing claim for an embedded library, so the CI matrix now
validates it directly instead of trusting a single host+arch. All new cross jobs
mirror the existing `arm-cross` pattern (checkout@v5 → apt install → configure with a
`cmake/` toolchain file → build → size) and are compile-only except the one
link+run job. Existing jobs are unchanged.

- ✅ **RISC-V freestanding compile** (`riscv-cross`) — `cmake/riscv-none-elf.cmake`
  (mirrors `arm-none-eabi.cmake`) builds `metl_embedded_smoke` with the bare-metal
  newlib RISC-V GNU toolchain (`gcc-riscv64-unknown-elf`) for **rv32imac** (ilp32)
  and **rv64** (lp64) via the `METL_RISCV_ARCH` option. Compile + `size` only.
- ✅ **Second-frontend ARM** (`arm-cross-clang`) — clang `--target=arm-none-eabi
  -mcpu=cortex-m4 -ffreestanding -fsyntax-only` over the full public-header smoke
  TU, reusing the freestanding libstdc++ headers from `gcc-arm-none-eabi` (their
  search paths queried from the GCC driver, handed to clang as `-isystem`). Proves a
  second compiler frontend accepts the headers for bare-metal ARM.
- ✅ **Big-endian** (`big-endian`) — `powerpc64-linux-gnu` (a big-endian target whose
  GCC defines `__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__`) syntax-checks every header
  big-endian and **builds + runs** `endian_test` under `qemu-user`, whose new
  byte-representation assertions fail loudly if `endian::native` were mis-detected.
- ✅ **Real embedded libc** — two jobs prove metl links (and runs) on a bare-metal
  libc, not just compiles:
  - `newlib-link` — links `tests/embedded/semihost_smoke.cpp` (a `main()` exercising
    optional/fixed_vector/expected/crc32/endian/fixed_string) against **newlib-nano**
    (`--specs=nano.specs --specs=nosys.specs`) for Cortex-M3. Link + `size`, no run.
    Guaranteed-green baseline / fallback for the picolibc run below.
  - 🟡 `picolibc-qemu` — links the same program against **picolibc**
    (`--specs=picolibc.specs --oslib=semihost`, `tests/embedded/mps2-an385.ld`) and
    **RUNS** it under `qemu-system-arm -semihosting`, asserting a `METL_SEMIHOST_PASS`
    sentinel (task #7 — the one place we go beyond compile/link). picolibc + libstdc++
    linking is version-sensitive; **may need CI iteration** (spec-file flag naming,
    package availability). If it proves fragile, `newlib-link` remains the green
    libc-link proof and picolibc+QEMU can be deferred.

### Code-size visibility (arm-cross)

The `arm-cross` job now additionally builds the embedded smoke library in
`MinSizeRel` (`-Os`, NDEBUG) and prints `arm-none-eabi-size` for each Cortex-M
CPU, so the optimized binary size shows up in the CI log on every build. It is
informational (no hard threshold yet) — the baseline to watch for size
regressions. The existing default-build size step is unchanged.

### `endian.hpp` hardening (byte-order detection)

The big-endian job confirmed the *primary* detection path is already correct: every
supported cross toolchain (arm-none-eabi, riscv*-elf, powerpc64) defines
`__BYTE_ORDER__`, so the existing `__ORDER_BIG_ENDIAN__` branch resolves
`endian::native = big` correctly and the new representation test passes big-endian.
The latent bug was the **fallback**: when `__BYTE_ORDER__` was undefined the header
*silently assumed little-endian*, which would miscompile `to_/from_*_endian` on an
undetected big-endian target. Fixed: the `#else` no longer guesses — it adds a chain
of well-known secondary endianness macros (`__BIG_ENDIAN__`, `__ARMEB__`,
`__AARCH64EB__`, `__MIPSEB__`, … and LE counterparts) and, if none resolve, stops the
build with an actionable `#error` instead of silently assuming LE. `_WIN32` remains a
fast path (Windows is LE-only). All CI hosts and cross targets define one of the
recognized signals, so host CI stays green.

### Compile-time cost trimming (best-effort)

Surveyed the heavy standard headers pulled in by public headers (`<functional>`,
`<memory>`, `<variant>`):

- **Trimmed:** `function_ref.hpp` dropped `<memory>` — it was included only for
  `std::addressof` in one constructor, now a one-line
  `metl::detail::function_ref_addressof` (`__builtin_addressof`, with a
  fallback). No API/behavior change.
- **`<variant>`:** never included (metl::variant is self-implemented) — nothing
  to trim.
- **Deliberately left:** the `<functional>` includes in `hash`, `optional`,
  `flat_map`/`flat_set`, `static_unordered_map`/`set`, and `intrusive_ptr` back
  `std::hash` / `std::equal_to` / `std::less`, which are genuinely used (and for
  the map/set default template parameters are part of the public type). Trimming
  them would mean substituting a metl-local comparator/hasher, i.e. an API
  change to the default `Compare`/`Hash`/`KeyEqual` types — out of scope for a
  no-behavior-change pass. `detail/construct.hpp` pulls `<memory>` only under
  C++20 (for `std::construct_at`), never on the C++17 surface.

## Section B — Backlog

**P0 — harness correctness (gates everything)**
1. ✅ **DONE** — Test assertion/reporting layer — tests signal only via exit codes; failures don't say *where*. Added header-only `tests/metl_check.hpp` with `CHECK`/`CHECK_EQ` printing `file:line: CHECK failed: …`; `fixed_vector_test` and `optional_test` migrated as a demonstration.
2. ✅ **DONE** — Real multi-threaded tests for concurrency types, gated into the TSAN job (fact ②). Added `tests/spsc_queue_threaded_test.cpp` (spsc_queue + atomic_ref).
3. ✅ **DONE** — Per-header self-containment compile check + umbrella-completeness check in CI. Added the `metl_header_self_contained` target + `cmake/CheckUmbrella.cmake` CTest guard and a `header-checks` CI job.

**P1 — CI/quality**
- ✅ **DONE** — Release `-Werror` gate. A `Release` build with `-Werror` failed
  on assert-only unused variables (`atomic_ref_test`, `register_access_test`):
  under NDEBUG the asserts — and thus those checks — compiled out, both a real
  coverage hole and a `-Wunused-variable` error. Both tests migrated to
  `CHECK`/`CHECK_EQ` (run in Release too), and a `release-werror` CI job
  (gcc + clang, `Release` + `METL_WARNINGS_AS_ERRORS=ON`, build + ctest) now
  gates optimized/NDEBUG warning hygiene going forward.
4. Promote clang-tidy from advisory (`ci.yml:186 continue-on-error`) to blocking.
5. Code coverage gate (the `try_*`/full-container branches are easy to leave uncovered).
6. Real google/benchmark benchmarks or remove the dead `metl_cc_benchmark` stub + README claim.
7. 🟡 **IN PROGRESS** — Per-symbol API docs (Doxygen) — especially the
   non-standard contracts above. Landed: a `docs/Doxyfile.in` + CMake `docs`
   target + a `docs` CI job (generates HTML from `include/metl`, fails on
   malformed doc comments; undocumented is tolerated for now). Added
   `docs/COOKBOOK.md` (task-oriented recipes) and a set of CI-compiled,
   CTest-run examples covering every module family
   (`examples/{containers,allocators,spsc_isr,mmio_peripheral,error_handling,coroutine_task}.cpp`
   plus the pre-existing `blinky_fsm`/`can_frame_parser`/`sensor_pipeline`),
   each built under `-Wall -Wextra -Werror -std=c++17`. The non-standard
   contracts (`at()`/`value()`/`get()` assert; `flat_map` positional indexing;
   `[[noreturn]]` assert path; `function_ref` rvalue rejection) are now
   surfaced in the README module map and the Cookbook contracts table.
   **Deferred:** writing per-symbol `///` doc comments in the headers
   themselves.

**P2 — API correctness/ergonomics**
8. ✅ **DONE** — Make the assert handler `[[noreturn]]`-safe (fact ①) — collapses the conditional-UB class.
9. Reconcile `std::`-divergences: `at()` asserts not throws; `flat_map::operator[]` positional; `value()`/`get()` assert. Rename/document.
10. Fix the concrete High/Med bugs above.
11. Missing utilities: `fixed_bitset`, documented iterator-invalidation contracts, `expected` monadic ops, `try_value()` recoverable paths, compile-time `static_string_map`.
