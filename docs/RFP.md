# Modern Embedded Template Library (METL)

RFP v0.1

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
