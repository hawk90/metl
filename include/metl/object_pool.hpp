#pragma once

/// @file
/// @brief Progress guarantees for `metl::object_pool` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `try_emplace`, `emplace` | wait-free, constant |
///   | `destroy`, `contains`, `size`, `empty`, `full` | wait-free, bounded |
///   | `clear`, destructor | wait-free, bounded by `Capacity` |
///
/// Acquiring a slot is constant: a freed slot keeps the link to the next freed
/// one in its own bytes, and slots never used yet are taken in index order.
/// Releasing is constant too: the pointer gives the index directly.
///
/// `metl::handle_pool` hands back a handle that detects use-after-free instead
/// of a pointer that does not. Prefer it unless you need a raw `T*`.
///
/// Single-threaded: this type does not synchronise.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/bitset.hpp"
#include "metl/detail/constant_init.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

/// Slot-based object pool with a compile-time FIXED number of slots.
///
/// Manages up to `Capacity` objects in inline storage; performs NO heap
/// allocation. Objects are constructed in place and referred to by raw pointer;
/// pointers remain stable for the object's lifetime. Non-copyable and
/// non-movable. Not thread-safe.
///
/// @par Reference invalidation
/// Objects never move: `try_emplace`/`emplace` invalidate nothing, and
/// `destroy` ends only the object it is given. `clear` ends them all.
///
/// @tparam T Pooled object type.
/// @tparam Capacity Number of slots (fixed at compile time).
template <typename T, std::size_t Capacity>
class object_pool {
  static_assert(std::is_object_v<T> && !std::is_array_v<T>, METL_DETAIL_OBJECT_TYPE_MESSAGE);

 public:
  using value_type = T;
  using size_type = std::size_t;
  using pointer = T*;
  using const_pointer = const T*;

  /// Constructs an empty pool with all slots free.
  /// Constant-initialized at namespace scope since C++20 (see
  /// detail/constant_init.hpp); a local one stores nothing to the slots.
  constexpr object_pool() noexcept {
    for (storage_type& slot : storage_) {
      detail::zero_for_constant_init(slot.bytes);
    }
  }

  ~object_pool() { clear(); }

  object_pool(const object_pool&) = delete;
  object_pool& operator=(const object_pool&) = delete;
  object_pool(object_pool&&) = delete;
  object_pool& operator=(object_pool&&) = delete;

  /// Constructs an object in a free slot: the one freed last, or, if none has
  /// been freed, the lowest one never used.
  /// @return Pointer to the new object, or nullptr if the pool is full (no assert).
  ///
  /// The slot is claimed before T's constructor runs, so a constructor that
  /// emplaces into this pool gets a different slot; if the constructor throws,
  /// the claim is released.
  template <typename... Args>
  METL_NODISCARD pointer try_emplace(Args&&... args) {
    size_type i = 0;
    if (free_head_ != 0) {
      i = free_head_ - 1;
      free_head_ = link(i);
    } else if (unused_ < Capacity) {
      i = unused_++;
    } else {
      return nullptr;
    }
    live_.set(i);
    ++size_;
    release_on_unwind claim{this, i};
    ::new (storage_[i].addr()) T(std::forward<Args>(args)...);
    claim.dismiss();
    return slot_ptr(i);
  }

  /// Constructs an object in a free slot, chosen as by try_emplace, and returns a pointer to it.
  /// @pre Pool is not full; a full pool asserts and aborts. Use try_emplace for a
  /// non-asserting path.
  template <typename... Args>
  METL_NODISCARD pointer emplace(Args&&... args) {
    pointer object = try_emplace(std::forward<Args>(args)...);
    METL_ASSERT(object != nullptr);
    return object;
  }

  /// Destroys a pooled object and frees its slot.
  /// @param object Pointer previously returned by this pool.
  /// @return true if destroyed; false if `object` is not a live slot of this pool.
  bool destroy(pointer object) noexcept {
    const size_type index = index_of(object);
    const bool live = index < Capacity && live_.test(index);
    if (live) {
      end_life(index);
    }
    return live;
  }

  /// Destroys all live objects and frees every slot.
  ///
  /// Each object leaves the pool before its destructor runs, as in `destroy`.
  /// An object a destructor emplaces into a slot this pass has already cleared
  /// survives, and is counted.
  void clear() noexcept {
    for (size_type i = 0; i < Capacity; ++i) {
      if (live_.test(i)) {
        end_life(i);
      }
    }
  }

  /// Returns true if `object` points to a live slot of this pool.
  METL_NODISCARD bool contains(const_pointer object) const noexcept {
    const size_type index = index_of(object);
    return index < Capacity && live_.test(index);
  }

  /// Returns true if no slots are in use.
  METL_NODISCARD constexpr bool empty() const noexcept { return size_ == 0; }
  /// Returns true if every slot is in use.
  METL_NODISCARD constexpr bool full() const noexcept { return size_ == Capacity; }
  /// Returns the number of live objects.
  METL_NODISCARD constexpr size_type size() const noexcept { return size_; }
  /// Returns the fixed slot count (`Capacity`).
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }
  /// Returns the number of free slots (`Capacity - size()`).
  METL_NODISCARD constexpr size_type available() const noexcept { return Capacity - size_; }

 private:
  // Smallest unsigned type that holds 0..Capacity: a free-list link is a slot
  // index plus one, so that 0, the zero-initialized value, ends the list.
  using link_type = std::conditional_t<Capacity <= 0xFFU,
                                       std::uint8_t,
                                       std::conditional_t<Capacity <= 0xFFFFU, std::uint16_t, std::uint32_t>>;

  // A slot holds a T while live and its free-list link while free, in the
  // same bytes, so it is sized for whichever is larger.
  struct storage_type {
    alignas(T) unsigned char bytes[sizeof(T) < sizeof(link_type) ? sizeof(link_type) : sizeof(T)];
    T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(&bytes[0])); }
    const T* ptr() const noexcept { return std::launder(reinterpret_cast<const T*>(&bytes[0])); }
    void* addr() noexcept { return &bytes[0]; }
    const void* addr() const noexcept { return &bytes[0]; }
  };

  // The free-list link of a freed slot, read and written as bytes: no object
  // lives there, and the bytes may not be aligned for link_type.
  link_type link(size_type index) const noexcept {
    link_type next = 0;
    std::memcpy(&next, &storage_[index].bytes[0], sizeof next);
    return next;
  }
  void push_free(size_type index) noexcept {
    const link_type head = free_head_;
    std::memcpy(&storage_[index].bytes[0], &head, sizeof head);
    free_head_ = static_cast<link_type>(index + 1);
  }

  pointer slot_ptr(size_type index) noexcept { return storage_[index].ptr(); }
  const_pointer slot_ptr(size_type index) const noexcept { return storage_[index].ptr(); }

  size_type index_of(const_pointer object) const noexcept {
    // `object` may be an arbitrary caller-supplied pointer that does not point
    // into `storage_`. Relational operators (`<`, `>=`) on pointers from
    // different objects are UB, so compare on integer addresses instead -- on the
    // flat-memory targets this library serves that is exactly the intended
    // containment test, with no <functional>/std::less dependency.
    //
    // The base is the slots' address, not slot_ptr(0): that launders a pointer
    // to slot 0, which needs a live T there. One unsigned offset is the whole
    // range test: an address below the storage, and nullptr, wrap to a value
    // past its end.
    const std::uintptr_t offset =
        reinterpret_cast<std::uintptr_t>(object) - reinterpret_cast<std::uintptr_t>(storage_[0].addr());
    if (offset >= sizeof(storage_type) * Capacity) {
      return Capacity;
    }
    // Inside the storage but not at the start of a slot (a pointer to a member
    // of a live object, say) is not one of this pool's pointers.
    if (offset % sizeof(storage_type) != 0) {
      return Capacity;
    }

    return static_cast<size_type>(offset / sizeof(storage_type));
  }

  // Releases a claimed slot if T's constructor exits by an exception.
  class release_on_unwind {
   public:
    release_on_unwind(object_pool* pool, size_type index) noexcept : pool_(pool), index_(index) {}
    release_on_unwind(const release_on_unwind&) = delete;
    release_on_unwind& operator=(const release_on_unwind&) = delete;
    release_on_unwind(release_on_unwind&&) = delete;
    release_on_unwind& operator=(release_on_unwind&&) = delete;
    ~release_on_unwind() {
      if (pool_ != nullptr) {
        pool_->live_.reset(index_);
        --pool_->size_;
        pool_->push_free(index_);
      }
    }
    void dismiss() noexcept { pool_ = nullptr; }

   private:
    object_pool* pool_ = nullptr;
    size_type index_ = 0;
  };

  // While its destructor runs a slot is neither live, so a re-entrant
  // destroy() or contains() does not see it, nor on the free list, so a
  // re-entrant try_emplace() cannot build over the object being destroyed.
  // The link goes in only after ~T() has finished with the bytes.
  void end_life(size_type index) noexcept {
    live_.reset(index);
    --size_;
    slot_ptr(index)->~T();
    push_free(index);
  }

  storage_type storage_[Capacity == 0 ? 1 : Capacity];
  detail::bitset<Capacity == 0 ? 1 : Capacity> live_{};
  link_type free_head_ = 0;  // freed slots, last freed first; 0 is none
  link_type unused_ = 0;     // slots at and above this were never used
  size_type size_ = 0;
};

}  // namespace metl
