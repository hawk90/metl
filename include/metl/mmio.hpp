#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/register_access.hpp"

#include <cstdint>
#include <type_traits>

namespace metl {

/// @brief Typed memory-mapped register at a fixed address known at compile time.
///
/// All accesses route through `read_once` / `write_once` (volatile lvalues), so
/// reads and writes are not folded, reordered, or eliminated by the optimizer.
/// @tparam T Trivially copyable register value type (e.g. `std::uint32_t`).
/// @tparam Address Absolute peripheral address; must be aligned to `alignof(T)`.
/// @pre `Address % alignof(T) == 0` (enforced by static_assert): a misaligned
///      volatile access to a peripheral register is undefined behavior.
template <typename T, std::uintptr_t Address>
struct mmio_register {
  static_assert(std::is_trivially_copyable_v<T>, "mmio_register requires a trivially copyable type");
  // A misaligned volatile access to a peripheral register is undefined
  // behavior. The address is a compile-time constant, so enforce it here.
  static_assert(Address % alignof(T) == 0, "mmio_register address must be aligned to alignof(T)");

  /// @brief The compile-time absolute register address.
  static constexpr std::uintptr_t address = Address;

  /// @brief Volatile read of the register. @return The current register value.
  METL_NODISCARD static T read() noexcept {
    return read_once<T>(reinterpret_cast<const volatile T*>(Address));
  }

  /// @brief Volatile write of the register. @param value New register value.
  static void write(T value) noexcept { write_once<T>(reinterpret_cast<volatile T*>(Address), value); }

  /// @brief Read-modify-write: bits selected by `mask` are replaced with the
  ///        corresponding bits of `value`; all other bits are preserved.
  /// @param mask Bits to update. @param value Source of the new bit values.
  static void modify(T mask, T value) noexcept {
    const T current = read();
    const T cleared = static_cast<T>(current & static_cast<T>(~mask));
    write(static_cast<T>(cleared | static_cast<T>(value & mask)));
  }

  /// @brief Set (OR-in) the given bits. @param bits Bits to set.
  static void set_bits(T bits) noexcept { write(static_cast<T>(read() | bits)); }

  /// @brief Clear the given bits. @param bits Bits to clear.
  static void clear_bits(T bits) noexcept { write(static_cast<T>(read() & static_cast<T>(~bits))); }
};

/// @brief Runtime-address variant of `mmio_register`.
///
/// Same volatile-access semantics as `mmio_register`, but the address is held in
/// the instance and may be configured at runtime (e.g. when the same peripheral
/// block exists at several addresses).
/// @tparam T Trivially copyable register value type.
template <typename T>
class mmio_ptr {
  static_assert(std::is_trivially_copyable_v<T>, "mmio_ptr requires a trivially copyable type");

 public:
  /// @brief Construct from an integer address (volatile hardware pointer).
  /// @param address Absolute peripheral address.
  /// @pre `address % alignof(T) == 0` (checked by runtime `METL_ASSERT`): a
  ///      misaligned volatile access is undefined behavior.
  /// @note Intentionally NOT constexpr: the integer-to-pointer reinterpret_cast
  ///       can never be a constant expression.
  explicit mmio_ptr(std::uintptr_t address) noexcept : addr_(reinterpret_cast<volatile T*>(address)) {
    METL_ASSERT(address % alignof(T) == 0);
  }

  /// @brief Construct from an existing volatile pointer.
  /// @param p Volatile pointer to the register; must be suitably aligned for `T`.
  explicit constexpr mmio_ptr(volatile T* p) noexcept : addr_(p) {}

  /// @brief Volatile read of the register. @return The current register value.
  METL_NODISCARD T read() const noexcept { return read_once<T>(addr_); }

  /// @brief Volatile write of the register. @param value New register value.
  void write(T value) const noexcept { write_once<T>(addr_, value); }

  /// @brief Read-modify-write: bits selected by `mask` take the corresponding
  ///        bits of `value`; all other bits are preserved.
  /// @param mask Bits to update. @param value Source of the new bit values.
  void modify(T mask, T value) const noexcept {
    const T current = read();
    const T cleared = static_cast<T>(current & static_cast<T>(~mask));
    write(static_cast<T>(cleared | static_cast<T>(value & mask)));
  }

  /// @brief Set (OR-in) the given bits. @param bits Bits to set.
  void set_bits(T bits) const noexcept { write(static_cast<T>(read() | bits)); }

  /// @brief Clear the given bits. @param bits Bits to clear.
  void clear_bits(T bits) const noexcept { write(static_cast<T>(read() & static_cast<T>(~bits))); }

  /// @brief The underlying volatile pointer. @return Volatile pointer to the register.
  METL_NODISCARD constexpr volatile T* get() const noexcept { return addr_; }

 private:
  volatile T* addr_;
};

}  // namespace metl
