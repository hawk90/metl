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

}  // namespace detail
}  // namespace metl
