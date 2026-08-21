#pragma once

/// @file
/// @brief Progress guarantees for `metl::static_message_queue` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `push`, `pop`, `emplace`, `try_*`, `front`, `back` | wait-free, bounded |
///   | `size`, `empty`, `full` | wait-free, bounded |
///   | `clear`, copy, destructor | wait-free, bounded by `size()` |
///
/// Push and pop are index arithmetic on a ring: nothing is shifted and the cost
/// does not depend on how full the queue is.
///
/// This type does not synchronise at all -- it is the single-threaded queue, and
/// the guarantees above are per thread. Between an ISR and the main loop use
/// `metl::spsc_queue`, which is wait-free across that boundary; between threads on
/// one core wrap this in `metl::guarded` with `irq_lock`, which makes it blocking
/// and bounded rather than wait-free.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

/// @brief Fixed-capacity single-threaded FIFO message queue (bounded ring buffer).
///
/// Backed by an inline array of @c Capacity slots with NO dynamic heap allocation.
///
/// @tparam T Element type.
/// @tparam Capacity Maximum number of elements the queue can hold.
/// @warning SINGLE-THREADED ONLY. `head_`/`tail_`/`size_` are plain non-atomic
///          indices: this queue is NOT thread-safe and NOT ISR-safe. Never share an
///          instance across threads or an ISR/thread boundary. For
///          single-producer/single-consumer hand-off between an interrupt and the
///          main loop, use `metl::spsc_queue` instead.
template <typename T, std::size_t Capacity>
class static_message_queue {
  // metl is a NO-EXCEPTION library. The only T operation that runs inside a noexcept context here is
  // DESTRUCTION: pop(), clear(), pop_front() and ~static_message_queue() are noexcept and destroy T.
  // A throwing destructor would escape that noexcept boundary and call std::terminate at RUNTIME, so
  // require nothrow destruction at COMPILE TIME instead.
  //
  // Note: construction (try_emplace/emplace/push) and move-assignment (try_pop) are deliberately NOT
  // required to be nothrow, because those functions are NOT noexcept -- a throwing move/copy there
  // propagates normally. Likewise the copy ctor/copy-assign run T's copy but are not noexcept, and
  // the move ctor/move-assign are already conditionally noexcept, so none of them can terminate.
  // Requiring nothrow move/copy would over-constrain T without preventing any terminate.
  static_assert(std::is_nothrow_destructible_v<T>,
                "metl::static_message_queue requires T to be nothrow destructible: metl is a "
                "no-exception library and its noexcept pop()/clear()/destructor paths would "
                "std::terminate if T's destructor threw.");

 public:
  using value_type = T;
  using size_type = std::size_t;
  using reference = T&;
  using const_reference = const T&;

  /// @brief Construct an empty queue.
  constexpr static_message_queue() noexcept : head_(0), tail_(0), size_(0) {}

  /// @brief Destroy the queue, running the destructor of every remaining element.
  ~static_message_queue() { clear(); }

  static_message_queue(const static_message_queue& other) : head_(0), tail_(0), size_(0) {
    for (size_type i = 0; i < other.size_; ++i) {
      const size_type index = other.physical_index(i);
      (void)emplace(other.storage_at(index));
    }
  }

  static_message_queue(static_message_queue&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
      : head_(0), tail_(0), size_(0) {
    for (size_type i = 0; i < other.size_; ++i) {
      const size_type index = other.physical_index(i);
      (void)emplace(static_cast<T&&>(other.storage_at(index)));
    }
    other.clear();
  }

  static_message_queue& operator=(const static_message_queue& other) {
    if (this == &other) {
      return *this;
    }

    clear();
    for (size_type i = 0; i < other.size_; ++i) {
      const size_type index = other.physical_index(i);
      (void)emplace(other.storage_at(index));
    }
    return *this;
  }

  static_message_queue& operator=(static_message_queue&& other) noexcept(
      std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>) {
    if (this == &other) {
      return *this;
    }

    clear();
    for (size_type i = 0; i < other.size_; ++i) {
      const size_type index = other.physical_index(i);
      (void)emplace(static_cast<T&&>(other.storage_at(index)));
    }
    other.clear();
    return *this;
  }

  /// @brief True if the queue holds no elements.
  METL_NODISCARD constexpr bool empty() const noexcept { return size_ == 0; }
  /// @brief True if the queue holds @c Capacity elements.
  METL_NODISCARD constexpr bool full() const noexcept { return size_ == Capacity; }
  /// @brief Current number of queued elements.
  METL_NODISCARD constexpr size_type size() const noexcept { return size_; }
  /// @brief Maximum number of elements the queue can hold.
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }

  /// @brief Access the oldest (front) element.
  /// @return Reference to the front element.
  /// @pre The queue must not be empty.
  METL_NODISCARD reference front() noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(head_);
  }

  /// @brief Access the oldest (front) element (const overload).
  /// @return Const reference to the front element.
  /// @pre The queue must not be empty.
  METL_NODISCARD const_reference front() const noexcept {
    METL_ASSERT(size_ > 0);
    return storage_at(head_);
  }

  /// @brief Construct an element in place at the back if space is available.
  /// @return True if enqueued; false if the queue is full.
  template <typename... Args>
  METL_NODISCARD bool try_emplace(Args&&... args) {
    if (full()) {
      return false;
    }

    new (storage_[tail_].addr()) T(std::forward<Args>(args)...);
    tail_ = advance(tail_);
    ++size_;
    return true;
  }

  /// @brief Construct an element in place at the back, asserting there is room.
  /// @return Reference to the newly inserted back element.
  /// @pre The queue must not be full.
  template <typename... Args>
  reference emplace(Args&&... args) {
    const bool inserted = try_emplace(std::forward<Args>(args)...);
    METL_ASSERT(inserted);
    (void)inserted;
    return back_ref();
  }

  /// @brief Copy-enqueue an element if space is available.
  /// @param value Element to copy.
  /// @return True if enqueued; false if the queue is full.
  METL_NODISCARD bool try_push(const T& value) { return try_emplace(value); }
  /// @brief Move-enqueue an element if space is available.
  /// @param value Element to move.
  /// @return True if enqueued; false if the queue is full.
  METL_NODISCARD bool try_push(T&& value) { return try_emplace(static_cast<T&&>(value)); }

  /// @brief Copy-enqueue an element, asserting there is room.
  /// @param value Element to copy.
  /// @return Reference to the newly inserted back element.
  /// @pre The queue must not be full.
  reference push(const T& value) { return emplace(value); }
  /// @brief Move-enqueue an element, asserting there is room.
  /// @param value Element to move.
  /// @return Reference to the newly inserted back element.
  /// @pre The queue must not be full.
  reference push(T&& value) { return emplace(static_cast<T&&>(value)); }

  /// @brief Move the front element into @c out and remove it, if any.
  /// @param out Destination that receives the moved-out element on success.
  /// @return True if an element was popped; false if the queue was empty.
  METL_NODISCARD bool try_pop(T& out) {
    if (empty()) {
      return false;
    }

    out = static_cast<T&&>(storage_at(head_));
    pop_front();
    return true;
  }

  /// @brief Remove the front element without returning it.
  /// @pre The queue must not be empty.
  void pop() noexcept {
    METL_ASSERT(size_ > 0);
    pop_front();
  }

  /// @brief Remove and destroy all elements, leaving the queue empty.
  void clear() noexcept {
    while (!empty()) {
      pop_front();
    }
  }

 private:
  using storage_type = storage_for<T>;

  constexpr size_type advance(size_type index) const noexcept {
    return Capacity == 0 ? 0 : (index + 1) % Capacity;
  }

  constexpr size_type physical_index(size_type offset) const noexcept {
    return Capacity == 0 ? 0 : (head_ + offset) % Capacity;
  }

  T& storage_at(size_type index) noexcept { return storage_[index].ref(); }
  const T& storage_at(size_type index) const noexcept { return storage_[index].ref(); }

  reference back_ref() noexcept {
    const size_type index = tail_ == 0 ? Capacity - 1 : tail_ - 1;
    return storage_at(index);
  }

  void pop_front() noexcept {
    storage_at(head_).~T();
    head_ = advance(head_);
    --size_;
  }

  storage_type storage_[Capacity == 0 ? 1 : Capacity];
  size_type head_;
  size_type tail_;
  size_type size_;
};

}  // namespace metl
