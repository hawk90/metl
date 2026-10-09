#pragma once

#include <cstddef>
#include <limits>

namespace metl {
namespace detail {

/// @brief How many elements lie between two free-running ring indices, as seen
///        by a caller that owns NEITHER index -- clamped to `[0, capacity]`.
///
/// Two loads of two counters that other threads are moving are not a snapshot.
/// Whatever order they are loaded in, and whatever memory order is used, a pop
/// can land between them and make `tail` read BEHIND `head`, and pushes can
/// make it read more than `capacity` ahead. The plain subtraction then wraps to
/// nearly `SIZE_MAX` -- a queue of four "holding" 1.8e19 elements -- which is
/// what `spsc_queue` and `spsc_byte_ring` returned to an observer thread. A
/// backward reading means the queue was being drained, so it reports 0; an
/// overshoot reports `capacity`.
///
/// The real distance never exceeds `capacity`, far below half the index range,
/// so anything in the upper half can only be a backward reading.
///
/// Progress guarantee: wait-free, bounded (arithmetic only).
constexpr std::size_t clamped_index_distance(std::size_t head,
                                             std::size_t tail,
                                             std::size_t capacity) noexcept {
  const std::size_t forward = tail - head;  // modular, so it survives index wrap
  if (forward > std::numeric_limits<std::size_t>::max() / 2) {
    return 0;
  }
  return forward < capacity ? forward : capacity;
}

/// @brief Where free-running ticket `sequence` stands relative to ticket `pos`.
///
/// Both are counters that wrap, so only their modular distance means anything.
/// Converting each to a signed type and subtracting -- the textbook Vyukov
/// formulation -- overflows `ptrdiff_t` once the counters straddle half the
/// range, which on a 32-bit target is 2^31 operations into a queue's life. That
/// is undefined behaviour, and clang at -O1 and above turned it into a push that
/// never returned. The distance is taken in unsigned arithmetic instead; a
/// distance in the upper half is a ticket behind, as in `clamped_index_distance`.
///
/// @return 0 when equal, negative when `sequence` is behind `pos`, positive when
///         it is ahead.
///
/// Progress guarantee: wait-free, bounded (arithmetic only).
constexpr int compare_tickets(std::size_t sequence, std::size_t pos) noexcept {
  const std::size_t distance = sequence - pos;  // modular
  if (distance == 0) {
    return 0;
  }
  return distance > std::numeric_limits<std::size_t>::max() / 2 ? -1 : 1;
}

}  // namespace detail
}  // namespace metl
