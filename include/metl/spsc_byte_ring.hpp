#pragma once

// Lock-free SPSC byte ring that hands out CONTIGUOUS spans, so a driver can fill
// or drain it in place instead of byte at a time.
//
// Why this is not `ring_buffer<std::byte, N>` or `spsc_queue<std::byte, N>`:
// both are element-oriented, and `detail/ring_core.hpp` says outright that a
// ring's elements are not contiguous, so neither can hand out a pointer and a
// length. That is exactly what a UART/SPI/CAN receive path needs -- a region to
// pass to a peripheral, then a single "I wrote n bytes" call.
//
// WHAT THIS DOES NOT DO: cache maintenance. If a DMA engine is not coherent with
// the CPU cache on your part, you must invalidate the region before reading what
// DMA deposited, and clean it before DMA transmits what you wrote. METL has no
// portable way to do that -- the operations are core- and MPU-specific -- so it
// does not pretend to. This type manages indices and hands out spans; keeping the
// cache honest is the driver's job, and the type is deliberately named for its
// concurrency contract rather than for DMA so that no one reads a guarantee into
// it that is not here.

#include "metl/config.hpp"
#include "metl/optimization.hpp"
#include "metl/span.hpp"

#include <atomic>
#include <cstddef>

namespace metl {

/// @brief Lock-free single-producer / single-consumer byte ring with contiguous
///        span access.
///
/// **Progress guarantee (SCOPE.md I3): wait-free, bounded.** Index arithmetic is
/// O(1); the copying helpers (`try_write`, `read`) are O(n) in the bytes they
/// move, bounded by `Capacity`. No allocation, no retry loop, no CAS.
///
/// **Concurrency contract.** Exactly one producer thread/ISR calls
/// `writable_span` / `commit_write` / `try_write` / `write`. Exactly one consumer
/// calls `readable_span` / `consume` / `read`. Anything else is undefined. This is
/// the same contract as `spsc_queue`, and it is what makes the span protocol safe:
/// the producer only ever adds readable bytes and the consumer only ever frees
/// writable ones, so each side's own view of the ring can grow underneath it but
/// never shrink.
///
/// **Each side's own size query is conservative, never optimistic.**
/// `writable_size()` on the producer and `readable_size()` on the consumer may
/// under-report if the other side has just moved its index, but they can never
/// over-report. Acting on them is therefore safe without further synchronisation.
/// `size_approx()` reads both indices and is a hint only.
///
/// **The wrap is visible, on purpose.** `writable_span()` and `readable_span()`
/// return the FIRST CONTIGUOUS RUN, which stops at the physical end of the buffer.
/// So `writable_span().size()` can be smaller than `writable_size()` — that is the
/// wrap, not a full ring. A caller that needs the rest calls again after
/// committing/consuming. The alternative (returning two spans) pushes the same
/// two-step onto every caller including the ones that never wrap; the alternative
/// after that (mapping the pages twice) needs an MMU and an OS, which I1/I4 rule
/// out. What is guaranteed: **an empty span means full (or empty) and nothing
/// else** — if there is any room at all, the contiguous run is non-empty.
///
/// @tparam Capacity Ring size in bytes. Power of two, at least 2, so the index
///         wrap is a mask rather than a division.
template <std::size_t Capacity>
class spsc_byte_ring {
  static_assert(Capacity >= 2, "spsc_byte_ring Capacity must be at least 2");
  static_assert((Capacity & (Capacity - 1)) == 0, "spsc_byte_ring Capacity must be a power of two");

 public:
  using value_type = std::byte;
  using size_type = std::size_t;

  spsc_byte_ring() noexcept : head_(0), tail_(0), storage_{} {}

  spsc_byte_ring(const spsc_byte_ring&) = delete;
  spsc_byte_ring(spsc_byte_ring&&) = delete;
  spsc_byte_ring& operator=(const spsc_byte_ring&) = delete;
  spsc_byte_ring& operator=(spsc_byte_ring&&) = delete;

  // -------------------------------------------------------------------------
  // Producer side
  // -------------------------------------------------------------------------

  /// @brief Producer: total free bytes.
  /// @return Free byte count. Conservative — may under-report if the consumer has
  ///         just freed more, never over-report.
  METL_NODISCARD size_type writable_size() const noexcept {
    const size_type tail = tail_.load(std::memory_order_relaxed);
    const size_type head = head_.load(std::memory_order_acquire);
    return Capacity - (tail - head);
  }

  /// @brief Producer: the first contiguous run of free bytes, to fill in place.
  /// @return A writable span. Empty **only** when the ring is full. Its size may be
  ///         less than `writable_size()` when the free region wraps; call again
  ///         after `commit_write` to reach the rest.
  /// @note The bytes are not published until `commit_write`. Until then the
  ///       consumer cannot see them, so a partially filled span is not a hazard.
  /// @warning This is a QUERY, not a checkout. It records nothing, so calling it
  ///          twice without committing simply returns the same region again. If a
  ///          transfer is filling that region, do not write into it yourself and do
  ///          not `commit_write` until the transfer reports how much it moved —
  ///          the ring cannot know a DMA is in flight and does not pretend to
  ///          track one.
  METL_NODISCARD span<std::byte> writable_span() noexcept {
    const size_type tail = tail_.load(std::memory_order_relaxed);
    return span<std::byte>(storage_ + (tail & (Capacity - 1)), writable_run());
  }

  /// @brief Producer: publish @p count bytes written into `writable_span()`.
  /// @pre @p count is at most the size of the span `writable_span()` returns now;
  ///      a larger value asserts and aborts.
  /// @note No `try_commit_write`: this cannot fail because the ring is full —
  ///       `writable_span()` already told the caller the exact room. Over-committing
  ///       is a programming error, not a recoverable outcome (SCOPE.md section 9
  ///       R1 applies to capacity failures, and this is not one).
  /// @note The bound is the CONTIGUOUS RUN, not the total free space, and the
  ///       difference matters: with the write index near the seam there can be 8
  ///       bytes free of which only 2 are contiguous. Bounding by the free space
  ///       would accept `commit_write(8)`, publishing six bytes the caller never
  ///       wrote and could not have written — its span was two bytes long.
  ///       Asymmetric with `consume` on purpose; see the note there.
  void commit_write(size_type count) noexcept {
    // Never stripped: a bad count publishes bytes that were never written, and
    // every later span calculation is built on this index, so this must hold even
    // at METL_HARDENING_NONE.
    METL_HARDEN(count <= writable_run());
    const size_type tail = tail_.load(std::memory_order_relaxed);
    // Release: everything the producer wrote into the span happens-before the
    // consumer's acquire load of this index.
    tail_.store(tail + count, std::memory_order_release);
  }

  /// @brief Producer: copy @p source in, all of it or none of it.
  /// @return true on success; false if it does not fit (ring unchanged).
  /// @note Handles the wrap internally, so a caller that is copying anyway does
  ///       not need the span protocol at all.
  METL_NODISCARD bool try_write(span<const std::byte> source) noexcept {
    const size_type tail = tail_.load(std::memory_order_relaxed);
    const size_type head = head_.load(std::memory_order_acquire);
    if (source.size() > Capacity - (tail - head)) {
      return false;
    }
    for (size_type i = 0; i < source.size(); ++i) {
      storage_[(tail + i) & (Capacity - 1)] = source[i];
    }
    tail_.store(tail + source.size(), std::memory_order_release);
    return true;
  }

  /// @brief Producer: copy @p source in.
  /// @pre It fits; overflow asserts and aborts. Use `try_write` where "does not
  ///      fit" is a normal outcome.
  void write(span<const std::byte> source) noexcept {
    const bool written = try_write(source);
    METL_ASSERT(written);
    (void)written;
  }

  // -------------------------------------------------------------------------
  // Consumer side
  // -------------------------------------------------------------------------

  /// @brief Consumer: total readable bytes.
  /// @return Byte count. Conservative — may under-report if the producer has just
  ///         committed more, never over-report.
  METL_NODISCARD size_type readable_size() const noexcept {
    const size_type tail = tail_.load(std::memory_order_acquire);
    const size_type head = head_.load(std::memory_order_relaxed);
    return tail - head;
  }

  /// @brief Consumer: the first contiguous run of readable bytes, to parse in place.
  /// @return A read-only span. Empty **only** when the ring is empty. Its size may
  ///         be less than `readable_size()` when the readable region wraps; call
  ///         again after `consume` to reach the rest.
  METL_NODISCARD span<const std::byte> readable_span() const noexcept {
    const size_type head = head_.load(std::memory_order_relaxed);
    return span<const std::byte>(storage_ + (head & (Capacity - 1)), readable_run());
  }

  /// @brief Consumer: release @p count bytes back to the producer.
  /// @pre @p count is at most `readable_size()`; a larger value asserts and aborts.
  /// @note No `try_consume`, for the same reason `commit_write` has no try form.
  /// @note Bounded by `readable_size()`, NOT by the contiguous run — deliberately
  ///       asymmetric with `commit_write`. Consuming past the seam only discards
  ///       bytes; nothing is read and nothing is exposed, so `consume(readable_size())`
  ///       is a legitimate "drop everything queued" without walking the wrap.
  ///       Committing past the seam would publish bytes that were never written,
  ///       which is why that direction is bounded tighter.
  void consume(size_type count) noexcept {
    const size_type tail = tail_.load(std::memory_order_acquire);
    const size_type head = head_.load(std::memory_order_relaxed);
    METL_HARDEN(count <= tail - head);
    // Release: the consumer is done reading those bytes before the producer may
    // observe the space as free and overwrite them.
    head_.store(head + count, std::memory_order_release);
  }

  /// @brief Consumer: copy out into @p destination and release what was copied.
  /// @return How many bytes were copied: `min(readable_size(), destination.size())`.
  /// @note Returns a count, not a success flag — a short read is the normal case,
  ///       not a failure, so this keeps its plain name (SCOPE.md section 9, R4).
  ///       It is still `[[nodiscard]]`: dropping the count loses data silently.
  METL_NODISCARD size_type read(span<std::byte> destination) noexcept {
    const size_type tail = tail_.load(std::memory_order_acquire);
    const size_type head = head_.load(std::memory_order_relaxed);
    const size_type available = tail - head;
    const size_type count = available < destination.size() ? available : destination.size();
    for (size_type i = 0; i < count; ++i) {
      destination[i] = storage_[(head + i) & (Capacity - 1)];
    }
    head_.store(head + count, std::memory_order_release);
    return count;
  }

  // -------------------------------------------------------------------------
  // Either side (hints only under concurrency)
  // -------------------------------------------------------------------------

  /// @brief Approximate readable byte count; a hint only under concurrent access.
  METL_NODISCARD size_type size_approx() const noexcept {
    return tail_.load(std::memory_order_relaxed) - head_.load(std::memory_order_relaxed);
  }

  /// @brief Approximate emptiness; a hint only under concurrent access. The
  ///        consumer should branch on `readable_span().empty()`, which is exact
  ///        for it.
  METL_NODISCARD bool empty() const noexcept { return size_approx() == 0; }

  /// @brief Approximate fullness; a hint only under concurrent access. The producer
  ///        should branch on `writable_span().empty()`, which is exact for it.
  METL_NODISCARD bool full() const noexcept { return size_approx() == Capacity; }

  /// @brief Ring size in bytes.
  METL_NODISCARD static constexpr size_type capacity() noexcept { return Capacity; }

  /// @brief Discard everything.
  /// @warning NOT thread-safe. Only call with both sides quiesced — it moves the
  ///          consumer index, which the producer may be reading concurrently.
  void clear() noexcept {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

 private:
  /// Contiguous free bytes from the write index: the size `writable_span()` gives
  /// out, and therefore the exact bound `commit_write` must enforce. Factored out
  /// so the two can never drift apart -- they did once, and the guard was the
  /// looser of the two.
  size_type writable_run() const noexcept {
    const size_type tail = tail_.load(std::memory_order_relaxed);
    const size_type head = head_.load(std::memory_order_acquire);
    const size_type free_total = Capacity - (tail - head);
    const size_type to_end = Capacity - (tail & (Capacity - 1));
    return free_total < to_end ? free_total : to_end;
  }

  /// Contiguous readable bytes from the read index.
  size_type readable_run() const noexcept {
    const size_type tail = tail_.load(std::memory_order_acquire);
    const size_type head = head_.load(std::memory_order_relaxed);
    const size_type available = tail - head;
    const size_type to_end = Capacity - (head & (Capacity - 1));
    return available < to_end ? available : to_end;
  }

  // Producer and consumer indices on separate cache lines so the two roles never
  // contend on one line. Unlike `spsc_queue` there are deliberately no cached
  // copies of the other side's index: that optimisation pays for itself when the
  // index is reloaded once per ELEMENT, and here one `writable_span()` call is
  // amortised over as many bytes as the caller then writes. Adding it would be an
  // unmeasured complication.
  METL_CACHELINE_ALIGNED std::atomic<size_type> head_;  ///< Consumer position (monotonic).
  METL_CACHELINE_ALIGNED std::atomic<size_type> tail_;  ///< Producer position (monotonic).
  METL_CACHELINE_ALIGNED std::byte storage_[Capacity];
};

}  // namespace metl
