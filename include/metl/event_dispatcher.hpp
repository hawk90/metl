#pragma once

#include "metl/config.hpp"
#include "metl/delegate.hpp"
#include "metl/optional.hpp"

#include <cstddef>

namespace metl {

template <typename Signature, std::size_t Capacity>
class event_dispatcher;

template <typename R, typename... Args, std::size_t Capacity>
class event_dispatcher<R(Args...), Capacity> {
 public:
  using delegate_type = delegate<R(Args...)>;
  using size_type = std::size_t;

  struct listener_id {
    size_type value;
  };

  constexpr event_dispatcher() noexcept : next_id_(1), slots_{} {}

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

  void clear() noexcept {
    for (size_type i = 0; i < Capacity; ++i) {
      slots_[i].active = false;
      slots_[i].listener = delegate_type();
    }
  }

  METL_NODISCARD constexpr size_type capacity() const noexcept { return Capacity; }

  METL_NODISCARD size_type size() const noexcept {
    size_type count = 0;
    for (size_type i = 0; i < Capacity; ++i) {
      if (slots_[i].active) {
        ++count;
      }
    }
    return count;
  }

  METL_NODISCARD bool empty() const noexcept { return size() == 0; }

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
