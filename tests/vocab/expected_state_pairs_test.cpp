// Every pair of states for expected's copy, move, assignment, swap and
// comparisons, for expected<T, E> and for expected<void, E>.
//
// Switching between a value and an error is where a lifetime bug lives: one of
// them constructed twice, destroyed twice, or never. Each case runs on value
// and error types that count their live objects, and checks both counts after
// the operation, so such a bug shows as a wrong count, not only as a wrong
// value. expected<void, E> has its own copy of every special member, so it gets
// the same table.

#include "metl_check.hpp"

#include <utility>

#include <metl/expected.hpp>
#include <metl/in_place.hpp>

namespace {

// Live objects, by Tag: 0 for the value type, 1 for the error type.
int g_live[2] = {0, 0};

template <int Tag>
struct tracked {
  int value;

  explicit tracked(int v) noexcept : value(v) { ++g_live[Tag]; }
  tracked(const tracked& other) noexcept : value(other.value) { ++g_live[Tag]; }
  tracked(tracked&& other) noexcept : value(other.value) { ++g_live[Tag]; }
  tracked& operator=(const tracked&) noexcept = default;
  tracked& operator=(tracked&&) noexcept = default;
  ~tracked() { --g_live[Tag]; }

  friend bool operator==(const tracked& a, const tracked& b) { return a.value == b.value; }
  friend bool operator!=(const tracked& a, const tracked& b) { return a.value != b.value; }
};

using value_type = tracked<0>;
using error_type = tracked<1>;
using result = metl::expected<value_type, error_type>;
using void_result = metl::expected<void, error_type>;

// A state is a value when positive, an error when negative, and for
// expected<void, E> success when 0.
constexpr int states[] = {1, 2, -1, -2};
constexpr int void_states[] = {0, -1, -2};

int values(int state) {
  return state > 0 ? 1 : 0;
}

int errors(int state) {
  return state < 0 ? 1 : 0;
}

result make(int state) {
  return state > 0 ? result{metl::in_place, state} : result{metl::unexpect, -state};
}

void_result make_void(int state) {
  return state == 0 ? void_result{} : void_result{metl::unexpect, -state};
}

bool holds(const result& e, int state) {
  return state > 0 ? (e.has_value() && e->value == state) : (!e.has_value() && e.error().value == -state);
}

bool holds(const void_result& e, int state) {
  return state == 0 ? e.has_value() : (!e.has_value() && e.error().value == -state);
}

bool counts_are(int value_count, int error_count) {
  return g_live[0] == value_count && g_live[1] == error_count;
}

// ---- expected<T, E> -----------------------------------------------------------

void construction(int s) {
  {
    const result source = make(s);
    const result copy(source);
    CHECK(holds(copy, s));
    CHECK(holds(source, s));
    CHECK(counts_are(2 * values(s), 2 * errors(s)));
  }
  {
    result source = make(s);
    const result moved(std::move(source));
    CHECK(holds(moved, s));
    CHECK_EQ(source.has_value(), s > 0);
    CHECK(counts_are(2 * values(s), 2 * errors(s)));
  }
  CHECK(counts_are(0, 0));
}

void self_assignment(int s) {
  result x = make(s);
  result& alias = x;
  x = alias;
  CHECK(holds(x, s));
  x = std::move(alias);
  CHECK(holds(x, s));
  CHECK(counts_are(values(s), errors(s)));
}

void assignment_and_swap(int a, int b) {
  {
    result x = make(a);
    const result y = make(b);
    x = y;
    CHECK(holds(x, b));
    CHECK(holds(y, b));
    CHECK(counts_are(2 * values(b), 2 * errors(b)));
  }
  {
    result x = make(a);
    result y = make(b);
    x = std::move(y);
    CHECK(holds(x, b));
    CHECK_EQ(y.has_value(), b > 0);
    CHECK(counts_are(2 * values(b), 2 * errors(b)));
  }
  {
    result x = make(a);
    result y = make(b);
    x.swap(y);
    CHECK(holds(x, b));
    CHECK(holds(y, a));
    CHECK(counts_are(values(a) + values(b), errors(a) + errors(b)));
    swap(x, y);
    CHECK(holds(x, a));
    CHECK(holds(y, b));
  }
  CHECK(counts_are(0, 0));
}

void comparisons(int a, int b) {
  const result x = make(a);
  const result y = make(b);
  CHECK_EQ(x == y, a == b);
  CHECK_EQ(x != y, a != b);

  // Against a bare value, or an unexpected, on either side: equal only in the
  // matching state.
  if (b > 0) {
    const value_type v(b);
    CHECK_EQ(x == v, a == b);
    CHECK_EQ(v == x, a == b);
    CHECK_EQ(x != v, a != b);
    CHECK_EQ(v != x, a != b);
  } else {
    const metl::unexpected<error_type> u(error_type(-b));
    CHECK_EQ(x == u, a == b);
    CHECK_EQ(u == x, a == b);
    CHECK_EQ(x != u, a != b);
    CHECK_EQ(u != x, a != b);
  }
}

void fallbacks(int s) {
  result x = make(s);
  const result& cx = x;
  CHECK_EQ(cx.value_or(value_type(9)).value, s > 0 ? s : 9);
  CHECK_EQ(make(s).value_or(value_type(9)).value, s > 0 ? s : 9);
  CHECK_EQ(cx.error_or(error_type(9)).value, s < 0 ? -s : 9);
  CHECK_EQ(make(s).error_or(error_type(9)).value, s < 0 ? -s : 9);
  CHECK(counts_are(values(s), errors(s)));
}

// ---- expected<void, E> --------------------------------------------------------

void void_construction(int s) {
  {
    const void_result source = make_void(s);
    const void_result copy(source);
    CHECK(holds(copy, s));
    CHECK(counts_are(0, 2 * errors(s)));
  }
  {
    void_result source = make_void(s);
    const void_result moved(std::move(source));
    CHECK(holds(moved, s));
    CHECK(counts_are(0, 2 * errors(s)));
  }
  CHECK(counts_are(0, 0));
}

void void_self_assignment(int s) {
  void_result x = make_void(s);
  void_result& alias = x;
  x = alias;
  CHECK(holds(x, s));
  x = std::move(alias);
  CHECK(holds(x, s));
  CHECK(counts_are(0, errors(s)));
}

void void_assignment_and_swap(int a, int b) {
  {
    void_result x = make_void(a);
    const void_result y = make_void(b);
    x = y;
    CHECK(holds(x, b));
    CHECK(holds(y, b));
    CHECK(counts_are(0, 2 * errors(b)));
  }
  {
    void_result x = make_void(a);
    void_result y = make_void(b);
    x = std::move(y);
    CHECK(holds(x, b));
    CHECK(counts_are(0, 2 * errors(b)));
  }
  {
    void_result x = make_void(a);
    void_result y = make_void(b);
    x.swap(y);
    CHECK(holds(x, b));
    CHECK(holds(y, a));
    CHECK(counts_are(0, errors(a) + errors(b)));
    swap(x, y);
    CHECK(holds(x, a));
    CHECK(holds(y, b));
  }
  CHECK(counts_are(0, 0));
  CHECK_EQ(make_void(a) == make_void(b), a == b);
  CHECK_EQ(make_void(a) != make_void(b), a != b);
}

// Every way of putting an error in, or taking it out, from either state.
void void_modifiers(int s) {
  {
    void_result x = make_void(s);
    const metl::unexpected<error_type> u(error_type(5));
    x = u;
    CHECK(holds(x, -5));
    CHECK(counts_are(0, 2));
  }
  {
    void_result x = make_void(s);
    x = metl::unexpected<error_type>(error_type(6));
    CHECK(holds(x, -6));
    CHECK(counts_are(0, 1));
  }
  {
    void_result x = make_void(s);
    x.emplace_error(7);
    CHECK(holds(x, -7));
    CHECK(counts_are(0, 1));
    x.emplace();
    CHECK(holds(x, 0));
    CHECK(counts_are(0, 0));
  }
  {
    void_result x = make_void(s);
    x.emplace();
    CHECK(holds(x, 0));
    CHECK(counts_are(0, 0));
  }
  {
    const void_result x = make_void(s);
    const metl::unexpected<error_type> u(error_type(-s));
    CHECK_EQ(x == u, s != 0);
    CHECK_EQ(u == x, s != 0);
    CHECK_EQ(x != u, s == 0);
    CHECK_EQ(u != x, s == 0);
    CHECK_EQ(x.error_or(error_type(9)).value, s == 0 ? 9 : -s);
    CHECK_EQ(make_void(s).error_or(error_type(9)).value, s == 0 ? 9 : -s);
  }
  CHECK(counts_are(0, 0));
}

// The monadic operations through every reference qualifier. Only the side that
// matches the state may run its callable.
void void_monadic(int s) {
  int calls = 0;
  const auto next = [&calls]() {
    ++calls;
    return metl::expected<int, error_type>(1);
  };
  const auto produce = [&calls]() {
    ++calls;
    return 1;
  };
  const auto run = [&calls]() { ++calls; };
  const auto recover = [&calls](const error_type&) {
    ++calls;
    return metl::expected<void, int>();
  };
  const auto relabel = [&calls](const error_type& e) {
    ++calls;
    return e.value + 10;
  };

  void_result x = make_void(s);
  const void_result& cx = x;
  const bool ok = s == 0;

  const metl::expected<int, error_type> chained[] = {
      x.and_then(next), cx.and_then(next), make_void(s).and_then(next), std::move(cx).and_then(next)};
  const metl::expected<int, error_type> produced[] = {x.transform(produce),
                                                      cx.transform(produce),
                                                      make_void(s).transform(produce),
                                                      std::move(cx).transform(produce)};
  const void_result ran[] = {
      x.transform(run), cx.transform(run), make_void(s).transform(run), std::move(cx).transform(run)};
  for (int i = 0; i < 4; ++i) {
    CHECK_EQ(chained[i].has_value(), ok);
    CHECK_EQ(produced[i].has_value(), ok);
    CHECK(holds(ran[i], s));
    if (!ok) {
      CHECK_EQ(chained[i].error().value, -s);
      CHECK_EQ(produced[i].error().value, -s);
    }
  }
  CHECK_EQ(calls, ok ? 12 : 0);

  calls = 0;
  const metl::expected<void, int> recovered[] = {
      x.or_else(recover), cx.or_else(recover), make_void(s).or_else(recover), std::move(cx).or_else(recover)};
  const metl::expected<void, int> relabelled[] = {x.transform_error(relabel),
                                                  cx.transform_error(relabel),
                                                  make_void(s).transform_error(relabel),
                                                  std::move(cx).transform_error(relabel)};
  for (int i = 0; i < 4; ++i) {
    CHECK(recovered[i].has_value());
    CHECK_EQ(relabelled[i].has_value(), ok);
    if (!ok) {
      CHECK_EQ(relabelled[i].error(), 10 - s);
    }
  }
  CHECK_EQ(calls, ok ? 0 : 8);
}

}  // namespace

int main() {
  for (const int a : states) {
    construction(a);
    self_assignment(a);
    fallbacks(a);
    for (const int b : states) {
      assignment_and_swap(a, b);
      comparisons(a, b);
    }
  }
  for (const int a : void_states) {
    void_construction(a);
    void_self_assignment(a);
    void_modifiers(a);
    void_monadic(a);
    for (const int b : void_states) {
      void_assignment_and_swap(a, b);
    }
  }
  CHECK(counts_are(0, 0));
  return metl_test::exit_code();
}
