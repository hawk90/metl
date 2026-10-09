#pragma once

/// @file
/// @brief Progress guarantees for `metl::object_pool` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `try_emplace`, `emplace` | wait-free, bounded by `Capacity` |
///   | `destroy`, `contains`, `size`, `empty`, `full` | wait-free, bounded |
///   | `clear`, destructor | wait-free, bounded by `Capacity` |
///
/// Acquiring a slot scans the occupancy flags for the first free one, so it is
/// `Capacity`-bounded rather than constant -- worth knowing if `Capacity` is large
/// and the pool is usually near full, because that is the case that scans furthest.
/// Releasing is constant: the pointer gives the index directly.
///
/// `metl::handle_pool` acquires in constant time from a free list, and hands back a
/// handle that detects use-after-free instead of a pointer that does not. Prefer it
/// unless you need a raw `T*`.
///
/// Single-threaded: this type does not synchronise.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <cstdint>
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
  constexpr object_pool() noexcept = default;

  ~object_pool() { clear(); }

  object_pool(const object_pool&) = delete;
  object_pool& operator=(const object_pool&) = delete;
  object_pool(object_pool&&) = delete;
  object_pool& operator=(object_pool&&) = delete;

  /// Constructs an object in the first free slot.
  /// @return Pointer to the new object, or nullptr if the pool is full (no assert).
  ///
  /// The slot is claimed before T's constructor runs, so a constructor that
  /// emplaces into this pool gets a different slot; if the constructor throws,
  /// the claim is released.
  template <typename... Args>
  METL_NODISCARD pointer try_emplace(Args&&... args) {
    for (size_type i = 0; i < Capacity; ++i) {
      if (state_[i] == slot_state::free) {
        state_[i] = slot_state::live;
        ++size_;
        release_on_unwind claim{this, i};
        ::new (storage_[i].addr()) T(std::forward<Args>(args)...);
        claim.dismiss();
        return slot_ptr(i);
      }
    }

    return nullptr;
  }

  /// Constructs an object in the first free slot and returns a pointer to it.
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
    const bool live = index < Capacity && state_[index] == slot_state::live;
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
      if (state_[i] == slot_state::live) {
        end_life(i);
      }
    }
  }

  /// Returns true if `object` points to a live slot of this pool.
  METL_NODISCARD bool contains(const_pointer object) const noexcept {
    const size_type index = index_of(object);
    return index < Capacity && state_[index] == slot_state::live;
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
  using storage_type = storage_for<T>;

  pointer slot_ptr(size_type index) noexcept { return storage_[index].ptr(); }
  const_pointer slot_ptr(size_type index) const noexcept { return storage_[index].ptr(); }

  size_type index_of(const_pointer object) const noexcept {
    if (object == nullptr) {
      return Capacity;
    }

    // `object` may be an arbitrary caller-supplied pointer that does not point
    // into `storage_`. Relational operators (`<`, `>=`) on pointers from
    // different objects are UB, so compare on integer addresses instead — on the
    // flat-memory targets this library serves that is exactly the intended
    // containment test, with no <functional>/std::less dependency.
    const_pointer begin = slot_ptr(0);
    const auto addr = reinterpret_cast<std::uintptr_t>(object);
    const auto lo = reinterpret_cast<std::uintptr_t>(begin);
    const auto hi = reinterpret_cast<std::uintptr_t>(begin + Capacity);
    if (addr < lo || addr >= hi) {
      return Capacity;
    }
    // Inside the storage but not at the start of a slot (a pointer to a member
    // of a live object, say) is not one of this pool's pointers.
    if ((addr - lo) % sizeof(storage_type) != 0) {
      return Capacity;
    }

    return static_cast<size_type>((addr - lo) / sizeof(storage_type));
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
        pool_->state_[index_] = slot_state::free;
        --pool_->size_;
      }
    }
    void dismiss() noexcept { pool_ = nullptr; }

   private:
    object_pool* pool_ = nullptr;
    size_type index_ = 0;
  };

  // A slot is `dying` while its destructor runs: no longer live, so a
  // re-entrant destroy() or contains() does not see it, and not yet free, so a
  // re-entrant try_emplace() cannot build over the object being destroyed.
  enum class slot_state : unsigned char { free, live, dying };

  void end_life(size_type index) noexcept {
    state_[index] = slot_state::dying;
    --size_;
    slot_ptr(index)->~T();
    state_[index] = slot_state::free;
  }

  storage_type storage_[Capacity == 0 ? 1 : Capacity];
  slot_state state_[Capacity == 0 ? 1 : Capacity]{};
  size_type size_ = 0;
};

}  // namespace metl
