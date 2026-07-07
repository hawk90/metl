#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/monotonic_buffer.hpp"

#include <cstddef>
#include <utility>

namespace metl {

template <typename T, std::size_t Capacity>
class static_allocator {
 public:
  using value_type = T;
  using pointer = T*;
  using const_pointer = const T*;
  using size_type = std::size_t;

  constexpr static_allocator() noexcept = default;

  METL_NODISCARD pointer try_allocate(size_type count = 1) noexcept {
    if (count == 0) {
      return nullptr;
    }

    void* memory = buffer_.allocate(sizeof(T) * count, alignof(T));
    return static_cast<pointer>(memory);
  }

  METL_NODISCARD pointer allocate(size_type count = 1) noexcept {
    pointer memory = try_allocate(count);
    METL_ASSERT(memory != nullptr);
    return memory;
  }

  template <typename... Args>
  METL_NODISCARD pointer try_new(Args&&... args) {
    return buffer_.template try_emplace<T>(std::forward<Args>(args)...);
  }

  template <typename... Args>
  METL_NODISCARD pointer create(Args&&... args) {
    pointer object = try_new(std::forward<Args>(args)...);
    METL_ASSERT(object != nullptr);
    return object;
  }

  template <typename... Args>
  void construct(pointer location, Args&&... args) {
    METL_ASSERT(location != nullptr);
    new (location) T(std::forward<Args>(args)...);
  }

  void destroy(pointer location) noexcept {
    METL_ASSERT(location != nullptr);
    location->~T();
  }

  void reset() noexcept { buffer_.reset(); }

  METL_NODISCARD constexpr size_type capacity_bytes() const noexcept { return Capacity; }
  METL_NODISCARD constexpr size_type used_bytes() const noexcept { return buffer_.used(); }
  METL_NODISCARD constexpr size_type remaining_bytes() const noexcept { return buffer_.remaining(); }
  METL_NODISCARD constexpr bool empty() const noexcept { return buffer_.empty(); }

 private:
  monotonic_buffer<Capacity> buffer_;
};

}  // namespace metl
