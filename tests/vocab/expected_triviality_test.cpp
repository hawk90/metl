// expected's copy, move and destructor are trivial when T's and E's are (#163),
// and a throwing copy or move destroys nothing it did not build (#685).
//
// Each copy and move used to be user-provided for every T and E, so
// expected<int, int> was not trivially copyable: on AAPCS a function returning
// one passed it through a hidden pointer instead of r0. Now each operation is
// trivial when both members' are, and user-provided otherwise.
//
// The copy and move constructors used to set the state first and build the
// member in their body. When that build threw, the already complete base ran
// its destructor and ended a member that was never built.

#include "metl_check.hpp"

#include <cstring>
#include <type_traits>
#include <utility>

#include <metl/expected.hpp>

namespace {

struct counted {
  static int live;
  int value = 0;
  explicit counted(int v) : value(v) { ++live; }
  counted(const counted& other) : value(other.value) { ++live; }
  counted(counted&& other) noexcept : value(other.value) {
    other.value = -1;
    ++live;
  }
  counted& operator=(const counted&) = default;
  counted& operator=(counted&&) = default;
  ~counted() { --live; }
};
int counted::live = 0;

struct move_only {
  int value = 0;
  move_only() = default;
  explicit move_only(int v) : value(v) {}
  move_only(const move_only&) = delete;
  move_only(move_only&&) = default;
  move_only& operator=(const move_only&) = delete;
  move_only& operator=(move_only&&) = default;
  ~move_only() = default;
};

struct pod {
  int a;
  short b;
};

template <typename X>
constexpr bool all_trivial =
    std::is_trivially_copy_constructible_v<X> && std::is_trivially_move_constructible_v<X> &&
    std::is_trivially_copy_assignable_v<X> && std::is_trivially_move_assignable_v<X> &&
    std::is_trivially_destructible_v<X> && std::is_trivially_copyable_v<X>;

template <typename X>
constexpr bool none_trivial =
    !std::is_trivially_copy_constructible_v<X> && !std::is_trivially_move_constructible_v<X> &&
    !std::is_trivially_copy_assignable_v<X> && !std::is_trivially_move_assignable_v<X> &&
    !std::is_trivially_destructible_v<X>;

static_assert(all_trivial<metl::expected<int, int>>);
static_assert(all_trivial<metl::expected<pod, unsigned char>>);
static_assert(all_trivial<metl::expected<void, int>>);
static_assert(all_trivial<metl::expected<void, pod>>);

// Either member alone decides.
static_assert(none_trivial<metl::expected<counted, int>>);
static_assert(none_trivial<metl::expected<int, counted>>);
static_assert(none_trivial<metl::expected<void, counted>>);

// A move-only member: trivial move, no copy.
static_assert(std::is_trivially_move_constructible_v<metl::expected<move_only, int>>);
static_assert(!std::is_copy_constructible_v<metl::expected<move_only, int>>);
static_assert(!std::is_copy_assignable_v<metl::expected<move_only, int>>);
static_assert(std::is_move_assignable_v<metl::expected<move_only, int>>);

// The noexcept of the user-provided paths follows the members.
static_assert(std::is_nothrow_move_constructible_v<metl::expected<counted, int>>);
static_assert(std::is_nothrow_move_assignable_v<metl::expected<counted, int>>);

// The layers add no members.
static_assert(sizeof(metl::expected<int, int>) == 8);
static_assert(sizeof(metl::expected<unsigned char, unsigned char>) == 2);
static_assert(sizeof(metl::expected<void, unsigned char>) == 2);
static_assert(sizeof(metl::expected<counted, int>) == 8);
static_assert(alignof(metl::expected<pod, unsigned char>) == alignof(pod));

// A trivial copy is a constant expression in C++17 as well.
constexpr metl::expected<int, int> made(7);
constexpr metl::expected<int, int> made_copy = made;
static_assert(made_copy.has_value());
constexpr metl::expected<void, int> failed(metl::unexpect, 3);
constexpr metl::expected<void, int> failed_copy = failed;
static_assert(!failed_copy.has_value());

#if !METL_NO_EXCEPTIONS
int built = 0;
int destroyed = 0;

struct throws_on_copy {
  throws_on_copy() { ++built; }
  throws_on_copy(const throws_on_copy& /*unused*/) { throw 1; }
  throws_on_copy(throws_on_copy&& /*unused*/) { throw 2; }  // NOLINT(performance-noexcept-move-constructor)
  throws_on_copy& operator=(const throws_on_copy&) = default;
  throws_on_copy& operator=(throws_on_copy&&) = default;
  ~throws_on_copy() { ++destroyed; }
};

// Builds an X from `tag`, copies and moves it, both of which throw, and
// reports whether every destructor that ran matched a constructor that
// finished.
template <typename X, typename Tag>
bool balanced(Tag tag) {
  built = 0;
  destroyed = 0;
  {
    X source(tag);
    try {
      X copy(source);
    } catch (int) {
    }
    try {
      X moved(std::move(source));
    } catch (int) {
    }
  }
  return built == 1 && destroyed == 1;
}
#endif

}  // namespace

int main() {
  // memcpy round trip of a trivially copyable expected, in both states.
  {
    metl::expected<int, int> a(42);
    metl::expected<int, int> b(metl::unexpect, 1);
    std::memcpy(static_cast<void*>(&b), static_cast<const void*>(&a), sizeof a);
    CHECK(b.has_value() && *b == 42);
    metl::expected<int, int> e(metl::unexpect, 5);
    std::memcpy(static_cast<void*>(&b), static_cast<const void*>(&e), sizeof e);
    CHECK(!b.has_value() && b.error() == 5);
  }

  // The user-provided paths: every pair of value and error states.
  {
    using X = metl::expected<counted, counted>;
    X value(metl::in_place, 5);
    X error(metl::unexpect, 9);
    X copy(value);
    CHECK(copy.has_value() && copy->value == 5 && counted::live == 3);
    X moved(std::move(copy));
    CHECK(moved.has_value() && moved->value == 5 && copy->value == -1);
    X error_copy(error);
    CHECK(!error_copy.has_value() && error_copy.error().value == 9);

    copy = error;  // value <- error
    CHECK(!copy.has_value() && copy.error().value == 9);
    copy = value;  // error <- value
    CHECK(copy.has_value() && copy->value == 5);
    copy = moved;  // value <- value
    CHECK(copy.has_value() && copy->value == 5);
    copy = std::move(error_copy);  // value <- error, by move
    CHECK(!copy.has_value() && copy.error().value == 9 && error_copy.error().value == -1);
    copy = std::move(moved);  // error <- value, by move
    CHECK(copy.has_value() && copy->value == 5);

    const X& self = copy;
    copy = self;
    CHECK(copy.has_value() && copy->value == 5);
    CHECK(counted::live == 5);  // value, error, copy, moved, error_copy
  }
  CHECK(counted::live == 0);

  {
    using V = metl::expected<void, counted>;
    V ok;
    V bad(metl::unexpect, 4);
    V copy(bad);
    CHECK(!copy.has_value() && copy.error().value == 4);
    copy = ok;
    CHECK(copy.has_value());
    copy = std::move(bad);
    CHECK(!copy.has_value() && copy.error().value == 4);
    V moved(std::move(copy));
    CHECK(!moved.has_value() && moved.error().value == 4);
  }
  CHECK(counted::live == 0);

  {
    metl::expected<move_only, int> a(metl::in_place, 3);
    metl::expected<move_only, int> b(std::move(a));
    CHECK(b->value == 3);
    metl::expected<move_only, int> c(metl::unexpect, 1);
    c = std::move(b);
    CHECK(c.has_value() && c->value == 3);
  }

  CHECK(*made_copy == 7);
  CHECK(failed_copy.error() == 3);

#if !METL_NO_EXCEPTIONS
  // #685: a throwing copy or move used to destroy the member it never built.
  CHECK((balanced<metl::expected<throws_on_copy, int>>(metl::in_place)));
  CHECK((balanced<metl::expected<int, throws_on_copy>>(metl::unexpect)));
  CHECK((balanced<metl::expected<void, throws_on_copy>>(metl::unexpect)));
#endif
  return metl_test::exit_code();
}
