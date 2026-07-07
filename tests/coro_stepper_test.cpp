#include <metl/coro/stepper.hpp>

namespace {

// Three-state stepper: read -> process -> done.
class three_state : public metl::coro::stepper {
 public:
  enum class state : unsigned char { read, process, finished };
  state s = state::read;
  int read_count = 0;
  int process_count = 0;

  metl::coro::step_result step() noexcept override {
    switch (s) {
      case state::read:
        ++read_count;
        s = state::process;
        return metl::coro::step_result::yield;
      case state::process:
        ++process_count;
        s = state::finished;
        return metl::coro::step_result::yield;
      case state::finished:
        return metl::coro::step_result::done;
    }
    return metl::coro::step_result::error;
  }

 protected:
  void on_reset() noexcept override {
    s = state::read;
    read_count = 0;
    process_count = 0;
  }
};

// Stepper that fails after one yield.
class failing_stepper : public metl::coro::stepper {
 public:
  int steps = 0;

  metl::coro::step_result step() noexcept override {
    ++steps;
    if (steps == 1) {
      return metl::coro::step_result::yield;
    }
    return metl::coro::step_result::error;
  }
};

}  // namespace

int main() {
  // ---- three_state: full happy path --------------------------------------
  {
    three_state t;
    if (t.is_done()) {
      return 1;
    }
    if (t.is_error()) {
      return 2;
    }

    // First poll: read -> yield. Still running.
    if (!t.poll()) {
      return 3;
    }
    if (t.read_count != 1) {
      return 4;
    }
    if (t.process_count != 0) {
      return 5;
    }

    // Second poll: process -> yield. Still running.
    if (!t.poll()) {
      return 6;
    }
    if (t.process_count != 1) {
      return 7;
    }

    // Third poll: finished -> done. Stops running.
    if (t.poll()) {
      return 8;
    }
    if (!t.is_done()) {
      return 9;
    }
    if (t.is_error()) {
      return 10;
    }

    // Further polls are no-ops.
    if (t.poll()) {
      return 11;
    }
    if (!t.is_done()) {
      return 12;
    }

    // reset() returns to the initial state.
    t.reset();
    if (t.is_done()) {
      return 13;
    }
    if (t.is_error()) {
      return 14;
    }
    if (t.read_count != 0 || t.process_count != 0) {
      return 15;
    }

    // Drive through once more after reset.
    if (!t.poll()) {
      return 16;
    }
    if (!t.poll()) {
      return 17;
    }
    if (t.poll()) {
      return 18;
    }
    if (!t.is_done()) {
      return 19;
    }
  }

  // ---- failing_stepper: error path ---------------------------------------
  {
    failing_stepper f;
    if (!f.poll()) {  // first call yields
      return 100;
    }
    if (f.is_done() || f.is_error()) {
      return 101;
    }

    if (f.poll()) {  // second call errors -> stops running
      return 102;
    }
    if (!f.is_done()) {
      return 103;
    }
    if (!f.is_error()) {
      return 104;
    }

    // Once errored, further polls remain idle.
    if (f.poll()) {
      return 105;
    }
    if (f.steps != 2) {
      return 106;
    }

    // reset() clears both flags.
    f.reset();
    if (f.is_done() || f.is_error()) {
      return 107;
    }
  }

  return 0;
}
