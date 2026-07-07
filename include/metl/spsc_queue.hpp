#pragma once

#include "metl/config.hpp"
#include "metl/optimization.hpp"
#include "metl/type_traits.hpp"

#include <atomic>
#include <cstddef>
#include <new>
#include <utility>

namespace metl {

/// @brief Lock-free single-producer / single-consumer bounded ring buffer.
///
/// Fixed-capacity: storage is an inline array of @c Capacity slots with NO dynamic
/// heap allocation. Capacity must be a power of two so indices can be masked instead
/// of using modulo.
///
/// @tparam T Element type.
/// @tparam Capacity Number of slots; must be a power of two and at least 2.
/// @note Thread-safe for EXACTLY one producer thread (`try_push`/`try_emplace`) and
///       one consumer thread (`try_pop`) running concurrently. Push uses
///       release/acquire on the tail and pop on the head, so a popped element
///       happens-after its push (acquire/release ordering).
/// @warning Undefined behavior with more than one concurrent producer or more than
///          one concurrent consumer. The destructor is NOT thread-safe: it drains
///          remaining elements and assumes no concurrent access.
template <typename T, std::size_t Capacity>
class spsc_queue {
  static_assert(Capacity >= 2, "spsc_queue Capacity must be at least 2");
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");

 public:
  using value_type = T;
  using size_type = std::size_t;

  spsc_queue() noexcept : head_(0), tail_(0) {}

  ~spsc_queue() {
    // Single-threaded at destruction: drain any remaining elements.
    std::size_t head = head_.load(std::memory_order_relaxed);
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    while (head != tail) {
      slot(head).ptr()->~T();
      ++head;
    }
  }

  spsc_queue(const spsc_queue&) = delete;
  spsc_queue(spsc_queue&&) = delete;
  spsc_queue& operator=(const spsc_queue&) = delete;
  spsc_queue& operator=(spsc_queue&&) = delete;

  /// @brief Producer: copy-enqueue an element if space is available.
  /// @param value Element to copy into the queue.
  /// @return True if enqueued; false if the queue is full.
  /// @note Producer-side only; call from the single producer thread.
  METL_NODISCARD bool try_push(const T& value) noexcept { return try_emplace(value); }

  /// @brief Producer: move-enqueue an element if space is available.
  /// @param value Element to move into the queue.
  /// @return True if enqueued; false if the queue is full.
  /// @note Producer-side only; call from the single producer thread.
  METL_NODISCARD bool try_push(T&& value) noexcept { return try_emplace(std::move(value)); }

  /// @brief Producer: construct an element in place if space is available.
  /// @return True if enqueued; false if the queue is full.
  /// @note Producer-side only; call from the single producer thread. Publishes the
  ///       new element with a release store on the tail.
  template <typename... Args>
  METL_NODISCARD bool try_emplace(Args&&... args) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t next = tail + 1;
    // Acquire so we observe any destructor by the consumer at this slot.
    const std::size_t head = head_.load(std::memory_order_acquire);
    if (next - head > Capacity) {
      return false;
    }
    ::new (slot(tail).addr()) T(std::forward<Args>(args)...);
    tail_.store(next, std::memory_order_release);
    return true;
  }

  /// @brief Consumer: dequeue the oldest element into @c out if one is available.
  /// @param out Destination that receives the moved-out element on success.
  /// @return True if an element was dequeued; false if the queue is empty.
  /// @note Consumer-side only; call from the single consumer thread. Acquires the
  ///       tail so the element published by the producer is fully visible.
  METL_NODISCARD bool try_pop(T& out) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    // Acquire so we observe the constructed element written by the producer.
    const std::size_t tail = tail_.load(std::memory_order_acquire);
    if (head == tail) {
      return false;
    }
    T* p = slot(head).ptr();
    out = std::move(*p);
    p->~T();
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  /// @brief Approximate number of queued elements.
  /// @return Element count; only a hint under concurrent access (relaxed loads).
  METL_NODISCARD std::size_t size_approx() const noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_relaxed);
    return tail - head;
  }

  /// @brief Approximate emptiness check; only a hint under concurrent access.
  METL_NODISCARD bool empty() const noexcept {
    return tail_.load(std::memory_order_relaxed) == head_.load(std::memory_order_relaxed);
  }

  /// @brief Approximate fullness check; only a hint under concurrent access.
  METL_NODISCARD bool full() const noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_relaxed);
    return (tail - head) == Capacity;
  }

  /// @brief Fixed number of slots in the ring buffer.
  METL_NODISCARD static constexpr std::size_t capacity() noexcept { return Capacity; }

 private:
  storage_for<T>& slot(std::size_t index) noexcept { return slots_[index & (Capacity - 1)]; }

  // Producer index, consumer index and the ring storage each occupy their own
  // cache line so the two roles never contend on a shared line (false sharing).
  METL_CACHELINE_ALIGNED std::atomic<std::size_t> head_;
  METL_CACHELINE_ALIGNED std::atomic<std::size_t> tail_;
  METL_CACHELINE_ALIGNED storage_for<T> slots_[Capacity];
};

}  // namespace metl
