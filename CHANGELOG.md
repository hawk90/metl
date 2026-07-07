# Changelog

All notable changes to METL are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] / [0.1.0-alpha1]

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
