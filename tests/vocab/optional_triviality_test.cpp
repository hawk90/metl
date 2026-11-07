// optional's copy, move and destructor are trivial exactly where std::optional's
// are (#163).
//
// Each one used to be user-provided for every T, so optional<int> was not
// trivially copyable: on AAPCS a function returning one passed it through a
// hidden pointer instead of r0, and a struct holding one could not be memcpy'd.
// Now each operation is trivial when T's is, and user-provided otherwise.

#include "metl_check.hpp"

#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>

#include <metl/optional.hpp>

namespace {

// Each operation of std::optional<T> and metl::optional<T> agrees, trivial or not.
template <typename T>
constexpr bool same_as_std =
    std::is_trivially_copy_constructible_v<metl::optional<T>> ==
        std::is_trivially_copy_constructible_v<std::optional<T>> &&
    std::is_trivially_move_constructible_v<metl::optional<T>> ==
        std::is_trivially_move_constructible_v<std::optional<T>> &&
    std::is_trivially_copy_assignable_v<metl::optional<T>> ==
        std::is_trivially_copy_assignable_v<std::optional<T>> &&
    std::is_trivially_move_assignable_v<metl::optional<T>> ==
        std::is_trivially_move_assignable_v<std::optional<T>> &&
    std::is_trivially_destructible_v<metl::optional<T>> ==
        std::is_trivially_destructible_v<std::optional<T>> &&
    std::is_trivially_copyable_v<metl::optional<T>> == std::is_trivially_copyable_v<std::optional<T>>;

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

// Trivially copyable, but its move is user-provided.
struct trivial_copy_user_move {
  int value = 0;
  trivial_copy_user_move() = default;
  trivial_copy_user_move(const trivial_copy_user_move&) = default;
  trivial_copy_user_move(trivial_copy_user_move&& other) noexcept : value(other.value) { other.value = -1; }
  trivial_copy_user_move& operator=(const trivial_copy_user_move&) = default;
  trivial_copy_user_move& operator=(trivial_copy_user_move&&) = default;
  ~trivial_copy_user_move() = default;
};

// Trivially destructible, with a user-provided copy assignment.
struct user_assign {
  int value = 0;
  user_assign() = default;
  user_assign(const user_assign&) = default;
  user_assign(user_assign&&) = default;
  user_assign& operator=(const user_assign& other) {
    value = other.value + 100;
    return *this;
  }
  user_assign& operator=(user_assign&&) = default;
  ~user_assign() = default;
};

struct move_only {
  int value = 0;
  move_only() = default;
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

static_assert(same_as_std<int>);
static_assert(same_as_std<pod>);
static_assert(same_as_std<counted>);
static_assert(same_as_std<trivial_copy_user_move>);
static_assert(same_as_std<user_assign>);
static_assert(same_as_std<move_only>);

static_assert(std::is_trivially_copyable_v<metl::optional<int>>);
static_assert(std::is_trivially_copyable_v<metl::optional<pod>>);
static_assert(!std::is_trivially_copyable_v<metl::optional<counted>>);
static_assert(!std::is_trivially_destructible_v<metl::optional<counted>>);

// A const T stays trivially copy constructible and destructible. Its assignment
// replaces the object, so it is user-provided (std::optional<const int> has
// none at all; METL keeps the replacement).
static_assert(std::is_trivially_copy_constructible_v<metl::optional<const int>>);
static_assert(std::is_trivially_destructible_v<metl::optional<const int>>);
static_assert(std::is_copy_assignable_v<metl::optional<const int>>);
static_assert(!std::is_trivially_copy_assignable_v<metl::optional<const int>>);

// The layers add no members.
static_assert(sizeof(metl::optional<int>) == sizeof(std::optional<int>));
static_assert(sizeof(metl::optional<char>) == 2);
static_assert(sizeof(metl::optional<counted>) == sizeof(std::optional<counted>));
static_assert(sizeof(metl::optional<metl::optional<char>>) == 3);
static_assert(alignof(metl::optional<pod>) == alignof(pod));

// A trivial copy is a constant expression. Engaging needs construct_at, which is
// constexpr only from C++20, so C++17 copies an empty one.
constexpr metl::optional<int> none_copy = metl::optional<int>{};
static_assert(!none_copy.has_value());
#if METL_DETAIL_CONSTEXPR_LIFETIME
constexpr metl::optional<int> seven(7);
constexpr metl::optional<int> seven_copy = seven;
static_assert(seven_copy.has_value() && *seven_copy == 7);
#endif

}  // namespace

int main() {
  // memcpy round trip of a trivially copyable optional.
  {
    metl::optional<int> a(42);
    metl::optional<int> b;
    std::memcpy(static_cast<void*>(&b), static_cast<const void*>(&a), sizeof a);
    CHECK(b.has_value() && *b == 42);
    metl::optional<int> empty;
    std::memcpy(static_cast<void*>(&b), static_cast<const void*>(&empty), sizeof empty);
    CHECK(!b.has_value());
  }

  // The user-provided paths: every pair of engaged and empty states.
  {
    metl::optional<counted> full(metl::in_place, 5);
    metl::optional<counted> empty;
    metl::optional<counted> copy(full);
    CHECK(copy.has_value() && copy->value == 5 && counted::live == 2);
    metl::optional<counted> moved(std::move(copy));
    CHECK(moved.has_value() && moved->value == 5);
    CHECK(copy.has_value() && copy->value == -1);  // engaged, moved-from
    CHECK(counted::live == 3);

    copy = empty;  // engaged <- empty
    CHECK(!copy.has_value() && counted::live == 2);
    copy = full;  // empty <- engaged
    CHECK(copy.has_value() && copy->value == 5 && counted::live == 3);
    copy = moved;  // engaged <- engaged: assigns
    CHECK(copy.has_value() && copy->value == 5 && counted::live == 3);
    copy = std::move(empty);  // engaged <- empty, by move
    CHECK(!copy.has_value() && counted::live == 2);
    copy = std::move(moved);  // empty <- engaged, by move
    CHECK(copy.has_value() && copy->value == 5 && counted::live == 3);
    CHECK(moved.has_value() && moved->value == -1);

    const metl::optional<counted>& self = copy;
    copy = self;  // self-assignment keeps the value
    CHECK(copy.has_value() && copy->value == 5 && counted::live == 3);
  }
  CHECK(counted::live == 0);

  // Trivial copy, user-provided move: each uses its own.
  {
    metl::optional<trivial_copy_user_move> a(metl::in_place);
    a->value = 3;
    metl::optional<trivial_copy_user_move> b(a);
    CHECK(b->value == 3 && a->value == 3);
    metl::optional<trivial_copy_user_move> c(std::move(a));
    CHECK(c->value == 3 && a->value == -1);
  }

  // User-provided copy assignment of T runs when both are engaged.
  {
    metl::optional<user_assign> a(metl::in_place);
    metl::optional<user_assign> b(metl::in_place);
    a->value = 1;
    b = a;
    CHECK(b->value == 101);
  }

  // A const T is replaced, not assigned.
  {
    metl::optional<const int> a(1);
    metl::optional<const int> b(2);
    a = b;
    CHECK(*a == 2);
  }

  // Move-only T.
  {
    metl::optional<move_only> a(metl::in_place);
    a->value = 9;
    metl::optional<move_only> b(std::move(a));
    CHECK(b->value == 9);
    metl::optional<move_only> c;
    c = std::move(b);
    CHECK(c->value == 9);
  }
  return metl_test::exit_code();
}
