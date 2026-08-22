# Modern Embedded Template Library (METL)

RFP v0.1

> **This is the founding request, kept as written. It is not the current plan.**
>
> Everything below describes what was asked for before any of it was built.
> Where building it produced a different answer, the answer is in
> [`SCOPE.md`](SCOPE.md) and **SCOPE.md wins** — it is the standing decision
> record, each entry carrying the argument that settled it. This document is
> here for provenance: it says what the project set out to do, which is worth
> being able to check a requirement against even when the requirement lost.
>
> A reader meeting this file first should know that several requirements below
> were deliberately not met:
>
> | This document asks for | What happened |
> |---|---|
> | **GoogleTest** (§8) | `tests/metl_check.hpp`, ~100 lines, no dependency. gtest would have been the first external dependency, and the same call was later made again for benchmarks. |
> | **cppcheck** (§8) | Not used. `clang-tidy` is blocking with a finding ratchet, and CodeQL runs weekly; a third analyser was not worth a third set of suppressions. |
> | **Benchmarks against STL / ETLCPP / Boost.Container / Abseil** (§8) | Not done, and not queued. The in-repo harness measures METL only. A comparison table is a claim about somebody else's library on one machine, and this repository has no way to keep it honest. |
> | **"Competitive performance against ETLCPP"** (§12) | Same reason. The measured performance claim METL does make is code size — a `.text` ratchet on a linked cross probe, in `tools/check_size.py`. |
> | **Intrusive structures** (§11, Phase 3) | **Rejected**, with an argument rather than a deferral: see the `intrusive_list` row in [`SCOPE.md`](SCOPE.md) §5 (Roadmap). Handles exist precisely to remove the failure class intrusive linkage reintroduces. |
> | The six named guides (§9) | Not written under those names. Their content lives in [`SCOPE.md`](SCOPE.md), [`CHOOSING.md`](CHOOSING.md), [`COOKBOOK.md`](COOKBOOK.md) and the per-symbol Doxygen. |
> | **MIT or Apache-2.0** (§10) | Resolved: Apache-2.0. |
>
> And one requirement here went unmet for a long time without anyone noticing,
> which is the better reason to keep this file. §1 lists **"Predictable memory
> usage"** as an objective and §8 lists **"Stack usage"** first among the
> metrics. Every other requirement in this document became an invariant with a
> CI job behind it; that one never got a number until 2026-08-23, when it turned
> out `static_unordered_map` costs 2.8x to 5.6x the data it stores and that
> asking for one more element can double the table. [`CHOOSING.md`](CHOOSING.md)
> now says so. A founding document is worth re-reading occasionally for exactly
> that: the requirement nobody argued with and nobody implemented.

## 1. Project Overview

### Project Name

METL (Modern Embedded Template Library)

### Objective

Build a modern embedded C++ library that combines the strengths of STL-like usability and embedded-focused determinism.

The project is intended to provide:

- Deterministic behavior
- No dynamic allocation by default
- Fixed-capacity containers
- Embedded-friendly API
- Modern C++17 implementation
- Cross-platform embedded support
- Header-only delivery
- Predictable memory usage

### Reference Philosophies

METL is informed by the design philosophies of:

- ETLCPP
- Abseil C++
- Folly

## 2. Project Goals

### Primary Goals

1. Provide STL-like usability for embedded environments.
2. Eliminate or tightly control heap allocation.
3. Guarantee compile-time predictable behavior where practical.
4. Support exception-free operation.
5. Maintain low-overhead abstractions.
6. Use modern C++17 implementation practices.
7. Remain RTOS- and baremetal-friendly.
8. Support host-side unit testing.

## 3. Scope

### In Scope

#### Core Utilities

- `type_traits`
- Compiler detection
- Config system
- Assert and panic handler
- Endian utilities
- Bit utilities
- `span`
- `optional`
- `expected`
- Variant-lite
- `fixed_function`
- `function_ref`

#### Containers

- `fixed_vector`
- `fixed_string`
- `fixed_queue`
- `fixed_deque`
- `fixed_stack`
- `ring_buffer`
- `flat_map`
- `flat_set`
- `static_unordered_map`
- `static_unordered_set`

#### Memory Utilities

- `object_pool`
- `static_allocator`
- `monotonic_buffer`
- Arena allocator
- `intrusive_ptr`

#### Embedded Utilities

- `crc8`, `crc16`, `crc32`
- Finite state machine
- Event dispatcher
- Delegate
- Compile-time lookup table
- Static message queue

#### Concurrency (Optional)

- `spsc_queue`
- `mpmc_queue`
- Lock-free ring buffer

## 4. Out of Scope

- Full STL replacement
- Dynamic runtime reflection
- Garbage collection
- Mandatory exception support
- RTTI dependency
- Heavy async runtime
- Coroutines as a core dependency
- OS-specific abstraction layer

## 5. Technical Requirements

### Language Standard

Primary target:

- C++17

Compatibility goal:

- Partial C++14 compatibility

### Compiler Support

Tier 1:

- GCC ARM Embedded
- Clang
- MSVC

Tier 2:

- IAR Embedded Workbench
- Keil ARM Compiler

### Platform Targets

- ARM Cortex-M
- ARM Cortex-A
- RISC-V
- Linux host testing
- Baremetal systems
- RTOS systems

## 6. Design Principles

### Deterministic Design

All public APIs should aim for:

- Predictable execution time
- Predictable memory usage
- Explicit ownership
- No hidden allocation

### Allocation Policy

Default:

- No heap allocation

Optional:

- User-provided allocator

### Error Handling Policy

Preferred:

- `expected<T, E>`
- Status/result patterns

Optional:

- Assert hook
- Panic handler

### ABI and API Stability

- Public API stability is prioritized.
- Internal implementation may evolve.
- Semantic versioning is required.

## 7. Architecture

```text
metl/
├── core/
├── containers/
├── memory/
├── utility/
├── algorithm/
├── embedded/
├── concurrency/
├── tests/
├── benchmarks/
├── examples/
└── docs/
```

## 8. Quality Requirements

### Static Analysis

Required:

- `clang-tidy`
- `cppcheck`

### Testing

- GoogleTest
- Host-based simulation tests
- Compile-time tests
- Property-based tests (optional)

### Benchmarking

Metrics:

- Stack usage
- Binary size
- Throughput
- Latency
- Allocation count

Comparison targets:

- STL
- ETLCPP
- Boost.Container
- Abseil

## 9. Documentation Requirements

Required documents:

- API Reference
- Memory Model Guide
- Deterministic Design Guide
- Porting Guide
- Compiler Compatibility Matrix
- Embedded Best Practices

## 10. License

Recommended:

- MIT

or

- Apache-2.0

## 11. Development Phases

### Phase 1: Core Foundation

- Core utilities
- Fixed containers
- `span`, `optional`, `expected`

### Phase 2: Embedded Features

- Pools
- FSM
- CRC
- Delegates

### Phase 3: Performance Layer

- Lock-free queues
- Flat containers
- Intrusive structures

## 12. Success Criteria

- Zero heap allocation in core containers
- Stable operation on Cortex-M targets
- Predictable memory footprint
- Identical API behavior across host and embedded targets
- Competitive performance against ETLCPP

## 13. Long-Term Vision

METL should become a modern, deterministic, embedded-oriented C++ utility library that combines:

- ETLCPP stability philosophy
- Abseil API clarity
- Folly-inspired performance awareness
- Modern C++17 implementation simplicity

## 14. Immediate Next Steps

1. Define namespace, config macros, and public include layout.
2. Establish toolchain and host-test baseline.
3. Implement the first core types: `span`, `optional`, and `fixed_vector`.
4. Add compile-time and runtime validation strategy.
5. Publish contribution and compatibility policies early.
