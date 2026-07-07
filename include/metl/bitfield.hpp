#pragma once

#include "metl/compiler.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace metl {

/// @brief Compile-time bitfield packed into an unsigned integer for register-style access.
/// @tparam Lsb Bit position of the field's least significant bit within the storage word.
/// @tparam Width Field width in bits; must be > 0.
/// @tparam T Unsigned integral storage type (defaults to `std::uint32_t`).
/// @pre `T` is unsigned integral, `Width > 0`, and `Lsb + Width <= sizeof(T) * 8` (enforced by
///      static_assert).
/// @note All operations are constexpr and heap-free, so the mask and shifted values fold to
///       immediates in optimized code.
template <std::size_t Lsb, std::size_t Width, typename T = std::uint32_t>
struct bitfield {
  static_assert(std::is_integral_v<T>, "bitfield T must be integral");
  static_assert(std::is_unsigned_v<T>, "bitfield T must be unsigned");
  static_assert(Width > 0, "bitfield Width must be > 0");
  static_assert(Lsb + Width <= sizeof(T) * 8, "bitfield Lsb + Width exceeds storage size");

  static constexpr std::size_t lsb = Lsb;
  static constexpr std::size_t width = Width;

  // Full-width fields need a guarded ones value to avoid UB from a shift
  // equal to the type width.
  static constexpr T mask = static_cast<T>(
      (Width == sizeof(T) * 8 ? static_cast<T>(~T{0}) : static_cast<T>((T{1} << Width) - T{1})) << Lsb);

  /// @brief Reads the field out of a full storage word, right-aligned.
  /// @param value The whole storage word.
  /// @return The field bits shifted down to bit 0.
  METL_NODISCARD static constexpr T extract(T value) noexcept {
    return static_cast<T>((value & mask) >> Lsb);
  }

  /// @brief Returns `value` with this field replaced by `field_val`.
  /// @param value The existing storage word.
  /// @param field_val The right-aligned field value to write (bits above `Width` are masked off).
  /// @return The updated storage word.
  METL_NODISCARD static constexpr T insert(T value, T field_val) noexcept {
    return static_cast<T>((value & static_cast<T>(~mask)) | (static_cast<T>(field_val << Lsb) & mask));
  }

  /// @brief Shifts and masks a right-aligned value into this field's position (no other bits set).
  /// @param field_val The right-aligned field value to encode.
  /// @return A storage word containing only this field's bits.
  METL_NODISCARD static constexpr T encode(T field_val) noexcept {
    return static_cast<T>(static_cast<T>(field_val << Lsb) & mask);
  }

  /// @brief Like `encode`, but takes a scoped/unscoped enum value.
  /// @tparam E An enum type (enforced by static_assert).
  /// @param e The enum value to encode into this field.
  /// @return A storage word containing only this field's bits.
  template <typename E>
  METL_NODISCARD static constexpr T encode_enum(E e) noexcept {
    static_assert(std::is_enum_v<E>, "encode_enum requires an enum type");
    return encode(static_cast<T>(e));
  }

  /// @brief Extracts this field from a storage word and returns it as an enum value.
  /// @tparam E An enum type (enforced by static_assert).
  /// @param value The whole storage word.
  /// @return The field value cast to `E`.
  template <typename E>
  METL_NODISCARD static constexpr E extract_enum(T value) noexcept {
    static_assert(std::is_enum_v<E>, "extract_enum requires an enum type");
    return static_cast<E>(extract(value));
  }
};

/// @brief `bitfield` alias with `std::uint8_t` storage.
template <std::size_t Lsb, std::size_t Width>
using bitfield_u8 = bitfield<Lsb, Width, std::uint8_t>;

/// @brief `bitfield` alias with `std::uint16_t` storage.
template <std::size_t Lsb, std::size_t Width>
using bitfield_u16 = bitfield<Lsb, Width, std::uint16_t>;

/// @brief `bitfield` alias with `std::uint32_t` storage.
template <std::size_t Lsb, std::size_t Width>
using bitfield_u32 = bitfield<Lsb, Width, std::uint32_t>;

/// @brief `bitfield` alias with `std::uint64_t` storage.
template <std::size_t Lsb, std::size_t Width>
using bitfield_u64 = bitfield<Lsb, Width, std::uint64_t>;

}  // namespace metl
