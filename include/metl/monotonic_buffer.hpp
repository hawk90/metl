#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

template <std::size_t Capacity>
class monotonic_buffer {
 public:
  using size_type = std::size_t;

  constexpr monotonic_buffer() noexcept : offset_(0), storage_{} {}

  METL_NODISCARD void* allocate(size_type bytes, size_type alignment = alignof(std::max_align_t)) noexcept {
    if (bytes == 0) {
      return nullptr;
    }

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(&storage_[0]);
    const std::uintptr_t current = base + offset_;
    const std::uintptr_t aligned = align_up(current, alignment);
    const size_type padding = static_cast<size_type>(aligned - current);

    if (padding > remaining() || bytes > (remaining() - padding)) {
      return nullptr;
    }

    offset_ += padding;
    void* result = &storage_[offset_];
    offset_ += bytes;
    return result;
  }

  template <typename T, typename... Args>
  METL_NODISCARD T* try_emplace(Args&&... args) {
    static_assert(alignof(T) <= alignof(std::max_align_t),
                  "type alignment exceeds monotonic_buffer maximum alignment");

    void* memory = allocate(sizeof(T), alignof(T));
    if (memory == nullptr) {
      return nullptr;
    }

    return new (memory) T(std::forward<Args>(args)...);
  }

  template <typename T, typename... Args>
  METL_NODISCARD T* emplace(Args&&... args) {
    T* object = try_emplace<T>(std::forward<Args>(args)...);
    METL_ASSERT(object != nullptr);
    return object;
  }

  void reset() noexcept { offset_ = 0; }

  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }
  METL_NODISCARD constexpr size_type used() const noexcept { return offset_; }
  METL_NODISCARD constexpr size_type remaining() const noexcept { return Capacity - offset_; }
  METL_NODISCARD constexpr bool empty() const noexcept { return offset_ == 0; }

 private:
  static constexpr std::uintptr_t align_up(std::uintptr_t value, size_type alignment) noexcept {
    return alignment == 0 ? value : (value + (alignment - 1)) & ~(alignment - 1);
  }

  size_type offset_;
  alignas(std::max_align_t) unsigned char storage_[Capacity == 0 ? 1 : Capacity];
};

}  // namespace metl
