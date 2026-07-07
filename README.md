# METL

Modern Embedded Template Library

[![CI](https://github.com/hawk90/metl/actions/workflows/ci.yml/badge.svg)](https://github.com/hawk90/metl/actions/workflows/ci.yml)
[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)

## Overview

METL is a header-only C++17 library that provides STL-like building blocks
designed for deterministic, embedded-oriented systems. It targets bare-metal
firmware, real-time control loops, and resource-constrained applications where
hidden allocations, exceptions, and RTTI are unacceptable, while remaining
fully usable on host platforms for development and testing.

## Highlights

- Header-only; drop the `include/` directory into any project.
- C++17, with no compiler extensions required.
- No exceptions, no heap, and no RTTI by default.
- Deterministic, fixed-capacity data structures and allocators.
- Bazel-style CMake helpers for libraries, tests, and binaries (plus
  scaffolding for future google/benchmark-based benchmarks).
- Clean under AddressSanitizer and UndefinedBehaviorSanitizer with
  `-Werror` enabled.

## Quick start

```cpp
#include <metl/fixed_vector.hpp>
#include <metl/optional.hpp>

metl::optional<int> first_even(metl::span<const int> xs) {
    for (int x : xs) {
        if (x % 2 == 0) return x;
    }
    return metl::nullopt;
}

int main() {
    metl::fixed_vector<int, 8> v;
    v.push_back(1);
    v.push_back(4);
    v.push_back(9);

    if (auto x = first_even(v)) {
        return *x;
    }
    return -1;
}
```

## Modules

Core types

- [`span`](include/metl/span.hpp) — non-owning contiguous view.
- [`optional`](include/metl/optional.hpp) — value-or-empty with monadic ops.
- [`expected`](include/metl/expected.hpp) — value-or-error, including `void`.
- [`variant`](include/metl/variant.hpp) — tagged union with visitation.
- [`in_place`](include/metl/in_place.hpp) — in-place construction tags.
- [`type_traits`](include/metl/type_traits.hpp) — trait helpers and
  `storage_for<T>`.
- [`hash`](include/metl/hash.hpp) — FNV-1a hashing and `hash_combine`.

Error handling without exceptions — worked example:
[`examples/error_handling.cpp`](examples/error_handling.cpp)
(`expected` + `optional` + `variant`).

> Contract note: `optional::value()`, `expected::value()`, and `get<T>()`
> **assert** (abort) on the empty / wrong-alternative case — there is no
> `bad_*_access` exception. Check `has_value()` / `holds_alternative<>()` first,
> or use `value_or` / `get_if` / `find`.

Containers

- [`fixed_vector`](include/metl/fixed_vector.hpp),
  [`fixed_string`](include/metl/fixed_string.hpp).
- [`fixed_queue`](include/metl/fixed_queue.hpp),
  [`fixed_stack`](include/metl/fixed_stack.hpp),
  [`fixed_deque`](include/metl/fixed_deque.hpp).
- [`ring_buffer`](include/metl/ring_buffer.hpp).
- [`flat_map`](include/metl/flat_map.hpp),
  [`flat_set`](include/metl/flat_set.hpp).
- [`static_unordered_map`](include/metl/static_unordered_map.hpp),
  [`static_unordered_set`](include/metl/static_unordered_set.hpp).

Worked example: [`examples/containers.cpp`](examples/containers.cpp)
(`fixed_vector` + `flat_map` + `ring_buffer`).

> Contract notes: `at()` **asserts** on out-of-range (it does not throw).
> `flat_map::operator[]` / `at()` are **positional** index accessors over the
> sorted storage — the *opposite* of `std::map`. Look up by key with
> `find` / `contains` / `try_emplace` instead.

Function objects

- [`fixed_function`](include/metl/fixed_function.hpp) — SBO function with
  `noexcept` signature and `fixed_any_invocable`.
- [`function_ref`](include/metl/function_ref.hpp) — non-owning callable
  (**lvalue callables only** — rvalue temporaries are rejected to avoid
  dangling).
- [`delegate`](include/metl/delegate.hpp),
  [`event_dispatcher`](include/metl/event_dispatcher.hpp).

Memory

- [`intrusive_ptr`](include/metl/intrusive_ptr.hpp) with CRTP refcount base.
- [`arena_allocator`](include/metl/arena_allocator.hpp),
  [`monotonic_buffer`](include/metl/monotonic_buffer.hpp),
  [`static_allocator`](include/metl/static_allocator.hpp).
- [`object_pool`](include/metl/object_pool.hpp).

Worked example: [`examples/allocators.cpp`](examples/allocators.cpp)
(`monotonic_buffer` per-tick scratch + `arena_allocator` LIFO mark/rewind).

Concurrency

- [`spsc_queue`](include/metl/spsc_queue.hpp).
- [`static_message_queue`](include/metl/static_message_queue.hpp).
- [`atomic_ref`](include/metl/atomic_ref.hpp).

Worked example: [`examples/spsc_isr.cpp`](examples/spsc_isr.cpp)
(ISR-to-main-loop `spsc_queue` pattern).

Coroutines / cooperative tasks

- [`coro/protothread`](include/metl/coro/protothread.hpp),
  [`coro/stepper`](include/metl/coro/stepper.hpp),
  [`coro/scheduler`](include/metl/coro/scheduler.hpp).

Worked example: [`examples/coroutine_task.cpp`](examples/coroutine_task.cpp).

Utility

- [`scope_exit`](include/metl/scope_exit.hpp),
  [`fsm`](include/metl/fsm.hpp),
  [`lookup_table`](include/metl/lookup_table.hpp).
- [`bit`](include/metl/bit.hpp),
  [`endian`](include/metl/endian.hpp).
- [`crc8`](include/metl/crc8.hpp),
  [`crc16`](include/metl/crc16.hpp),
  [`crc32`](include/metl/crc32.hpp).

Worked example (`fsm`): [`examples/blinky_fsm.cpp`](examples/blinky_fsm.cpp).

Embedded

- [`mmio`](include/metl/mmio.hpp),
  [`bitfield`](include/metl/bitfield.hpp),
  [`register_access`](include/metl/register_access.hpp).

Worked example: [`examples/mmio_peripheral.cpp`](examples/mmio_peripheral.cpp)
(a fake memory-mapped UART).

## Documentation

- **[Cookbook](docs/COOKBOOK.md)** — task-oriented recipes (fixed-capacity
  vector, error handling without exceptions, ISR↔main-loop SPSC queue,
  memory-mapped registers, a small FSM, …). Every snippet mirrors a compiled
  example.
- **[Examples](examples/)** — self-contained, `main()`-returning-0 programs,
  each compiled under `-Wall -Wextra -Werror -std=c++17` and run in CI:

  | Example | Modules |
  | --- | --- |
  | [`containers.cpp`](examples/containers.cpp) | `fixed_vector`, `flat_map`, `ring_buffer` |
  | [`allocators.cpp`](examples/allocators.cpp) | `arena_allocator`, `monotonic_buffer` |
  | [`spsc_isr.cpp`](examples/spsc_isr.cpp) | `spsc_queue` (ISR pattern) |
  | [`mmio_peripheral.cpp`](examples/mmio_peripheral.cpp) | `mmio`, `register_access`, `bitfield` |
  | [`error_handling.cpp`](examples/error_handling.cpp) | `expected`, `optional`, `variant` |
  | [`coroutine_task.cpp`](examples/coroutine_task.cpp) | `coro/protothread` |
  | [`blinky_fsm.cpp`](examples/blinky_fsm.cpp) | `fsm`, `mmio`, `delegate` |
  | [`can_frame_parser.cpp`](examples/can_frame_parser.cpp) | `bitfield`, `crc16`, `span`, `expected` |
  | [`sensor_pipeline.cpp`](examples/sensor_pipeline.cpp) | `spsc_queue`→`ring_buffer`→`fixed_vector` (threaded) |

  Build and run them with:

  ```sh
  cmake -B build -S . -DMETL_BUILD_EXAMPLES=ON
  cmake --build build -j
  ctest --test-dir build -R metl_example --output-on-failure
  ```

- **API reference (Doxygen)** — generate HTML from the public headers:

  ```sh
  cmake -B build -S .
  cmake --build build --target docs   # requires doxygen; output in build/docs/html
  ```

- **Non-standard contracts** — a few types deliberately diverge from `std::`
  (asserting accessors, positional `flat_map` indexing, lvalue-only
  `function_ref`). See the
  [contracts table in the Cookbook](docs/COOKBOOK.md#non-standard-contracts-you-must-know).

## Build & test

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build -j
```

With sanitizers and warnings-as-errors:

```sh
cmake -B build -S . -DMETL_ENABLE_ASAN=ON -DMETL_ENABLE_UBSAN=ON -DMETL_WARNINGS_AS_ERRORS=ON
cmake --build build -j
ctest --test-dir build -j
```

## CMake integration

After `cmake --install`, downstream projects can consume METL via
`find_package`:

```cmake
find_package(metl CONFIG REQUIRED)

add_executable(app main.cpp)
target_link_libraries(app PRIVATE metl::metl)
```

## Zephyr

METL ships as a header-only [Zephyr module](https://docs.zephyrproject.org/latest/develop/modules.html).
The module manifest (`zephyr/module.yml`) plus a tiny CMake/Kconfig shim
(`zephyr/CMakeLists.txt`, `zephyr/Kconfig`) expose METL's `include/` directory
to Zephyr applications — no sources are compiled into the RTOS image
(interface-only module).

Enable it from an application's `prj.conf`:

```conf
CONFIG_CPP=y
CONFIG_STD_CPP17=y
CONFIG_METL=y
# metl is exception-free / RTTI-free; keep both OFF (Zephyr default).
CONFIG_CPP_EXCEPTIONS=n
CONFIG_CPP_RTTI=n
```

Point Zephyr at this checkout as an extra module and build the bundled sample
([`samples/zephyr/metl_hello`](samples/zephyr/metl_hello)):

```sh
# From an initialized Zephyr workspace (west + Zephyr SDK):
west build -b qemu_cortex_m3 samples/zephyr/metl_hello \
  -- -DEXTRA_ZEPHYR_MODULES=/abs/path/to/metl

# Build and RUN on QEMU, asserting the sample's success sentinel:
west twister -p qemu_cortex_m3 -T samples/zephyr/metl_hello \
  -x=EXTRA_ZEPHYR_MODULES=/abs/path/to/metl
```

Alternatively, add METL to your workspace's `west.yml` manifest so `west update`
fetches it and it is discovered as a module automatically. Application code then
just includes the headers:

```cpp
#include <zephyr/kernel.h>
#include <metl/fixed_vector.hpp>
#include <metl/expected.hpp>

int main(void) {
    metl::fixed_vector<int, 8> v;
    v.push_back(42);
    printk("metl on Zephyr: %d\n", v[0]);
    return 0;
}
```

> C++ standard library note: the sample uses `CONFIG_REQUIRES_FULL_LIBCPP=y` as
> a low-risk default. METL is freestanding-friendly, so the minimal libc may
> also work for headers that don't pull in `<functional>` — see the comments in
> [`samples/zephyr/metl_hello/prj.conf`](samples/zephyr/metl_hello/prj.conf).

## ESP-IDF (ESP32)

METL ships as a header-only
[ESP-IDF component](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html).
A component shim (`components/metl/CMakeLists.txt`) registers METL's `include/`
directory via `idf_component_register(INCLUDE_DIRS ...)` with no sources
(header-only), and an `idf_component.yml` manifest makes it consumable by the
IDF Component Manager. Any component that declares `REQUIRES metl` can then
`#include <metl/...>`.

Build the bundled sample
([`samples/esp-idf/metl_hello`](samples/esp-idf/metl_hello)) — its top-level
`CMakeLists.txt` adds this repo's `components/` directory via
`EXTRA_COMPONENT_DIRS`, and `main/` declares `REQUIRES metl`:

```sh
# From an ESP-IDF environment (idf.py on PATH, IDF_PATH set):
cd samples/esp-idf/metl_hello
idf.py set-target esp32       # or esp32c3 (RISC-V), esp32s3, ...
idf.py build
```

Application code just includes the headers; ESP-IDF compiles `.cpp` as C++17 by
default and calls `app_main()` (which needs C linkage):

```cpp
#include <cstdio>
#include <metl/fixed_vector.hpp>
#include <metl/expected.hpp>

extern "C" void app_main(void) {
    metl::fixed_vector<int, 8> v;
    v.push_back(42);
    std::printf("metl on ESP-IDF: %d\n", v[0]);
}
```

To consume METL from your own IDF project without vendoring it, point
`EXTRA_COMPONENT_DIRS` at this checkout's `components/` directory, or add a local
`path:` dependency on `components/metl` in your `main/idf_component.yml`.

> Provisional: the component + sample are structurally complete and follow
> ESP-IDF conventions, but the `esp-idf` CI job (which builds the sample for
> `esp32`/`esp32c3` in the official Espressif Docker image) is non-blocking
> while its IDF-version / target wiring is tuned — see the CI notes below.

## Compatibility matrix

| Toolchain         | Minimum version |
| ----------------- | --------------- |
| GCC               | 9               |
| Clang             | 11              |
| MSVC              | 19.20 (VS 2019) |
| arm-none-eabi-gcc | 10              |

Tested targets:

- Cortex-M0, Cortex-M3, Cortex-M4, Cortex-M7
- Cortex-A class
- RISC-V (32-bit and 64-bit)
- x86-64 host (Linux, macOS, Windows)

## Platform support matrix

What CI actually exercises versus what is supported-but-not-automatically-verified.
"CI-verified" means a job in [`.github/workflows/ci.yml`](.github/workflows/ci.yml)
builds (and, where noted, runs) METL on that platform on every push/PR.

### CI-verified

| Area | Platform / config | What CI does | Job |
| --- | --- | --- | --- |
| Host | Linux / macOS / Windows × gcc / clang / MSVC × Debug / Release / MinSizeRel | build + `ctest` | `host-test` |
| Host hardening | Release **+ `-Werror`** (clang) | build + `ctest` (NDEBUG warning gate) | `release-werror` |
| Host LTO | Release + IPO/LTO | build + `ctest` | `lto` |
| Sanitizers | Linux / clang — ASan+UBSan, TSan (Debug, `-Werror`) | build + `ctest` (incl. threaded tests) | `sanitizers` |
| ARM Cortex-M (gcc) | Cortex-M0/M3/M4/M7, freestanding | cross-compile + code size | `arm-cross` |
| ARM Cortex-M (clang) | cortex-m4, `arm-none-eabi` target | second frontend, `-fsyntax-only` | `arm-cross-clang` |
| RISC-V | rv64 (linux-gnu g++) | freestanding `-fsyntax-only` | `riscv-cross` |
| Xtensa (ESP32) | ESP-IDF component, `esp32` target | `idf.py build` (Docker) — **provisional** | `esp-idf` |
| RISC-V (ESP32-C3) | ESP-IDF component, `esp32c3` target | `idf.py build` (Docker) — **provisional** | `esp-idf` |
| Big-endian | powerpc64 (BE) | build headers + build & **run** endian test under qemu-user | `big-endian` |
| Bare-metal libc (link) | Cortex-M3 + newlib-nano (nosys) | compile + **link** + size | `newlib-link` |
| Bare-metal libc (run) | Cortex-M3 + picolibc, mps2-an385 | link + **run** under qemu-system-arm (semihosting) | `picolibc-qemu` |
| Zephyr RTOS | qemu_cortex_m3 module build + run | `west build` + twister **run** (QEMU) — **provisional** | `zephyr` |
| Header hygiene | per-header self-containment + umbrella completeness | `-fsyntax-only` per header + `ctest` | `header-checks` |
| Install / consume | `find_package(metl)` downstream | install + build + **run** a consumer | `install-check` |
| Examples | every `examples/*.cpp`, `-Wall -Wextra -Werror` | build + **run** (self-checking) | `examples` |

Cross-cutting frontends covered by the above: **GCC**, **Clang**, and **MSVC** on
host; **arm-none-eabi-gcc** and **clang** (bare-metal ARM); **RISC-V** (rv64 and
ESP32-C3); **Xtensa** (ESP32); **PowerPC64** big-endian. Build types: Debug,
Release, MinSizeRel (`-Os`), plus LTO. Runtime configs: no-exceptions, no-RTTI,
freestanding, newlib-nano and picolibc libcs.

> **Provisional** jobs (`esp-idf`, `zephyr`) are `continue-on-error: true`:
> their component/module wiring is real and correct-by-construction, but the
> Docker/RTOS build automation is still being tuned, so they do not gate the
> pipeline yet.

### Documented-only (not CI-verified)

METL is written to be portable to these toolchains, but there is no free public
CI that automatically verifies them. They are supported on a best-effort basis:

| Toolchain | Status | Notes |
| --- | --- | --- |
| **IAR EWARM** | Not CI-verified | Proprietary; no free public CI runner exists. METL avoids GNU-only extensions and compiles under `-Wpedantic` specifically to maximize compatibility with strict/commercial compilers, but IAR is **not** automatically exercised — treat it as supported-by-design, verify locally. |
| **ARM Compiler 6 (armclang)** | Partially proxied | Arm Compiler 6 is LLVM/Clang-based, so the `arm-cross-clang` job (clang → `arm-none-eabi`) covers the same frontend family; the exact armclang driver/CMSIS toolchain is not run in CI. |

## Design principles

- No hidden allocation — every allocation is visible at the call site.
- Explicit ownership — types describe lifetime and storage clearly.
- Deterministic execution time — no surprise reallocation or rehashing.
- Exception-free friendly — errors are returned via `expected`-style values.
- Low-overhead abstractions — zero-cost wherever possible.
- Host and embedded parity — the same code runs in both environments.
- Composable building blocks — small, orthogonal types over monolithic ones.

## Project layout

```text
metl/
├── CMakeLists.txt
├── cmake/
├── include/
│   └── metl/
├── tests/
├── examples/
├── samples/
│   ├── zephyr/         # Zephyr sample app (metl_hello)
│   └── esp-idf/        # ESP-IDF sample app (metl_hello)
├── zephyr/             # Zephyr module manifest + CMake/Kconfig shim
├── components/
│   └── metl/           # ESP-IDF component shim (idf_component_register + manifest)
├── docs/
└── .github/
```

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for build instructions, code style,
sanitizer requirements, and the pull request checklist.

## License

METL is licensed under the [Apache License, Version 2.0](LICENSE).

## Status

Pre-alpha (`v0.1.0-alpha1`). The public API may change without notice before
the 1.0 release. See [CHANGELOG.md](CHANGELOG.md) for the current state.
