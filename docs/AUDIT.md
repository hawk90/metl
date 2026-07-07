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
- **② TSAN CI exercises zero concurrency** (`.github/workflows/ci.yml:94`) — no test spawns
  a thread, examples default OFF. `spsc_queue`/`atomic_ref`/`static_message_queue` ordering
  is never validated; a regression would pass green.

| Sev | Issue | Location |
|---|---|---|
| HIGH | `function_ref` non-`explicit` ctor binds rvalue temporaries → dangling (P0792 deletes this) | `function_ref.hpp:37` |
| HIGH | `scheduler::run_once` not reentrancy-safe: a task detaching during `poll` shifts the vector → stale index OOB | `coro/scheduler.hpp:103` |
| HIGH (cond.) | `static_unordered_map::emplace` doesn't check existing key → duplicate double-constructs + `++size_` twice | `static_unordered_map.hpp:374` |
| HIGH (cond.) | full-table `emplace`/`operator[]` reach `construct_at(npos,…)` wild OOB if handler returns | `static_unordered_map.hpp:527` |
| MED | `variant`/`expected` assignment destroys active member then constructs w/o rollback → double-destroy if ctor throws | `expected.hpp:545,342`, `variant.hpp:396,297` |
| MED | `variant` comparisons use `get<T>` by type → fail to compile for duplicate alt types | `variant.hpp:649` |
| MED | `flat_map/set::operator[]`/`at` are **positional** index accessors, not key lookups (opposite of `std::map`) | `flat_map.hpp:113` |
| MED | `fixed_string(const char*)` silently yields empty string on overflow (discards `assign` failure); non-`explicit` | `fixed_string.hpp:25` |
| MED | `mmio_ptr(uintptr_t)` constexpr ctor always `reinterpret_cast`s → IFNDR; no alignment enforcement (UB) | `mmio.hpp:47,21` |
| MED | `arena_allocator`/`static_allocator` size math can integer-overflow **before** the bounds check → OOB | `arena_allocator.hpp:101`, `static_allocator.hpp:27` |
| MED | `fnv1a_hash` hashes raw object representation (padding/pointers) → breaks hash/equality invariant | `hash.hpp:107` |
| MED | `fixed_function::operator()` is `const` but `const_cast`s storage → UB mutating a const instance | `fixed_function.hpp:267` |
| MED | `intrusive_ptr` destroys via CRTP base cast → non-virtual base of deeper hierarchy = UB/leak | `intrusive_ptr.hpp:85` |
| MED | `static_message_queue` filed under "Concurrency" but uses plain non-atomic indices — single-threaded only | `static_message_queue.hpp:158` |
| MED | `fsm::dispatch` updates state *after* action → reentrant dispatch re-fires same transition | `fsm.hpp:61` |
| LOW | Pervasive non-functional `constexpr` labels (placement-new/launder not constant-evaluable in C++17) | optional/expected/variant/fixed_vector/flat_map |
| — | **Clean:** `spsc_queue` fences correct, `intrusive_ptr` refcount ordering correct, hash probes bounded (no infinite loop), bit/bitfield/crc all correct | — |

## Section B — Backlog

**P0 — harness correctness (gates everything)**
1. Test assertion/reporting layer — tests signal only via exit codes; failures don't say *where*. Add header-only `CHECK`/`CHECK_EQ` with `file:line: expected … got …`.
2. Real multi-threaded tests for concurrency types, gated into the TSAN job (fact ②).
3. Per-header self-containment compile check + umbrella-completeness check in CI.

**P1 — CI/quality**
4. Promote clang-tidy from advisory (`ci.yml:186 continue-on-error`) to blocking.
5. Code coverage gate (the `try_*`/full-container branches are easy to leave uncovered).
6. Real google/benchmark benchmarks or remove the dead `metl_cc_benchmark` stub + README claim.
7. Per-symbol API docs (Doxygen) — especially the non-standard contracts above.

**P2 — API correctness/ergonomics**
8. Make the assert handler `[[noreturn]]`-safe (fact ①) — collapses the conditional-UB class.
9. Reconcile `std::`-divergences: `at()` asserts not throws; `flat_map::operator[]` positional; `value()`/`get()` assert. Rename/document.
10. Fix the concrete High/Med bugs above.
11. Missing utilities: `fixed_bitset`, documented iterator-invalidation contracts, `expected` monadic ops, `try_value()` recoverable paths, compile-time `static_string_map`.
