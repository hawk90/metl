// Regression tests for metl::expected exception safety:
//   (1) cross-state assignment (error -> value) must not leave a destroyed
//       member paired with a stale discriminant if the T constructor throws;
//       it must roll back to the original (error) state.
//   (2) cross-state swap (value <-> error) must roll back on a throwing move.

#include "metl_check.hpp"

#include <metl/expected.hpp>

namespace {

// A value type that is neither nothrow-constructible-from-int nor
// nothrow-move-constructible, and can be armed to throw during construction.
struct val {
  static int live;
  static bool arm;
  int x = 0;

  explicit val(int v) : x(v) {
    if (arm) {
      arm = false;
      throw 7;
    }
    ++live;
  }
  val(const val& o) : x(o.x) {
    if (arm) {
      arm = false;
      throw 7;
    }
    ++live;
  }
  val(val&& o) noexcept(false) : x(o.x) {
    if (arm) {
      arm = false;
      throw 7;
    }
    ++live;
  }
  val& operator=(const val&) = default;
  val& operator=(val&&) = default;
  ~val() { --live; }
};

int val::live = 0;
bool val::arm = false;

}  // namespace

int main() {
  // ---- Happy-path cross-state assignment still works ----
  {
    val::live = 0;
    metl::expected<val, int> e(metl::unexpect, 99);
    CHECK(!e.has_value());
    metl::expected<val, int> src(metl::in_place, 5);
    e = src;  // error -> value
    CHECK(e.has_value());
    CHECK_EQ(e->x, 5);
  }
  CHECK_EQ(val::live, 0);

#if defined(__cpp_exceptions)
  // ---- (1) throwing cross-state assignment rolls back to error state ----
  {
    val::live = 0;
    metl::expected<val, int> dst(metl::unexpect, 99);  // error state
    metl::expected<val, int> src(metl::in_place, 5);   // value state
    CHECK_EQ(val::live, 1);

    val::arm = true;  // the copy into dst throws mid-construct
    bool threw = false;
    try {
      dst = src;
    } catch (int) {
      threw = true;
    }
    CHECK(threw);
    CHECK(!dst.has_value());  // rolled back to error state
    CHECK_EQ(dst.error(), 99);
    CHECK_EQ(val::live, 1);  // only src's value alive; nothing double-destroyed
  }
  CHECK_EQ(val::live, 0);

  // ---- (2) throwing cross-state swap rolls back ----
  {
    val::live = 0;
    metl::expected<val, int> a(metl::in_place, 1);  // value
    metl::expected<val, int> b(metl::unexpect, 2);  // error
    CHECK_EQ(val::live, 1);

    val::arm = true;  // the move-construct of a's value into b throws
    bool threw = false;
    try {
      a.swap(b);
    } catch (int) {
      threw = true;
    }
    CHECK(threw);
    // Rolled back: a still has its value, b still has its error.
    CHECK(a.has_value());
    CHECK_EQ(a->x, 1);
    CHECK(!b.has_value());
    CHECK_EQ(b.error(), 2);
    CHECK_EQ(val::live, 1);
  }
  CHECK_EQ(val::live, 0);
#endif

  // ---- Happy-path cross-state swap ----
  {
    val::live = 0;
    metl::expected<val, int> a(metl::in_place, 1);  // value
    metl::expected<val, int> b(metl::unexpect, 2);  // error
    a.swap(b);
    CHECK(!a.has_value());
    CHECK_EQ(a.error(), 2);
    CHECK(b.has_value());
    CHECK_EQ(b->x, 1);
  }
  CHECK_EQ(val::live, 0);

  return metl_test::exit_code();
}
