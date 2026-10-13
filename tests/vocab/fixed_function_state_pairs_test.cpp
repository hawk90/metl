// Every pair of states for fixed_function's and fixed_any_invocable's copy,
// move, assignment and swap.
//
// Moving a target between two wrappers, either of which may be empty, is where
// a lifetime bug lives: a target destroyed twice, or moved out and never
// destroyed. Each case runs on a target that counts its live objects and
// checks the count after the operation, so such a bug shows as a wrong count,
// not only as a wrong result.

#include "metl_check.hpp"

#include <utility>

#include <metl/fixed_function.hpp>

namespace {

int g_live = 0;

struct copyable_target {
  int value;
  explicit copyable_target(int v) noexcept : value(v) { ++g_live; }
  copyable_target(const copyable_target& other) noexcept : value(other.value) { ++g_live; }
  copyable_target(copyable_target&& other) noexcept : value(other.value) { ++g_live; }
  copyable_target& operator=(const copyable_target&) = delete;
  ~copyable_target() { --g_live; }
  int operator()() const { return value; }
};

struct move_only_target {
  int value;
  explicit move_only_target(int v) noexcept : value(v) { ++g_live; }
  move_only_target(const move_only_target&) = delete;
  move_only_target(move_only_target&& other) noexcept : value(other.value) { ++g_live; }
  move_only_target& operator=(const move_only_target&) = delete;
  ~move_only_target() { --g_live; }
  int operator()() { return value; }
};

using function = metl::fixed_function<int()>;
using invocable = metl::fixed_any_invocable<int()>;

// A state is 0 for empty, or the value the target returns.
constexpr int states[] = {0, 1, 2};

template <typename Wrapper, typename Target>
Wrapper make(int state) {
  return state == 0 ? Wrapper{} : Wrapper{Target{state}};
}

template <typename Wrapper>
bool holds(Wrapper& w, int state) {
  return state == 0 ? !w.has_value() : (w.has_value() && w() == state);
}

int engaged(int state) {
  return state == 0 ? 0 : 1;
}

// What both wrappers do: move construction and assignment, swap, and reset.
template <typename Wrapper, typename Target>
void moves(int a, int b) {
  {
    Wrapper source = make<Wrapper, Target>(a);
    Wrapper moved(std::move(source));
    CHECK(holds(moved, a));
    CHECK(!source.has_value());  // a move empties the source
    CHECK_EQ(g_live, engaged(a));
  }
  {
    Wrapper x = make<Wrapper, Target>(a);
    Wrapper y = make<Wrapper, Target>(b);
    x = std::move(y);
    CHECK(holds(x, b));
    CHECK(!y.has_value());
    CHECK_EQ(g_live, engaged(b));
  }
  {
    Wrapper x = make<Wrapper, Target>(a);
    Wrapper y = make<Wrapper, Target>(b);
    x.swap(y);
    CHECK(holds(x, b));
    CHECK(holds(y, a));
    CHECK_EQ(g_live, engaged(a) + engaged(b));
    swap(x, y);
    CHECK(holds(x, a));
    CHECK(holds(y, b));
    x = nullptr;
    CHECK(!x.has_value());
    CHECK_EQ(g_live, engaged(b));
  }
  {
    Wrapper x = make<Wrapper, Target>(a);
    Wrapper& alias = x;
    x = std::move(alias);
    CHECK(holds(x, a));
    x.swap(alias);
    CHECK(holds(x, a));
    CHECK_EQ(g_live, engaged(a));
  }
  CHECK_EQ(g_live, 0);
}

// fixed_function alone can also copy.
void copies(int a, int b) {
  {
    const function source = make<function, copyable_target>(a);
    function copy(source);
    CHECK(holds(copy, a));
    CHECK_EQ(g_live, 2 * engaged(a));
  }
  {
    function x = make<function, copyable_target>(a);
    const function y = make<function, copyable_target>(b);
    x = y;
    CHECK(holds(x, b));
    CHECK_EQ(y.has_value(), b != 0);
    CHECK_EQ(g_live, 2 * engaged(b));
  }
  {
    function x = make<function, copyable_target>(a);
    const function& alias = x;
    x = alias;
    CHECK(holds(x, a));
    CHECK_EQ(g_live, engaged(a));
  }
  CHECK_EQ(g_live, 0);
}

}  // namespace

int main() {
  for (const int a : states) {
    for (const int b : states) {
      moves<function, copyable_target>(a, b);
      moves<invocable, move_only_target>(a, b);
      copies(a, b);
    }
  }
  CHECK_EQ(g_live, 0);
  return metl_test::exit_code();
}
