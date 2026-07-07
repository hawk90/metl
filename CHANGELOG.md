# Changelog

All notable changes to METL are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- **Fuzzing harnesses (libFuzzer, ASan+UBSan) + a blocking CI fuzz-smoke job.**
  Five `LLVMFuzzerTestOneInput` harnesses under `fuzz/`
  (`fuzz_fixed_string`, `fuzz_flat_map`, `fuzz_static_unordered_map`,
  `fuzz_allocators`, `fuzz_crc`) drive the targets with the fuzz bytes and are
  built with `-fsanitize=fuzzer,address,undefined` behind a new Clang-only,
  default-OFF CMake option `METL_BUILD_FUZZERS` (a non-clang or option-off
  configure is unaffected; default host/arm/sanitizer builds are untouched).
  Crucially, the harnesses respect metl's assert-based **contract**: metl
  containers abort on precondition violations (push past capacity, pop empty,
  OOB index), so the harnesses perform **only contract-valid operations** —
  `try_*` variants, `size()/capacity()` checks before any asserting call, and
  `% size()`-bounded indices — treating the input as an opcode stream. This
  makes any ASan/UBSan finding (heap/stack OOB, UB, use-after-poison, leak,
  uninitialized read) a genuine defect rather than a by-design abort.
  `fuzz_crc` also differential-checks overload agreement and the
  streaming/resumability property (a mismatch would be a real CRC bug). A new
  blocking `fuzz-smoke` CI job (`needs: preflight`) builds the harnesses and
  runs each for a bounded time (`-max_total_time=25`) against a tiny seed corpus
  under `fuzz/corpus/`, failing on any crash/leak/timeout. No library defect was
  found (200k+ runs per target clean); one over-strict harness invariant
  (`fixed_string` `strlen == size`, false by design for embedded NULs) was fixed
  during bring-up to `strlen <= size`. See `docs/AUDIT.md` Section C.
- **ClusterFuzzLite continuous fuzzing (OSS-Fuzz tech, no upstream
  registration).** `.clusterfuzzlite/` (`Dockerfile`, `build.sh`, `project.yaml`)
  plus non-blocking `cflite-pr` (per-PR, code-change mode) and `cflite-batch`
  (scheduled) GitHub Actions workflows run the OSS-Fuzz toolchain directly in
  CI. The `build.sh`/`Dockerfile` are OSS-Fuzz-compatible, so upstream
  google/oss-fuzz registration remains a drop-in follow-up (tracked in
  `docs/TODO.md`). These workflows are `continue-on-error` (non-blocking); the
  blocking, always-green memory-safety gate is the in-repo `fuzz-smoke` job.
- **`SECURITY.md` — vulnerability disclosure policy.** How to report privately
  (GitHub security advisory), supported versions, response expectations, and a
  clear statement that an abort from a documented precondition violation is
  contractually correct (use `try_*`) — while a memory-safety failure reachable
  through a contract-valid API is a reportable security issue.
- **Per-symbol Doxygen API documentation across all public headers.** Every
  public class/struct in `include/metl/` (plus `coro/` and the public-facing
  `detail/construct.hpp`) now carries a `///` brief and a short contract note
  (fixed capacity, no heap allocation, thread-safety where relevant); key
  members and free functions document `@param`/`@tparam`/`@return`/`@pre` where
  non-obvious. The non-standard, easy-to-trip contracts are surfaced with
  `@warning`/`@note` at the symbols themselves: `at()`/`value()`/`error()` and
  `variant` `get<>()`/`visit()` **assert (abort by default), they do not throw**
  the corresponding `std::` exceptions; `flat_map`/`flat_set`
  `operator[]`/`at()` are **positional index accessors, not key lookup** (use
  `find()`/`nth()`); `function_ref` **rejects rvalue callables** (dangling
  prevention); the failed-assert/`panic` path is **`[[noreturn]]`** even with a
  user handler; `static_message_queue` is **single-threaded / not ISR-safe**
  (use `spsc_queue`); fixed-capacity overflow asserts while the `try_*` variants
  return `false`. Additive only — no API, signature, or behavior change; the
  strict `docs` job (`WARN_AS_ERROR=FAIL_ON_WARNINGS`) builds clean.
- **Zephyr module support.** metl is now consumable as a header-only
  [Zephyr module](https://docs.zephyrproject.org/latest/develop/modules.html):
  `zephyr/module.yml` (manifest) plus a minimal `zephyr/CMakeLists.txt` +
  `zephyr/Kconfig` shim expose metl's `include/` to Zephyr applications
  (interface-only — no sources compiled into the RTOS image). Applications opt
  in with `CONFIG_METL=y`. A runnable sample lives under
  `samples/zephyr/metl_hello/` (`CMakeLists.txt`, `prj.conf`, `src/main.cpp`,
  `sample.yaml`) exercising `fixed_vector` + `expected` + `span` on
  `qemu_cortex_m3` / `native_sim`. A new `zephyr` CI job builds the sample in
  the official Zephyr CI Docker image and runs it under QEMU via twister,
  asserting a success sentinel. See the README "Zephyr" section.
- **ESP-IDF component support (ESP32).** metl is now consumable as a header-only
  [ESP-IDF component](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html):
  `components/metl/CMakeLists.txt` registers metl's `include/` via
  `idf_component_register(INCLUDE_DIRS ...)` with no `SRCS` (interface-only —
  nothing compiled into the image), and `components/metl/idf_component.yml` is a
  manifest for the IDF Component Manager. A consumer declares `REQUIRES metl` to
  get `<metl/...>` on its include path. A runnable sample lives under
  `samples/esp-idf/metl_hello/` (top-level `CMakeLists.txt` wiring
  `EXTRA_COMPONENT_DIRS` → this repo's `components/`, `main/CMakeLists.txt`
  with `REQUIRES metl`, and `main/main.cpp`) exercising `fixed_vector` +
  `expected` + `span` in `app_main()`. A new **non-blocking** `esp-idf` CI job
  (`continue-on-error: true`, matching the `zephyr` pattern) uses the official
  `espressif/esp-idf-ci-action` to `idf.py build` the sample for **`esp32`
  (Xtensa)** and **`esp32c3` (RISC-V)** in the pinned `espressif/idf:v5.3.3`
  Docker image — the Xtensa target adds a compiler frontend not covered by any
  other job. Provisional (non-blocking) until the Docker build wiring is
  validated green. See the README "ESP-IDF (ESP32)" and "Platform support
  matrix" sections.
- **Platform support matrix (README).** New "Platform support matrix" section
  enumerating CI-verified coverage (host gcc/clang/MSVC × Debug/Release/
  MinSizeRel/LTO, ASan/UBSan/TSan, ARM Cortex-M gcc+clang, RISC-V rv64, Xtensa
  ESP32 [provisional], PowerPC64 big-endian run, newlib-nano link, picolibc +
  QEMU run, Zephyr module [provisional], ESP-IDF component [provisional]) versus
  documented-only toolchains that are not automatically verified (IAR EWARM —
  proprietary, no free public CI; ARM Compiler 6 — LLVM-based, partially proxied
  by the `arm-cross-clang` job).
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

- **optional — genuine `constexpr` on C++20:** `metl::optional` now stores its
  value in a union (the active member is named directly, no `std::launder`) and
  routes its object lifetime through the new `metl::detail::construct_at` /
  `destroy_at` helpers (`metl/detail/construct.hpp`), which forward to
  `std::construct_at` / `std::destroy_at` (constant-evaluable since C++20) and
  fall back to placement-new / explicit destruction on C++17. As a result
  `constexpr metl::optional<int> o{42}; static_assert(*o == 42);` is a real
  constant expression on a C++20 toolchain. On C++17 behavior, size, and
  alignment are unchanged. The `const`-qualified `value()` overloads are now
  `constexpr` too. The remaining laundered-storage types
  (`expected`/`variant`/`fixed_vector`/`flat_map`/`flat_set`) carry an honest
  source note that their `constexpr` labels are effective only outside constant
  evaluation; a genuine conversion is deferred (see `docs/AUDIT.md`).
- **function_ref — dropped the `<memory>` include.** It was pulled in solely
  for `std::addressof` in one constructor; replaced with a tiny local
  `metl::detail::function_ref_addressof` (`__builtin_addressof`, universally
  available on the gcc/clang/MSVC matrix, with the standard operator&-defeating
  fallback). Behavior and API are unchanged; the header is lighter to include.
  The `<functional>` includes in `hash`, `optional`, `flat_map`/`flat_set`,
  `static_unordered_map`/`set`, and `intrusive_ptr` are deliberately kept: they
  back `std::hash` / `std::equal_to` / `std::less` that are genuinely required
  (and, for the map/set defaults, are part of the public type), so trimming them
  would change the API rather than just an include.
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

- **endian:** `endian::native` no longer silently assumes little-endian when the
  byte order can't be detected. `__BYTE_ORDER__` (defined by every supported GCC/Clang
  cross target, big-endian included) remains the authoritative signal; a chain of
  well-known secondary macros (`__BIG_ENDIAN__`, `__ARMEB__`, `__AARCH64EB__`,
  `__MIPSEB__`, … and their LE counterparts) is checked next, and if none resolve the
  header stops with an actionable `#error` instead of guessing little-endian (which
  would miscompile `to_/from_*_endian` on an undetected big-endian target). Now
  exercised by a big-endian (`powerpc64`) CI job.
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

- **Release coverage hole closed:** `atomic_ref_test` and `register_access_test`
  used bare `assert()` on locals that were consumed *only* by the assert. Under
  NDEBUG (Release) the asserts — and therefore those checks — compiled out, both
  hiding a real coverage gap and tripping `-Wunused-variable` under `-Werror`.
  Both are migrated to `CHECK` / `CHECK_EQ` from `tests/metl_check.hpp`, so the
  assertions now run in Release too and the variables are always used.
- Added `tests/optional_constexpr_test.cpp`, which proves `metl::optional` is
  constant-evaluable on C++20 (`static_assert`s guarded by
  `#if __cplusplus >= 202002L`, a no-op on the C++17 matrix) and runs a runtime
  smoke everywhere.
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

- **arm-cross code-size visibility** — the `arm-cross` job now also does an
  explicit `MinSizeRel` (`-Os`, NDEBUG) build of the embedded smoke library and
  prints `arm-none-eabi-size` for each Cortex-M target, so per-build binary size
  is visible in the log. Informational only (no hard threshold yet) — the number
  to watch for size regressions.
- **`release-werror` CI job** — builds and tests in `Release`
  (`-DCMAKE_BUILD_TYPE=Release -DMETL_WARNINGS_AS_ERRORS=ON`) with both gcc and
  clang, gating warning hygiene on the optimized/NDEBUG surface that the
  Debug-only sanitizer/arm jobs never see (e.g. asserts and their operands
  compiling out). Added only after the assert-only-variable tests were migrated
  to `CHECK`/`CHECK_EQ`, so it lands green.
- **Embedded / environment validation CI matrix** (mirrors the existing
  `arm-cross` job; existing jobs unchanged):
  - `cmake/riscv-none-elf.cmake` toolchain (analogue of `arm-none-eabi.cmake`) and a
    `riscv-cross` job building the freestanding embedded-smoke library with the
    bare-metal RISC-V GNU toolchain for `rv32imac` (ilp32) and `rv64` (lp64), selected
    via the new `METL_RISCV_ARCH` option. Compile + size only.
  - `arm-cross-clang` job — a second compiler frontend (clang, `--target=arm-none-eabi
    -mcpu=cortex-m4 -ffreestanding -fsyntax-only`) validates the public headers for
    bare-metal ARM, reusing `gcc-arm-none-eabi`'s freestanding libstdc++ headers.
  - `big-endian` job — `powerpc64-linux-gnu` syntax-checks every header big-endian and
    builds + runs `endian_test` under `qemu-user`, exercising `endian.hpp` on a real
    big-endian target.
  - `newlib-link` job — links `tests/embedded/semihost_smoke.cpp` against newlib-nano
    (`--specs=nano.specs --specs=nosys.specs`) for Cortex-M3, proving metl links
    against a real bare-metal libc (link + size, no run).
  - `picolibc-qemu` job — links the same program against picolibc with semihosting
    (`tests/embedded/mps2-an385.ld`) and runs it under `qemu-system-arm -semihosting`,
    asserting a success sentinel — proving metl links *and runs* on a real embedded
    libc. (picolibc + libstdc++ linking is version-sensitive; the `newlib-link` job is
    the guaranteed-green libc-link fallback.)
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
