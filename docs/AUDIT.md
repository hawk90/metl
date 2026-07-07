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
| LOW | Pervasive non-functional `constexpr` labels (placement-new/launder not constant-evaluable in C++17) | optional/expected/variant/fixed_vector/flat_map |
| — | **Clean:** `spsc_queue` fences correct, `intrusive_ptr` refcount ordering correct, hash probes bounded (no infinite loop), bit/bitfield/crc all correct | — |

## Section B — Backlog

**P0 — harness correctness (gates everything)**
1. ✅ **DONE** — Test assertion/reporting layer — tests signal only via exit codes; failures don't say *where*. Added header-only `tests/metl_check.hpp` with `CHECK`/`CHECK_EQ` printing `file:line: CHECK failed: …`; `fixed_vector_test` and `optional_test` migrated as a demonstration.
2. ✅ **DONE** — Real multi-threaded tests for concurrency types, gated into the TSAN job (fact ②). Added `tests/spsc_queue_threaded_test.cpp` (spsc_queue + atomic_ref).
3. ✅ **DONE** — Per-header self-containment compile check + umbrella-completeness check in CI. Added the `metl_header_self_contained` target + `cmake/CheckUmbrella.cmake` CTest guard and a `header-checks` CI job.

**P1 — CI/quality**
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
