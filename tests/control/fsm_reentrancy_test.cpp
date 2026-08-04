// Regression test: fsm::dispatch must commit the new state BEFORE invoking the
// transition action. Previously the state was updated *after* the action, so an
// action that reentrantly called dispatch() still observed the old state and
// could re-fire the very transition in progress.

#include "metl_check.hpp"

#include <array>

#include <metl/fsm.hpp>

namespace {

enum class st { a, b, c };
enum class ev { go };

using fsm_t = metl::fsm<st, ev, 2>;

struct recorder {
  fsm_t* machine = nullptr;
  int ab_count = 0;  // times the a->b transition action fired
  int bc_count = 0;  // times the b->c transition action fired
  bool reentered = false;

  void on_transition(st from, ev /*e*/, st to) {
    if (from == st::a && to == st::b) {
      ++ab_count;
      if (!reentered) {
        reentered = true;
        // Reentrant dispatch: with the fix, current_state_ is already 'b', so
        // this drives b->c instead of re-firing a->b.
        (void)machine->dispatch(ev::go);
      }
    } else if (from == st::b && to == st::c) {
      ++bc_count;
    }
  }
};

}  // namespace

int main() {
  recorder log;

  const std::array<metl::fsm_transition<st, ev>, 2> transitions{{
      {st::a, ev::go, st::b, metl::delegate<void(st, ev, st)>::bind<recorder, &recorder::on_transition>(log)},
      {st::b, ev::go, st::c, metl::delegate<void(st, ev, st)>::bind<recorder, &recorder::on_transition>(log)},
  }};

  fsm_t machine(st::a, transitions);
  log.machine = &machine;

  CHECK(machine.dispatch(ev::go));

  // With the fix: a->b fires exactly once, the reentrant dispatch drives b->c,
  // and the machine ends in state c. (The old, buggy behavior re-fired a->b:
  // ab_count == 2, bc_count == 0, and the machine stuck at b.)
  CHECK_EQ(log.ab_count, 1);
  CHECK_EQ(log.bc_count, 1);
  CHECK(machine.current_state() == st::c);

  return metl_test::exit_code();
}
