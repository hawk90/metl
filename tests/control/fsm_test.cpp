#include <array>

#include <metl/fsm.hpp>

namespace {

enum class state {
  idle,
  running,
  stopped,
};

enum class event {
  start,
  stop,
  reset,
};

struct recorder {
  int exit_idle = 0;
  int enter_running = 0;
  int action_count = 0;
  int enter_stopped = 0;
  int exit_running = 0;

  void on_exit_idle(state) { ++exit_idle; }
  void on_enter_running(state) { ++enter_running; }
  void on_enter_stopped(state) { ++enter_stopped; }
  void on_exit_running(state) { ++exit_running; }
  void on_transition(state from, event received, state to) {
    if (from == state::idle && received == event::start && to == state::running) {
      ++action_count;
    }
    if (from == state::running && received == event::stop && to == state::stopped) {
      action_count += 2;
    }
  }
};

}  // namespace

int main() {
  recorder log;

  const std::array<metl::fsm_transition<state, event>, 3> transitions{{
      {state::idle,
       event::start,
       state::running,
       metl::delegate<void(state, event, state)>::bind<recorder, &recorder::on_transition>(log)},
      {state::running,
       event::stop,
       state::stopped,
       metl::delegate<void(state, event, state)>::bind<recorder, &recorder::on_transition>(log)},
      {state::stopped, event::reset, state::idle, {}},
  }};

  const std::array<metl::fsm_state_hook<state>, 2> entry_hooks{{
      {state::running, metl::delegate<void(state)>::bind<recorder, &recorder::on_enter_running>(log)},
      {state::stopped, metl::delegate<void(state)>::bind<recorder, &recorder::on_enter_stopped>(log)},
  }};

  const std::array<metl::fsm_state_hook<state>, 2> exit_hooks{{
      {state::idle, metl::delegate<void(state)>::bind<recorder, &recorder::on_exit_idle>(log)},
      {state::running, metl::delegate<void(state)>::bind<recorder, &recorder::on_exit_running>(log)},
  }};

  metl::fsm<state, event, transitions.size(), entry_hooks.size(), exit_hooks.size()> machine(
      state::idle, transitions, entry_hooks, exit_hooks);

  if (machine.current_state() != state::idle) {
    return 1;
  }

  if (machine.can_dispatch(event::stop)) {
    return 2;
  }

  if (machine.dispatch(event::stop)) {
    return 3;
  }

  if (!machine.can_dispatch(event::start) || !machine.dispatch(event::start)) {
    return 4;
  }

  if (machine.current_state() != state::running || log.exit_idle != 1 || log.enter_running != 1 ||
      log.action_count != 1) {
    return 5;
  }

  if (!machine.dispatch(event::stop)) {
    return 6;
  }

  if (machine.current_state() != state::stopped || log.exit_running != 1 || log.enter_stopped != 1 ||
      log.action_count != 3) {
    return 7;
  }

  if (!machine.dispatch(event::reset) || machine.current_state() != state::idle) {
    return 8;
  }

  if (machine.dispatch(event::reset)) {
    return 9;
  }

  return 0;
}
