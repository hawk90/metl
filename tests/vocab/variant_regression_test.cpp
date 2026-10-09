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
//   (3) a valueless variant compares equal to another valueless one and
//       before any engaged one, and assigning from it makes the target
//       valueless.

#include "metl_check.hpp"

#include <utility>

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

// ---- (3) support: a comparable alternative that can be made to throw --------
// Unassignable, so a same-index assignment destroys and reconstructs, and a
// throw in the reconstruct is how a variant<int, brittle> becomes valueless.
struct brittle {
  static int live;
  static bool arm;
  int value;

  explicit brittle(int v) : value(v) { ++live; }
  brittle(const brittle& o) : value(o.value) {
    if (arm) {
      arm = false;
      throw 44;
    }
    ++live;
  }
  brittle& operator=(const brittle&) = delete;
  ~brittle() { --live; }

  friend bool operator==(const brittle& a, const brittle& b) { return a.value == b.value; }
  friend bool operator!=(const brittle& a, const brittle& b) { return a.value != b.value; }
  friend bool operator<(const brittle& a, const brittle& b) { return a.value < b.value; }
  friend bool operator>(const brittle& a, const brittle& b) { return a.value > b.value; }
  friend bool operator<=(const brittle& a, const brittle& b) { return a.value <= b.value; }
  friend bool operator>=(const brittle& a, const brittle& b) { return a.value >= b.value; }
};

int brittle::live = 0;
bool brittle::arm = false;

using breakable = metl::variant<int, brittle>;

// Leaves `target` valueless by failing a same-index reconstruct.
void make_valueless(breakable& target) {
  target.emplace<brittle>(1);
  const breakable source(metl::in_place_index<1>, 2);
  brittle::arm = true;
  try {
    target = source;
  } catch (int) {
  }
}

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

  // ---- (3) a valueless variant: comparing it, and assigning from it ----
  {
    brittle::live = 0;
    breakable a;
    breakable b;
    make_valueless(a);
    make_valueless(b);
    CHECK(a.valueless_by_exception());
    CHECK(b.valueless_by_exception());
    CHECK_EQ(brittle::live, 0);

    // Two valueless variants are equal, and neither orders before the other.
    CHECK(a == b);
    CHECK(!(a != b));
    CHECK(!(a < b));
    CHECK(!(a > b));
    CHECK(a <= b);
    CHECK(a >= b);

    // A valueless variant orders before any engaged one.
    const breakable engaged(metl::in_place_index<0>, 5);
    CHECK(!(a == engaged));
    CHECK(a != engaged);
    CHECK(a < engaged);
    CHECK(!(engaged < a));
    CHECK(engaged > a);
    CHECK(!(a > engaged));
    CHECK(a <= engaged);
    CHECK(!(engaged <= a));
    CHECK(engaged >= a);
    CHECK(!(a >= engaged));

    // Assigning FROM a valueless variant makes the target valueless and
    // destroys what it held.
    breakable copy_target(metl::in_place_index<1>, 7);
    CHECK_EQ(brittle::live, 1);
    copy_target = a;
    CHECK(copy_target.valueless_by_exception());
    CHECK_EQ(brittle::live, 0);

    breakable move_target(metl::in_place_index<1>, 8);
    CHECK_EQ(brittle::live, 1);
    move_target = std::move(b);
    CHECK(move_target.valueless_by_exception());
    CHECK_EQ(brittle::live, 0);

    const breakable copied(a);
    CHECK(copied.valueless_by_exception());
  }
  CHECK_EQ(brittle::live, 0);

  return metl_test::exit_code();
}

#else

int main() {
  return metl_test::skip("variant_regression_test", "built without exceptions");
}

#endif  // !METL_NO_EXCEPTIONS
