// Every name deprecated for removal in 2.0 still works in 1.x, and does what
// its replacement does (#645). The rest of the suite uses the replacements;
// this file keeps the old names covered until 2.0 removes them, and their
// assertions tripped (tools/check_assert_trips.py) wherever METL_ASSERT is
// compiled in.
#include "assert_trip.hpp"
#include "metl_check.hpp"
#include "metl_deprecated.hpp"

#include <cstddef>

#include <metl/coro/deadline_scheduler.hpp>
#include <metl/coro/scheduler.hpp>
#include <metl/fixed_string.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/lookup_table.hpp>
#include <metl/optional.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

using metl_test::trips;

struct transparent_hash {
  using is_transparent = void;
  std::size_t operator()(int key) const noexcept { return static_cast<std::size_t>(key); }
  std::size_t operator()(long key) const noexcept { return static_cast<std::size_t>(key); }
};
struct transparent_equal {
  using is_transparent = void;
  template <typename A, typename B>
  bool operator()(const A& a, const B& b) const noexcept {
    return static_cast<long>(a) == static_cast<long>(b);
  }
};

void fixed_string_try_pop_back() {
  metl::fixed_string<4> text("ab");
  CHECK(text.try_pop_back());
  CHECK_EQ(text.size(), std::size_t{1});
  CHECK(text.try_pop_back());
  CHECK(!text.try_pop_back());  // empty: false, no assert
  CHECK(text.empty());
}

void unordered_find_iter() {
  metl::static_unordered_map<int, int, 4> map;
  CHECK(map.try_emplace(1, 10));
  CHECK(map.find_iter(1) == map.find_iterator(1));
  CHECK(map.find_iter(2) == map.end());
  const auto& cmap = map;
  CHECK(cmap.find_iter(1) == cmap.find_iterator(1));

  metl::static_unordered_set<int, 4> set;
  CHECK(set.try_emplace(1));
  CHECK(set.find_iter(1) == set.find_iterator(1));
  CHECK(set.find_iter(2) == set.end());
  const auto& cset = set;
  CHECK(cset.find_iter(1) == cset.find_iterator(1));

  // The heterogeneous overloads.
  metl::static_unordered_map<int, int, 4, transparent_hash, transparent_equal> tmap;
  CHECK(tmap.try_emplace(3, 30));
  CHECK(tmap.find_iter(3L) == tmap.find_iterator(3L));
  CHECK(static_cast<const decltype(tmap)&>(tmap).find_iter(3L) == tmap.find_iterator(3L));
  metl::static_unordered_set<int, 4, transparent_hash, transparent_equal> tset;
  CHECK(tset.try_emplace(3));
  CHECK(tset.find_iter(3L) == tset.find_iterator(3L));
  CHECK(static_cast<const decltype(tset)&>(tset).find_iter(3L) == tset.find_iterator(3L));
}

void positional_access() {
  using map_type = metl::flat_map<int, int, 2>;
  static map_type map;
  CHECK(map.try_emplace(2, 20));
  CHECK(map.try_emplace(1, 10));
  CHECK_EQ(&map[0], &map.nth(0));
  CHECK_EQ(&map.at(1), &map.nth(1));
  const map_type& cmap = map;
  CHECK_EQ(&cmap[1], &cmap.nth(1));
  CHECK_EQ(&cmap.at(0), &cmap.nth(0));
  CHECK(!metl_test::asserts_active || trips([] { (void)map[2]; }));
  CHECK(!metl_test::asserts_active || trips([] { (void)map.at(2); }));
  CHECK(!metl_test::asserts_active || trips([] { (void)static_cast<const map_type&>(map)[2]; }));
  CHECK(!metl_test::asserts_active || trips([] { (void)static_cast<const map_type&>(map).at(2); }));

  using set_type = metl::flat_set<int, 2>;
  static set_type set;
  CHECK(set.try_emplace(2));
  CHECK(set.try_emplace(1));
  CHECK_EQ(&set[0], &set.nth(0));
  CHECK_EQ(&set.at(1), &set.nth(1));
  const set_type& cset = set;
  CHECK_EQ(&cset[1], &cset.nth(1));
  CHECK_EQ(&cset.at(0), &cset.nth(0));
  CHECK(!metl_test::asserts_active || trips([] { (void)set[2]; }));
  CHECK(!metl_test::asserts_active || trips([] { (void)set.at(2); }));
  CHECK(!metl_test::asserts_active || trips([] { (void)static_cast<const set_type&>(set)[2]; }));
  CHECK(!metl_test::asserts_active || trips([] { (void)static_cast<const set_type&>(set).at(2); }));

  static constexpr auto table = metl::make_lookup_table<int, int, 2>({{{1, 10}, {2, 20}}});
  static_assert(table[1].value == 20, "positional operator[] is still constexpr");
  static_assert(table.at(0).value == 10, "positional at is still constexpr");
  CHECK_EQ(&table[1], &table.nth(1));
  CHECK_EQ(&table.at(0), &table.nth(0));
  CHECK(!metl_test::asserts_active || trips([] { (void)table[2]; }));
  CHECK(!metl_test::asserts_active || trips([] { (void)table.at(2); }));
}

bool never_done(void*) noexcept {
  return false;
}
metl::optional<unsigned> no_rearm(void*, unsigned) noexcept {
  return metl::nullopt;
}

void scheduler_task_count() {
  int a = 0;
  int b = 0;
  metl::coro::scheduler<2> round_robin;
  CHECK(round_robin.try_attach(&a, &never_done));
  CHECK(round_robin.try_attach(&b, &never_done));
  CHECK_EQ(round_robin.task_count(), round_robin.size());
  CHECK_EQ(round_robin.task_count(), std::size_t{2});

  metl::coro::deadline_scheduler<4, unsigned> deadlines;
  CHECK(deadlines.try_schedule(&a, &no_rearm, 5));
  CHECK(deadlines.try_schedule(&a, &no_rearm, 9));  // the same task, twice
  CHECK_EQ(deadlines.task_count(), deadlines.size());
  CHECK_EQ(deadlines.task_count(), std::size_t{2});  // entries, not tasks
}

}  // namespace

int main() {
  fixed_string_try_pop_back();
  unordered_find_iter();
  positional_access();
  scheduler_task_count();
  return metl_test::exit_code();
}
