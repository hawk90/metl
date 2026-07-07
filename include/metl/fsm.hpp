#pragma once

#include "metl/compiler.hpp"
#include "metl/delegate.hpp"

#include <array>
#include <cstddef>

namespace metl {

template <typename State, typename Event>
struct fsm_transition {
  State from;
  Event event;
  State to;
  delegate<void(State, Event, State)> action;
};

template <typename State>
struct fsm_state_hook {
  State state;
  delegate<void(State)> handler;
};

template <typename State,
          typename Event,
          std::size_t TransitionCount,
          std::size_t EntryHookCount = 0,
          std::size_t ExitHookCount = 0>
class fsm {
 public:
  using state_type = State;
  using event_type = Event;
  using transition_type = fsm_transition<State, Event>;
  using state_hook_type = fsm_state_hook<State>;

  constexpr fsm(State initial_state, const std::array<transition_type, TransitionCount>& transitions) noexcept
      : current_state_(initial_state), transitions_(transitions), entry_hooks_{}, exit_hooks_{} {}

  constexpr fsm(State initial_state,
                const std::array<transition_type, TransitionCount>& transitions,
                const std::array<state_hook_type, EntryHookCount>& entry_hooks,
                const std::array<state_hook_type, ExitHookCount>& exit_hooks) noexcept
      : current_state_(initial_state),
        transitions_(transitions),
        entry_hooks_(entry_hooks),
        exit_hooks_(exit_hooks) {}

  METL_NODISCARD constexpr State current_state() const noexcept { return current_state_; }

  METL_NODISCARD bool can_dispatch(Event event) const noexcept {
    return find_transition(current_state_, event) != nullptr;
  }

  METL_NODISCARD bool dispatch(Event event) noexcept {
    const transition_type* transition = find_transition(current_state_, event);
    if (transition == nullptr) {
      return false;
    }

    const State previous = current_state_;
    const State next = transition->to;
    invoke_hook(exit_hooks_, previous);

    // Commit the state transition BEFORE running the action. If the action
    // reentrantly calls dispatch(), it must observe the new state; otherwise it
    // would still see `previous` and could re-fire this very transition.
    current_state_ = next;

    if (transition->action) {
      transition->action(previous, event, next);
    }

    invoke_hook(entry_hooks_, next);
    return true;
  }

 private:
  template <std::size_t Count>
  static void invoke_hook(const std::array<state_hook_type, Count>& hooks, State state) noexcept {
    for (std::size_t i = 0; i < Count; ++i) {
      if (hooks[i].state == state && hooks[i].handler) {
        hooks[i].handler(state);
        return;
      }
    }
  }

  const transition_type* find_transition(State state, Event event) const noexcept {
    for (std::size_t i = 0; i < TransitionCount; ++i) {
      if (transitions_[i].from == state && transitions_[i].event == event) {
        return &transitions_[i];
      }
    }
    return nullptr;
  }

  State current_state_;
  std::array<transition_type, TransitionCount> transitions_;
  std::array<state_hook_type, EntryHookCount> entry_hooks_;
  std::array<state_hook_type, ExitHookCount> exit_hooks_;
};

}  // namespace metl
