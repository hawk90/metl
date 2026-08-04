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

- 🟡 **Zephyr module** (`zephyr` CI job) — metl is now packaged as a header-only
  Zephyr module (`zephyr/module.yml` + `zephyr/CMakeLists.txt` + `zephyr/Kconfig`;
  interface-only, no sources compiled into the image). A sample under
  `samples/zephyr/metl_hello/` enables `CONFIG_METL=y`, includes
  `fixed_vector`/`expected`/`span`, and prints a success sentinel. CI runs inside
  the official Zephyr CI Docker image (`ghcr.io/zephyrproject-rtos/ci`), inits a
  throwaway upstream-Zephyr workspace (LTS v3.7.0), then (1) `west build` for
  `qemu_cortex_m3` as the structural gate and (2) `twister` build+RUN on QEMU
  asserting the sentinel via the console harness. **May need CI iteration** (this
  can't be run on macOS locally): candidate points are the pinned CI-image tag,
  the pinned Zephyr revision, the libc choice in the sample `prj.conf`
  (`CONFIG_REQUIRES_FULL_LIBCPP=y` is the low-risk default; minimal libc may
  suffice), and QEMU run/exit semantics. The `west build` step is the
  high-confidence, correct-by-construction proof; the twister run is the
  execution assertion that most likely needs tuning. The module manifest, shim,
  sample, and README "Zephyr" docs are structurally verifiable independent of the
  CI run.

- 🟡 **ESP-IDF component / Xtensa (ESP32)** (`esp-idf` CI job) — metl is now
  packaged as a header-only ESP-IDF component (`components/metl/CMakeLists.txt`
  calls `idf_component_register(INCLUDE_DIRS ...)` with no `SRCS`; interface-only,
  nothing compiled into the image) plus an `idf_component.yml` manifest for the
  IDF Component Manager. A sample under `samples/esp-idf/metl_hello/` wires the
  in-tree `components/` dir via `EXTRA_COMPONENT_DIRS`, its `main` component does
  `REQUIRES metl`, and `main.cpp` exercises `fixed_vector`/`expected`/`span` in
  `app_main()`. CI uses the official `espressif/esp-idf-ci-action@v1` to
  `idf.py build` inside the pinned `espressif/idf:v5.3.3` Docker image for a
  **matrix of `esp32` (Xtensa) and `esp32c3` (RISC-V)**. The **Xtensa** target is
  the key new coverage — a compiler frontend/target no other job touches (ARM,
  RISC-V-elf, x86, PPC64). ESP-IDF compiles `.cpp` as C++ by default (default std
  is newer than C++17; exceptions and RTTI OFF by default — exactly metl's
  contract), so no extra `CONFIG` is needed for metl's C++17 surface.
  **May need CI iteration** (this can't be run on macOS locally): candidate
  points are the pinned IDF Docker tag (`v5.3.3`; `release-v5.3` / `v5.3` are
  moving alternatives) and the target list. Non-blocking (`continue-on-error`),
  same pattern as `zephyr`. The component shim, manifest, sample, and README
  "ESP-IDF (ESP32)" docs are structurally verifiable independent of the CI run.

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
7. ✅ **DONE** — Per-symbol API docs (Doxygen) — especially the
   non-standard contracts above. Landed earlier: a `docs/Doxyfile.in` + CMake
   `docs` target + a `docs` CI job (generates HTML from `include/metl`, fails on
   malformed doc comments via `WARN_AS_ERROR=FAIL_ON_WARNINGS`; undocumented is
   tolerated). Added `docs/COOKBOOK.md` (task-oriented recipes) and a set of
   CI-compiled, CTest-run examples covering every module family
   (`examples/{containers,allocators,spsc_isr,mmio_peripheral,error_handling,coroutine_task}.cpp`
   plus the pre-existing `blinky_fsm`/`can_frame_parser`/`sensor_pipeline`),
   each built under `-Wall -Wextra -Werror -std=c++17`. **Now complete:**
   per-symbol `///` Doxygen comments are written across all ~50 public headers
   (`include/metl/`, plus `coro/` and the public-facing `detail/construct.hpp`).
   Every public class/struct has a brief + a capacity/no-heap/thread-safety
   contract note, and key members/free functions carry
   `@param`/`@tparam`/`@return`/`@pre` where non-obvious. The non-standard
   contracts are surfaced at the symbols themselves with `@warning`/`@note`:
   `at()`/`value()`/`error()`/variant `get<>()` assert (abort) and do NOT throw;
   `flat_map`/`flat_set` `operator[]`/`at` are positional (not key lookup) →
   `find()`/`nth()`; `function_ref` rejects rvalue callables; the assert/panic
   path is `[[noreturn]]` even with a user handler; `static_message_queue` is
   single-threaded/non-ISR-safe → `spsc_queue`; fixed capacity overflow asserts
   while `try_*` returns false. Verified: `docs` target builds clean under
   `WARN_AS_ERROR`, clang-format 18.1.8 stays clean, and Debug ctest is
   unchanged (60/60) — the change is additive documentation only.

**P2 — API correctness/ergonomics**
8. ✅ **DONE** — Make the assert handler `[[noreturn]]`-safe (fact ①) — collapses the conditional-UB class.
9. Reconcile `std::`-divergences: `at()` asserts not throws; `flat_map::operator[]` positional; `value()`/`get()` assert. Rename/document.
10. Fix the concrete High/Med bugs above.
11. Missing utilities: `fixed_bitset`, documented iterator-invalidation contracts, `expected` monadic ops, `try_value()` recoverable paths, compile-time `static_string_map`.

## Section C — Fuzzing / security (bug bounty)

**Fuzzing harnesses (libFuzzer, ASan+UBSan).** ✅ **DONE** — five harnesses under
`fuzz/` (`fuzz_fixed_string`, `fuzz_flat_map`, `fuzz_static_unordered_map`,
`fuzz_allocators`, `fuzz_crc`), each an `LLVMFuzzerTestOneInput` built with
`-fsanitize=fuzzer,address,undefined` behind the opt-in Clang-only CMake option
`METL_BUILD_FUZZERS` (default OFF; the default host/arm/sanitizer builds are
untouched). A blocking `fuzz-smoke` CI job (`needs: preflight`) builds them and
runs each for a bounded time against a seed corpus, failing on any
crash/leak/timeout.

**Contract-respecting by construction.** metl containers assert-abort on
precondition violations (push past capacity, pop empty, OOB index), and an abort
inside a fuzzer reads as a crash. The harnesses therefore treat the fuzz bytes
as an **opcode stream of contract-VALID operations only** — `try_*` variants,
`size()/capacity()` checks before any asserting call, and `% size()`-bounded
indices — so that any ASan/UBSan finding (heap/stack OOB, UB, use-after-poison,
leak, uninitialized read) is a **genuine defect**. `fuzz_crc` additionally
differential-checks overload agreement and the streaming/resumability property
(fold(prefix) then resume == fold(whole)); a mismatch would be a real CRC bug.

**Findings.** No library defect surfaced. One issue was found and fixed **in the
harness** during bring-up: an over-strict `fixed_string` invariant
(`strlen(c_str()) == size()`) that is false by design when the buffer holds an
embedded NUL (`try_push_back('\0')` is contract-valid) — corrected to
`strlen(c_str()) <= size()` with the terminator-at-`size()` check retained. This
is a harness bug, not a metl bug, and confirms the harness would have flagged a
real terminator/length desync. The libFuzzer engine explored 200k+ runs per
target with no crash on the current code.

**Continuous fuzzing.** ClusterFuzzLite (`.clusterfuzzlite/` + non-blocking
`cflite-pr`/`cflite-batch` workflows) runs the OSS-Fuzz toolchain directly in
GitHub Actions with no upstream registration; its `build.sh`/`Dockerfile` are
OSS-Fuzz-compatible so upstream google/oss-fuzz registration is a drop-in
follow-up. `SECURITY.md` documents the disclosure policy. Chosen over an
immediate OSS-Fuzz PR because it lands entirely in-repo (nothing to merge
upstream, no project-approval latency) while staying registration-ready.

## Section E — Assert hardening levels & follow-up anti-patterns (2026-08-04)

A second anti-pattern pass, focused on two themes the 2026-07-07 audit left open:
(1) the always-on assert posture, and (2) a handful of standard-UB / correctness
smells distinct from Section A. **Necessity is judged against this project's
actual characteristics** — flat-memory embedded targets, `METL_NO_EXCEPTIONS` the
common config, UBSan-clean CI — not against a language-lawyer ideal. Pedantic-UB
items that cannot fail on any real target and are not sanitizer-flagged are marked
optional/deferred rather than treated as bugs.

### E.1 — Runtime-check posture: `METL_HARDENING` levels (supersedes "never downgraded")

Context: the 2026-07-07 pass added `METL_DASSERT` but **deliberately kept every
existing `METL_ASSERT` site always-on** (Section C: "Existing METL_ASSERT sites are
unchanged (never downgraded)"). A census showed **182 `METL_ASSERT` vs 2
`METL_DASSERT`** across 25 headers — there was no way to trade the always-on
checking for release performance, and no defense-in-depth floor below it.

**Decision — a hardening-level knob, but CHECKED-BY-DEFAULT.** metl's identity
(Design Principles: *"no silent surprises"*, *"every precondition asserts by
default"*) is to be safer than the STL, so we do **not** flip hot-path accessors
to unchecked-in-release. Making `operator[]`/`front`/`back` UB in release would be
a *silent behavior change* (still compiles, now corrupts) — the most dangerous
kind of break, and one that contradicts the stated contract. Performance is an
explicit opt-in instead. Model: libc++ `_LIBCPP_HARDENING_MODE` + Abseil's
`ABSL_HARDENING_ASSERT`.

- `METL_HARDENING` ∈ { `NONE` (0), `FAST` (1), `DEBUG` (2) }. Default: `DEBUG`
  when `METL_DEBUG || !NDEBUG`, else `FAST`. Consumer-overridable via
  `-DMETL_HARDENING=…`, independent of the consumer's own `NDEBUG`.
- **`METL_ASSERT`** — the default precondition check (bounds, non-empty, capacity,
  allocator overflow). Active at `>= FAST`, so **it stays on in release by
  default**; stripped only at `NONE`. Essentially every precondition uses this.
- **`METL_DASSERT`** — active only at `DEBUG`. Reserved for checks too *expensive*
  to ship (e.g. an O(n) invariant scan), NOT for ordinary accessor bounds. Kept
  deliberately rare (the original 2 sites; the accessor triage was reverted).
- **`METL_HARDEN`** — always on, never stripped (§E.3). The memory-safety floor.

So the three levels are: `NONE` (strip preconditions, keep the `METL_HARDEN`
floor) / `FAST` (all preconditions on — release default) / `DEBUG` (+ expensive
DCHECKs). No accessor became unchecked-by-default; the value is the `NONE`
opt-out, the `METL_HARDEN` floor, and a consumer-controllable level.

**ODR consistency (load-bearing):** `METL_HARDENING` changes the bodies of
inline/template functions (which checks compile in), so it MUST be uniform across
every TU linked into a program — mixing levels (a Debug TU + a Release TU) is an
ODR violation (UB). Same constraint as `NDEBUG` / `_LIBCPP_HARDENING_MODE`;
documented at `config.hpp`. Debug ctest keeps every check on, so 60/60 is
unchanged.

### E.2 — Follow-up correctness findings (distinct from Section A)

| Sev | Necessity (this project) | Issue | Location | Status |
|---|---|---|---|---|
| MED | **Required** — layout corruption on any target | `atomic_ref` reinterprets `T` as `std::atomic<T>` with no lock-free guard; a *locking* atomic has a larger `sizeof`/different layout → R/W past the referenced object. `is_always_lock_free` was computed but never enforced | `atomic_ref.hpp:41` | ✅ `static_assert(is_always_lock_free)` |
| MED | **Required** — use-after-destruction on any target | converting `variant::operator=(T&&)` routes through `emplace<Decayed>` unconditionally → `reset()` destroys the active alternative *before* reading an aliasing RHS (`v = get<T>(v)`). Distinct from Section A's copy/move-assign exception-safety fix | `variant.hpp:338` | ✅ in-place assign when the active index already matches |
| LOW | **Marginal** — throwing-ctor only; unreachable under `METL_NO_EXCEPTIONS` | `arena_allocator::try_emplace` commits the destroy record *before* running `T`'s ctor → a throwing ctor leaves a record over unconstructed storage; a later `rewind` runs `~T()` on it | `arena_allocator.hpp:62` | ✅ construct first, patch `destroy` after; + power-of-two `alignment` assert |
| LOW | **Optional** — benign on flat memory, not UBSan-flagged | `object_pool::index_of` uses relational `<`/`>=` on an unrelated caller pointer (UB); switched to `uintptr_t` comparison — exact containment test on flat targets, no `<functional>` dependency | `object_pool.hpp:116` | ✅ `uintptr_t` range test |
| LOW | **Deferred** — no real-target failure, high-risk core rewrite | `fixed_vector`/`flat_map`/`flat_set` form a contiguous `data()` over an array of per-element `storage_for<T>`; `data()+i` (i>0) is cross-object pointer arithmetic. A real fix needs a union-of-`T[N]` storage rewrite — the same reason Section A deferred the `constexpr` conversion of these types | `fixed_vector.hpp:138`, `flat_map.hpp:387`, `flat_set.hpp:373` | ⏸ deferred (documented) |

**Explicitly not defects on this project** (deliberate / documented): the
`[[nodiscard]]` gaps and `noexcept`-on-throwing-`T` smells are being addressed as
cheap compile-time `static_assert` guards under E.1's static_assert-promotion
(e.g. MMIO `sizeof(T)` ≤ bus width, nothrow-move on queue element types), not as
runtime changes; and `METL_ASSERT`-guarded preconditions (e.g. `flat_map::emplace`
on a duplicate key) **abort in release too** under the default level, so they are
a documented precondition, not "silent corruption" — the corruption story only
holds if a consumer redefines `METL_ASSERT` to a no-op.

### E.3 — Strippability hazards the hardening model itself introduces

Making `METL_ASSERT` compile out at `METL_HARDENING_NONE` is safe **only** where
the check is a caller precondition (violation = caller's fault, UB acceptable).
A follow-up scan of every assert site found a small set of **defense-in-depth
guards the library relies on to never corrupt memory even on misuse** — written
as `METL_ASSERT`, so they silently vanish at `NONE`, regressing a guarantee the
2026-07-07 pass deliberately added (Section A row: `construct_at` "hard-guards
`index < bucket_count`"). These need an always-on tier.

**New macro `METL_HARDEN(expr)`** — always on, independent of `METL_HARDENING`
(the old always-on `METL_ASSERT` semantics). Reserved for the wild-OOB-**write** /
memory-safety floor. Mirrors Abseil's `ABSL_HARDENING_ASSERT`. Consequence:
`NONE` is "strip **preconditions**", not "strip everything" — a curated security
floor survives, which suits a library with a `SECURITY.md`/fuzzing/bug-bounty
posture.

| Sev | Site | Hazard at `NONE` | Fix |
|---|---|---|---|
| HIGH | `static_unordered_set.hpp:405` `emplace`/`construct_at` | full table → `index == npos` → wild placement-new + state write + `++size_` (OOB **write**). The set lacks the map's guard entirely | add non-strippable `METL_HARDEN(index < bucket_count)` |
| HIGH | `static_unordered_map.hpp:589` `construct_at` | the existing bounds guard is itself `METL_ASSERT` → false at `NONE`; the code comment claiming it defends a user-disabled assert is now untrue | change that one guard to `METL_HARDEN` |
| MED | `arena_allocator.hpp:149` `allocate_impl` | non-power-of-two `alignment` (runtime `allocate()` path) → silent offset corruption → later OOB allocation | `METL_HARDEN` / `panic` on the runtime path |
| MED | `flat_map.hpp:344` / `flat_set.hpp:343` `emplace` | full container → `return data()[Capacity]` one-past-end reference (OOB **read**/dangling) | branch on `inserted` / non-strippable capacity guard |
| LOW | `atomic_ref.hpp:63` ctor | misaligned pointer → torn/UB atomic access rather than abort (acceptable precondition) | document that `NONE` removes the alignment guarantee |

**Verified non-issues** (so they are not re-flagged): no side-effect-in-assert
anywhere — every mutating call (`try_emplace_back`, `try_assign`, `try_insert_at`,
`locate_insert_index`, …) is hoisted onto the line *above* its assert, and assert
arguments are pure comparisons; no unused-variable `-Werror` breaks — every
assert-only bool has a following `(void)var;` or is `return`ed, and the stripped
macro `(void)sizeof((expr) ? 1 : 0)` still textually references the operand; and
`assert.hpp`'s `assertion_failed`/`panic` machinery is referenced only inside the
active (`>= FAST`) macro branch, so nothing dangles when `METL_ASSERT` is a no-op.
