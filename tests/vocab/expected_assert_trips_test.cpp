// Trips every METL_ASSERT precondition of expected<T, E> and expected<void, E>
// once (#364).
//
// The accessors trip on the wrong state: value(), operator* and operator-> on
// an error, error() on a value. The monadic operations' error paths trip only
// while the expected is switching (#374): a member's destructor that calls,
// say, and_then on its own expected finds it holding neither member, and the
// assert stops it reading an error that is not there. Each such case builds
// the expected in storage that is never destroyed, since the trip leaves it
// mid-switch.

#include "assert_trip.hpp"
#include "metl_check.hpp"

#include <new>
#include <utility>

#include <metl/expected.hpp>
#include <metl/in_place.hpp>

namespace {

using metl_test::trips;

// A member whose destructor runs `g_mid` on its owner, once.
void (*g_mid)(void*) = nullptr;
void* g_owner = nullptr;

struct member {
  int id;
  explicit member(int i) noexcept : id(i) {}
  member(const member& other) noexcept : id(other.id) {}
  member(member&& other) noexcept : id(other.id) { other.id = 0; }
  member& operator=(const member&) = default;
  member& operator=(member&&) = default;
  ~member() {
    if (id != 0 && g_mid != nullptr) {
      void (*mid)(void*) = g_mid;
      g_mid = nullptr;
      mid(g_owner);
    }
  }
};

using result = metl::expected<member, member>;
using void_result = metl::expected<void, member>;

// ---- accessors in the wrong state ---------------------------------------------

void accessors_trip_in_the_wrong_state() {
  static result error(metl::unexpect, 1);
  static result value(metl::in_place, 1);
  CHECK(trips([] { (void)static_cast<const result&>(error)->id; }));
  CHECK(trips([] { (void)error->id; }));
  CHECK(trips([] { (void)*static_cast<const result&>(error); }));
  CHECK(trips([] { (void)*error; }));
  CHECK(trips([] { (void)*std::move(static_cast<const result&>(error)); }));
  CHECK(trips([] { (void)*std::move(error); }));
  CHECK(trips([] { (void)error.value(); }));
  CHECK(trips([] { (void)static_cast<const result&>(error).value(); }));
  CHECK(trips([] { (void)std::move(error).value(); }));
  CHECK(trips([] { (void)std::move(static_cast<const result&>(error)).value(); }));
  CHECK(trips([] { (void)value.error(); }));
  CHECK(trips([] { (void)static_cast<const result&>(value).error(); }));
  CHECK(trips([] { (void)std::move(value).error(); }));
  CHECK(trips([] { (void)std::move(static_cast<const result&>(value)).error(); }));

  static void_result failed(metl::unexpect, 1);
  static void_result ok;
  CHECK(trips([] { failed.value(); }));
  CHECK(trips([] { std::move(failed).value(); }));
  CHECK(trips([] { *failed; }));
  CHECK(trips([] { (void)ok.error(); }));
  CHECK(trips([] { (void)static_cast<const void_result&>(ok).error(); }));
  CHECK(trips([] { (void)std::move(ok).error(); }));
  CHECK(trips([] { (void)std::move(static_cast<const void_result&>(ok)).error(); }));
}

// ---- monadic operations while switching -------------------------------------------

// Builds `R` in fresh storage, holding a member whose destructor runs `op` on
// it, and then switches the member out, so `op` runs mid-switch.
template <typename R, typename Build, typename Switch>
bool trips_mid_switch(void (*op)(void*), Build build, Switch do_switch) {
  alignas(R) static unsigned char raw[sizeof(R)];
  R* e = ::new (static_cast<void*>(raw)) R(build());
  g_owner = e;
  g_mid = op;
  const bool tripped = trips([&] { do_switch(*e); });
  g_mid = nullptr;
  return tripped;
}

template <typename R>
R& as(void* p) {
  return *static_cast<R*>(p);
}

bool on_result(void (*op)(void*)) {
  return trips_mid_switch<result>(
      op, [] { return result(metl::in_place, 1); }, [](result& e) { e.emplace_error(2); });
}

bool on_void(void (*op)(void*)) {
  return trips_mid_switch<void_result>(
      op, [] { return void_result(metl::unexpect, 1); }, [](void_result& e) { e.emplace(); });
}

// The callables the monadic operations take. Function objects at namespace
// scope, so the non-capturing lambdas below can build them.
struct next_fn {  // and_then
  template <typename... A>
  metl::expected<int, member> operator()(A&&...) const {
    return metl::expected<int, member>(1);
  }
};
struct map_fn {  // transform, transform_error
  template <typename... A>
  int operator()(A&&...) const {
    return 1;
  }
};
struct recover_fn {  // or_else
  template <typename A>
  metl::expected<member, int> operator()(A&&) const {
    return metl::expected<member, int>(metl::in_place, 1);
  }
};
struct run_fn {  // expected<void, E>::transform to void
  void operator()() const {}
};
struct void_recover_fn {  // expected<void, E>::or_else
  template <typename A>
  metl::expected<void, int> operator()(A&&) const {
    return metl::expected<void, int>();
  }
};

void monadic_error_paths_trip_while_switching() {
  using r = result;
  // and_then: T -> expected<U, E>
  CHECK(on_result([](void* p) { (void)as<r>(p).and_then(next_fn{}); }));
  CHECK(on_result([](void* p) { (void)static_cast<const r&>(as<r>(p)).and_then(next_fn{}); }));
  CHECK(on_result([](void* p) { (void)std::move(as<r>(p)).and_then(next_fn{}); }));
  CHECK(on_result([](void* p) { (void)std::move(static_cast<const r&>(as<r>(p))).and_then(next_fn{}); }));
  // transform: T -> U
  CHECK(on_result([](void* p) { (void)as<r>(p).transform(map_fn{}); }));
  CHECK(on_result([](void* p) { (void)static_cast<const r&>(as<r>(p)).transform(map_fn{}); }));
  CHECK(on_result([](void* p) { (void)std::move(as<r>(p)).transform(map_fn{}); }));
  CHECK(on_result([](void* p) { (void)std::move(static_cast<const r&>(as<r>(p))).transform(map_fn{}); }));
  // or_else: E -> expected<T, G>
  CHECK(on_result([](void* p) { (void)as<r>(p).or_else(recover_fn{}); }));
  CHECK(on_result([](void* p) { (void)static_cast<const r&>(as<r>(p)).or_else(recover_fn{}); }));
  CHECK(on_result([](void* p) { (void)std::move(as<r>(p)).or_else(recover_fn{}); }));
  CHECK(on_result([](void* p) { (void)std::move(static_cast<const r&>(as<r>(p))).or_else(recover_fn{}); }));
  // transform_error: E -> G
  CHECK(on_result([](void* p) { (void)as<r>(p).transform_error(map_fn{}); }));
  CHECK(on_result([](void* p) { (void)static_cast<const r&>(as<r>(p)).transform_error(map_fn{}); }));
  CHECK(on_result([](void* p) { (void)std::move(as<r>(p)).transform_error(map_fn{}); }));
  CHECK(
      on_result([](void* p) { (void)std::move(static_cast<const r&>(as<r>(p))).transform_error(map_fn{}); }));
}

void void_monadic_error_paths_trip_while_switching() {
  using v = void_result;
  CHECK(on_void([](void* p) { (void)as<v>(p).and_then(next_fn{}); }));
  CHECK(on_void([](void* p) { (void)static_cast<const v&>(as<v>(p)).and_then(next_fn{}); }));
  CHECK(on_void([](void* p) { (void)std::move(as<v>(p)).and_then(next_fn{}); }));
  CHECK(on_void([](void* p) { (void)std::move(static_cast<const v&>(as<v>(p))).and_then(next_fn{}); }));
  // transform has two error paths, for a void and for a value result.
  CHECK(on_void([](void* p) { (void)as<v>(p).transform(run_fn{}); }));
  CHECK(on_void([](void* p) { (void)as<v>(p).transform(map_fn{}); }));
  CHECK(on_void([](void* p) { (void)static_cast<const v&>(as<v>(p)).transform(run_fn{}); }));
  CHECK(on_void([](void* p) { (void)static_cast<const v&>(as<v>(p)).transform(map_fn{}); }));
  CHECK(on_void([](void* p) { (void)std::move(as<v>(p)).transform(run_fn{}); }));
  CHECK(on_void([](void* p) { (void)std::move(as<v>(p)).transform(map_fn{}); }));
  CHECK(on_void([](void* p) { (void)std::move(static_cast<const v&>(as<v>(p))).transform(run_fn{}); }));
  CHECK(on_void([](void* p) { (void)std::move(static_cast<const v&>(as<v>(p))).transform(map_fn{}); }));
  CHECK(on_void([](void* p) { (void)as<v>(p).or_else(void_recover_fn{}); }));
  CHECK(on_void([](void* p) { (void)static_cast<const v&>(as<v>(p)).or_else(void_recover_fn{}); }));
  CHECK(on_void([](void* p) { (void)std::move(as<v>(p)).or_else(void_recover_fn{}); }));
  CHECK(
      on_void([](void* p) { (void)std::move(static_cast<const v&>(as<v>(p))).or_else(void_recover_fn{}); }));
  CHECK(on_void([](void* p) { (void)as<v>(p).transform_error(map_fn{}); }));
  CHECK(on_void([](void* p) { (void)static_cast<const v&>(as<v>(p)).transform_error(map_fn{}); }));
  CHECK(on_void([](void* p) { (void)std::move(as<v>(p)).transform_error(map_fn{}); }));
  CHECK(on_void([](void* p) { (void)std::move(static_cast<const v&>(as<v>(p))).transform_error(map_fn{}); }));
}

}  // namespace

int main() {
  if (!metl_test::asserts_active) {
    return metl_test::skip("expected_assert_trips_test", "METL_ASSERT is stripped at this hardening level");
  }
  accessors_trip_in_the_wrong_state();
  monadic_error_paths_trip_while_switching();
  void_monadic_error_paths_trip_while_switching();
  return metl_test::exit_code();
}
