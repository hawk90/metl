#pragma once

#include "metl/config.hpp"
#include "metl/type_traits.hpp"

#include <atomic>
#include <cstddef>
#include <new>
#include <utility>

namespace metl {

// Single-producer / single-consumer wait-free ring buffer.
//
// One thread may call try_push/try_emplace; a different (or the same)
// thread may call try_pop. Concurrent calls to the same role from
// multiple threads are undefined.
//
// Capacity must be a power of two so the producer/consumer indices can
// be masked instead of taking a modulo.
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

  METL_NODISCARD bool try_push(const T& value) noexcept { return try_emplace(value); }

  METL_NODISCARD bool try_push(T&& value) noexcept { return try_emplace(std::move(value)); }

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

  METL_NODISCARD std::size_t size_approx() const noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_relaxed);
    return tail - head;
  }

  METL_NODISCARD bool empty() const noexcept {
    return tail_.load(std::memory_order_relaxed) == head_.load(std::memory_order_relaxed);
  }

  METL_NODISCARD bool full() const noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_relaxed);
    return (tail - head) == Capacity;
  }

  METL_NODISCARD static constexpr std::size_t capacity() noexcept { return Capacity; }

 private:
  static constexpr std::size_t cache_line_size = 64;

  storage_for<T>& slot(std::size_t index) noexcept { return slots_[index & (Capacity - 1)]; }

  alignas(cache_line_size) std::atomic<std::size_t> head_;
  alignas(cache_line_size) std::atomic<std::size_t> tail_;
  alignas(cache_line_size) storage_for<T> slots_[Capacity];
};

}  // namespace metl
