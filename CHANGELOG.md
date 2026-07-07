# Changelog

All notable changes to METL are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Portable attribute layer (`metl/attributes.hpp`, abseil `attributes.h`
  style):** `__has_cpp_attribute`/`__has_attribute`-gated macros with empty
  fallbacks, so applying one is always safe (honored or a no-op). Consolidates
  `METL_NODISCARD` and adds `METL_NORETURN`, `METL_ALWAYS_INLINE`,
  `METL_MAYBE_UNUSED`, `METL_DEPRECATED(msg)`, `METL_LIFETIME_BOUND`
  (`[[clang::lifetimebound]]`), `METL_CONST_INIT`
  (`constinit` / `[[clang::require_constant_initialization]]`), and
  `METL_ATTRIBUTE_TRIVIAL_ABI` (`[[clang::trivial_abi]]`). `compiler.hpp`
  includes it so every current include site keeps `METL_NODISCARD`.
- **Optimization hints (`metl/optimization.hpp`, abseil `optimization.h`
  style):** `METL_PREDICT_TRUE`/`METL_PREDICT_FALSE` (`__builtin_expect`,
  identity fallback), `METL_ASSUME(cond)`, `METL_CACHELINE_SIZE`, and
  `METL_CACHELINE_ALIGNED` (portable `alignas`).
- **Feature detection (`compiler.hpp`, abseil `config.h` style):**
  `METL_HAVE_BUILTIN(x)`, `METL_HAVE_FEATURE(x)`, `METL_HAVE_INCLUDE(x)` wrappers
  with safe `0` fallbacks so they are always valid in `#if`.
- **`METL_DASSERT`** — a debug-only assertion (DCHECK) alongside the always-on,
  hardened `METL_ASSERT`. Active when `!NDEBUG` or `METL_DEBUG`; otherwise
  evaluates its expression only in an unevaluated context (no side effects, no
  unused warnings). `METL_ASSERT` is never downgraded to `METL_DASSERT`.

### Changed

- **function_ref / span:** the callable/container/array constructors mark the
  bound referent `METL_LIFETIME_BOUND`, so clang (`-Wdangling`) diagnoses a
  view that would outlive its referent at the call site. This complements the
  existing deleted rvalue-binding overloads; valid lvalue usage is unaffected.
- **assert:** the global assert/panic handler storage is now `METL_CONST_INIT`,
  making its constant initialization explicit and static-init-order-safe (and
  rejecting any future change to dynamic initialization).
- **intrusive_ptr:** applies `METL_ATTRIBUTE_TRIVIAL_ABI` so the single-owner
  pointer is passed/returned in a register and destroyed by the callee, matching
  a raw pointer's calling convention. Observable behavior is unchanged (verified
  under ASan/UBSan).
- **spsc_queue:** the per-role cache-line padding now uses
  `METL_CACHELINE_ALIGNED` / `METL_CACHELINE_SIZE` instead of a hand-rolled
  `alignas(64)` (identical layout).
- **METL_ASSERT:** the failed-check branch is marked `METL_PREDICT_FALSE` so the
  success path stays the straight-line case (behavior unchanged).

### Documentation

- Added `docs/COOKBOOK.md` — task-oriented recipes (fixed-capacity vector,
  key/value lookup with `flat_map`, bounded FIFO, error handling without
  exceptions, per-tick scratch allocators, ISR↔main-loop SPSC queue,
  memory-mapped register access, a small FSM, cooperative protothreads). Each
  snippet mirrors a compiled example. Includes an explicit table of the
  non-standard contracts (`at()`/`value()`/`get()` assert instead of throwing;
  `flat_map::operator[]`/`at` are **positional**, not key lookups; the assert
  path is provably `[[noreturn]]`; `function_ref` rejects rvalue callables).
- Added a `docs/Doxyfile.in`, a CMake `docs` target (`cmake --build build
  --target docs`), and a `docs` CI job that generates HTML API docs from
  `include/metl` and fails on malformed doc comments (undocumented symbols are
  tolerated for now).
- README: linked each module family to its worked example, added a
  "Documentation" section (Cookbook + examples table + Doxygen instructions),
  and called out the non-standard contracts inline.

### Examples

- Added one focused, CI-compiled example per module family, each a
  self-contained `main()` returning 0 with in-program self-checks and built
  under `-Wall -Wextra -Werror -std=c++17`:
  `containers.cpp` (`fixed_vector` + `flat_map` + `ring_buffer`),
  `allocators.cpp` (`arena_allocator` + `monotonic_buffer`),
  `spsc_isr.cpp` (`spsc_queue` ISR↔main-loop pattern),
  `mmio_peripheral.cpp` (`mmio` + `register_access` + `bitfield` driving a fake
  peripheral), `error_handling.cpp` (`expected` + `optional` + `variant`), and
  `coroutine_task.cpp` (`coro/protothread`).
- All examples (new and pre-existing) are now wired into the examples CMake as
  CTest smoke tests and built + run by a new `examples` CI job.

### Fixed

- **assert:** the failed-assert and panic paths are now provably `[[noreturn]]`.
  After invoking the (customizable) handler, `detail::assertion_failed`,
  `detail::panic_failed`, and `panic` unconditionally `std::abort()`, so a
  user-installed handler that incorrectly returns can no longer fall through
  past a failed precondition into undefined behavior. The customization point is
  unchanged (handlers still receive expression/file/line).
- **function_ref:** rvalue callables are rejected instead of silently binding to
  a temporary that dangles at the end of the full-expression. Following
  `std::function_ref` (P0792), the callable constructor is lvalue-only and the
  rvalue overload is deleted; lvalue callables and function pointers are
  unaffected.
- **fixed_string:** the `const char*` constructor now asserts on overflow rather
  than silently producing an empty string. `assign()` remains the recoverable,
  non-asserting path (returns `false` and leaves the string unchanged).
- **coro/scheduler:** `run_once()` is now reentrancy-safe. It snapshots the set
  of tasks attached at entry and polls exactly that set, skipping any task a
  prior poll detached. A task that `detach()`es (itself or another task) or
  attaches a task from within its `poll()` can no longer shift the underlying
  vector out from under the cached index/size (previously a stale/out-of-bounds
  read). Added `is_attached()`.
- **static_unordered_map:** `emplace(key, value)` now finds an existing key
  first. A duplicate-key `emplace` is a no-op returning the existing element
  (std::unordered_map semantics: it does NOT overwrite), instead of
  double-constructing over the live element and incrementing `size_` twice.
  `construct_at` also hard-guards its index so a full-table insert can never
  reach an out-of-bounds `construct_at(npos, …)`.
- **variant / expected:** cross- and same-state assignment (and
  `expected::swap`) are now exception-safe. A throwing copy/move constructor can
  no longer leave a destroyed member paired with an unchanged discriminant
  (which double-destroyed on the next destructor). `variant` marks itself
  valueless across a throwing same-index reconstruct; `expected` uses the
  std::expected "reinit" pattern (construct into a temporary / restore the old
  member on throw).
- **variant:** comparison operators now compare the active alternative *by
  index* rather than by `get<T>`, so they compile and work for a `variant` with
  duplicate alternative types.
- **mmio:** `mmio_register<T, Address>` static_asserts `Address` is aligned to
  `alignof(T)`, and `mmio_ptr<T>(uintptr_t)` asserts alignment at runtime; a
  misaligned volatile access is undefined behavior. The `mmio_ptr(uintptr_t)`
  constructor is no longer `constexpr` — its only path is an integer→pointer
  `reinterpret_cast`, so a `constexpr` qualifier there was ill-formed (IFNDR).
- **arena_allocator / static_allocator:** size math is now overflow-safe. A huge
  byte/element request that would wrap `size_type` before the bounds check is
  rejected (`nullptr`) instead of overrunning the buffer.
- **hash:** `fnv1a_hash`'s default (raw-object-representation) overload is now
  constrained via `static_assert` to types with
  `has_unique_object_representations`, so it can't silently break the
  hash/equality invariant for padded types, pointers, or floating point.
- **fixed_function / fixed_any_invocable:** the type-erased callable is stored in
  a `mutable` buffer, so the `const` `operator()` invoking a mutable target is
  well-defined rather than mutating a `const` subobject through `const_cast`.
- **intrusive_ptr:** `intrusive_ref_counter<Derived>` now `static_assert`s that
  `Derived` is `final` or has a virtual destructor, since reference-count
  release destroys the object through `Derived`. This makes the "Derived must be
  the most-derived type" contract explicit and rejects the silently-sliced
  deeper-non-virtual-hierarchy case.
- **fsm:** `dispatch()` commits the new state *before* running the transition
  action, so an action that reentrantly dispatches observes the new state and
  can no longer re-fire the transition in progress.

### Changed

- **flat_map / flat_set:** documented that `operator[]` and `at()` are
  **positional** (index into sorted order), NOT key lookups — the opposite of
  `std::map`/`std::set`. Added an explicit `nth()` positional accessor as a
  self-documenting alias; key access remains via `find()` / `contains()` /
  `lower_bound()`. Signatures are unchanged (no API break).
- **static_message_queue:** documented that it is a single-threaded FIFO with
  plain (non-atomic) indices — NOT concurrent and NOT ISR-safe. Use
  `spsc_queue` for interrupt↔main-loop hand-off.

### Testing

- Added `tests/metl_check.hpp` providing `CHECK` / `CHECK_EQ`, which report
  `file:line` and the offending values on failure instead of only an exit code.
  `fixed_vector_test` and `optional_test` migrated as a demonstration.
- Added `tests/spsc_queue_threaded_test.cpp`, a bounded, deterministic
  multi-threaded producer/consumer test for `spsc_queue` and a concurrent
  `atomic_ref` counter, so the ThreadSanitizer CI job validates real
  concurrency.
- Added focused correctness regression tests (all using `CHECK`/`CHECK_EQ`) for
  the Section A fixes: `coro_scheduler_reentrancy`, `static_unordered_map_emplace`,
  `variant_regression`, `expected_regression`, `mmio_regression`,
  `allocator_overflow`, `fixed_function_const`, `fsm_reentrancy`,
  `hash_unique_repr`, and `intrusive_ptr_contract`. Several exercise the fixed
  paths under ASan/UBSan (memory safety) and throwing-constructor rollback.
- Added `tests/fixed_vector_asan_test.cpp`. Under AddressSanitizer it asserts
  the poison boundaries are exact (live elements addressable, tail poisoned, the
  boundary tracking `push_back`/`pop_back`) and that a real read into the
  poisoned tail is trapped (a forked child performs the OOB and is killed). It is
  a trivial pass in non-ASan configurations.

### Build

- Added per-header self-containment and umbrella-completeness checks: the
  `metl_header_self_contained` target compiles one translation unit per public
  header, `cmake/CheckUmbrella.cmake` verifies `metl.hpp` includes every other
  header, both registered with CTest, plus a `header-checks` CI job.
- **fixed_vector:** under AddressSanitizer, the unused-capacity tail
  `[size(), capacity())` of the inline buffer is poisoned (à la
  `absl::InlinedVector`) so an out-of-bounds access past `size()` is trapped even
  though the whole buffer is one object. Mutating operations unpoison the buffer
  while they rearrange elements and re-poison the tail on exit; the destructor
  unpoisons everything so no stale poison outlives the storage. Fully gated on
  ASan detection — a no-op (and still `constexpr`-constructible) otherwise.
- New public headers `metl/attributes.hpp` and `metl/optimization.hpp` added to
  the umbrella, the installed header set, and the self-containment checks. The
  Doxyfile `PREDEFINED` list strips the new attribute macros.

## [0.1.0-alpha1]

Initial pre-alpha snapshot. The library is feature-incomplete and the public
API is subject to change before the 1.0 release.

### Added

Core utilities:

- `metl::span` — non-owning view over a contiguous range (C++20-style backport).
- `metl::optional` — value-or-empty wrapper with P2505 monadic operations
  (`and_then`, `transform`, `or_else`).
- `metl::expected` — value-or-error type, including the `void` value
  specialization.
- `metl::variant` — type-safe tagged union with visitation.
- `metl::in_place_t`, `metl::in_place_type_t`, `metl::in_place_index_t` tag
  types.
- `metl::type_traits` extensions, including `storage_for<T>` aligned storage
  helper.
- `metl::hash` — FNV-1a hashing primitive and `hash_combine` helper.

Containers:

- `metl::fixed_vector` — fixed-capacity contiguous container.
- `metl::fixed_string` — fixed-capacity null-terminated string.
- `metl::fixed_queue`, `metl::fixed_stack`, `metl::fixed_deque` — fixed-capacity
  adapter containers.
- `metl::ring_buffer` — circular buffer over fixed storage.
- `metl::flat_map`, `metl::flat_set` — sorted-vector associative containers.
- `metl::static_unordered_map`, `metl::static_unordered_set` — open-addressed
  hash containers with power-of-two capacity and heterogeneous lookup.

Function objects:

- `metl::fixed_function` — small-buffer-optimized function wrapper, including
  the `noexcept` signature specialization.
- `metl::fixed_any_invocable` — move-only invocable, sibling to `fixed_function`.
- `metl::function_ref` — non-owning callable reference.
- `metl::delegate` — typed callable bound to an object and member function.
- `metl::event_dispatcher` — fixed-capacity multicast dispatcher.

Memory:

- `metl::intrusive_ptr` and CRTP `intrusive_ref_counter` base.
- `metl::arena_allocator` — bump-pointer arena.
- `metl::monotonic_buffer` — monotonic growing buffer over fixed storage.
- `metl::static_allocator` — allocator adapter over user-owned storage.
- `metl::object_pool` — fixed-capacity object pool with freelist.

Concurrency:

- `metl::spsc_queue` — single-producer/single-consumer lock-free queue.
- `metl::static_message_queue` — fixed-capacity message queue.
- `metl::atomic_ref` — non-owning atomic view over an existing object.

Utility:

- `metl::scope_exit` — RAII deferred-action helper.
- `metl::fsm` — table-driven finite state machine.
- `metl::lookup_table` — compile-time lookup table.
- `metl::bit` — bit manipulation helpers.
- `metl::endian` — byte-order utilities.
- `metl::crc8`, `metl::crc16`, `metl::crc32` — CRC primitives.

Embedded:

- `metl::mmio` — memory-mapped I/O accessors.
- `metl::bitfield` — typed bitfield helpers.
- `metl::register_access` — typed register access utilities.

### Changed

- N/A (initial release).

### Build

- CMake build with Bazel-style helper functions
  (`metl_cc_library`, `metl_cc_test`, `metl_cc_binary`, `metl_cc_benchmark`).
- ASAN, UBSAN, and TSAN toggles
  (`METL_ENABLE_ASAN`, `METL_ENABLE_UBSAN`, `METL_ENABLE_TSAN`).
- Install rules and CMake package config export
  (`find_package(metl)` provides `metl::metl`).
- `METL_WARNINGS_AS_ERRORS` toggle to promote warnings to errors.

### Testing

- 42 host test binaries registered with CTest.
- All tests pass under ASAN + UBSAN with `-Werror` enabled.

[Unreleased]: ./
[0.1.0-alpha1]: ./
