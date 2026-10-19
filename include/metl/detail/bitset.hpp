#pragma once

/// @file
/// @brief Packed fixed-size bit storage: the implementation behind
/// `metl::exp::bitset` (metl/exp/bitset.hpp), where its contract is stated.
///
/// It lives in `detail` rather than in `exp/` so that a Tier 0 type can use it
/// without depending on a Tier 2 header: `handle_pool`'s occupancy flags are
/// the candidate (#336). There is no public `metl::detail::bitset` API contract.

#include "metl/attributes.hpp"
#include "metl/bit.hpp"
#include "metl/config.hpp"

#include <cstddef>
#include <cstdint>

namespace metl {
namespace detail {

template <std::size_t N>
class bitset {
  static_assert(N > 0, "metl::exp::bitset needs at least one bit");

 public:
  using size_type = std::size_t;
  using word_type = std::uint32_t;

  static constexpr size_type word_bits = 32;
  static constexpr size_type word_count = (N + word_bits - 1) / word_bits;

  constexpr bitset() noexcept = default;

  METL_NODISCARD static constexpr size_type size() noexcept { return N; }

  METL_NODISCARD constexpr bool test(size_type index) const noexcept {
    METL_HARDEN(index < N);
    return ((words_[index / word_bits] >> (index % word_bits)) & 1U) != 0U;
  }

  constexpr void set(size_type index) noexcept {
    METL_HARDEN(index < N);
    words_[index / word_bits] |= bit(index);
  }

  constexpr void reset(size_type index) noexcept {
    METL_HARDEN(index < N);
    words_[index / word_bits] &= ~bit(index);
  }

  constexpr void reset() noexcept {
    for (size_type k = 0; k < word_count; ++k) {
      words_[k] = 0;
    }
  }

  METL_NODISCARD constexpr size_type count() const noexcept {
    size_type total = 0;
    for (size_type k = 0; k < word_count; ++k) {
      total += static_cast<size_type>(metl::popcount(words_[k]));
    }
    return total;
  }

  METL_NODISCARD constexpr bool any() const noexcept {
    for (size_type k = 0; k < word_count; ++k) {
      if (words_[k] != 0) {
        return true;
      }
    }
    return false;
  }

  METL_NODISCARD constexpr bool none() const noexcept { return !any(); }

  METL_NODISCARD constexpr bool all() const noexcept { return count() == N; }

  METL_NODISCARD constexpr size_type find_first_set() const noexcept {
    for (size_type k = 0; k < word_count; ++k) {
      if (words_[k] != 0) {
        return (k * word_bits) + static_cast<size_type>(metl::countr_zero(words_[k]));
      }
    }
    return N;
  }

  METL_NODISCARD constexpr size_type find_first_clear() const noexcept {
    for (size_type k = 0; k < word_count; ++k) {
      // No mask for the tail: its bits are always zero, so when every bit
      // below N is set the lowest clear bit found is bit N itself.
      const word_type clear = ~words_[k];
      if (clear != 0) {
        return (k * word_bits) + static_cast<size_type>(metl::countr_zero(clear));
      }
    }
    return N;
  }

  /// Bit `i` moves to `i + count`; bits shifted past `N - 1` are lost.
  constexpr bitset& operator<<=(size_type count) noexcept {
    if (count >= N) {
      reset();
      return *this;
    }
    const size_type words = count / word_bits;
    const size_type bits = count % word_bits;
    for (size_type k = word_count; k-- > 0;) {
      word_type value = 0;
      if (k >= words) {
        value = static_cast<word_type>(words_[k - words] << bits);
        if (bits != 0 && k > words) {
          value |= static_cast<word_type>(words_[k - words - 1] >> (word_bits - bits));
        }
      }
      words_[k] = value;
    }
    words_[word_count - 1] &= valid_mask(word_count - 1);
    return *this;
  }

  /// Bit `i` moves to `i - count`; bits shifted below 0 are lost.
  constexpr bitset& operator>>=(size_type count) noexcept {
    if (count >= N) {
      reset();
      return *this;
    }
    const size_type words = count / word_bits;
    const size_type bits = count % word_bits;
    for (size_type k = 0; k < word_count; ++k) {
      word_type value = 0;
      if (k + words < word_count) {
        value = static_cast<word_type>(words_[k + words] >> bits);
        if (bits != 0 && k + words + 1 < word_count) {
          value |= static_cast<word_type>(words_[k + words + 1] << (word_bits - bits));
        }
      }
      words_[k] = value;
    }
    return *this;
  }

  METL_NODISCARD friend constexpr bool operator==(const bitset& lhs, const bitset& rhs) noexcept {
    for (size_type k = 0; k < word_count; ++k) {
      if (lhs.words_[k] != rhs.words_[k]) {
        return false;
      }
    }
    return true;
  }

  METL_NODISCARD friend constexpr bool operator!=(const bitset& lhs, const bitset& rhs) noexcept {
    return !(lhs == rhs);
  }

 private:
  static constexpr word_type bit(size_type index) noexcept { return word_type{1} << (index % word_bits); }

  /// The bits of word `k` that hold a value: all of them, except in the last
  /// word when N is not a multiple of the word size.
  static constexpr word_type valid_mask(size_type k) noexcept {
    const size_type tail = N % word_bits;
    if (k + 1 < word_count || tail == 0) {
      return ~word_type{0};
    }
    return static_cast<word_type>((word_type{1} << tail) - 1U);
  }

  word_type words_[word_count] = {};
};

}  // namespace detail
}  // namespace metl
