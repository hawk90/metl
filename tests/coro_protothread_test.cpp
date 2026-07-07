#include <metl/coro/protothread.hpp>

namespace {

// A protothread that yields five times, incrementing a counter each turn.
class counter_thread : public metl::coro::protothread {
 public:
  int counter = 0;

  bool run() noexcept {
    METL_PT_BEGIN();
    while (counter < 5) {
      ++counter;
      METL_PT_YIELD();
    }
    METL_PT_END();
  }
};

// Protothread that waits on an external boolean, then waits while another is
// true, then completes.
class gated_thread : public metl::coro::protothread {
 public:
  bool gate_open = false;
  bool busy = true;
  int phase = 0;

  bool run() noexcept {
    METL_PT_BEGIN();
    METL_PT_WAIT_UNTIL(gate_open);
    phase = 1;
    METL_PT_WAIT_WHILE(busy);
    phase = 2;
    METL_PT_END();
  }
};

// Protothread that loops indefinitely until an explicit exit flag is set.
class restartable_thread : public metl::coro::protothread {
 public:
  int loops = 0;
  bool should_exit = false;

  bool run() noexcept {
    METL_PT_BEGIN();
    while (true) {
      ++loops;
      if (should_exit) {
        METL_PT_EXIT();
      }
      METL_PT_YIELD();
    }
    METL_PT_END();
  }
};

}  // namespace

int main() {
  // ---- counter_thread: yield 5 times then exit. ------------------------
  {
    counter_thread t;
    if (t.is_done()) {
      return 1;
    }

    // First five polls should yield, counter advancing 1..5.
    for (int i = 1; i <= 5; ++i) {
      const bool done = t.run();
      if (done) {
        return 10 + i;
      }
      if (t.counter != i) {
        return 20 + i;
      }
      if (t.is_done()) {
        return 30 + i;
      }
    }

    // Sixth poll exits the while-loop and completes.
    if (!t.run()) {
      return 40;
    }
    if (!t.is_done()) {
      return 41;
    }
    if (t.counter != 5) {
      return 42;
    }

    // Calling run() after done should still report done. (Behavior is
    // undefined by the macros — but is_done() must stay true and pt_line_
    // must remain -1, so we just verify is_done.)
    if (!t.is_done()) {
      return 43;
    }

    // Reset clears the done flag and restarts from the beginning.
    t.counter = 0;
    t.reset();
    if (t.is_done()) {
      return 44;
    }
    if (t.run()) {
      return 45;
    }
    if (t.counter != 1) {
      return 46;
    }
  }

  // ---- gated_thread: METL_PT_WAIT_UNTIL / METL_PT_WAIT_WHILE -----------
  {
    gated_thread g;
    // Gate closed, busy true: should stay at phase 0.
    if (g.run()) {
      return 100;
    }
    if (g.phase != 0) {
      return 101;
    }
    if (g.run()) {
      return 102;
    }
    if (g.phase != 0) {
      return 103;
    }

    // Open the gate. Next poll proceeds past WAIT_UNTIL, reaches WAIT_WHILE.
    g.gate_open = true;
    if (g.run()) {
      return 104;
    }
    if (g.phase != 1) {
      return 105;
    }

    // Still busy: stays at phase 1.
    if (g.run()) {
      return 106;
    }
    if (g.phase != 1) {
      return 107;
    }

    // Clear busy: next poll runs to completion.
    g.busy = false;
    if (!g.run()) {
      return 108;
    }
    if (!g.is_done()) {
      return 109;
    }
    if (g.phase != 2) {
      return 110;
    }
  }

  // ---- restartable_thread: METL_PT_RESTART / METL_PT_EXIT --------------
  {
    restartable_thread r;
    if (r.run()) {  // loops -> 1, yield
      return 200;
    }
    if (r.loops != 1) {
      return 201;
    }
    if (r.run()) {  // loops -> 2, yield
      return 202;
    }
    if (r.loops != 2) {
      return 203;
    }

    // External reset() rewinds.
    r.reset();
    if (r.is_done()) {
      return 204;
    }
    if (r.run()) {  // loops -> 3 (loops field not cleared), yield
      return 205;
    }
    if (r.loops != 3) {
      return 206;
    }

    // Now ask for exit.
    r.should_exit = true;
    if (!r.run()) {
      return 207;
    }
    if (!r.is_done()) {
      return 208;
    }
    if (r.loops != 4) {
      return 209;
    }
  }

  return 0;
}
