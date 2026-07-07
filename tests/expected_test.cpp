#include <metl/expected.hpp>

namespace {

struct tracker {
  static int constructions;
  static int destructions;

  tracker() : value(0) { ++constructions; }
  explicit tracker(int input) : value(input) { ++constructions; }
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
  // ------- Original tests --------------------------------------------------
  metl::expected<int, int> ok(7);
  if (!ok || ok.value() != 7) {
    return 1;
  }

  ok = 9;
  if (ok.value_or(0) != 9) {
    return 2;
  }

  metl::expected<int, int> err(metl::make_unexpected(13));
  if (err.has_value() || err.error() != 13) {
    return 3;
  }

  err = 21;
  if (!err || *err != 21) {
    return 4;
  }

  err = metl::make_unexpected(33);
  if (err.has_value() || err.error() != 33) {
    return 5;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::expected<tracker, int> value(metl::make_unexpected(1));
    if (value.has_value()) {
      return 6;
    }

    value.emplace(42);
    if (!value || value->value != 42) {
      return 7;
    }

    value.emplace_error(8);
    if (value.has_value() || value.error() != 8) {
      return 8;
    }
  }

  if (tracker::constructions != tracker::destructions) {
    return 9;
  }

  // ------- in_place_t / unexpect_t constructors ---------------------------
  {
    metl::expected<int, int> a(metl::in_place, 11);
    if (!a || *a != 11) {
      return 10;
    }

    metl::expected<int, int> b(metl::unexpect, 99);
    if (b || b.error() != 99) {
      return 11;
    }
  }

  // ------- unexpected wrapper API -----------------------------------------
  {
    metl::unexpected<int> u(metl::in_place, 5);
    if (u.error() != 5 || u.value() != 5) {
      return 12;
    }

    metl::unexpected<int> u2(7);
    if (u2.error() != 7) {
      return 13;
    }

    // Comparisons.
    if (u == u2) {
      return 14;
    }
    metl::unexpected<int> u3(5);
    if (!(u == u3)) {
      return 15;
    }

    // CTAD.
    metl::unexpected ctad(123);
    if (ctad.error() != 123) {
      return 16;
    }

    // swap.
    metl::unexpected<int> sa(1);
    metl::unexpected<int> sb(2);
    sa.swap(sb);
    if (sa.error() != 2 || sb.error() != 1) {
      return 17;
    }
    swap(sa, sb);
    if (sa.error() != 1 || sb.error() != 2) {
      return 18;
    }
  }

  // ------- error_or -------------------------------------------------------
  {
    metl::expected<int, int> e_ok(5);
    if (e_ok.error_or(99) != 99) {
      return 19;
    }
    metl::expected<int, int> e_err = metl::make_unexpected(7);
    if (e_err.error_or(99) != 7) {
      return 20;
    }
  }

  // ------- Monadic: and_then ----------------------------------------------
  {
    metl::expected<int, int> e(10);
    auto r = e.and_then([](int v) { return metl::expected<int, int>(v + 1); });
    if (!r || *r != 11) {
      return 21;
    }

    metl::expected<int, int> e2 = metl::make_unexpected(42);
    auto r2 = e2.and_then([](int v) { return metl::expected<int, int>(v + 1); });
    if (r2 || r2.error() != 42) {
      return 22;
    }
  }

  // ------- Monadic: transform ---------------------------------------------
  {
    metl::expected<int, int> e(3);
    auto r = e.transform([](int v) { return v * 2; });
    if (!r || *r != 6) {
      return 23;
    }

    metl::expected<int, int> e2 = metl::make_unexpected(9);
    auto r2 = e2.transform([](int v) { return v * 2; });
    if (r2 || r2.error() != 9) {
      return 24;
    }
  }

  // ------- Monadic: or_else -----------------------------------------------
  {
    metl::expected<int, int> e(5);
    auto r = e.or_else([](int) { return metl::expected<int, int>(99); });
    if (!r || *r != 5) {
      return 25;
    }

    metl::expected<int, int> e2 = metl::make_unexpected(1);
    auto r2 = e2.or_else([](int) { return metl::expected<int, int>(99); });
    if (!r2 || *r2 != 99) {
      return 26;
    }
  }

  // ------- Monadic: transform_error ---------------------------------------
  {
    metl::expected<int, int> e(5);
    auto r = e.transform_error([](int x) { return x + 100; });
    if (!r || *r != 5) {
      return 27;
    }

    metl::expected<int, int> e2 = metl::make_unexpected(7);
    auto r2 = e2.transform_error([](int x) { return x + 100; });
    if (r2 || r2.error() != 107) {
      return 28;
    }
  }

  // ------- expected<void, E> ----------------------------------------------
  {
    metl::expected<void, int> v;
    if (!v) {
      return 29;
    }

    metl::expected<void, int> v_err(metl::unexpect, 7);
    if (v_err || v_err.error() != 7) {
      return 30;
    }

    // in_place ctor.
    metl::expected<void, int> v_ip(metl::in_place);
    if (!v_ip) {
      return 31;
    }

    // Conversion from unexpected.
    metl::expected<void, int> v_conv(metl::make_unexpected(13));
    if (v_conv || v_conv.error() != 13) {
      return 32;
    }

    // Assignment.
    v = metl::make_unexpected(50);
    if (v || v.error() != 50) {
      return 33;
    }
    v.emplace();
    if (!v) {
      return 34;
    }

    // error_or.
    if (v.error_or(77) != 77) {
      return 35;
    }
    metl::expected<void, int> v_err2(metl::unexpect, 9);
    if (v_err2.error_or(77) != 9) {
      return 36;
    }

    // Monadic and_then on void.
    auto chained = v.and_then([]() { return metl::expected<int, int>(123); });
    if (!chained || *chained != 123) {
      return 37;
    }

    // Monadic transform from void to int.
    auto tr = v.transform([]() { return 7; });
    if (!tr || *tr != 7) {
      return 38;
    }

    // Monadic transform from void to void.
    int counter = 0;
    auto tr_void = v.transform([&counter]() { ++counter; });
    if (!tr_void || counter != 1) {
      return 39;
    }

    // Monadic or_else on void.
    metl::expected<void, int> v_e(metl::unexpect, 5);
    auto recovered = v_e.or_else([](int) { return metl::expected<void, int>(); });
    if (!recovered) {
      return 40;
    }

    // Monadic transform_error on void.
    metl::expected<void, int> v_e2(metl::unexpect, 5);
    auto te = v_e2.transform_error([](int x) { return x + 1; });
    if (te || te.error() != 6) {
      return 41;
    }
  }

  // ------- swap (expected) ------------------------------------------------
  {
    metl::expected<int, int> a(1);
    metl::expected<int, int> b(2);
    a.swap(b);
    if (*a != 2 || *b != 1) {
      return 42;
    }

    metl::expected<int, int> c(1);
    metl::expected<int, int> d = metl::make_unexpected(99);
    c.swap(d);
    if (c.has_value() || c.error() != 99 || !d.has_value() || *d != 1) {
      return 43;
    }

    swap(c, d);
    if (!c.has_value() || *c != 1 || d.has_value() || d.error() != 99) {
      return 44;
    }
  }

  // ------- Comparisons (expected) -----------------------------------------
  {
    metl::expected<int, int> a(5);
    metl::expected<int, int> b(5);
    metl::expected<int, int> c(6);
    if (!(a == b) || (a != b)) {
      return 45;
    }
    if (a == c) {
      return 46;
    }
    if (!(a == 5) || !(5 == a)) {
      return 47;
    }
    if (a == 6) {
      return 48;
    }

    metl::expected<int, int> e_err = metl::make_unexpected(42);
    metl::unexpected<int> u(42);
    if (!(e_err == u) || !(u == e_err)) {
      return 49;
    }
    metl::unexpected<int> u2(7);
    if (e_err == u2) {
      return 50;
    }
  }

  // ------- expected<void, E> comparisons ----------------------------------
  {
    metl::expected<void, int> a;
    metl::expected<void, int> b;
    if (!(a == b)) {
      return 51;
    }

    metl::expected<void, int> c(metl::unexpect, 5);
    metl::expected<void, int> d(metl::unexpect, 5);
    if (!(c == d)) {
      return 52;
    }
    if (a == c) {
      return 53;
    }
  }

  return 0;
}
