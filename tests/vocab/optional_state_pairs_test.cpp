// Every pair of states for optional's copy, move, assignment, swap and
// comparisons.
//
// Between an engaged and an empty optional is where a lifetime bug lives: a
// value constructed twice, destroyed twice, or never. Each case runs on a type
// that counts its live objects and checks the count after the operation, so
// such a bug shows as a wrong count, not only as a wrong value. The
// comparisons are checked against std::optional, pair by pair.

#include "metl_check.hpp"

#include <optional>
#include <utility>

#include <metl/in_place.hpp>
#include <metl/optional.hpp>

namespace {

struct tracked {
  static int live;
  int value;

  explicit tracked(int v) noexcept : value(v) { ++live; }
  tracked(const tracked& other) noexcept : value(other.value) { ++live; }
  tracked(tracked&& other) noexcept : value(other.value) { ++live; }
  tracked& operator=(const tracked&) noexcept = default;
  tracked& operator=(tracked&&) noexcept = default;
  ~tracked() { --live; }

  friend bool operator==(const tracked& a, const tracked& b) { return a.value == b.value; }
  friend bool operator!=(const tracked& a, const tracked& b) { return a.value != b.value; }
  friend bool operator<(const tracked& a, const tracked& b) { return a.value < b.value; }
  friend bool operator<=(const tracked& a, const tracked& b) { return a.value <= b.value; }
  friend bool operator>(const tracked& a, const tracked& b) { return a.value > b.value; }
  friend bool operator>=(const tracked& a, const tracked& b) { return a.value >= b.value; }
};

int tracked::live = 0;

using opt = metl::optional<tracked>;

// A state is 0 for empty, or the value held.
constexpr int states[] = {0, 1, 2};

opt make(int state) {
  return state == 0 ? opt{} : opt{metl::in_place, state};
}

std::optional<int> oracle(int state) {
  return state == 0 ? std::optional<int>{} : std::optional<int>{state};
}

int engaged(int state) {
  return state == 0 ? 0 : 1;
}

bool holds(const opt& o, int state) {
  return state == 0 ? !o.has_value() : (o.has_value() && o->value == state);
}

void construction(int s) {
  {
    const opt source = make(s);
    const opt copy(source);
    CHECK(holds(copy, s));
    CHECK(holds(source, s));
    CHECK_EQ(tracked::live, 2 * engaged(s));
  }
  {
    opt source = make(s);
    const opt moved(std::move(source));
    CHECK(holds(moved, s));
    CHECK_EQ(source.has_value(), s != 0);  // a moved-from optional stays engaged
    CHECK_EQ(tracked::live, 2 * engaged(s));
  }
  CHECK_EQ(tracked::live, 0);
}

void self_assignment(int s) {
  opt x = make(s);
  opt& alias = x;
  x = alias;
  CHECK(holds(x, s));
  x = std::move(alias);
  CHECK(holds(x, s));
  x.swap(alias);
  CHECK(holds(x, s));
  CHECK_EQ(tracked::live, engaged(s));
}

void assignment_and_swap(int a, int b) {
  {
    opt x = make(a);
    const opt y = make(b);
    x = y;
    CHECK(holds(x, b));
    CHECK(holds(y, b));
    CHECK_EQ(tracked::live, 2 * engaged(b));
  }
  {
    opt x = make(a);
    opt y = make(b);
    x = std::move(y);
    CHECK(holds(x, b));
    CHECK_EQ(y.has_value(), b != 0);
    CHECK_EQ(tracked::live, 2 * engaged(b));
  }
  {
    opt x = make(a);
    opt y = make(b);
    x.swap(y);
    CHECK(holds(x, b));
    CHECK(holds(y, a));
    CHECK_EQ(tracked::live, engaged(a) + engaged(b));
    swap(x, y);
    CHECK(holds(x, a));
    CHECK(holds(y, b));
  }
  CHECK_EQ(tracked::live, 0);
}

void comparisons(int a, int b) {
  const opt x = make(a);
  const opt y = make(b);
  const std::optional<int> sx = oracle(a);
  const std::optional<int> sy = oracle(b);
  CHECK_EQ(x == y, sx == sy);
  CHECK_EQ(x != y, sx != sy);
  CHECK_EQ(x < y, sx < sy);
  CHECK_EQ(x <= y, sx <= sy);
  CHECK_EQ(x > y, sx > sy);
  CHECK_EQ(x >= y, sx >= sy);

  // Against a bare value, on either side. A value is never empty.
  if (b != 0) {
    const tracked v(b);
    CHECK_EQ(x == v, sx == b);
    CHECK_EQ(v == x, b == sx);
    CHECK_EQ(x != v, sx != b);
    CHECK_EQ(v != x, b != sx);
    CHECK_EQ(x < v, sx < b);
    CHECK_EQ(v < x, b < sx);
    CHECK_EQ(x <= v, sx <= b);
    CHECK_EQ(v <= x, b <= sx);
    CHECK_EQ(x > v, sx > b);
    CHECK_EQ(v > x, b > sx);
    CHECK_EQ(x >= v, sx >= b);
    CHECK_EQ(v >= x, b >= sx);
  }
}

void comparisons_with_nullopt(int s) {
  const opt x = make(s);
  const std::optional<int> sx = oracle(s);
  CHECK_EQ(x == metl::nullopt, sx == std::nullopt);
  CHECK_EQ(metl::nullopt == x, std::nullopt == sx);
  CHECK_EQ(x != metl::nullopt, sx != std::nullopt);
  CHECK_EQ(metl::nullopt != x, std::nullopt != sx);
  CHECK_EQ(x < metl::nullopt, sx < std::nullopt);
  CHECK_EQ(metl::nullopt < x, std::nullopt < sx);
  CHECK_EQ(x <= metl::nullopt, sx <= std::nullopt);
  CHECK_EQ(metl::nullopt <= x, std::nullopt <= sx);
  CHECK_EQ(x > metl::nullopt, sx > std::nullopt);
  CHECK_EQ(metl::nullopt > x, std::nullopt > sx);
  CHECK_EQ(x >= metl::nullopt, sx >= std::nullopt);
  CHECK_EQ(metl::nullopt >= x, std::nullopt >= sx);
}

// value_or and the monadic operations, through every reference qualifier. On
// an empty optional the callable must not run.
void observers(int s) {
  static constexpr int fallback = 9;
  const int expect = s == 0 ? fallback : s;
  int calls = 0;
  const auto next = [&calls](const tracked& t) {
    ++calls;
    return metl::optional<int>(t.value + 1);
  };
  const auto plus_one = [&calls](const tracked& t) {
    ++calls;
    return t.value + 1;
  };
  const auto recover = [&calls]() {
    ++calls;
    return opt{metl::in_place, fallback};
  };

  opt x = make(s);
  const opt& cx = x;
  CHECK_EQ(x.value_or(tracked(fallback)).value, expect);
  CHECK_EQ(cx.value_or(tracked(fallback)).value, expect);
  CHECK_EQ(make(s).value_or(tracked(fallback)).value, expect);
  CHECK_EQ(std::move(cx).value_or(tracked(fallback)).value, expect);

  const metl::optional<int> chained[] = {
      x.and_then(next), cx.and_then(next), make(s).and_then(next), std::move(cx).and_then(next)};
  const metl::optional<int> mapped[] = {x.transform(plus_one),
                                        cx.transform(plus_one),
                                        make(s).transform(plus_one),
                                        std::move(cx).transform(plus_one)};
  for (int i = 0; i < 4; ++i) {
    CHECK_EQ(chained[i].has_value(), s != 0);
    CHECK_EQ(mapped[i].has_value(), s != 0);
    CHECK_EQ(chained[i].value_or(0), s == 0 ? 0 : s + 1);
    CHECK_EQ(mapped[i].value_or(0), s == 0 ? 0 : s + 1);
  }
  CHECK_EQ(calls, s == 0 ? 0 : 8);

  calls = 0;
  CHECK_EQ(cx.or_else(recover)->value, expect);
  CHECK_EQ(make(s).or_else(recover)->value, expect);
  CHECK_EQ(calls, s == 0 ? 2 : 0);
  CHECK_EQ(tracked::live, engaged(s));
}

}  // namespace

int main() {
  for (const int a : states) {
    construction(a);
    self_assignment(a);
    comparisons_with_nullopt(a);
    observers(a);
    for (const int b : states) {
      assignment_and_swap(a, b);
      comparisons(a, b);
    }
  }
  CHECK_EQ(tracked::live, 0);
  return metl_test::exit_code();
}
