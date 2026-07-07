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
class arena_allocator {
 public:
  using size_type = std::size_t;

  struct mark_type {
    size_type offset;
  };

  constexpr arena_allocator() noexcept : offset_(0), storage_{} {}

  METL_NODISCARD mark_type mark() const noexcept { return mark_type{offset_}; }

  // Public raw allocator. Every successful allocation pushes a record so
  // rewind() can always walk back through allocations in LIFO order. Raw
  // allocations carry destroy=nullptr, so rewind only unwinds offsets for
  // them. This guarantees rewind safety even when raw allocate() is
  // interleaved with try_emplace<T>().
  METL_NODISCARD void* allocate(size_type bytes, size_type alignment = alignof(std::max_align_t)) noexcept {
    return allocate_impl(bytes, alignment, nullptr);
  }

  template <typename T, typename... Args>
  METL_NODISCARD T* try_emplace(Args&&... args) {
    static_assert(alignof(T) <= alignof(std::max_align_t),
                  "type alignment exceeds arena_allocator maximum alignment");

    void* memory = allocate_impl(sizeof(T), alignof(T), &destroy_object<T>);
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

  void rewind(mark_type target) noexcept {
    METL_ASSERT(target.offset <= offset_);
    while (offset_ > target.offset) {
      const allocation_record record = load_record(offset_);
      if (record.destroy != nullptr) {
        record.destroy(&storage_[record.payload_offset]);
      }
      offset_ = record.previous_offset;
    }
  }

  void reset() noexcept { rewind(mark_type{0}); }

  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }
  METL_NODISCARD constexpr size_type used() const noexcept { return offset_; }
  METL_NODISCARD constexpr size_type remaining() const noexcept { return Capacity - offset_; }
  METL_NODISCARD constexpr bool empty() const noexcept { return offset_ == 0; }

 private:
  using destroy_fn_t = void (*)(void*) noexcept;

  struct allocation_record {
    size_type previous_offset;
    size_type payload_offset;
    destroy_fn_t destroy;
  };

  template <typename T>
  static void destroy_object(void* memory) noexcept {
    static_cast<T*>(memory)->~T();
  }

  static constexpr std::uintptr_t align_up(std::uintptr_t value, size_type alignment) noexcept {
    return alignment == 0 ? value : (value + (alignment - 1)) & ~(alignment - 1);
  }

  void* allocate_impl(size_type bytes, size_type alignment, destroy_fn_t destroy) noexcept {
    if (bytes == 0) {
      return nullptr;
    }

    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(&storage_[0]);
    const std::uintptr_t current = base + offset_;
    const std::uintptr_t aligned = align_up(current, alignment);
    const size_type padding = static_cast<size_type>(aligned - current);

    // Overflow-safe bounds check. Computing `padding + bytes + sizeof(record)`
    // up front can wrap size_type for a huge `bytes`, yielding a small total
    // that slips past the bound and overruns the arena. Instead subtract each
    // component from the remaining space (which never underflows because each
    // step is guarded), so no addition can overflow before the check.
    const size_type space = remaining();
    if (padding > space) {
      return nullptr;
    }
    size_type left = space - padding;
    if (bytes > left) {
      return nullptr;
    }
    left -= bytes;
    if (sizeof(allocation_record) > left) {
      return nullptr;
    }

    // Safe now: padding + bytes + sizeof(record) <= space <= Capacity.
    const size_type total_bytes = padding + bytes + sizeof(allocation_record);

    const size_type previous_offset = offset_;
    const size_type payload_offset = previous_offset + padding;
    const size_type next_offset = previous_offset + total_bytes;

    store_record(next_offset, allocation_record{previous_offset, payload_offset, destroy});
    offset_ = next_offset;
    return &storage_[payload_offset];
  }

  void store_record(size_type end_offset, const allocation_record& record) noexcept {
    const unsigned char* source = reinterpret_cast<const unsigned char*>(&record);
    unsigned char* destination = &storage_[end_offset - sizeof(allocation_record)];
    for (size_type i = 0; i < sizeof(allocation_record); ++i) {
      destination[i] = source[i];
    }
  }

  allocation_record load_record(size_type end_offset) const noexcept {
    allocation_record record{};
    const unsigned char* source = &storage_[end_offset - sizeof(allocation_record)];
    unsigned char* destination = reinterpret_cast<unsigned char*>(&record);
    for (size_type i = 0; i < sizeof(allocation_record); ++i) {
      destination[i] = source[i];
    }
    return record;
  }

  size_type offset_;
  alignas(std::max_align_t) unsigned char storage_[Capacity == 0 ? 1 : Capacity];
};

}  // namespace metl
