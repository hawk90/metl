// Trips every METL_ASSERT precondition of optional, variant, fixed_function,
// fixed_any_invocable, intrusive_ptr and span once (#364).
//
// Each check is a precondition a caller can break: dereferencing an empty
// optional, get on the wrong alternative, assigning a target that does not
// fit. A check that no test trips can be deleted or inverted with every test
// still passing; tools/check_assert_trips.py counts these trips against every
// METL_ASSERT in include/metl.
//
// Every overload is tripped on its own: the const, rvalue and lvalue
// accessors each carry a copy of the check.

#include "assert_trip.hpp"
#include "metl_check.hpp"

#include <cstddef>
#include <utility>

#include <metl/fixed_function.hpp>
#include <metl/in_place.hpp>
#include <metl/intrusive_ptr.hpp>
#include <metl/optional.hpp>
#include <metl/span.hpp>
#include <metl/variant.hpp>

namespace {

using metl_test::trips;

// ---- optional -------------------------------------------------------------------

struct point {
  int x;
};

void optional_accessors_trip_when_empty() {
  metl::optional<point> empty;
  const metl::optional<point>& cempty = empty;
  CHECK(trips([&] { (void)cempty->x; }));
  CHECK(trips([&] { (void)empty->x; }));
  CHECK(trips([&] { (void)*cempty; }));
  CHECK(trips([&] { (void)*empty; }));
  CHECK(trips([&] { (void)*std::move(cempty); }));
  CHECK(trips([&] { (void)*std::move(empty); }));
  CHECK(trips([&] { (void)empty.value(); }));
  CHECK(trips([&] { (void)cempty.value(); }));
  CHECK(trips([&] { (void)std::move(empty).value(); }));
  CHECK(trips([&] { (void)std::move(cempty).value(); }));

  // The same calls on an engaged optional do not trip.
  const metl::optional<point> engaged(metl::in_place, point{1});
  CHECK(!trips([&] { (void)engaged->x; }));
}

// ---- variant ---------------------------------------------------------------------

void variant_get_trips_on_the_wrong_alternative() {
  metl::variant<int, long> v(metl::in_place_index<0>, 1);
  const metl::variant<int, long>& cv = v;
  CHECK(trips([&] { (void)metl::get<long>(v); }));
  CHECK(trips([&] { (void)metl::get<long>(cv); }));
  CHECK(trips([&] { (void)metl::get<long>(std::move(v)); }));
  CHECK(trips([&] { (void)metl::get<long>(std::move(cv)); }));
  CHECK(trips([&] { (void)metl::get<1>(v); }));
  CHECK(trips([&] { (void)metl::get<1>(cv); }));
  CHECK(trips([&] { (void)metl::get<1>(std::move(v)); }));
  CHECK(trips([&] { (void)metl::get<1>(std::move(cv)); }));
  CHECK(!trips([&] { (void)metl::get<int>(v); }));
}

#if !METL_NO_EXCEPTIONS
// Unassignable with a copy that can throw: a same-alternative assignment
// destroys and rebuilds, and a throw there leaves the variant valueless.
struct brittle {
  static bool arm;
  int value;
  explicit brittle(int v) : value(v) {}
  brittle(const brittle& other) : value(other.value) {
    if (arm) {
      arm = false;
      throw 0;
    }
  }
  brittle& operator=(const brittle&) = delete;
};
bool brittle::arm = false;

struct visitor {
  int operator()(int) const { return 0; }
  int operator()(const brittle&) const { return 1; }
};

void visit_trips_on_a_valueless_variant() {
  using var = metl::variant<int, brittle>;
  var v(metl::in_place_index<1>, 1);
  const var source(metl::in_place_index<1>, 2);
  brittle::arm = true;
  try {
    v = source;
  } catch (int) {
  }
  CHECK(v.valueless_by_exception());
  const var& cv = v;
  CHECK(trips([&] { (void)metl::visit(visitor{}, v); }));
  CHECK(trips([&] { (void)metl::visit(visitor{}, cv); }));
  CHECK(trips([&] { (void)metl::visit(visitor{}, std::move(v)); }));
  CHECK(trips([&] { (void)metl::visit(visitor{}, std::move(cv)); }));
}
#endif

// ---- fixed_function / fixed_any_invocable -------------------------------------------

int plain() {
  return 1;
}
int plain_noexcept() noexcept {
  return 1;
}

// Larger than the 16 bytes the wrappers below hold.
struct too_big {
  unsigned char bytes[64] = {};
  int operator()() const noexcept { return bytes[0]; }
};

template <typename Wrapper, typename Function>
void assign_trips_when_it_does_not_fit(Function function) {
  static Wrapper pointer_target;  // a function pointer does not fit in 4 bytes
  static Wrapper callable_target;
  CHECK(trips([&] { pointer_target.assign(function); }));
  CHECK(trips([&] { callable_target.assign(too_big{}); }));
}

void fixed_function_assign_trips_when_it_does_not_fit() {
  static_assert(sizeof(int (*)()) > 4, "a function pointer must not fit the 4-byte wrapper");
  assign_trips_when_it_does_not_fit<metl::fixed_function<int(), 4>>(&plain);
  assign_trips_when_it_does_not_fit<metl::fixed_function<int() noexcept, 4>>(&plain_noexcept);
  assign_trips_when_it_does_not_fit<metl::fixed_any_invocable<int(), 4>>(&plain);
  assign_trips_when_it_does_not_fit<metl::fixed_any_invocable<int() noexcept, 4>>(&plain_noexcept);
}

// ---- intrusive_ptr -------------------------------------------------------------------

struct node final : metl::intrusive_ref_counter<node> {
  int value = 0;
};

void intrusive_ptr_trips_on_null() {
  const node* none = nullptr;
  CHECK(trips([&] { intrusive_ptr_add_ref(none); }));
  CHECK(trips([&] { intrusive_ptr_release(none); }));
  static const metl::intrusive_ptr<node> empty;
  CHECK(trips([&] { (void)*empty; }));
  CHECK(trips([&] { (void)empty->value; }));
}

// ---- span ------------------------------------------------------------------------------

void span_trips_out_of_range() {
  int values[2] = {1, 2};
  const metl::span<int> none;
  const metl::span<int> two(values, 2);
  CHECK(trips([&] { (void)none.front(); }));
  CHECK(trips([&] { (void)none.back(); }));
  CHECK(trips([&] { (void)two[2]; }));
  CHECK(!trips([&] { (void)two[1]; }));
}

}  // namespace

int main() {
  if (!metl_test::asserts_active) {
    return metl_test::skip("vocab_assert_trips_test", "METL_ASSERT is stripped at this hardening level");
  }
  optional_accessors_trip_when_empty();
  variant_get_trips_on_the_wrong_alternative();
#if !METL_NO_EXCEPTIONS
  visit_trips_on_a_valueless_variant();
#endif
  fixed_function_assign_trips_when_it_does_not_fit();
  intrusive_ptr_trips_on_null();
  span_trips_out_of_range();
  return metl_test::exit_code();
}
