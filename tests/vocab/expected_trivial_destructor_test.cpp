// expected is trivially destructible when T and E are (#474).
//
// Then the destructor's two checks cannot fire: no member destructor runs, so
// nothing can see or interrupt the switching state. Without a destructor of
// its own, expected is a literal type in C++17 as well, so a constexpr
// function can return one: twice() and try_parse_* are constant-evaluated
// below in every standard. Since #163 the copy and move are trivial too when T's and E's
// are (expected_triviality_test.cpp).

#include "metl_check.hpp"

#include <type_traits>

#include <metl/expected.hpp>
#include <metl/parse.hpp>
#include <metl/span.hpp>

namespace {

struct counted {
  static int destroyed;
  int value = 0;
  ~counted() { ++destroyed; }
};
int counted::destroyed = 0;

static_assert(std::is_trivially_destructible_v<metl::expected<int, int>>);
static_assert(std::is_trivially_destructible_v<metl::expected<void, int>>);
static_assert(std::is_trivially_destructible_v<metl::expected<const int, unsigned char>>);
static_assert(!std::is_trivially_destructible_v<metl::expected<counted, int>>);
static_assert(!std::is_trivially_destructible_v<metl::expected<int, counted>>);
static_assert(!std::is_trivially_destructible_v<metl::expected<void, counted>>);


// A literal type in C++17: a constexpr function can build and return one.
constexpr metl::expected<int, int> twice(int x) {
  if (x < 0) {
    return metl::unexpected<int>(x);
  }
  return x * 2;
}
constexpr auto doubled = twice(21);
constexpr auto refused = twice(-1);
static_assert(doubled.has_value());
static_assert(!refused.has_value());  // the accessors are constexpr only from C++20

// try_parse_* runs at compile time in C++17 too. Its accessors are constexpr
// only from C++20, so C++17 reads the state here and the value at run time.
constexpr char digits[] = {'1', '2', '7'};
constexpr char letters[] = {'x'};
constexpr auto parsed = metl::try_parse_uint<unsigned>(metl::span<const char>(digits, 3));
constexpr auto rejected = metl::try_parse_uint<unsigned>(metl::span<const char>(letters, 1));
static_assert(parsed.has_value() && !rejected.has_value());

}  // namespace

int main() {
  // A non-trivial member is still destroyed exactly once, in either state.
  counted::destroyed = 0;
  {
    metl::expected<counted, int> value_state{metl::in_place};
    metl::expected<int, counted> error_state{metl::unexpect};
    metl::expected<void, counted> void_error{metl::unexpect};
  }
  CHECK_EQ(counted::destroyed, 3);
  CHECK_EQ(parsed->value, 127U);
  CHECK_EQ(*doubled, 42);
  CHECK_EQ(refused.error(), -1);
  return metl_test::exit_code();
}
