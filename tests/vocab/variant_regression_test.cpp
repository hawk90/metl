// Regression tests for metl::variant:
//   (1) comparison operators must compile and work for a variant with
//       DUPLICATE alternative types (previously compared via get<T>, which is
//       ill-formed when a type is not a unique alternative).
//   (2) same-index assignment must be exception-safe. It assigns in place
//       with T::operator=, so a throwing assignment leaves the alternative
//       held (T's own guarantee). An alternative with no assignment operator
//       is destroyed and reconstructed instead; if that construction throws,
//       the variant must be valueless, never a destroyed member paired with a
//       stale discriminant (double-destroy UB).

#include "metl_check.hpp"

#include <metl/in_place.hpp>
#include <metl/variant.hpp>

// Every case here throws; without exceptions there is nothing to run.
#if !METL_NO_EXCEPTIONS

namespace {

// ---- (2) support: a type whose copy-construction can be made to throw ----
struct throwing {
  static int live;
  static bool arm;  // when true, the next copy-construction throws.
  int value = 0;

  explicit throwing(int v) : value(v) { ++live; }
  throwing(const throwing& o) : value(o.value) {
    if (arm) {
      arm = false;
      throw 42;
    }
    ++live;
  }
  throwing& operator=(const throwing& o) {
    if (arm) {
      arm = false;
      throw 43;
    }
    value = o.value;
    return *this;
  }
  ~throwing() { --live; }
};

int throwing::live = 0;
bool throwing::arm = false;

// The same, without an assignment operator: the reconstruct path.
struct throwing_unassignable {
  static int live;
  static bool arm;
  const int value;

  explicit throwing_unassignable(int v) : value(v) { ++live; }
  throwing_unassignable(const throwing_unassignable& o) : value(o.value) {
    if (arm) {
      arm = false;
      throw 42;
    }
    ++live;
  }
  throwing_unassignable& operator=(const throwing_unassignable&) = delete;
  ~throwing_unassignable() { --live; }
};

int throwing_unassignable::live = 0;
bool throwing_unassignable::arm = false;

}  // namespace

int main() {
  // ---- (1) duplicate alternative types ----
  {
    using dup = metl::variant<int, int>;
    dup a(metl::in_place_index<0>, 5);
    dup b(metl::in_place_index<0>, 5);
    dup c(metl::in_place_index<1>, 5);  // same value, different alternative
    dup d(metl::in_place_index<0>, 9);

    CHECK(a == b);  // same index, equal value
    CHECK(a != c);  // different index compares unequal
    CHECK(a != d);  // same index, different value
    CHECK(a < c);   // index 0 < index 1
    CHECK(a < d);   // 5 < 9 within index 0
    CHECK(c > a);
    CHECK(a <= b);
    CHECK(a >= b);
  }

  // ---- (2) same-index assignment exception safety ----
  {
    throwing::live = 0;
    metl::variant<throwing> dst(metl::in_place_index<0>, 1);
    metl::variant<throwing> src(metl::in_place_index<0>, 2);
    CHECK_EQ(throwing::live, 2);

    throwing::arm = true;  // the in-place copy-assign throws
    bool threw = false;
    try {
      dst = src;
    } catch (int) {
      threw = true;
    }
    CHECK(threw);
    // Assigned in place: the member was never destroyed, so it is still held.
    CHECK(!dst.valueless_by_exception());
    CHECK_EQ(throwing::live, 2);
  }
  CHECK_EQ(throwing::live, 0);

  {
    throwing_unassignable::live = 0;
    metl::variant<throwing_unassignable> dst(metl::in_place_index<0>, 1);
    metl::variant<throwing_unassignable> src(metl::in_place_index<0>, 2);
    CHECK_EQ(throwing_unassignable::live, 2);

    throwing_unassignable::arm = true;  // the reconstruct throws mid-construct
    bool threw = false;
    try {
      dst = src;
    } catch (int) {
      threw = true;
    }
    CHECK(threw);
    // dst's old member was destroyed before the throwing construct; it must now
    // be valueless (not pointing at a destroyed object).
    CHECK(dst.valueless_by_exception());
    CHECK_EQ(throwing_unassignable::live, 1);
  }
  CHECK_EQ(throwing_unassignable::live, 0);

  return metl_test::exit_code();
}

#else

int main() {
  return metl_test::skip("variant_regression_test", "built without exceptions");
}

#endif  // !METL_NO_EXCEPTIONS
