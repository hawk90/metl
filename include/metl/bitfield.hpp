#pragma once

#include "metl/compiler.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace metl {

// Compile-time bitfield mask/extract/insert helpers for register-style
// access. All operations are constexpr so the mask and shifted values
// fold to immediates in optimized code.
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

  METL_NODISCARD static constexpr T extract(T value) noexcept {
    return static_cast<T>((value & mask) >> Lsb);
  }

  METL_NODISCARD static constexpr T insert(T value, T field_val) noexcept {
    return static_cast<T>((value & static_cast<T>(~mask)) | (static_cast<T>(field_val << Lsb) & mask));
  }

  METL_NODISCARD static constexpr T encode(T field_val) noexcept {
    return static_cast<T>(static_cast<T>(field_val << Lsb) & mask);
  }

  template <typename E>
  METL_NODISCARD static constexpr T encode_enum(E e) noexcept {
    static_assert(std::is_enum_v<E>, "encode_enum requires an enum type");
    return encode(static_cast<T>(e));
  }

  template <typename E>
  METL_NODISCARD static constexpr E extract_enum(T value) noexcept {
    static_assert(std::is_enum_v<E>, "extract_enum requires an enum type");
    return static_cast<E>(extract(value));
  }
};

template <std::size_t Lsb, std::size_t Width>
using bitfield_u8 = bitfield<Lsb, Width, std::uint8_t>;

template <std::size_t Lsb, std::size_t Width>
using bitfield_u16 = bitfield<Lsb, Width, std::uint16_t>;

template <std::size_t Lsb, std::size_t Width>
using bitfield_u32 = bitfield<Lsb, Width, std::uint32_t>;

template <std::size_t Lsb, std::size_t Width>
using bitfield_u64 = bitfield<Lsb, Width, std::uint64_t>;

}  // namespace metl
