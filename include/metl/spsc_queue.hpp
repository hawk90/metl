#pragma once

#include "metl/config.hpp"
#include "metl/optimization.hpp"
#include "metl/type_traits.hpp"

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>
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

  // metl is a NO-EXCEPTION library. try_push/try_emplace, try_pop, and the destructor are all
  // marked noexcept while running T's move ctor (try_emplace), move-assign (try_pop) and destructor
  // (try_pop / ~spsc_queue). If any of those threw, the exception would escape a noexcept boundary
  // and call std::terminate at RUNTIME. Requiring nothrow here turns that latent runtime terminate
  // into a clear COMPILE-TIME error. (Copy is intentionally not required: it is only reachable via
  // the copy overload of try_push(const T&) and constraining it would reject nothrow-movable types.)
  static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T> &&
                    std::is_nothrow_destructible_v<T>,
                "metl::spsc_queue requires T to be nothrow move-constructible, nothrow "
                "move-assignable, and nothrow destructible: metl is a no-exception library and its "
                "noexcept push/pop/destroy paths would std::terminate if T's move ctor, move "
                "assignment, or destructor threw.");

 public:
  using value_type = T;
  using size_type = std::size_t;

  spsc_queue() noexcept : head_(0), cached_tail_(0), tail_(0), cached_head_(0) {}

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
    // Fast path: decide from the producer's own cached copy of the consumer
    // index, which lives on the producer's cache line. Only when that copy says
    // "full" do we reload head_ and pull in the consumer's line. The cached
    // value is always <= the true head, so it can only ever be pessimistic --
    // it reports full when there may be room, never room when there is none.
    if (next - cached_head_ > Capacity) {
      // Acquire so we observe any destructor run by the consumer at this slot.
      cached_head_ = head_.load(std::memory_order_acquire);
      if (next - cached_head_ > Capacity) {
        return false;
      }
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
    // Mirror of the producer fast path: consult the consumer's cached copy of
    // the producer index first, and reload tail_ only when it says "empty".
    // Every slot below cached_tail_ was published by a release store that the
    // acquire load below already synchronised with, so consuming them without
    // re-acquiring is safe.
    if (head == cached_tail_) {
      // Acquire so we observe the constructed element written by the producer.
      cached_tail_ = tail_.load(std::memory_order_acquire);
      if (head == cached_tail_) {
        return false;
      }
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

  // Producer state, consumer state and the ring storage each occupy their own
  // cache line so the two roles never contend on a shared line (false sharing).
  //
  // Each side's cached copy of the OTHER side's index deliberately shares a line
  // with the index that side owns: cached_tail_ is read and written only by the
  // consumer, so it belongs next to head_. Putting it anywhere else would
  // reintroduce exactly the cross-line traffic the cache is there to avoid.
  METL_CACHELINE_ALIGNED std::atomic<std::size_t> head_;
  std::size_t cached_tail_;
  METL_CACHELINE_ALIGNED std::atomic<std::size_t> tail_;
  std::size_t cached_head_;
  METL_CACHELINE_ALIGNED storage_for<T> slots_[Capacity];
};

}  // namespace metl
