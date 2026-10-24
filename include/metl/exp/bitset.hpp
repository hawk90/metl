#pragma once

/// @file
/// @brief `metl::exp::bitset<N>`: N bits packed into 32-bit words.
///
/// experimental: may change or be removed in a minor release.
/// Tracking issue: #506, which decides promotion in 1.9.0 on the criteria #336
/// set, including when it is removed. It is not included by <metl/metl.hpp>, and the 1.x Semantic
/// Versioning promise does not cover it (docs/SCOPE.md section 3, Tier 2).

#include "metl/detail/bitset.hpp"

#include <cstddef>

namespace metl {
namespace exp {

/// @brief A fixed-size set of N bits, packed 32 to a word.
///
/// The experiment is whether packing pays for itself. A packed bit is a
/// read-modify-write where a `bool` flag is a plain store, so a small set can
/// cost more code than the RAM it saves. #336 measured it as `handle_pool`'s
/// occupancy flags and found it slower there, so `handle_pool` keeps no flag at
/// all. Its callers, through `detail::bitset`, are `object_pool`'s live bits
/// (#503) and the unordered containers' two-bit bucket states (#504).
///
/// Storage is `ceil(N / 32)` words of `std::uint32_t` and nothing else. Bit `i`
/// is bit `i % 32` of word `i / 32`. That order is stated so the operations
/// below are exact. It is not a wire format: there is no access to the words,
/// and the bits past `N - 1` in the last word are always zero.
///
/// Operations:
///   * `test(i)`, `set(i)`, `reset(i)`: one bit. `i < N` is checked with
///     METL_HARDEN, so an index past the end stops the program at every
///     hardening level instead of reading or writing past the storage.
///   * `reset()`: every bit.
///   * `count()`, `any()`, `none()`, `all()`.
///   * `find_first_set()`, `find_first_clear()`: the lowest such index, or `N`
///     when there is none. A full set has no clear bit, whatever the tail holds.
///   * `<<=` moves bit `i` to `i + n` and `>>=` moves it to `i - n`; bits moved
///     outside `[0, N)` are lost, and a shift by `N` or more clears the set.
///   * `==`, `!=`.
///
/// Everything is `constexpr` and `noexcept`, and a default-constructed bitset
/// is all clear and constant-initialized.
///
/// Progress guarantees:
///
///   | Operation                              | Guarantee                   |
///   |----------------------------------------|-----------------------------|
///   | `test` / `set` / `reset(i)`            | wait-free, bounded, O(1)    |
///   | `reset()` / `count` / `any` / `none` / `all` | wait-free, bounded, O(N/32) |
///   | `find_first_set` / `find_first_clear`  | wait-free, bounded, O(N/32) |
///   | `<<=` / `>>=` / `==` / `!=`            | wait-free, bounded, O(N/32) |
///
/// @note Not thread-safe: `set` and `reset` are read-modify-writes of a whole
///       word, so two writers, or a writer and an ISR, touching different bits
///       of one word lose updates. Synchronize, as for any other container.
///
/// @tparam N Number of bits; at least 1.
template <std::size_t N>
using bitset = metl::detail::bitset<N>;

}  // namespace exp
}  // namespace metl
