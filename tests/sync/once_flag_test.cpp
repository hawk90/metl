// metl::once_flag / try_call_once (#486), in one context: the state
// machine, what the lock covers and what it does not, re-entry from inside the
// initializer, and a corrupted flag in a debug build. A recording lock policy shows when the
// lock is held. Runs on the host and, unchanged, on every Cortex-M under QEMU,
// ARMv6-M included, where the default irq_lock is the only lock there is.
// A real interrupt is in once_flag_irq_test.cpp, threads in
// once_flag_threaded_test.cpp.

#include "assert_trip.hpp"
#include "metl_check.hpp"

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#include <metl/attributes.hpp>
#include <metl/once_flag.hpp>

namespace {

using metl_test::trips;

// ---- a lock that records ----------------------------------------------------

struct recording_lock {
  using state_type = int;
  static int depth;  // > 0 while held
  static int taken;  // lock() calls so far

  static state_type lock() noexcept {
    ++depth;
    ++taken;
    return 0;
  }
  static void unlock(state_type) noexcept { --depth; }
};
int recording_lock::depth = 0;
int recording_lock::taken = 0;

void reset_lock() {
  recording_lock::depth = 0;
  recording_lock::taken = 0;
}

using flag_type = metl::once_flag<recording_lock>;

// ---- compile time -----------------------------------------------------------

static_assert(sizeof(metl::once_flag<>) == sizeof(std::uint32_t), "the state is one word");
static_assert(std::is_same_v<metl::once_flag<>, metl::once_flag<metl::irq_lock>>, "irq_lock is the default");
static_assert(!std::is_copy_constructible_v<flag_type> && !std::is_move_constructible_v<flag_type>,
              "a flag is its address: it is neither copied nor moved");
static_assert(!std::is_copy_assignable_v<flag_type> && !std::is_move_assignable_v<flag_type>, "nor assigned");
static_assert(std::is_trivially_destructible_v<metl::once_flag<>>, "nothing runs at exit");

// A namespace-scope flag needs no constructor at run time: an interrupt that
// fires before main() finds it "not started". Enforced where METL_CONST_INIT
// has a meaning (C++20, or Clang in C++17).
METL_CONST_INIT metl::once_flag<> g_constant_flag;

// ---- tests ------------------------------------------------------------------

void test_fresh_flag_is_not_done() {
  const flag_type flag;
  CHECK(!flag.is_done());
  CHECK(!g_constant_flag.is_done());
}

void test_runs_once_and_reports_done() {
  reset_lock();
  flag_type flag;
  int runs = 0;
  CHECK(metl::try_call_once(flag, [&] { ++runs; }));
  CHECK_EQ(runs, 1);
  CHECK(flag.is_done());
  CHECK_EQ(recording_lock::taken, 1);

  // Done: the fast path. Nothing runs and the lock is not taken.
  CHECK(metl::try_call_once(flag, [&] { ++runs; }));
  CHECK(metl::try_call_once(flag, [&] { ++runs; }));
  CHECK_EQ(runs, 1);
  CHECK_EQ(recording_lock::taken, 1);
  CHECK_EQ(recording_lock::depth, 0);
}

void test_lock_is_not_held_while_the_initializer_runs() {
  reset_lock();
  flag_type flag;
  int depth_inside = -1;
  CHECK(metl::try_call_once(flag, [&] { depth_inside = recording_lock::depth; }));
  CHECK_EQ(depth_inside, 0);
}

void test_reentry_from_the_initializer_returns_false() {
  reset_lock();
  flag_type flag;
  int runs = 0;
  bool inner = true;
  bool done_inside = true;
  CHECK(metl::try_call_once(flag, [&] {
    ++runs;
    // What an interrupt that preempts the initializer sees: "running".
    inner = metl::try_call_once(flag, [&] { ++runs; });
    done_inside = flag.is_done();
  }));
  CHECK(!inner);
  CHECK(!done_inside);
  CHECK_EQ(runs, 1);
  CHECK(flag.is_done());
  CHECK_EQ(recording_lock::taken, 2);  // outer start, inner attempt
}

void test_arguments_are_forwarded_and_the_result_discarded() {
  flag_type flag;
  struct move_only {
    int value;
    explicit move_only(int v) : value(v) {}
    move_only(move_only&&) = default;
    move_only(const move_only&) = delete;
  };
  int seen = 0;
  int target = 0;
  CHECK(metl::try_call_once(
      flag,
      [&](move_only m, int& out) {
        seen = m.value;
        out = 7;
        return 99;  // discarded
      },
      move_only{5},
      target));
  CHECK_EQ(seen, 5);
  CHECK_EQ(target, 7);
}

struct counter {
  int runs = 0;
  void bump(int by) { runs += by; }
};

void test_member_function_through_invoke() {
  flag_type flag;
  counter c;
  CHECK(metl::try_call_once(flag, &counter::bump, c, 3));
  CHECK_EQ(c.runs, 3);
}

void test_separate_flags_are_independent() {
  flag_type a;
  flag_type b;
  int runs = 0;
  CHECK(metl::try_call_once(a, [&] { ++runs; }));
  CHECK(!b.is_done());
  CHECK(metl::try_call_once(b, [&] { ++runs; }));
  CHECK_EQ(runs, 2);
}

void test_default_irq_lock_flag() {
  metl::once_flag<> flag;
  int runs = 0;
  CHECK(metl::try_call_once(flag, [&] { ++runs; }));
  CHECK(metl::try_call_once(flag, [&] { ++runs; }));
  CHECK_EQ(runs, 1);
  CHECK(flag.is_done());
}

void test_corrupted_flag_traps_in_debug() {
  // A state no operation produces stands for memory corruption. Running the
  // initializer or reporting "running" would each be wrong, so a debug build
  // stops. It is METL_DASSERT: other levels compile the check out.
  const bool tripped = trips([] {
    flag_type flag;
    const std::uint32_t corrupt = 3;
    std::memcpy(static_cast<void*>(&flag), &corrupt, sizeof corrupt);
    (void)metl::try_call_once(flag, [] {});
  });
  CHECK_EQ(tripped, METL_HARDENING >= METL_HARDENING_DEBUG);
}

}  // namespace

int main() {
  test_fresh_flag_is_not_done();
  test_runs_once_and_reports_done();
  test_lock_is_not_held_while_the_initializer_runs();
  test_reentry_from_the_initializer_returns_false();
  test_arguments_are_forwarded_and_the_result_discarded();
  test_member_function_through_invoke();
  test_separate_flags_are_independent();
  test_default_irq_lock_flag();
  test_corrupted_flag_traps_in_debug();
  return metl_test::exit_code();
}
