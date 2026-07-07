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
  if (empty.has_value()) {
    return 1;
  }

  empty = 7;
  if (!empty || *empty != 7) {
    return 2;
  }

  metl::optional<int> copied(empty);
  if (!copied || copied.value() != 7) {
    return 3;
  }

  copied.emplace(11);
  if (copied.value_or(0) != 11) {
    return 4;
  }

  copied.reset();
  if (copied.value_or(9) != 9) {
    return 5;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::optional<tracker> item;
    item.emplace(42);
    if (!item || item->value != 42) {
      return 6;
    }

    metl::optional<tracker> moved(static_cast<metl::optional<tracker>&&>(item));
    if (!moved || moved->value != 42) {
      return 7;
    }

    moved = metl::nullopt;
    if (moved.has_value()) {
      return 8;
    }
  }

  if (tracker::constructions != tracker::destructions) {
    return 9;
  }

  // ---- in_place_t constructor --------------------------------------------
  {
    metl::optional<tracker> in_place(metl::in_place, 3, 4);
    if (!in_place || in_place->value != 7) {
      return 10;
    }
  }

  // ---- make_optional ------------------------------------------------------
  {
    auto a = metl::make_optional(42);
    if (!a || *a != 42) {
      return 11;
    }
    auto b = metl::make_optional<tracker>(1, 2);
    if (!b || b->value != 3) {
      return 12;
    }
  }

  // ---- optional<optional<T>> hijack prevention ---------------------------
  {
    metl::optional<int> inner(5);
    metl::optional<metl::optional<int>> outer(inner);
    if (!outer || !*outer || **outer != 5) {
      return 13;
    }

    // outer empty initially
    metl::optional<metl::optional<int>> outer2;
    if (outer2.has_value()) {
      return 14;
    }
  }

  // ---- swap ---------------------------------------------------------------
  {
    metl::optional<int> a(1);
    metl::optional<int> b(2);
    a.swap(b);
    if (*a != 2 || *b != 1) {
      return 15;
    }

    metl::optional<int> c;
    metl::optional<int> d(99);
    c.swap(d);
    if (!c || *c != 99 || d.has_value()) {
      return 16;
    }

    using metl::swap;
    swap(c, d);
    if (c.has_value() || !d || *d != 99) {
      return 17;
    }
  }

  // ---- comparisons: optional vs optional ---------------------------------
  {
    metl::optional<int> a(1);
    metl::optional<int> b(1);
    metl::optional<int> c(2);
    metl::optional<int> e;
    metl::optional<int> f;

    if (!(a == b))
      return 20;
    if (a != b)
      return 21;
    if (!(a < c))
      return 22;
    if (!(c > a))
      return 23;
    if (!(a <= b))
      return 24;
    if (!(a >= b))
      return 25;
    if (!(e == f))
      return 26;
    if (!(e < a))
      return 27;  // empty < non-empty
    if (a < e)
      return 28;
  }

  // ---- comparisons: optional vs nullopt ----------------------------------
  {
    metl::optional<int> a(1);
    metl::optional<int> e;

    if (a == metl::nullopt)
      return 30;
    if (!(e == metl::nullopt))
      return 31;
    if (metl::nullopt == a)
      return 32;
    if (!(metl::nullopt == e))
      return 33;
    if (!(a != metl::nullopt))
      return 34;
    if (e != metl::nullopt)
      return 35;
    if (a < metl::nullopt)
      return 36;
    if (metl::nullopt < e)
      return 37;
    if (!(metl::nullopt < a))
      return 38;
  }

  // ---- comparisons: optional vs T ----------------------------------------
  {
    metl::optional<int> a(5);
    metl::optional<int> e;

    if (!(a == 5))
      return 40;
    if (a == 6)
      return 41;
    if (!(5 == a))
      return 42;
    if (a != 5)
      return 43;
    if (!(e != 5))
      return 44;
    if (!(a < 6))
      return 45;
    if (!(4 < a))
      return 46;
    if (!(e < 5))
      return 47;  // empty always < T
  }

  // ---- monadic and_then ---------------------------------------------------
  {
    auto doubler = [](int x) -> metl::optional<int> { return metl::optional<int>(x * 2); };
    metl::optional<int> a(5);
    auto r = a.and_then(doubler);
    if (!r || *r != 10)
      return 50;

    metl::optional<int> e;
    auto r2 = e.and_then(doubler);
    if (r2.has_value())
      return 51;

    // rvalue
    auto r3 = metl::optional<int>(7).and_then(doubler);
    if (!r3 || *r3 != 14)
      return 52;

    // const&
    const metl::optional<int> ca(3);
    auto r4 = ca.and_then(doubler);
    if (!r4 || *r4 != 6)
      return 53;
  }

  // ---- monadic transform --------------------------------------------------
  {
    auto add_one = [](int x) { return x + 1; };
    metl::optional<int> a(10);
    auto r = a.transform(add_one);
    if (!r || *r != 11)
      return 60;

    metl::optional<int> e;
    auto r2 = e.transform(add_one);
    if (r2.has_value())
      return 61;

    // rvalue
    auto r3 = metl::optional<int>(20).transform(add_one);
    if (!r3 || *r3 != 21)
      return 62;

    // const&
    const metl::optional<int> ca(5);
    auto r4 = ca.transform(add_one);
    if (!r4 || *r4 != 6)
      return 63;
  }

  // ---- monadic or_else ----------------------------------------------------
  {
    auto fallback = []() -> metl::optional<int> { return metl::optional<int>(99); };
    metl::optional<int> e;
    auto r = static_cast<metl::optional<int>&&>(e).or_else(fallback);
    if (!r || *r != 99)
      return 70;

    metl::optional<int> a(7);
    auto r2 = static_cast<metl::optional<int>&&>(a).or_else(fallback);
    if (!r2 || *r2 != 7)
      return 71;

    const metl::optional<int> ce;
    auto r3 = ce.or_else(fallback);
    if (!r3 || *r3 != 99)
      return 72;

    const metl::optional<int> ca(5);
    auto r4 = ca.or_else(fallback);
    if (!r4 || *r4 != 5)
      return 73;
  }

  // ---- std::hash specialization ------------------------------------------
  {
    metl::optional<int> a(42);
    metl::optional<int> e;
    std::hash<metl::optional<int>> h;
    if (h(a) != std::hash<int>{}(42))
      return 80;
    if (h(e) != 0)
      return 81;
  }

  // ---- value_or ref-qual variants ----------------------------------------
  {
    metl::optional<int> a(5);
    metl::optional<int> e;
    int v1 = a.value_or(99);
    int v2 = e.value_or(99);
    if (v1 != 5 || v2 != 99)
      return 90;

    int v3 = static_cast<metl::optional<int>&&>(metl::optional<int>(7)).value_or(99);
    if (v3 != 7)
      return 91;

    const metl::optional<int> ca(11);
    int v4 = ca.value_or(99);
    if (v4 != 11)
      return 92;
  }

  // ---- operator= from nullopt --------------------------------------------
  {
    metl::optional<int> a(1);
    a = metl::nullopt;
    if (a.has_value())
      return 100;
  }

  return 0;
}
