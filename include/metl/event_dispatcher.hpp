#pragma once

/// @file
/// @brief Progress guarantees for `metl::event_dispatcher` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `subscribe`, `unsubscribe`, `clear`, `size`, `empty` | wait-free, bounded by `Capacity` |
///   | `dispatch` | bounded by `Capacity` **plus every listener's own cost** |
///
/// The listener table is a flat array of `Capacity` slots scanned linearly.
/// `subscribe` stops at the first free slot and `unsubscribe` at the matching id,
/// so neither is constant, but neither visits more than `Capacity` slots; `size`
/// and `empty` count the live slots and always visit all `Capacity`.
///
/// `dispatch` is the operation to think about: it walks the whole table and calls
/// each live listener, so its worst case is the sum of what the listeners do. This
/// header cannot bound that -- a listener that blocks blocks the dispatch, and
/// every later listener with it. If dispatch happens on a deadline, the bound is
/// something the listeners have to keep, not something the dispatcher can enforce.
///
/// Single-threaded: this type does not synchronise. Subscribing from an ISR while
/// the main loop dispatches is a data race, not a slow path.

#include "metl/config.hpp"
#include "metl/delegate.hpp"
#include "metl/optional.hpp"

#include <cstddef>
#include <cstdint>

namespace metl {

template <typename Signature, std::size_t Capacity>
class event_dispatcher;

/// @brief Fixed-capacity, single-threaded fan-out of an event to delegates.
///
/// Holds up to `Capacity` listeners in an inline array — no heap allocation.
/// Each listener is a non-owning `delegate`, so the bound targets must outlive
/// their subscription. Subscribing returns a `listener_id` used to
/// unsubscribe. Ids come from a 64-bit counter on every target, so they do not
/// repeat in practice: a stale id cannot unsubscribe a newer listener short of
/// 2^64 subscriptions. Id 0 is never issued; a slot whose
/// id is 0 is free.
/// @tparam Capacity Maximum number of simultaneous listeners.
/// @note Not thread-safe: subscribe/unsubscribe/dispatch must not run
///       concurrently.
template <typename R, typename... Args, std::size_t Capacity>
class event_dispatcher<R(Args...), Capacity> {
 public:
  using delegate_type = delegate<R(Args...)>;
  using size_type = std::size_t;

  /// @brief Opaque handle identifying a subscribed listener.
  struct listener_id {
    std::uint64_t value;
  };

  /// @brief Constructs an empty dispatcher with no listeners.
  constexpr event_dispatcher() noexcept : next_id_(1), slots_{} {}

  /// @brief Registers a listener.
  /// @param listener Delegate to invoke on dispatch; its target must outlive
  ///        the subscription.
  /// @return The new listener's id, or nullopt if `listener` is empty or the
  ///         dispatcher is at capacity.
  METL_NODISCARD optional<listener_id> subscribe(delegate_type listener) noexcept {
    if (!listener) {
      return nullopt;
    }

    for (size_type i = 0; i < Capacity; ++i) {
      if (slots_[i].id.value == 0) {
        slots_[i].listener = listener;
        slots_[i].id = listener_id{next_id_++};
        if (next_id_ == 0) {
          next_id_ = 1;  // wrapped: keep 0 unissued, it marks a free slot
        }
        return slots_[i].id;
      }
    }

    return nullopt;
  }

  /// @brief Removes a previously registered listener.
  /// @param id Handle returned by subscribe.
  /// @return true if a matching active listener was found and removed.
  /// @note Plain name and discardable, like `erase` and `cancel`: the boolean
  ///       answers "was it there", it does not report a failure (SCOPE.md
  ///       section 9, R4).
  bool unsubscribe(listener_id id) noexcept {
    if (id.value == 0) {
      return false;  // never issued; would otherwise match a free slot
    }
    for (size_type i = 0; i < Capacity; ++i) {
      if (slots_[i].id.value == id.value) {
        slots_[i].id = listener_id{0};
        slots_[i].listener = delegate_type();
        return true;
      }
    }

    return false;
  }

  /// @brief Removes all listeners.
  void clear() noexcept {
    for (size_type i = 0; i < Capacity; ++i) {
      slots_[i].id = listener_id{0};
      slots_[i].listener = delegate_type();
    }
  }

  /// @brief Returns the maximum number of listeners.
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }

  /// @brief Returns the number of currently active listeners.
  METL_NODISCARD size_type size() const noexcept {
    size_type count = 0;
    for (size_type i = 0; i < Capacity; ++i) {
      if (slots_[i].id.value != 0) {
        ++count;
      }
    }
    return count;
  }

  /// @brief Returns true when no listeners are registered.
  METL_NODISCARD bool empty() const noexcept { return size() == 0; }

  /// @brief Invokes every active listener with the given arguments.
  /// @note Listeners are called in slot order; any return values are discarded.
  void dispatch(Args... args) const {
    for (size_type i = 0; i < Capacity; ++i) {
      if (slots_[i].id.value != 0) {
        slots_[i].listener(args...);
      }
    }
  }

 private:
  // No separate `active` flag: id 0 is never issued, so it marks a free slot.
  // That keeps a slot at id + delegate (16 B on ARM32, as before the ids went
  // 64-bit) instead of paying 8-byte-alignment padding after a bool.
  struct slot {
    listener_id id;
    delegate_type listener;
  };

  std::uint64_t next_id_;
  slot slots_[Capacity == 0 ? 1 : Capacity];
};

}  // namespace metl
