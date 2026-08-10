#pragma once

#include "metl/attributes.hpp"
#include "metl/config.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace metl {

/// @file
/// @brief Generation-tagged index handle: a pointer replacement for
///        fixed-capacity containers.
///
/// A `versioned_handle` is `{index, generation}` packed into a single unsigned
/// integer. It exists because METL containers are fixed-capacity, which makes
/// storing a pointer unnecessary: a slot is identified by its index, and the
/// generation counter distinguishes the object that occupies the slot *now*
/// from one that occupied it earlier.
///
/// Why not tag the spare bits of a pointer, the way general-purpose libraries
/// do? Two reasons, and the second is the decisive one:
///
///   * Alignment tagging yields ~3 bits (`alignof(Node) == 8`), which is enough
///     for a flag but not for a counter that has to survive wraparound.
///   * Upper-bit packing yields ~16 bits but assumes the top of a pointer is
///     software's to use. It is not, and increasingly less so: AArch64 PAC signs
///     the upper bits, MTE claims 56-59, x86-64 LA57 leaves seven, Intel LAM and
///     AMD UAI give the top bits hardware semantics, and HWASAN uses the top
///     byte. A library that does not control its deployment target cannot make
///     that assumption.
///
/// A handle makes no assumption about pointer representation at all, so it is
/// immune to every item on that list, and it carries a full-width generation
/// counter rather than three bits. It is also half the size of a pointer on a
/// 64-bit host, and small enough (32 bits by default) that the atomic form fits
/// a single-word CAS on 32-bit MCUs.
///
/// @see handle_pool.hpp for the container that issues and validates handles.

namespace detail {

/// Maps a total bit width to the unsigned integer that holds it, rounded up to
/// the next natural machine word so the atomic form can be a single-word CAS.
/// A 16-bit index with an 8-bit generation lands in a 32-bit word with 8 bits
/// spare rather than in some awkward 24-bit representation.
template <std::size_t Bits>
struct handle_packed_type {
  static_assert(Bits <= 64, "versioned_handle index + generation must fit 64 bits");
  using type = std::conditional_t<(Bits <= 16),
                                  std::uint16_t,
                                  std::conditional_t<(Bits <= 32), std::uint32_t, std::uint64_t>>;
};

}  // namespace detail

/// @brief `{index, generation}` packed into one unsigned integer.
///
/// @tparam Tag Phantom type that keeps handles from different containers from
///         mixing. `handle_pool` passes itself, so a handle from one pool will
///         not compile against another. Costs nothing at runtime.
/// @tparam IndexT Unsigned integer holding the slot index (low bits).
/// @tparam GenT Unsigned integer holding the generation counter (high bits).
///
/// **Generation 0 is reserved** to mean "no handle". A default-constructed
/// handle is null, and `handle_pool` never issues generation 0 (its counter
/// skips 0 on wraparound).
///
/// Progress guarantee: every operation is wait-free and bounded — pure integer
/// arithmetic, no loops, no branches on data. Safe in ISR context.
///
/// @note Trivially copyable, so a handle can be memcpy'd, stored in a POD
///       message struct, or sent through a queue like any other integer.
template <typename Tag, typename IndexT = std::uint16_t, typename GenT = std::uint16_t>
class versioned_handle {
  static_assert(std::is_unsigned<IndexT>::value, "versioned_handle IndexT must be unsigned");
  static_assert(std::is_unsigned<GenT>::value, "versioned_handle GenT must be unsigned");

 public:
  using tag_type = Tag;
  using index_type = IndexT;
  using generation_type = GenT;

  /// Number of bits each field occupies within the packed value.
  static constexpr std::size_t index_bits = sizeof(IndexT) * 8u;
  static constexpr std::size_t generation_bits = sizeof(GenT) * 8u;

  /// The single unsigned integer the handle packs into.
  using packed_type = typename detail::handle_packed_type<index_bits + generation_bits>::type;

  /// Largest representable slot index.
  static constexpr index_type max_index = static_cast<index_type>(~index_type{0});
  /// Largest representable generation.
  static constexpr generation_type max_generation = static_cast<generation_type>(~generation_type{0});

  /// Constructs the null handle (generation 0).
  constexpr versioned_handle() noexcept : value_(0) {}

  /// Constructs a handle for `index` at `generation`.
  /// @note A `generation` of 0 produces a null handle by construction; that
  ///       value is reserved and `handle_pool` never issues it.
  constexpr versioned_handle(index_type index, generation_type generation) noexcept
      : value_(static_cast<packed_type>(static_cast<packed_type>(index) |
                                        (static_cast<packed_type>(generation) << index_bits))) {}

  /// Rebuilds a handle from its packed representation (e.g. after an atomic load).
  METL_NODISCARD static constexpr versioned_handle from_packed(packed_type packed) noexcept {
    versioned_handle handle;
    handle.value_ = packed;
    return handle;
  }

  /// The packed representation — what an atomic form stores.
  METL_NODISCARD constexpr packed_type packed() const noexcept { return value_; }

  /// The slot index. Meaningful only when the handle is non-null.
  METL_NODISCARD constexpr index_type index() const noexcept {
    return static_cast<index_type>(value_ & static_cast<packed_type>(max_index));
  }

  /// The generation counter. 0 means null.
  METL_NODISCARD constexpr generation_type generation() const noexcept {
    return static_cast<generation_type>(value_ >> index_bits);
  }

  /// True when the handle is not null. A non-null handle may still be *stale*;
  /// only the issuing pool can tell (see `handle_pool::get`).
  METL_NODISCARD constexpr bool valid() const noexcept { return generation() != 0; }

  /// @copydoc valid
  METL_NODISCARD constexpr explicit operator bool() const noexcept { return valid(); }

  friend constexpr bool operator==(versioned_handle lhs, versioned_handle rhs) noexcept {
    return lhs.value_ == rhs.value_;
  }
  friend constexpr bool operator!=(versioned_handle lhs, versioned_handle rhs) noexcept {
    return lhs.value_ != rhs.value_;
  }
  /// Total order over the packed value, so handles can key a `flat_map`.
  friend constexpr bool operator<(versioned_handle lhs, versioned_handle rhs) noexcept {
    return lhs.value_ < rhs.value_;
  }

 private:
  packed_type value_;
};

}  // namespace metl
