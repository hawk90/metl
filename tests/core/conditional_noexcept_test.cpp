// A constructor that is `noexcept` while it runs user code turns that code's
// exception into std::terminate. Each type below constructs something the user
// supplies -- a hasher, a key comparison, a table entry, a lock policy, a
// callable -- and was unconditionally noexcept. Now each is noexcept exactly
// when what it constructs is, as std's containers and P0052's scope_exit are.
//
// The static_asserts hold in every build. The runtime half needs exceptions and
// reports a skip without them.
#include "metl_check.hpp"

#include <array>
#include <cstddef>
#include <functional>
#include <type_traits>

#include <metl/lock.hpp>
#include <metl/lookup_table.hpp>
#include <metl/scope_exit.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

bool g_throw = false;

void maybe_throw() {
  if (g_throw) {
    g_throw = false;
#if !METL_NO_EXCEPTIONS
    throw 1;
#endif
  }
}

// Default construction is fine; only the move can throw (#275).
struct move_throwing_hash {
  move_throwing_hash() = default;
  move_throwing_hash(const move_throwing_hash&) = default;
  move_throwing_hash(move_throwing_hash&&) { maybe_throw(); }
  move_throwing_hash& operator=(const move_throwing_hash&) = default;
  move_throwing_hash& operator=(move_throwing_hash&&) = default;
  std::size_t operator()(int key) const noexcept { return static_cast<std::size_t>(key); }
};

struct move_throwing_equal {
  move_throwing_equal() = default;
  move_throwing_equal(const move_throwing_equal&) = default;
  move_throwing_equal(move_throwing_equal&&) { maybe_throw(); }
  move_throwing_equal& operator=(const move_throwing_equal&) = default;
  move_throwing_equal& operator=(move_throwing_equal&&) = default;
  bool operator()(int lhs, int rhs) const noexcept { return lhs == rhs; }
};

struct throwing_hash {
  throwing_hash() { maybe_throw(); }
  std::size_t operator()(int key) const noexcept { return static_cast<std::size_t>(key); }
};

struct throwing_equal {
  throwing_equal() { maybe_throw(); }
  bool operator()(int lhs, int rhs) const noexcept { return lhs == rhs; }
};

struct throwing_value {
  throwing_value() { maybe_throw(); }
  bool operator==(const throwing_value&) const noexcept { return true; }
};

struct throwing_lock {
  using state_type = int;
  static int lock() {
    maybe_throw();
    return 0;
  }
  static void unlock(int) noexcept {}
};

int g_calls = 0;

struct throwing_copy {
  throwing_copy() = default;
  throwing_copy(const throwing_copy&) { maybe_throw(); }
  throwing_copy(throwing_copy&&) = delete;
  void operator()() const noexcept { ++g_calls; }
};

struct plain_callable {
  void operator()() const noexcept { ++g_calls; }
};

using map_hash = metl::static_unordered_map<int, int, 4, throwing_hash>;
using map_equal = metl::static_unordered_map<int, int, 4, std::hash<int>, throwing_equal>;
using map_plain = metl::static_unordered_map<int, int, 4>;
using set_hash = metl::static_unordered_set<int, 4, throwing_hash>;
using set_equal = metl::static_unordered_set<int, 4, std::hash<int>, throwing_equal>;
using set_plain = metl::static_unordered_set<int, 4>;
using table_throwing = metl::lookup_table<int, throwing_value, 2>;
using table_plain = metl::lookup_table<int, int, 2>;

static_assert(!std::is_nothrow_default_constructible_v<map_hash>,
              "a throwing Hash makes the map's ctor throwing");
static_assert(!std::is_nothrow_default_constructible_v<map_equal>, "so does a throwing KeyEqual");
static_assert(std::is_nothrow_default_constructible_v<map_plain>, "and the ordinary map stays noexcept");
static_assert(!std::is_nothrow_default_constructible_v<set_hash>, "set: throwing Hash");
static_assert(!std::is_nothrow_default_constructible_v<set_equal>, "set: throwing KeyEqual");
static_assert(std::is_nothrow_default_constructible_v<set_plain>, "set: ordinary");
// The move constructors move the Hash and KeyEqual too (#275).
using map_move_hash = metl::static_unordered_map<int, int, 4, move_throwing_hash>;
using map_move_equal = metl::static_unordered_map<int, int, 4, std::hash<int>, move_throwing_equal>;
using set_move_hash = metl::static_unordered_set<int, 4, move_throwing_hash>;
using set_move_equal = metl::static_unordered_set<int, 4, std::hash<int>, move_throwing_equal>;
static_assert(!std::is_nothrow_move_constructible_v<map_move_hash>, "map: Hash whose move can throw");
static_assert(!std::is_nothrow_move_constructible_v<map_move_equal>, "map: KeyEqual whose move can throw");
static_assert(!std::is_nothrow_move_constructible_v<set_move_hash>, "set: Hash whose move can throw");
static_assert(!std::is_nothrow_move_constructible_v<set_move_equal>, "set: KeyEqual whose move can throw");
static_assert(std::is_nothrow_move_constructible_v<map_plain>, "the ordinary map's move stays noexcept");
static_assert(std::is_nothrow_move_constructible_v<set_plain>, "the ordinary set's move stays noexcept");
static_assert(!std::is_nothrow_default_constructible_v<table_throwing>,
              "value-initialising a throwing entry");
static_assert(std::is_nothrow_default_constructible_v<table_plain>, "lookup_table of ints stays noexcept");
static_assert(!std::is_nothrow_default_constructible_v<metl::scoped_lock<throwing_lock>>,
              "a lock policy whose lock() can throw");
static_assert(std::is_nothrow_default_constructible_v<metl::scoped_lock<metl::null_lock>>,
              "null_lock stays noexcept");
static_assert(!std::is_nothrow_constructible_v<metl::scope_exit<throwing_copy>, const throwing_copy&>,
              "copying a throwing callable into a scope_exit");
static_assert(std::is_nothrow_constructible_v<metl::scope_exit<plain_callable>, plain_callable>,
              "an ordinary callable stays noexcept");
static_assert(!noexcept(metl::make_scope_exit(std::declval<const throwing_copy&>())),
              "make_scope_exit forwards the same answer");
static_assert(noexcept(metl::make_scope_exit(plain_callable{})), "make_scope_exit: ordinary");

#if !METL_NO_EXCEPTIONS
// Runs `construct` with the next user construction armed to throw, and reports
// whether the exception reached the caller -- rather than std::terminate.
template <typename F>
bool propagates(F construct) {
  g_throw = true;
  try {
    construct();
  } catch (int) {
    return true;
  }
  return false;
}
#endif

}  // namespace

int main() {
#if METL_NO_EXCEPTIONS
  return metl_test::skip("conditional_noexcept", "the runtime half needs exceptions; the static_asserts ran");
#else
  CHECK(propagates([] { map_hash map; }));
  CHECK(propagates([] { map_equal map; }));
  CHECK(propagates([] { set_hash set; }));
  CHECK(propagates([] { set_equal set; }));
  CHECK(propagates([] { table_throwing table; }));
  CHECK(propagates([] { metl::scoped_lock<throwing_lock> guard; }));
  {
    map_move_hash map_source;
    (void)map_source.try_emplace(1, 1);
    CHECK(propagates([&] { map_move_hash moved(static_cast<map_move_hash&&>(map_source)); }));
    set_move_equal set_source;
    (void)set_source.try_emplace(1);
    CHECK(propagates([&] { set_move_equal moved(static_cast<set_move_equal&&>(set_source)); }));
  }

  // P0052: if storing the callable throws, the guard calls the callable it was
  // given -- the cleanup still happens -- and the exception propagates.
  {
    const throwing_copy callable;
    g_calls = 0;
    CHECK(propagates([&] { metl::scope_exit<throwing_copy> guard(callable); }));
    CHECK_EQ(g_calls, 1);

    g_calls = 0;
    CHECK(propagates([&] { auto guard = metl::make_scope_exit(callable); }));
    CHECK_EQ(g_calls, 1);

    // Constructed without a throw, it runs once, at scope exit, as before.
    g_calls = 0;
    {
      metl::scope_exit<throwing_copy> guard(callable);
    }
    CHECK_EQ(g_calls, 1);
  }

  return metl_test::exit_code();
#endif
}
