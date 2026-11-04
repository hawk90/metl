// Assignment semantics of optional that std users rely on.
//
//   * optional<bool> from another optional: bool is constructible from
//     optional<U> through its explicit operator bool, so the source's
//     has_value() became the stored value (an empty optional<int> gave an
//     engaged optional<bool> holding false). Refused now.
//   * optional<const T> did not compile: placement new needed a const T* as
//     void*. It constructs, assigns by replacing, and value_or drops the const.

#include "metl_check.hpp"

#include <type_traits>

#include <metl/optional.hpp>

namespace {

// --- optional<bool> ----------------------------------------------------------

static_assert(!std::is_constructible_v<metl::optional<bool>, metl::optional<int>>,
              "optional<bool> must not take an optional's has_value() as its value");
static_assert(!std::is_constructible_v<metl::optional<bool>, const metl::optional<int>&>, "");
static_assert(!std::is_assignable_v<metl::optional<bool>&, metl::optional<int>>, "");
static_assert(std::is_constructible_v<metl::optional<bool>, bool>, "a bool still converts");
static_assert(std::is_constructible_v<metl::optional<bool>, int>, "so does an int");
static_assert(std::is_constructible_v<metl::optional<int>, metl::optional<int>>, "copy is unaffected");

// --- optional<const T> -----------------------------------------------------------

struct point {
  int x;
};

void optional_of_const() {
  metl::optional<const int> a(3);
  metl::optional<const int> b;
  b = a;  // empty: constructs
  CHECK(b.has_value());
  CHECK_EQ(*b, 3);
  b = metl::optional<const int>(5);  // engaged: a const int cannot be assigned, so it is replaced
  CHECK_EQ(*b, 5);
  static_assert(std::is_same_v<decltype(a.value_or(0)), int>, "value_or returns the unqualified type");
  CHECK_EQ(metl::optional<const int>().value_or(7), 7);

  metl::optional<const point> p(point{1});
  p = point{2};
  CHECK_EQ(p->x, 2);
  p.reset();
  CHECK(!p.has_value());
}

}  // namespace

int main() {
  optional_of_const();
  return metl_test::exit_code();
}
