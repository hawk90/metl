// Regression tests for metl::variant:
//   (1) comparison operators must compile and work for a variant with
//       DUPLICATE alternative types (previously compared via get<T>, which is
//       ill-formed when a type is not a unique alternative).
//   (2) same-index assignment must be exception-safe: if the underlying
//       copy/move constructor throws, the variant must not be left with a
//       destroyed member paired with a stale discriminant (double-destroy UB).

#include "metl_check.hpp"

#include <metl/in_place.hpp>
#include <metl/variant.hpp>

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
  throwing& operator=(const throwing&) = default;
  ~throwing() { --live; }
};

int throwing::live = 0;
bool throwing::arm = false;

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
#if defined(__cpp_exceptions)
  {
    throwing::live = 0;
    metl::variant<throwing> dst(metl::in_place_index<0>, 1);
    metl::variant<throwing> src(metl::in_place_index<0>, 2);
    CHECK_EQ(throwing::live, 2);

    throwing::arm = true;  // the same-index copy-assign will throw mid-construct
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
    // Only src's member remains live; dst's destroyed member was accounted for.
    CHECK_EQ(throwing::live, 1);
  }
  // dst destroyed (valueless: nothing to destroy), src destroyed its member.
  CHECK_EQ(throwing::live, 0);
#endif

  return metl_test::exit_code();
}
