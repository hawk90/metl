#pragma once

#include "metl/attributes.hpp"
#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/optimization.hpp"
#include "metl/type_traits.hpp"

#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

/// @brief Bounded lock-free queue for multiple producers and multiple consumers.
///
/// Fixed capacity, inline storage, no heap. Uses the sequence-number scheme
/// (Vyukov's bounded MPMC queue): every slot carries a counter that says whose
/// turn it is, so a producer and a consumer can tell "this slot is ready for me"
/// from "someone else got there first" without ever needing a double-width CAS
/// and without an ABA window — the sequence numbers only ever move forward.
///
/// **Tier 1 — capability-gated.** Needs a lock-free compare-exchange on
/// `std::size_t`. ARMv7-M and up have `LDREX`/`STREX`; **ARMv6-M (Cortex-M0/M0+)
/// does not**, and the `static_assert` below fires there rather than silently
/// degrading to a lock. On such a target, or on any single-core target where a
/// lock-free retry loop is the wrong tool, use
/// `guarded<static_message_queue<T, N>, irq_lock>` instead — masking interrupts
/// is the correct lock between an ISR and the main loop, and a lock-free retry
/// loop is not (an ISR that preempts a producer mid-retry spins forever).
///
/// Progress guarantees:
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `try_push` / `try_emplace` / `try_pop` | **lock-free**, not wait-free |
///   | `size_approx` / `empty` / `full` | wait-free, bounded (and only a hint) |
///
/// Lock-free means system-wide progress, not per-thread: a thread can lose the
/// compare-exchange arbitrarily many times if others keep winning. Under
/// docs/SCOPE.md §1 that restricts this type to **multi-core use** — never
/// between an ISR and the main loop on a single core.
///
/// @tparam T Element type; must be nothrow move-constructible/assignable and
///         nothrow destructible, for the same reason `spsc_queue` requires it.
/// @tparam Capacity Number of slots; must be a power of two and at least 2.
///
/// @note `try_pop` returning false means the queue *appeared* empty at some
///       point during the call, and `try_push` returning false likewise means it
///       appeared full. Neither is a synchronised snapshot — no lock-free queue
///       can offer one.
///
/// **This queue does not scale with thread count — it degrades.** Every producer
/// contends on one enqueue counter and every consumer on one dequeue counter, so
/// adding threads adds cache-line contention on a single word rather than
/// parallelism. Measured with `bench/bench_mpmc.cpp` on an arm64 host (one
/// machine, and a laptop at that — treat the direction as the signal, not the
/// absolute figures):
///
///   | Configuration        | Throughput   |
///   |----------------------|--------------|
///   | 1 producer, 1 consumer | 47 Mops/s  |
///   | 2 producers, 2 consumers | 6.8 Mops/s |
///   | 4 producers, 4 consumers | 3.5 Mops/s |
///
/// Uncontended, a push+pop round trip costs about 7.5 ns here against 5.6 ns for
/// `spsc_queue`, and two-thread `spsc_queue` throughput is roughly double the
/// 1×1 figure above.
///
/// The practical reading: **if the roles are genuinely fixed, use `spsc_queue`.**
/// Reach for this type when the producer or consumer count is not one, and size
/// the workload knowing that contention, not the queue, will be the limit.
template <typename T, std::size_t Capacity>
class mpmc_queue {
  static_assert(Capacity >= 2, "mpmc_queue Capacity must be at least 2");
  static_assert((Capacity & (Capacity - 1)) == 0, "mpmc_queue Capacity must be a power of two");

  static_assert(std::atomic<std::size_t>::is_always_lock_free,
                "metl::mpmc_queue requires a lock-free compare-exchange on std::size_t. "
                "ARMv6-M (Cortex-M0/M0+) has no CAS instruction. Use "
                "metl::guarded<metl::static_message_queue<T, N>, metl::irq_lock> there — and "
                "on any single-core target, where a lock-free retry loop preempted by an ISR "
                "would spin forever.");

  // Same reasoning as spsc_queue: this is a no-exception library and the push /
  // pop paths are noexcept, so a throwing move or destructor would terminate at
  // runtime. Requiring nothrow here turns that into a compile error.
  static_assert(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T> &&
                    std::is_nothrow_destructible_v<T>,
                "metl::mpmc_queue requires T to be nothrow move-constructible, nothrow "
                "move-assignable and nothrow destructible");

 public:
  using value_type = T;
  using size_type = std::size_t;

  mpmc_queue() noexcept : enqueue_pos_(0), dequeue_pos_(0) {
    // Slot i starts "ready for the producer whose ticket is i".
    for (size_type i = 0; i < Capacity; ++i) {
      cells_[i].sequence.store(i, std::memory_order_relaxed);
    }
  }

  /// @note Not thread-safe: destroys whatever is left and assumes no concurrent
  ///       access, exactly like `spsc_queue`'s destructor.
  /// @note Destroys in place rather than draining through `try_pop`. The drain
  ///       version needed a `T discarded;` to pop into, which silently made
  ///       `mpmc_queue<T>` require a DEFAULT-CONSTRUCTIBLE T -- a requirement
  ///       none of the static_asserts above state, that `spsc_queue` does not
  ///       have, and that surfaced only as an error inside the destructor.
  ~mpmc_queue() {
    // Single-threaded at destruction: every ticket between the two counters was
    // claimed and constructed before its slot was published, so this range is
    // exactly the live elements.
    size_type pos = dequeue_pos_.load(std::memory_order_relaxed);
    const size_type end = enqueue_pos_.load(std::memory_order_relaxed);
    while (pos != end) {
      cells_[pos & mask].storage.ptr()->~T();
      ++pos;
    }
  }

  mpmc_queue(const mpmc_queue&) = delete;
  mpmc_queue& operator=(const mpmc_queue&) = delete;
  mpmc_queue(mpmc_queue&&) = delete;
  mpmc_queue& operator=(mpmc_queue&&) = delete;

  /// Constructs an element in place if a slot is available.
  /// @return true if enqueued; false if the queue appeared full.
  template <typename... Args>
  METL_NODISCARD bool try_emplace(Args&&... args) noexcept {
    cell* target = nullptr;
    size_type pos = enqueue_pos_.load(std::memory_order_relaxed);

    for (;;) {
      target = &cells_[pos & mask];
      const size_type sequence = target->sequence.load(std::memory_order_acquire);
      // Signed difference: sequence and pos both wrap, and only their *distance*
      // is meaningful. Comparing them directly would break at the wrap point.
      const std::ptrdiff_t difference =
          static_cast<std::ptrdiff_t>(sequence) - static_cast<std::ptrdiff_t>(pos);

      if (difference == 0) {
        // The slot is waiting for exactly this ticket; claim the ticket.
        if (enqueue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
          break;
        }
        // Lost the race: `pos` was refreshed by the failed exchange, retry.
      } else if (difference < 0) {
        // The slot still holds an element a consumer has not taken: full.
        return false;
      } else {
        // Another producer already advanced past us; re-read and retry.
        pos = enqueue_pos_.load(std::memory_order_relaxed);
      }
    }

    ::new (target->storage.addr()) T(std::forward<Args>(args)...);
    // Release: publishes the element, and hands the slot to the consumer whose
    // ticket is pos + 1.
    target->sequence.store(pos + 1, std::memory_order_release);
    return true;
  }

  /// Copy-enqueues an element if a slot is available.
  METL_NODISCARD bool try_push(const T& value) noexcept { return try_emplace(value); }

  /// Move-enqueues an element if a slot is available.
  METL_NODISCARD bool try_push(T&& value) noexcept { return try_emplace(std::move(value)); }

  /// Dequeues the oldest available element into `out`.
  /// @return true if an element was dequeued; false if the queue appeared empty.
  METL_NODISCARD bool try_pop(T& out) noexcept {
    cell* target = nullptr;
    size_type pos = dequeue_pos_.load(std::memory_order_relaxed);

    for (;;) {
      target = &cells_[pos & mask];
      const size_type sequence = target->sequence.load(std::memory_order_acquire);
      const std::ptrdiff_t difference =
          static_cast<std::ptrdiff_t>(sequence) - static_cast<std::ptrdiff_t>(pos + 1);

      if (difference == 0) {
        if (dequeue_pos_.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
          break;
        }
      } else if (difference < 0) {
        // The producer has not published this slot yet: empty.
        return false;
      } else {
        pos = dequeue_pos_.load(std::memory_order_relaxed);
      }
    }

    T* element = target->storage.ptr();
    out = std::move(*element);
    element->~T();
    // Release: hands the slot back to the producer whose ticket is pos + Capacity.
    target->sequence.store(pos + Capacity, std::memory_order_release);
    return true;
  }

  /// Approximate number of queued elements; only a hint under concurrent access.
  /// @note Plain unsigned subtraction, deliberately, and NOT `tail > head ? ... : 0`.
  ///       Both counters are monotonic and wrap; their difference is meaningful
  ///       across the wrap and the comparison is not. The guarded version
  ///       reported 0 for a non-empty queue once the counters wrapped, which made
  ///       `full()` answer *false* on a full queue -- optimistic, which is the
  ///       wrong direction for a hint. `spsc_queue::size_approx` always did the
  ///       plain subtraction; this now matches it.
  METL_NODISCARD size_type size_approx() const noexcept {
    const size_type tail = enqueue_pos_.load(std::memory_order_relaxed);
    const size_type head = dequeue_pos_.load(std::memory_order_relaxed);
    return tail - head;
  }

  /// Approximate emptiness check; only a hint under concurrent access.
  METL_NODISCARD bool empty() const noexcept { return size_approx() == 0; }

  /// Approximate fullness check; only a hint under concurrent access.
  METL_NODISCARD bool full() const noexcept { return size_approx() >= Capacity; }

  /// Fixed number of slots.
  METL_NODISCARD static constexpr size_type capacity() noexcept { return Capacity; }

 private:
  static constexpr size_type mask = Capacity - 1;

  // The sequence number shares a line with the slot it describes on purpose: a
  // producer that claims a slot touches both, so splitting them would double the
  // coherence traffic per operation.
  struct cell {
    std::atomic<size_type> sequence;
    storage_for<T> storage;
  };

  // Producer and consumer tickets get their own lines; the two sides contend
  // heavily on their own counter and not at all on the other's.
  METL_CACHELINE_ALIGNED cell cells_[Capacity];
  METL_CACHELINE_ALIGNED std::atomic<size_type> enqueue_pos_;
  METL_CACHELINE_ALIGNED std::atomic<size_type> dequeue_pos_;
};

}  // namespace metl
