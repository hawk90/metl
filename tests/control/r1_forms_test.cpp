// The forms SCOPE.md section 9 R1 asks for, on the success path: the asserting
// attach* on coro::scheduler, try_subscribe on event_dispatcher, push_back on
// ring_buffer. What they do when full is in utility_assert_trips_test and
// container_assert_trips_test.

#include "metl_check.hpp"

#include <utility>

#include <metl/coro/scheduler.hpp>
#include <metl/coro/stepper.hpp>
#include <metl/event_dispatcher.hpp>
#include <metl/ring_buffer.hpp>

namespace {

int polled = 0;

bool one_poll(void*) noexcept {
  ++polled;
  return false;
}

struct one_step : metl::coro::stepper {
  int steps = 0;
  metl::coro::step_result step() noexcept override {
    ++steps;
    return metl::coro::step_result::yield;
  }
};

struct one_run {
  int runs = 0;
  bool run() noexcept {
    ++runs;
    return true;
  }
};

void attach_forms_poll_their_task() {
  metl::coro::scheduler<3> scheduler;
  int task = 0;
  one_step stepper;
  one_run protothread;
  scheduler.attach(&task, &one_poll);
  scheduler.attach_stepper(stepper);
  scheduler.attach_protothread(protothread);
  CHECK(scheduler.is_attached(&task));
  CHECK(scheduler.is_attached(&stepper));
  CHECK(scheduler.is_attached(&protothread));
  (void)scheduler.run_once();
  CHECK_EQ(polled, 1);
  CHECK_EQ(stepper.steps, 1);
  CHECK_EQ(protothread.runs, 1);
}

void on_event(int) noexcept {}

void try_subscribe_reports_and_changes_nothing() {
  using dispatcher_type = metl::event_dispatcher<void(int), 1>;
  dispatcher_type dispatcher;
  CHECK(!dispatcher.try_subscribe(dispatcher_type::delegate_type{}));
  CHECK_EQ(dispatcher.size(), 0u);
  const auto id = dispatcher.try_subscribe(dispatcher_type::delegate_type::from_function<&on_event>());
  CHECK(id.has_value());
  CHECK_EQ(dispatcher.size(), 1u);
  CHECK(!dispatcher.try_subscribe(dispatcher_type::delegate_type::from_function<&on_event>()));
  CHECK_EQ(dispatcher.size(), 1u);
  CHECK(dispatcher.unsubscribe(id.value()));
}

struct move_only {
  int value;
  explicit move_only(int input) noexcept : value(input) {}
  move_only(move_only&&) noexcept = default;
  move_only& operator=(move_only&&) noexcept = default;
};

void ring_buffer_push_back_appends() {
  metl::ring_buffer<int, 2> ints;
  const int first = 1;
  CHECK_EQ(ints.push_back(first), 1);
  CHECK_EQ(ints.push_back(2), 2);
  CHECK_EQ(ints.front(), 1);
  CHECK_EQ(ints.back(), 2);

  metl::ring_buffer<move_only, 1> moved;
  CHECK_EQ(moved.push_back(move_only{7}).value, 7);
}

}  // namespace

int main() {
  attach_forms_poll_their_task();
  try_subscribe_reports_and_changes_nothing();
  ring_buffer_push_back_appends();
  return metl_test::exit_code();
}
