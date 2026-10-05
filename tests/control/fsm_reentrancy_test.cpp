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

using hooked_fsm_t = metl::fsm<st, ev, 2, 3, 3>;

struct recorder {
  fsm_t* machine = nullptr;
  hooked_fsm_t* hooked = nullptr;
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
        if (machine != nullptr) {
          (void)machine->dispatch(ev::go);
        } else {
          (void)hooked->dispatch(ev::go);
        }
      }
    } else if (from == st::b && to == st::c) {
      ++bc_count;
    }
  }
};

// Records hook calls as a string: "xA" for exit a, "eC" for enter c.
struct hook_log {
  char text[16] = {};
  int length = 0;

  void push(char kind, st state) {
    if (length + 2 < static_cast<int>(sizeof(text))) {
      text[length++] = kind;
      text[length++] = static_cast<char>('A' + static_cast<int>(state));
    }
  }
  void enter(st state) { push('e', state); }
  void exit(st state) { push('x', state); }

  bool equals(const char* expected) const {
    int i = 0;
    for (; expected[i] != '\0'; ++i) {
      if (i >= length || text[i] != expected[i]) {
        return false;
      }
    }
    return i == length;
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

  // Hooks around a chained dispatch. `b` is passed
  // through inside a transition action, so it is neither entered nor exited:
  // the hooks run `exit a, enter c`. They used to run `exit a, exit b,
  // enter c, enter b` -- `b` exited before it was entered, and the last entry
  // hook naming a state the machine had already left.
  {
    hook_log hooks;
    recorder chain;
    using hooked_t = metl::fsm<st, ev, 2, 3, 3>;
    using hook_t = metl::fsm_state_hook<st>;
    using on_state = metl::delegate<void(st)>;

    const std::array<metl::fsm_transition<st, ev>, 2> table{{
        {st::a,
         ev::go,
         st::b,
         metl::delegate<void(st, ev, st)>::bind<recorder, &recorder::on_transition>(chain)},
        {st::b,
         ev::go,
         st::c,
         metl::delegate<void(st, ev, st)>::bind<recorder, &recorder::on_transition>(chain)},
    }};
    const std::array<hook_t, 3> entry{{
        {st::a, on_state::bind<hook_log, &hook_log::enter>(hooks)},
        {st::b, on_state::bind<hook_log, &hook_log::enter>(hooks)},
        {st::c, on_state::bind<hook_log, &hook_log::enter>(hooks)},
    }};
    const std::array<hook_t, 3> exit{{
        {st::a, on_state::bind<hook_log, &hook_log::exit>(hooks)},
        {st::b, on_state::bind<hook_log, &hook_log::exit>(hooks)},
        {st::c, on_state::bind<hook_log, &hook_log::exit>(hooks)},
    }};

    hooked_t hooked(st::a, table, entry, exit);
    chain.hooked = &hooked;
    CHECK(hooked.dispatch(ev::go));
    CHECK(hooked.current_state() == st::c);
    CHECK(hooks.equals("xAeC"));
  }

  // Without a chained dispatch the order is unchanged: exit, action, entry.
  {
    hook_log hooks;
    using hooked_t = metl::fsm<st, ev, 1, 3, 3>;
    using hook_t = metl::fsm_state_hook<st>;
    using on_state = metl::delegate<void(st)>;

    const std::array<metl::fsm_transition<st, ev>, 1> table{{{st::a, ev::go, st::b, {}}}};
    const std::array<hook_t, 3> entry{{
        {st::a, on_state::bind<hook_log, &hook_log::enter>(hooks)},
        {st::b, on_state::bind<hook_log, &hook_log::enter>(hooks)},
        {st::c, on_state::bind<hook_log, &hook_log::enter>(hooks)},
    }};
    const std::array<hook_t, 3> exit{{
        {st::a, on_state::bind<hook_log, &hook_log::exit>(hooks)},
        {st::b, on_state::bind<hook_log, &hook_log::exit>(hooks)},
        {st::c, on_state::bind<hook_log, &hook_log::exit>(hooks)},
    }};

    hooked_t plain(st::a, table, entry, exit);
    CHECK(plain.dispatch(ev::go));
    CHECK(hooks.equals("xAeB"));
  }

  return metl_test::exit_code();
}
