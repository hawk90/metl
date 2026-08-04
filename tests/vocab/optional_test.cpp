#include "metl_check.hpp"

#include <functional>
#include <utility>

#include <metl/optional.hpp>

namespace {

struct tracker {
  static int constructions;
  static int destructions;

  tracker() : value(0) { ++constructions; }
  explicit tracker(int input) : value(input) { ++constructions; }
  tracker(int a, int b) : value(a + b) { ++constructions; }
  tracker(const tracker& other) : value(other.value) { ++constructions; }
  tracker(tracker&& other) noexcept : value(other.value) { ++constructions; }

  tracker& operator=(const tracker& other) {
    value = other.value;
    return *this;
  }

  tracker& operator=(tracker&& other) noexcept {
    value = other.value;
    return *this;
  }

  ~tracker() { ++destructions; }

  int value;
};

int tracker::constructions = 0;
int tracker::destructions = 0;

}  // namespace

int main() {
  metl::optional<int> empty;
  CHECK(!empty.has_value());

  empty = 7;
  CHECK(empty.has_value());
  CHECK_EQ(*empty, 7);

  metl::optional<int> copied(empty);
  CHECK(copied.has_value());
  CHECK_EQ(copied.value(), 7);

  copied.emplace(11);
  CHECK_EQ(copied.value_or(0), 11);

  copied.reset();
  CHECK_EQ(copied.value_or(9), 9);

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::optional<tracker> item;
    item.emplace(42);
    CHECK(item.has_value());
    CHECK_EQ(item->value, 42);

    metl::optional<tracker> moved(static_cast<metl::optional<tracker>&&>(item));
    CHECK(moved.has_value());
    CHECK_EQ(moved->value, 42);

    moved = metl::nullopt;
    CHECK(!moved.has_value());
  }

  CHECK_EQ(tracker::constructions, tracker::destructions);

  // ---- in_place_t constructor --------------------------------------------
  {
    metl::optional<tracker> in_place(metl::in_place, 3, 4);
    CHECK(in_place.has_value());
    CHECK_EQ(in_place->value, 7);
  }

  // ---- make_optional ------------------------------------------------------
  {
    auto a = metl::make_optional(42);
    CHECK(a.has_value());
    CHECK_EQ(*a, 42);
    auto b = metl::make_optional<tracker>(1, 2);
    CHECK(b.has_value());
    CHECK_EQ(b->value, 3);
  }

  // ---- optional<optional<T>> hijack prevention ---------------------------
  {
    metl::optional<int> inner(5);
    metl::optional<metl::optional<int>> outer(inner);
    CHECK(outer.has_value());
    CHECK((*outer).has_value());
    CHECK_EQ(**outer, 5);

    // outer empty initially
    metl::optional<metl::optional<int>> outer2;
    CHECK(!outer2.has_value());
  }

  // ---- swap ---------------------------------------------------------------
  {
    metl::optional<int> a(1);
    metl::optional<int> b(2);
    a.swap(b);
    CHECK_EQ(*a, 2);
    CHECK_EQ(*b, 1);

    metl::optional<int> c;
    metl::optional<int> d(99);
    c.swap(d);
    CHECK(c.has_value());
    CHECK_EQ(*c, 99);
    CHECK(!d.has_value());

    using metl::swap;
    swap(c, d);
    CHECK(!c.has_value());
    CHECK(d.has_value());
    CHECK_EQ(*d, 99);
  }

  // ---- comparisons: optional vs optional ---------------------------------
  {
    metl::optional<int> a(1);
    metl::optional<int> b(1);
    metl::optional<int> c(2);
    metl::optional<int> e;
    metl::optional<int> f;

    CHECK(a == b);
    CHECK(!(a != b));
    CHECK(a < c);
    CHECK(c > a);
    CHECK(a <= b);
    CHECK(a >= b);
    CHECK(e == f);
    CHECK(e < a);  // empty < non-empty
    CHECK(!(a < e));
  }

  // ---- comparisons: optional vs nullopt ----------------------------------
  {
    metl::optional<int> a(1);
    metl::optional<int> e;

    CHECK(!(a == metl::nullopt));
    CHECK(e == metl::nullopt);
    CHECK(!(metl::nullopt == a));
    CHECK(metl::nullopt == e);
    CHECK(a != metl::nullopt);
    CHECK(!(e != metl::nullopt));
    CHECK(!(a < metl::nullopt));
    CHECK(!(metl::nullopt < e));
    CHECK(metl::nullopt < a);
  }

  // ---- comparisons: optional vs T ----------------------------------------
  {
    metl::optional<int> a(5);
    metl::optional<int> e;

    CHECK(a == 5);
    CHECK(!(a == 6));
    CHECK(5 == a);
    CHECK(!(a != 5));
    CHECK(e != 5);
    CHECK(a < 6);
    CHECK(4 < a);
    CHECK(e < 5);  // empty always < T
  }

  // ---- monadic and_then ---------------------------------------------------
  {
    auto doubler = [](int x) -> metl::optional<int> { return metl::optional<int>(x * 2); };
    metl::optional<int> a(5);
    auto r = a.and_then(doubler);
    CHECK(r.has_value());
    CHECK_EQ(*r, 10);

    metl::optional<int> e;
    auto r2 = e.and_then(doubler);
    CHECK(!r2.has_value());

    // rvalue
    auto r3 = metl::optional<int>(7).and_then(doubler);
    CHECK(r3.has_value());
    CHECK_EQ(*r3, 14);

    // const&
    const metl::optional<int> ca(3);
    auto r4 = ca.and_then(doubler);
    CHECK(r4.has_value());
    CHECK_EQ(*r4, 6);
  }

  // ---- monadic transform --------------------------------------------------
  {
    auto add_one = [](int x) { return x + 1; };
    metl::optional<int> a(10);
    auto r = a.transform(add_one);
    CHECK(r.has_value());
    CHECK_EQ(*r, 11);

    metl::optional<int> e;
    auto r2 = e.transform(add_one);
    CHECK(!r2.has_value());

    // rvalue
    auto r3 = metl::optional<int>(20).transform(add_one);
    CHECK(r3.has_value());
    CHECK_EQ(*r3, 21);

    // const&
    const metl::optional<int> ca(5);
    auto r4 = ca.transform(add_one);
    CHECK(r4.has_value());
    CHECK_EQ(*r4, 6);
  }

  // ---- monadic or_else ----------------------------------------------------
  {
    auto fallback = []() -> metl::optional<int> { return metl::optional<int>(99); };
    metl::optional<int> e;
    auto r = static_cast<metl::optional<int>&&>(e).or_else(fallback);
    CHECK(r.has_value());
    CHECK_EQ(*r, 99);

    metl::optional<int> a(7);
    auto r2 = static_cast<metl::optional<int>&&>(a).or_else(fallback);
    CHECK(r2.has_value());
    CHECK_EQ(*r2, 7);

    const metl::optional<int> ce;
    auto r3 = ce.or_else(fallback);
    CHECK(r3.has_value());
    CHECK_EQ(*r3, 99);

    const metl::optional<int> ca(5);
    auto r4 = ca.or_else(fallback);
    CHECK(r4.has_value());
    CHECK_EQ(*r4, 5);
  }

  // ---- std::hash specialization ------------------------------------------
  {
    metl::optional<int> a(42);
    metl::optional<int> e;
    std::hash<metl::optional<int>> h;
    CHECK_EQ(h(a), std::hash<int>{}(42));
    CHECK_EQ(h(e), static_cast<std::size_t>(0));
  }

  // ---- value_or ref-qual variants ----------------------------------------
  {
    metl::optional<int> a(5);
    metl::optional<int> e;
    int v1 = a.value_or(99);
    int v2 = e.value_or(99);
    CHECK_EQ(v1, 5);
    CHECK_EQ(v2, 99);

    int v3 = static_cast<metl::optional<int>&&>(metl::optional<int>(7)).value_or(99);
    CHECK_EQ(v3, 7);

    const metl::optional<int> ca(11);
    int v4 = ca.value_or(99);
    CHECK_EQ(v4, 11);
  }

  // ---- operator= from nullopt --------------------------------------------
  {
    metl::optional<int> a(1);
    a = metl::nullopt;
    CHECK(!a.has_value());
  }

  return metl_test::exit_code();
}
