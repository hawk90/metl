// Regression tests for the low-severity findings of the 2026-10-05 review
// (docs/AUDIT.md G.4 and G.5). Each block failed, or did not compile, before
// its fix.

#include "metl_check.hpp"

#include <cstddef>

#include <metl/coro/deadline_scheduler.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_function.hpp>
#include <metl/fixed_string.hpp>
#include <metl/function_ref.hpp>
#include <metl/optional.hpp>
#include <metl/span.hpp>

namespace {

int g_calls = 0;
int returns_value() {
  ++g_calls;
  return 7;
}

struct nested_poll {
  metl::coro::deadline_scheduler<3, unsigned>* sched = nullptr;
  int runs = 0;
};

metl::optional<unsigned> inner_task(void* /*self*/, unsigned /*now*/) noexcept {
  return metl::nullopt;
}

metl::optional<unsigned> outer_task(void* self, unsigned now) noexcept {
  auto* state = static_cast<nested_poll*>(self);
  ++state->runs;
  if (state->runs == 1) {
    // A poll that runs the scheduler again, then fills it. The inner run_due
    // used to clear the "slot reserved for my re-arm" flag while this poll was
    // still on the stack, so the fill below took the reserved slot.
    (void)state->sched->run_due(now + 1);  // runs the inner task's poll
    while (state->sched->try_schedule(nullptr, &inner_task, now + 100)) {
    }
  }
  return state->runs == 1 ? metl::optional<unsigned>(now + 10) : metl::nullopt;
}

}  // namespace

int main() {
  // fixed_string orders bytes as unsigned char, as std::string does.
  {
    const metl::fixed_string<4> high("\xff");
    const metl::fixed_string<4> low("a");
    CHECK(low < high);
    CHECK(!(high < low));
  }

  // span(ptr, 0) is the count constructor, not ambiguous.
  {
    int data[3] = {1, 2, 3};
    const metl::span<int> empty(data, 0);
    CHECK_EQ(empty.size(), 0u);
    const metl::span<int> pair(data, data + 3);
    CHECK_EQ(pair.size(), 3u);
  }

  // A value-returning callable in a void signature discards the result.
  {
    g_calls = 0;
    metl::fixed_function<void()> f = &returns_value;
    f();
    auto lambda = [] { return 3; };
    metl::fixed_function<void()> g = lambda;
    g();
    metl::function_ref<void()> r = lambda;
    r();
    CHECK_EQ(g_calls, 1);
  }

  // fixed_deque::emplace_front keeps head_ consistent (the throwing case is
  // host-only; this pins the ordinary path the reorder must not break).
  {
    metl::fixed_deque<int, 3> d;
    d.emplace_front(2);
    d.emplace_front(1);
    d.emplace_back(3);
    CHECK_EQ(d.front(), 1);
    CHECK_EQ(d.back(), 3);
    CHECK_EQ(d.size(), 3u);
  }

  // Nested run_due keeps the outer poll's re-arm slot reserved.
  {
    metl::coro::deadline_scheduler<3, unsigned> sched;
    nested_poll state;
    state.sched = &sched;
    CHECK(sched.try_schedule(&state, &outer_task, 0));
    CHECK(sched.try_schedule(nullptr, &inner_task, 1));
    (void)sched.run_due(0);
    CHECK_EQ(state.runs, 1);
    // The outer task re-armed for t=10 despite the inner fill.
    CHECK_EQ(sched.run_due(10), 1u);
    CHECK_EQ(state.runs, 2);
  }

  return metl_test::exit_code();
}
