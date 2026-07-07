#pragma once

#include "metl/compiler.hpp"
#include "metl/register_access.hpp"

#include <cstdint>
#include <type_traits>

namespace metl {

// Typed memory-mapped register at a fixed address known at compile time.
// All accesses route through read_once / write_once so reads and writes
// are not folded, reordered, or eliminated by the optimizer.
template <typename T, std::uintptr_t Address>
struct mmio_register {
  static_assert(std::is_trivially_copyable_v<T>, "mmio_register requires a trivially copyable type");

  static constexpr std::uintptr_t address = Address;

  METL_NODISCARD static T read() noexcept {
    return read_once<T>(reinterpret_cast<const volatile T*>(Address));
  }

  static void write(T value) noexcept { write_once<T>(reinterpret_cast<volatile T*>(Address), value); }

  // Read-modify-write. The bits selected by mask are replaced with the
  // corresponding bits in value; all other bits are preserved.
  static void modify(T mask, T value) noexcept {
    const T current = read();
    const T cleared = static_cast<T>(current & static_cast<T>(~mask));
    write(static_cast<T>(cleared | static_cast<T>(value & mask)));
  }

  static void set_bits(T bits) noexcept { write(static_cast<T>(read() | bits)); }

  static void clear_bits(T bits) noexcept { write(static_cast<T>(read() & static_cast<T>(~bits))); }
};

// Runtime-address variant. Same semantics as mmio_register, but the
// address is held in the instance and may be configured at runtime
// (e.g. when the same peripheral block exists at several addresses).
template <typename T>
class mmio_ptr {
  static_assert(std::is_trivially_copyable_v<T>, "mmio_ptr requires a trivially copyable type");

 public:
  explicit constexpr mmio_ptr(std::uintptr_t address) noexcept
      : addr_(reinterpret_cast<volatile T*>(address)) {}

  explicit constexpr mmio_ptr(volatile T* p) noexcept : addr_(p) {}

  METL_NODISCARD T read() const noexcept { return read_once<T>(addr_); }

  void write(T value) const noexcept { write_once<T>(addr_, value); }

  void modify(T mask, T value) const noexcept {
    const T current = read();
    const T cleared = static_cast<T>(current & static_cast<T>(~mask));
    write(static_cast<T>(cleared | static_cast<T>(value & mask)));
  }

  void set_bits(T bits) const noexcept { write(static_cast<T>(read() | bits)); }

  void clear_bits(T bits) const noexcept { write(static_cast<T>(read() & static_cast<T>(~bits))); }

  METL_NODISCARD constexpr volatile T* get() const noexcept { return addr_; }

 private:
  volatile T* addr_;
};

}  // namespace metl
