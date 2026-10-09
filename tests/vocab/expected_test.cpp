#include "metl_check.hpp"

#include <type_traits>
#include <utility>

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

// ---- Lifetime-counted value and error types --------------------------------
// Two DISTINCT counters, so a path that builds the wrong member, or skips a
// destructor, moves one count and not the other. `tracker` above counts both
// sides of an expected<tracker, tracker> together, which balances by accident.
template <int Tag>
struct counted {
  static int live;
  int value;

  explicit counted(int input) noexcept : value(input) { ++live; }
  counted(const counted& other) noexcept : value(other.value) { ++live; }
  counted(counted&& other) noexcept : value(other.value) { ++live; }
  counted& operator=(const counted& other) noexcept {
    value = other.value;
    return *this;
  }
  counted& operator=(counted&& other) noexcept {
    value = other.value;
    return *this;
  }
  ~counted() { --live; }
};

template <int Tag>
int counted<Tag>::live = 0;

using live_value = counted<0>;
using live_error = counted<1>;

// An error whose move constructor is not noexcept (it never actually throws).
// expected::swap picks its value<->error branch on which member is nothrow-
// movable, so this is what reaches the second branch.
struct live_error_throwing_move {
  static int live;
  int value;

  explicit live_error_throwing_move(int input) noexcept : value(input) { ++live; }
  live_error_throwing_move(const live_error_throwing_move& other) noexcept : value(other.value) { ++live; }
  live_error_throwing_move(live_error_throwing_move&& other) noexcept(false) : value(other.value) { ++live; }
  live_error_throwing_move& operator=(const live_error_throwing_move&) = default;
  live_error_throwing_move& operator=(live_error_throwing_move&&) = default;
  ~live_error_throwing_move() { --live; }
};

int live_error_throwing_move::live = 0;

// The move constructor is reached only by moving a NAMED expected: a prvalue
// initialiser is elided straight into the target, so `expected e = make()` and
// every `expected{...}` temporary never call it.
void move_constructor_moves_the_active_member() {
  using result = metl::expected<live_value, live_error>;
  {
    result source(metl::in_place, 7);
    result moved(std::move(source));
    CHECK(moved.has_value());
    CHECK_EQ(moved->value, 7);
    CHECK_EQ(live_value::live, 2);
    CHECK_EQ(live_error::live, 0);
  }
  {
    result source(metl::unexpect, 9);
    result moved(std::move(source));
    CHECK(!moved.has_value());
    CHECK_EQ(moved.error().value, 9);
    CHECK_EQ(live_value::live, 0);
    CHECK_EQ(live_error::live, 2);
  }
  CHECK_EQ(live_value::live, 0);
  CHECK_EQ(live_error::live, 0);
}

void void_copy_assignment_copies_the_state() {
  using result = metl::expected<void, live_error>;
  {
    result target;
    const result failure(metl::unexpect, 4);
    target = failure;  // value -> error
    CHECK(!target.has_value());
    CHECK_EQ(target.error().value, 4);

    const result other_failure(metl::unexpect, 5);
    target = other_failure;  // error -> error
    CHECK(!target.has_value());
    CHECK_EQ(target.error().value, 5);

    const result success;
    target = success;  // error -> value
    CHECK(target.has_value());
    CHECK_EQ(live_error::live, 2);  // the two named failures
  }
  CHECK_EQ(live_error::live, 0);
}

void error_state_copy_assignment_replaces_the_error() {
  using result = metl::expected<live_value, live_error>;
  {
    result target(metl::unexpect, 1);
    const result source(metl::unexpect, 2);
    target = source;
    CHECK(!target.has_value());
    CHECK_EQ(target.error().value, 2);
    CHECK_EQ(live_error::live, 2);
    CHECK_EQ(live_value::live, 0);
  }
  CHECK_EQ(live_error::live, 0);
}

void error_error_swap_exchanges_the_errors() {
  using result = metl::expected<live_value, live_error>;
  {
    result a(metl::unexpect, 1);
    result b(metl::unexpect, 2);
    a.swap(b);
    CHECK(!a.has_value());
    CHECK(!b.has_value());
    CHECK_EQ(a.error().value, 2);
    CHECK_EQ(b.error().value, 1);
    CHECK_EQ(live_error::live, 2);
  }
  CHECK_EQ(live_error::live, 0);
}

// The value<->error swap moves one member aside, destroys it, and builds the
// other in its place. Skipping a destructor there leaves both expecteds
// reading correctly -- only the count shows the leaked member.
template <typename Error>
void value_error_swap_destroys_what_it_moves(int& error_live) {
  using result = metl::expected<live_value, Error>;
  {
    result a(metl::in_place, 1);
    result b(metl::unexpect, 2);
    CHECK_EQ(live_value::live, 1);
    CHECK_EQ(error_live, 1);

    a.swap(b);  // a: value -> error, b: error -> value
    CHECK(!a.has_value());
    CHECK_EQ(a.error().value, 2);
    CHECK(b.has_value());
    CHECK_EQ(b->value, 1);
    CHECK_EQ(live_value::live, 1);
    CHECK_EQ(error_live, 1);

    b.swap(a);  // the mirror call: swap_value_error with the operands reversed
    CHECK(a.has_value());
    CHECK_EQ(a->value, 1);
    CHECK(!b.has_value());
    CHECK_EQ(b.error().value, 2);
    CHECK_EQ(live_value::live, 1);
    CHECK_EQ(error_live, 1);
  }
  CHECK_EQ(live_value::live, 0);
  CHECK_EQ(error_live, 0);
}

void run_lifetime_checks() {
  move_constructor_moves_the_active_member();
  void_copy_assignment_copies_the_state();
  error_state_copy_assignment_replaces_the_error();
  error_error_swap_exchanges_the_errors();
  static_assert(std::is_nothrow_move_constructible_v<live_error>);
  value_error_swap_destroys_what_it_moves<live_error>(live_error::live);
  static_assert(!std::is_nothrow_move_constructible_v<live_error_throwing_move>);
  value_error_swap_destroys_what_it_moves<live_error_throwing_move>(live_error_throwing_move::live);
}

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

    // and_then may change the value type while keeping the same error type E.
    auto r3 = e.and_then([](int v) { return metl::expected<long, int>(v + 100L); });
    if (!r3 || *r3 != 110L) {
      return 25;
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

  // ---- expected<void, E>: the short-circuit half of the monadic API ---------
  // Coverage showed the complementary branches untested: and_then / transform
  // when the expected holds an ERROR, and or_else / transform_error when it
  // holds a VALUE. Those are the branches that make the API worth having -- an
  // and_then that ran its function on an error, or an or_else that ran its
  // handler on success, would pass every test that only exercised the other
  // direction.
  {
    using ev_t = metl::expected<void, int>;

    // Named void_ok / void_err rather than ok / err: this function already has
    // locals with those names near the top, and the build is -Wshadow -Werror.
    const ev_t void_ok;
    const ev_t void_err(metl::unexpect, 42);

    // and_then: runs on value, short-circuits on error.
    int and_then_calls = 0;
    auto make_ok = [&and_then_calls] {
      ++and_then_calls;
      return metl::expected<int, int>(7);
    };

    const auto from_ok = void_ok.and_then(make_ok);
    if (!from_ok.has_value() || from_ok.value() != 7 || and_then_calls != 1) {
      return 54;
    }
    const auto from_err = void_err.and_then(make_ok);
    if (from_err.has_value() || from_err.error() != 42 || and_then_calls != 1) {
      return 55;  // the function must NOT have run, and the error must propagate
    }

    // transform: same shape.
    int transform_calls = 0;
    auto to_value = [&transform_calls] {
      ++transform_calls;
      return 9;
    };

    const auto t_ok = void_ok.transform(to_value);
    if (!t_ok.has_value() || t_ok.value() != 9 || transform_calls != 1) {
      return 56;
    }
    const auto t_err = void_err.transform(to_value);
    if (t_err.has_value() || t_err.error() != 42 || transform_calls != 1) {
      return 57;
    }

    // transform to void: the U == void specialisation, both directions.
    int void_calls = 0;
    auto to_void = [&void_calls] { ++void_calls; };
    const auto tv_ok = void_ok.transform(to_void);
    if (!tv_ok.has_value() || void_calls != 1) {
      return 58;
    }
    const auto tv_err = void_err.transform(to_void);
    if (tv_err.has_value() || tv_err.error() != 42 || void_calls != 1) {
      return 59;
    }

    // or_else: runs on error, short-circuits on value.
    int or_else_calls = 0;
    auto recover = [&or_else_calls](int e) {
      ++or_else_calls;
      return metl::expected<void, long>(metl::unexpect, static_cast<long>(e) + 1);
    };

    const auto o_err = void_err.or_else(recover);
    if (o_err.has_value() || o_err.error() != 43 || or_else_calls != 1) {
      return 60;
    }
    const auto o_ok = void_ok.or_else(recover);
    if (!o_ok.has_value() || or_else_calls != 1) {
      return 61;  // the handler must NOT have run on a value
    }

    // transform_error: same shape.
    int te_calls = 0;
    auto widen = [&te_calls](int e) {
      ++te_calls;
      return static_cast<long>(e) * 2;
    };

    const auto te_err = void_err.transform_error(widen);
    if (te_err.has_value() || te_err.error() != 84 || te_calls != 1) {
      return 62;
    }
    const auto te_ok = void_ok.transform_error(widen);
    if (!te_ok.has_value() || te_calls != 1) {
      return 63;
    }
  }

  run_lifetime_checks();
  return metl_test::exit_code();
}
