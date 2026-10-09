// Associative containers given a key of another type, and their moves.
//
//   * The insert paths took a template K, found the position (or bucket) with
//     K itself, then stored Key(K). With a narrowing or converting K the stored
//     key was not the one that chose the slot: static_unordered_set held the
//     same uint32 twice, and flat_set<uint8_t, ..., std::less<>> stored 300 as
//     44 after 200, out of order. The key is now converted first.
//   * Moving re-inserted every element through emplace, which runs the hasher
//     or comparator, while the move was noexcept whenever the elements' moves
//     were: a throwing hasher ended in std::terminate. flat_map / flat_set now
//     append the already-sorted source without comparing, and the unordered
//     containers' noexcept includes the hasher and key comparison.

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

#include <metl/config.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/hash.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

struct wrapped {
  int value;
  wrapped(int v) : value(v) {}  // NOLINT(google-explicit-constructor): converting on purpose
  friend bool operator==(const wrapped& a, const wrapped& b) noexcept { return a.value == b.value; }
};

// Hashes int and wrapped differently, the way a hasher with a template or
// overloaded call operator may: the slot chosen for an int then says nothing
// about where the stored wrapped belongs.
struct overloaded_hash {
  std::size_t operator()(const wrapped& w) const noexcept { return static_cast<std::size_t>(w.value) * 7u; }
  std::size_t operator()(int v) const noexcept { return static_cast<std::size_t>(v) + 1u; }
};

void converting_keys() {
  {
    metl::static_unordered_set<std::uint32_t, 8, metl::fnv1a_hash> set;
    const int minus_one = -1;
    set.emplace(minus_one);
    CHECK(!set.try_emplace(0xFFFFFFFFu));  // already present, as the uint32 it became
    CHECK_EQ(set.size(), std::size_t{1});
    CHECK(set.contains(0xFFFFFFFFu));
  }
  {
    metl::static_unordered_set<std::uint8_t, 8, metl::identity_hash> set;
    set.emplace(300);
    CHECK(set.contains(std::uint8_t{44}));
  }
  {
    metl::flat_set<std::uint8_t, 8, std::less<>> set;
    set.emplace(100);
    set.emplace(200);
    set.emplace(300);  // stored as 44, which sorts first
    CHECK_EQ(set.nth(0), std::uint8_t{44});
    CHECK_EQ(set.nth(1), std::uint8_t{100});
    CHECK_EQ(set.nth(2), std::uint8_t{200});
    CHECK(set.contains(std::uint8_t{44}));
  }
  {
    metl::flat_set<int, 8, std::less<>> set;
    set.emplace(2);
    set.emplace(3);
    CHECK(!set.try_emplace(2.5));  // 2.5 converts to 2, which is already there
    CHECK_EQ(set.size(), std::size_t{2});
  }
  {
    metl::static_unordered_map<wrapped, int, 8, overloaded_hash> map;
    map.emplace(5, 1);
    CHECK(map.find(wrapped{5}) != nullptr);
    CHECK(!map.try_emplace(wrapped{5}, 2));
    CHECK_EQ(map.size(), std::size_t{1});
  }
}

#if !METL_NO_EXCEPTIONS
int g_compare_calls = 0;
struct counting_less {
  bool operator()(int a, int b) const {  // may throw: not noexcept
    ++g_compare_calls;
    return a < b;
  }
};

struct throwing_hash {
  std::size_t operator()(int v) const { return static_cast<std::size_t>(v); }  // not noexcept
};

void moves_do_not_run_user_code() {
  metl::flat_set<int, 8, counting_less> set;
  set.emplace(1);
  set.emplace(2);
  set.emplace(3);
  g_compare_calls = 0;
  metl::flat_set<int, 8, counting_less> moved(static_cast<decltype(set)&&>(set));
  CHECK_EQ(g_compare_calls, 0);
  CHECK_EQ(moved.size(), std::size_t{3});
  metl::flat_set<int, 8, counting_less> assigned;
  assigned = static_cast<decltype(moved)&&>(moved);
  CHECK_EQ(g_compare_calls, 0);
  CHECK_EQ(assigned.nth(2), 3);

  metl::flat_map<int, int, 8, counting_less> map;
  map.emplace(1, 10);
  map.emplace(2, 20);
  g_compare_calls = 0;
  metl::flat_map<int, int, 8, counting_less> moved_map(static_cast<decltype(map)&&>(map));
  CHECK_EQ(g_compare_calls, 0);
  CHECK_EQ(moved_map.size(), std::size_t{2});
}

// A hasher that may throw makes the unordered moves potentially throwing, so a
// throw propagates instead of terminating; a nothrow hasher keeps them noexcept.
static_assert(!std::is_nothrow_move_constructible_v<metl::static_unordered_map<int, int, 4, throwing_hash>>,
              "");
static_assert(!std::is_nothrow_move_constructible_v<metl::static_unordered_set<int, 4, throwing_hash>>, "");
static_assert(std::is_nothrow_move_constructible_v<metl::static_unordered_map<int, int, 4>>, "");
static_assert(std::is_nothrow_move_constructible_v<metl::static_unordered_set<int, 4>>, "");
#endif

}  // namespace

int main() {
  converting_keys();
#if !METL_NO_EXCEPTIONS
  moves_do_not_run_user_code();
#endif
  return metl_test::exit_code();
}
