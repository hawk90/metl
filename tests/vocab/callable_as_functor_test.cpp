// A fixed_function or fixed_any_invocable used directly as a container's
// comparator or hasher (#692). Asking whether such a container is copy or move
// constructible used to be a hard error rather than a value: the container's
// `explicit C(const Compare&)` made the trait try the callable's converting
// constructor with the container itself, and that constructor's constraint
// asked the same trait again before checking that the container is not
// callable. On clang that also stopped fixed_vector<flat_map<..., F>> from
// compiling.
//
// The trait values below are the point. A regression shows as this file
// failing to compile, which is why no mutant covers it: a mutant that stops the
// build is not counted as killed.
#include "metl_check.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

#include <metl/fixed_function.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

using copyable_less = metl::fixed_function<bool(int, int), 16>;
using nothrow_less = metl::fixed_function<bool(int, int) noexcept, 16>;
using move_only_less = metl::fixed_any_invocable<bool(int, int), 16>;
using copyable_hash = metl::fixed_function<std::size_t(int), 16>;
using move_only_hash = metl::fixed_any_invocable<std::size_t(int), 16>;

// Asked first, before anything else instantiates these types: on GCC the
// nothrow-move query failed only when it was the first one asked.
static_assert(std::is_nothrow_move_constructible_v<metl::flat_map<int, int, 4, copyable_less>>);
static_assert(std::is_nothrow_move_constructible_v<metl::flat_set<int, 4, copyable_less>>);
static_assert(std::is_nothrow_move_constructible_v<metl::fixed_priority_queue<int, 4, copyable_less>>);
static_assert(std::is_nothrow_move_constructible_v<metl::flat_map<int, int, 4, nothrow_less>>);
static_assert(std::is_nothrow_move_constructible_v<metl::flat_set<int, 4, nothrow_less>>);
static_assert(std::is_nothrow_move_constructible_v<metl::fixed_priority_queue<int, 4, nothrow_less>>);
static_assert(std::is_nothrow_move_constructible_v<metl::flat_map<int, int, 4, move_only_less>>);
static_assert(std::is_nothrow_move_constructible_v<metl::flat_set<int, 4, move_only_less>>);
static_assert(std::is_nothrow_move_constructible_v<metl::fixed_priority_queue<int, 4, move_only_less>>);

// NothrowMove: the ordered containers' move never calls the comparator; the
// unordered ones re-insert, which calls a hasher that is not noexcept.
template <typename Container, bool Copyable, bool NothrowMove = true>
constexpr bool traits_are_values() {
  static_assert(std::is_copy_constructible_v<Container> == Copyable);
  static_assert(std::is_move_constructible_v<Container>);
  static_assert(std::is_nothrow_move_constructible_v<Container> == NothrowMove);
  // A container is not a callable, so the callable does not take one.
  static_assert(!std::is_constructible_v<copyable_less, Container&>);
  static_assert(!std::is_constructible_v<move_only_less, Container&&>);
  return true;
}

static_assert(traits_are_values<metl::flat_map<int, int, 4, copyable_less>, true>());
static_assert(traits_are_values<metl::flat_set<int, 4, copyable_less>, true>());
static_assert(traits_are_values<metl::fixed_priority_queue<int, 4, copyable_less>, true>());
static_assert(traits_are_values<metl::flat_map<int, int, 4, nothrow_less>, true>());
static_assert(traits_are_values<metl::flat_set<int, 4, nothrow_less>, true>());
static_assert(traits_are_values<metl::fixed_priority_queue<int, 4, nothrow_less>, true>());
static_assert(traits_are_values<metl::flat_map<int, int, 4, move_only_less>, false>());
static_assert(traits_are_values<metl::flat_set<int, 4, move_only_less>, false>());
static_assert(traits_are_values<metl::fixed_priority_queue<int, 4, move_only_less>, false>());
// The unordered containers have no constructor from a lone Hash, so they never
// hit this; they stay pinned so a new constructor does not bring it back.
static_assert(traits_are_values<metl::static_unordered_map<int, int, 4, copyable_hash>, true, false>());
static_assert(traits_are_values<metl::static_unordered_set<int, 4, copyable_hash>, true, false>());
static_assert(traits_are_values<metl::static_unordered_map<int, int, 4, move_only_hash>, false, false>());
static_assert(traits_are_values<metl::static_unordered_set<int, 4, move_only_hash>, false, false>());

bool less(int lhs, int rhs) noexcept {
  return lhs < rhs;
}

}  // namespace

int main() {
  // fixed_vector asks the element's traits; with a fixed_function comparator
  // this did not compile on clang.
  using map_type = metl::flat_map<int, int, 4, copyable_less>;
  metl::fixed_vector<map_type, 2> maps;
  CHECK(maps.try_emplace_back(copyable_less(&less)));
  CHECK(maps[0].try_emplace(2, 20));
  CHECK(maps[0].try_emplace(1, 10));
  metl::fixed_vector<map_type, 2> moved(std::move(maps));
  CHECK_EQ(moved.size(), std::size_t{1});
  CHECK_EQ(moved[0].begin()->key, 1);

  using queue_type = metl::fixed_priority_queue<int, 4, copyable_less>;
  metl::fixed_vector<queue_type, 2> queues;
  CHECK(queues.try_emplace_back(copyable_less(&less)));
  CHECK(queues[0].try_push(1));
  CHECK(queues[0].try_push(3));
  CHECK_EQ(queues[0].top(), 3);
  metl::fixed_vector<queue_type, 2> moved_queues(std::move(queues));
  CHECK_EQ(moved_queues[0].top(), 3);
  return metl_test::exit_code();
}
