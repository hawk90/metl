#pragma once

#include "metl/compiler.hpp"
#include "metl/delegate.hpp"

#include <array>
#include <cstddef>

namespace metl {

/// @brief One transition rule: `from` state + `event` moves to `to` state,
///        optionally invoking `action(from, event, to)`.
/// @tparam State The state type. @tparam Event The event type.
template <typename State, typename Event>
struct fsm_transition {
  State from;                                  ///< Source state this rule matches.
  Event event;                                 ///< Triggering event this rule matches.
  State to;                                    ///< Destination state to commit on a match.
  delegate<void(State, Event, State)> action;  ///< Optional transition action.
};

/// @brief A per-state hook: `handler(state)` runs on entry to / exit from `state`.
/// @tparam State The state type.
template <typename State>
struct fsm_state_hook {
  State state;                    ///< State this hook is associated with.
  delegate<void(State)> handler;  ///< Callback invoked for that state.
};

/// @brief Fixed-size table-driven finite state machine (no heap, no virtuals).
///
/// Dispatch looks up a matching transition for the current state and event in a
/// linear table. On a match it runs the exit hook, commits the new state, runs
/// the transition action, then runs the entry hook. See `dispatch` for the
/// important commit-before-action ordering.
/// @tparam State The state type (comparable with `==`).
/// @tparam Event The event type (comparable with `==`).
/// @tparam TransitionCount Number of transition rules in the table.
/// @tparam EntryHookCount Number of entry hooks (default 0).
/// @tparam ExitHookCount Number of exit hooks (default 0).
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

  /// @brief Construct with an initial state and transition table (no hooks).
  /// @param initial_state The starting state.
  /// @param transitions The transition rule table.
  constexpr fsm(State initial_state, const std::array<transition_type, TransitionCount>& transitions) noexcept
      : current_state_(initial_state), transitions_(transitions), entry_hooks_{}, exit_hooks_{} {}

  /// @brief Construct with an initial state, transition table, and entry/exit hooks.
  /// @param initial_state The starting state.
  /// @param transitions The transition rule table.
  /// @param entry_hooks Hooks run when a state is entered.
  /// @param exit_hooks Hooks run when a state is exited.
  constexpr fsm(State initial_state,
                const std::array<transition_type, TransitionCount>& transitions,
                const std::array<state_hook_type, EntryHookCount>& entry_hooks,
                const std::array<state_hook_type, ExitHookCount>& exit_hooks) noexcept
      : current_state_(initial_state),
        transitions_(transitions),
        entry_hooks_(entry_hooks),
        exit_hooks_(exit_hooks) {}

  /// @brief The current state. @return The state the machine is currently in.
  METL_NODISCARD constexpr State current_state() const noexcept { return current_state_; }

  /// @brief Whether a transition exists for the current state and `event`.
  /// @param event The event to test. @return true if `dispatch(event)` would fire.
  METL_NODISCARD bool can_dispatch(Event event) const noexcept {
    return find_transition(current_state_, event) != nullptr;
  }

  /// @brief Dispatch an event, running the matching transition if any.
  ///
  /// On a match: runs the exit hook for the old state, commits the new state,
  /// runs the transition action, then runs the entry hook for the new state.
  /// @param event The event to process.
  /// @return true if a transition fired, false if no rule matched (state unchanged).
  /// @note The new state is committed BEFORE the transition action runs, so a
  ///       reentrant `dispatch()` from within the action observes the updated
  ///       state and cannot re-fire this same transition.
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
