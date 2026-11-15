// What a moved-from container holds (#559): the contract its header states.
// fixed_string keeps its contents; every other fixed-capacity container is left
// empty and usable. Usable includes its comparator, hasher and key equality,
// which a move copies rather than moves (#691).

#include "metl_check.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

#include <metl/fixed_deque.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/fixed_string.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

template <typename Container, typename Fill>
void check(Fill fill, std::size_t left_behind) {
  Container constructed_from;
  CHECK(fill(constructed_from));
  const Container constructed(std::move(constructed_from));
  CHECK_EQ(constructed.size(), std::size_t{1});
  CHECK_EQ(constructed_from.size(), left_behind);

  Container assigned_from;
  CHECK(fill(assigned_from));
  Container assigned;
  assigned = std::move(assigned_from);
  CHECK_EQ(assigned.size(), std::size_t{1});
  CHECK_EQ(assigned_from.size(), left_behind);

  // Usable: the source takes an element again.
  if (left_behind == 0) {
    CHECK(fill(assigned_from));
    CHECK_EQ(assigned_from.size(), std::size_t{1});
  }
}

// A functor that a move leaves unusable, as a moved-from fixed_function is.
struct dies_on_move {
  bool live = true;
  dies_on_move() = default;
  dies_on_move(const dies_on_move&) = default;
  dies_on_move(dies_on_move&& other) noexcept : live(other.live) { other.live = false; }
  dies_on_move& operator=(const dies_on_move&) = default;
  dies_on_move& operator=(dies_on_move&& other) noexcept {
    live = other.live;
    other.live = false;
    return *this;
  }
  ~dies_on_move() = default;
};
struct dying_less : dies_on_move {
  bool operator()(int lhs, int rhs) const {
    CHECK(live);
    return lhs < rhs;
  }
};
struct dying_hash : dies_on_move {
  std::size_t operator()(int key) const {
    CHECK(live);
    return static_cast<std::size_t>(key);
  }
};
struct dying_equal : dies_on_move {
  bool operator()(int lhs, int rhs) const {
    CHECK(live);
    return lhs == rhs;
  }
};

// Two elements, so the second insertion compares (or hashes and compares) and
// a dead functor shows. Both containers are used after the move.
template <typename Container, typename Fill>
void check_functors_survive(Fill fill) {
  Container constructed_from;
  CHECK(fill(constructed_from, 1));
  Container constructed(std::move(constructed_from));
  CHECK(fill(constructed_from, 1));
  CHECK(fill(constructed_from, 2));
  CHECK_EQ(constructed_from.size(), std::size_t{2});
  CHECK(fill(constructed, 2));
  CHECK_EQ(constructed.size(), std::size_t{2});

  Container assigned_from;
  CHECK(fill(assigned_from, 1));
  Container assigned;
  assigned = std::move(assigned_from);
  CHECK(fill(assigned_from, 1));
  CHECK(fill(assigned_from, 2));
  CHECK_EQ(assigned_from.size(), std::size_t{2});
  CHECK(fill(assigned, 2));
  CHECK_EQ(assigned.size(), std::size_t{2});
}

// A move-only functor cannot be copied, so it is moved, and the container
// stays movable.
struct move_only_less {
  move_only_less() = default;
  move_only_less(const move_only_less&) = delete;
  move_only_less(move_only_less&&) = default;
  move_only_less& operator=(const move_only_less&) = delete;
  move_only_less& operator=(move_only_less&&) = default;
  ~move_only_less() = default;
  bool operator()(int lhs, int rhs) const { return lhs < rhs; }
};
struct move_only_hash {
  move_only_hash() = default;
  move_only_hash(const move_only_hash&) = delete;
  move_only_hash(move_only_hash&&) = default;
  move_only_hash& operator=(const move_only_hash&) = delete;
  move_only_hash& operator=(move_only_hash&&) = default;
  ~move_only_hash() = default;
  std::size_t operator()(int key) const noexcept { return static_cast<std::size_t>(key); }
};

template <typename Container, typename Fill>
void check_move_only_functor(Fill fill) {
  static_assert(std::is_move_constructible_v<Container>);
  static_assert(std::is_move_assignable_v<Container>);
  static_assert(!std::is_copy_constructible_v<Container>);
  Container from;
  CHECK(fill(from, 1));
  Container to(std::move(from));
  CHECK_EQ(to.size(), std::size_t{1});
  Container again;
  again = std::move(to);
  CHECK_EQ(again.size(), std::size_t{1});
}

// The move copies the functor, so its noexcept follows the functor's copy.
struct throwing_copy_less {
  throwing_copy_less() = default;
  throwing_copy_less(const throwing_copy_less& /*unused*/) noexcept(false) {}
  throwing_copy_less(throwing_copy_less&&) noexcept = default;
  throwing_copy_less& operator=(const throwing_copy_less& /*unused*/) noexcept(false) { return *this; }
  throwing_copy_less& operator=(throwing_copy_less&&) noexcept = default;
  ~throwing_copy_less() = default;
  bool operator()(int lhs, int rhs) const noexcept { return lhs < rhs; }
};
struct throwing_copy_hash {
  throwing_copy_hash() = default;
  throwing_copy_hash(const throwing_copy_hash& /*unused*/) noexcept(false) {}
  throwing_copy_hash(throwing_copy_hash&&) noexcept = default;
  throwing_copy_hash& operator=(const throwing_copy_hash& /*unused*/) noexcept(false) { return *this; }
  throwing_copy_hash& operator=(throwing_copy_hash&&) noexcept = default;
  ~throwing_copy_hash() = default;
  std::size_t operator()(int key) const noexcept { return static_cast<std::size_t>(key); }
};

template <typename Container>
constexpr bool move_follows_copy =
    !std::is_nothrow_move_constructible_v<Container> && !std::is_nothrow_move_assignable_v<Container>;
static_assert(move_follows_copy<metl::fixed_priority_queue<int, 4, throwing_copy_less>>);
static_assert(move_follows_copy<metl::flat_map<int, int, 4, throwing_copy_less>>);
static_assert(move_follows_copy<metl::flat_set<int, 4, throwing_copy_less>>);
static_assert(move_follows_copy<metl::static_unordered_map<int, int, 4, throwing_copy_hash>>);
static_assert(move_follows_copy<metl::static_unordered_set<int, 4, throwing_copy_hash>>);
// A functor whose move can throw but whose copy cannot: the move copies it, so
// the container's move is noexcept.
struct throwing_move_less {
  throwing_move_less() = default;
  throwing_move_less(const throwing_move_less&) noexcept = default;
  throwing_move_less(throwing_move_less&& /*unused*/) noexcept(false) {}
  throwing_move_less& operator=(const throwing_move_less&) noexcept = default;
  throwing_move_less& operator=(throwing_move_less&& /*unused*/) noexcept(false) { return *this; }
  ~throwing_move_less() = default;
  bool operator()(int lhs, int rhs) const noexcept { return lhs < rhs; }
};
template <typename Container>
constexpr bool move_is_noexcept =
    std::is_nothrow_move_constructible_v<Container> && std::is_nothrow_move_assignable_v<Container>;
static_assert(move_is_noexcept<metl::fixed_priority_queue<int, 4, throwing_move_less>>);
static_assert(move_is_noexcept<metl::flat_map<int, int, 4, throwing_move_less>>);
static_assert(move_is_noexcept<metl::flat_set<int, 4, throwing_move_less>>);
// The default functors copy without throwing, so those moves stay noexcept.
static_assert(std::is_nothrow_move_constructible_v<metl::fixed_priority_queue<int, 4>>);
static_assert(std::is_nothrow_move_constructible_v<metl::flat_map<int, int, 4>>);
static_assert(std::is_nothrow_move_constructible_v<metl::flat_set<int, 4>>);
static_assert(std::is_nothrow_move_assignable_v<metl::flat_map<int, int, 4>>);
static_assert(std::is_nothrow_move_assignable_v<metl::flat_set<int, 4>>);

}  // namespace

int main() {
  const auto push_back = [](auto& c) { return c.try_push_back(1); };
  const auto push = [](auto& c) { return c.try_push(1); };
  const auto emplace_key = [](auto& c) { return c.try_emplace(1); };
  const auto emplace_pair = [](auto& c) { return c.try_emplace(1, 1); };

  check<metl::fixed_vector<int, 4>>(push_back, 0);
  check<metl::ring_buffer<int, 4>>(push_back, 0);
  check<metl::fixed_deque<int, 4>>(push_back, 0);
  check<metl::fixed_queue<int, 4>>(push, 0);
  check<metl::fixed_stack<int, 4>>(push, 0);
  check<metl::fixed_priority_queue<int, 4>>(push, 0);
  check<metl::flat_map<int, int, 4>>(emplace_pair, 0);
  check<metl::flat_set<int, 4>>(emplace_key, 0);
  check<metl::static_unordered_map<int, int, 4>>(emplace_pair, 0);
  check<metl::static_unordered_set<int, 4>>(emplace_key, 0);
  check<metl::fixed_string<4>>([](auto& c) { return c.try_push_back('a'); }, 1);

  const auto push_value = [](auto& c, int v) { return c.try_push(v); };
  const auto emplace_value = [](auto& c, int v) { return c.try_emplace(v); };
  const auto emplace_entry = [](auto& c, int v) { return c.try_emplace(v, v); };
  check_functors_survive<metl::fixed_priority_queue<int, 4, dying_less>>(push_value);
  check_functors_survive<metl::flat_map<int, int, 4, dying_less>>(emplace_entry);
  check_functors_survive<metl::flat_set<int, 4, dying_less>>(emplace_value);
  check_functors_survive<metl::static_unordered_map<int, int, 4, dying_hash, dying_equal>>(emplace_entry);
  check_functors_survive<metl::static_unordered_set<int, 4, dying_hash, dying_equal>>(emplace_value);

  check_move_only_functor<metl::fixed_priority_queue<int, 4, move_only_less>>(push_value);
  check_move_only_functor<metl::flat_map<int, int, 4, move_only_less>>(emplace_entry);
  check_move_only_functor<metl::flat_set<int, 4, move_only_less>>(emplace_value);
  check_move_only_functor<metl::static_unordered_map<int, int, 4, move_only_hash>>(emplace_entry);
  check_move_only_functor<metl::static_unordered_set<int, 4, move_only_hash>>(emplace_value);
  return metl_test::exit_code();
}
