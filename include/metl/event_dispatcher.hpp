#pragma once

#include "metl/config.hpp"
#include "metl/delegate.hpp"
#include "metl/optional.hpp"

#include <cstddef>

namespace metl {

template <typename Signature, std::size_t Capacity>
class event_dispatcher;

/// @brief Fixed-capacity, single-threaded fan-out of an event to delegates.
///
/// Holds up to `Capacity` listeners in an inline array — no heap allocation.
/// Each listener is a non-owning `delegate`, so the bound targets must outlive
/// their subscription. Subscribing returns a stable `listener_id` used to
/// unsubscribe.
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
    size_type value;
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
      if (!slots_[i].active) {
        slots_[i].listener = listener;
        slots_[i].id = listener_id{next_id_++};
        slots_[i].active = true;
        return slots_[i].id;
      }
    }

    return nullopt;
  }

  /// @brief Removes a previously registered listener.
  /// @param id Handle returned by subscribe.
  /// @return true if a matching active listener was found and removed.
  METL_NODISCARD bool unsubscribe(listener_id id) noexcept {
    for (size_type i = 0; i < Capacity; ++i) {
      if (slots_[i].active && slots_[i].id.value == id.value) {
        slots_[i].active = false;
        slots_[i].listener = delegate_type();
        return true;
      }
    }

    return false;
  }

  /// @brief Removes all listeners.
  void clear() noexcept {
    for (size_type i = 0; i < Capacity; ++i) {
      slots_[i].active = false;
      slots_[i].listener = delegate_type();
    }
  }

  /// @brief Returns the maximum number of listeners.
  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }

  /// @brief Returns the number of currently active listeners.
  METL_NODISCARD size_type size() const noexcept {
    size_type count = 0;
    for (size_type i = 0; i < Capacity; ++i) {
      if (slots_[i].active) {
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
      if (slots_[i].active) {
        slots_[i].listener(args...);
      }
    }
  }

 private:
  struct slot {
    listener_id id;
    delegate_type listener;
    bool active;
  };

  size_type next_id_;
  slot slots_[Capacity == 0 ? 1 : Capacity];
};

}  // namespace metl
