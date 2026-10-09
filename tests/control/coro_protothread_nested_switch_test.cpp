// A yield inside a switch of the caller's own.
//
// METL_PT_YIELD expands to `case __LINE__:`, and inside a nested switch that
// label belongs to the nested switch. On resume the protothread's own switch
// found no matching case, skipped the rest of the body, and run() reported the
// task done: `steps` stopped at 1 of 3, with no diagnostic. Resuming there now
// reaches METL_PT_BEGIN's default and aborts through METL_HARDEN, so this holds
// at every hardening level. The handler longjmps out, so it also runs on QEMU.

#include "metl_check.hpp"

#include <csetjmp>

#include <metl/assert.hpp>
#include <metl/coro/protothread.hpp>

namespace {

std::jmp_buf g_jump;
bool g_fired = false;

void capture(const char* /*expr*/, const char* /*file*/, int /*line*/) noexcept {
  g_fired = true;
  std::longjmp(g_jump, 1);
}

class nested_switch_task : public metl::coro::protothread {
 public:
  int mode = 1;
  int steps = 0;

  bool run() noexcept {
    METL_PT_BEGIN();
    switch (mode) {
      case 1:
        ++steps;
        METL_PT_YIELD();
        ++steps;
        break;
      default:
        break;
    }
    ++steps;
    METL_PT_END();
  }
};

class plain_task : public metl::coro::protothread {
 public:
  int steps = 0;

  bool run() noexcept {
    METL_PT_BEGIN();
    ++steps;
    METL_PT_YIELD();
    ++steps;
    METL_PT_END();
  }
};

}  // namespace

int main() {
  metl::set_assert_handler(&capture);

  // The yield inside the caller's switch: the resume must not report done.
  {
    nested_switch_task task;
    CHECK(!task.run());  // yields inside the inner switch
    CHECK_EQ(task.steps, 1);
    g_fired = false;
    bool finished = false;
    if (setjmp(g_jump) == 0) {
      finished = task.run();
    }
    CHECK(g_fired);
    CHECK(!finished);
  }

  // Ordinary yields are untouched, and a finished task run again stays done
  // without tripping the guard.
  {
    plain_task task;
    CHECK(!task.run());
    CHECK(task.run());
    CHECK(task.is_done());
    CHECK_EQ(task.steps, 2);
    g_fired = false;
    CHECK(task.run());
    CHECK(!g_fired);
    CHECK_EQ(task.steps, 2);
    task.reset();
    CHECK(!task.run());
    CHECK_EQ(task.steps, 3);
  }

  return metl_test::exit_code();
}
