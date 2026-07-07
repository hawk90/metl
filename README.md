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

Function objects

- [`fixed_function`](include/metl/fixed_function.hpp) — SBO function with
  `noexcept` signature and `fixed_any_invocable`.
- [`function_ref`](include/metl/function_ref.hpp) — non-owning callable.
- [`delegate`](include/metl/delegate.hpp),
  [`event_dispatcher`](include/metl/event_dispatcher.hpp).

Memory

- [`intrusive_ptr`](include/metl/intrusive_ptr.hpp) with CRTP refcount base.
- [`arena_allocator`](include/metl/arena_allocator.hpp),
  [`monotonic_buffer`](include/metl/monotonic_buffer.hpp),
  [`static_allocator`](include/metl/static_allocator.hpp).
- [`object_pool`](include/metl/object_pool.hpp).

Concurrency

- [`spsc_queue`](include/metl/spsc_queue.hpp).
- [`static_message_queue`](include/metl/static_message_queue.hpp).
- [`atomic_ref`](include/metl/atomic_ref.hpp).

Utility

- [`scope_exit`](include/metl/scope_exit.hpp),
  [`fsm`](include/metl/fsm.hpp),
  [`lookup_table`](include/metl/lookup_table.hpp).
- [`bit`](include/metl/bit.hpp),
  [`endian`](include/metl/endian.hpp).
- [`crc8`](include/metl/crc8.hpp),
  [`crc16`](include/metl/crc16.hpp),
  [`crc32`](include/metl/crc32.hpp).

Embedded

- [`mmio`](include/metl/mmio.hpp),
  [`bitfield`](include/metl/bitfield.hpp),
  [`register_access`](include/metl/register_access.hpp).

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
