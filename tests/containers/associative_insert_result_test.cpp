// try_emplace, emplace and try_insert_or_assign return std::pair<iterator, bool>
// on the four associative containers, as std's do (#162).
//
// Pinned here, on every container:
// - a new key: {iterator to it, true};
// - a present key: {iterator to the element already there, false}, the value
//   untouched by try_emplace and emplace, assigned by try_insert_or_assign;
// - a new key with no room: {end(), false}, nothing stored. A present key is
//   still found when the container is full;
// - emplace answers as try_emplace does for a new and a present key. On a
//   flat container a present key used to trip an assertion;
// - the iterator is right where the insert moves things: a flat insert in the
//   middle shifts the tail, and an unordered insert can rebuild the table
//   first, which moves every element;
// - a converted key (a long into an int map) lands where the iterator says;
// - code that used the old bool results does not compile.

// converted_key passes a long to an int map on purpose; a consumer's
// -Wconversion would report the very conversion it relies on.
#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wconversion"
#endif
#if defined(_MSC_VER)
#pragma warning(disable : 4244)  // conversion, possible loss of data
#endif

#include "metl_check.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>

#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

template <std::size_t N>
using fmap = metl::flat_map<int, int, N>;
template <std::size_t N>
using fset = metl::flat_set<int, N>;
template <std::size_t N>
using umap = metl::static_unordered_map<int, int, N>;
template <std::size_t N>
using uset = metl::static_unordered_set<int, N>;

// ---- what compiles ----------------------------------------------------------

template <typename C, typename = void>
struct try_emplace_is_bool : std::false_type {};
template <typename C>
struct try_emplace_is_bool<C, std::void_t<decltype(static_cast<bool>(std::declval<C&>().try_emplace(1, 1)))>>
    : std::true_type {};
template <typename C, typename = void>
struct set_try_emplace_is_bool : std::false_type {};
template <typename C>
struct set_try_emplace_is_bool<C, std::void_t<decltype(static_cast<bool>(std::declval<C&>().try_emplace(1)))>>
    : std::true_type {};
template <typename C, typename = void>
struct emplace_has_value_member : std::false_type {};
template <typename C>
struct emplace_has_value_member<C, std::void_t<decltype(std::declval<C&>().emplace(1, 1).value)>>
    : std::true_type {};

static_assert(!try_emplace_is_bool<fmap<4>>::value);
static_assert(!try_emplace_is_bool<umap<4>>::value);
static_assert(!set_try_emplace_is_bool<fset<4>>::value);
static_assert(!set_try_emplace_is_bool<uset<4>>::value);
static_assert(!emplace_has_value_member<fmap<4>>::value);
static_assert(!emplace_has_value_member<umap<4>>::value);

template <typename C>
using map_result = decltype(std::declval<C&>().try_emplace(1, 1));
template <typename C>
using set_result = decltype(std::declval<C&>().try_emplace(1));
static_assert(std::is_same_v<map_result<fmap<4>>, std::pair<fmap<4>::iterator, bool>>);
static_assert(std::is_same_v<map_result<umap<4>>, std::pair<umap<4>::iterator, bool>>);
static_assert(std::is_same_v<set_result<fset<4>>, std::pair<fset<4>::iterator, bool>>);
static_assert(std::is_same_v<set_result<uset<4>>, std::pair<uset<4>::iterator, bool>>);
static_assert(std::is_same_v<decltype(std::declval<fmap<4>&>().emplace(1, 1)), map_result<fmap<4>>>);
static_assert(std::is_same_v<decltype(std::declval<umap<4>&>().emplace(1, 1)), map_result<umap<4>>>);
static_assert(std::is_same_v<decltype(std::declval<fset<4>&>().emplace(1)), set_result<fset<4>>>);
static_assert(std::is_same_v<decltype(std::declval<uset<4>&>().emplace(1)), set_result<uset<4>>>);
static_assert(
    std::is_same_v<decltype(std::declval<fmap<4>&>().try_insert_or_assign(1, 1)), map_result<fmap<4>>>);
static_assert(
    std::is_same_v<decltype(std::declval<umap<4>&>().try_insert_or_assign(1, 1)), map_result<umap<4>>>);

// ---- what it answers --------------------------------------------------------

int key_of(const int& element) {
  return element;
}
template <typename Entry>
auto key_of(const Entry& entry) -> decltype(entry.key) {
  return entry.key;
}

// Inserts one key through `insert` and checks the pair it gives back.
template <typename C, typename Insert>
void expect_inserted(C& c, int key, Insert insert) {
  const std::size_t before = c.size();
  const auto result = insert(c, key);
  CHECK(result.second);
  if (CHECK(result.first != c.end())) {
    CHECK_EQ(key_of(*result.first), key);
  }
  CHECK_EQ(c.size(), before + 1);
}

template <typename C, typename Insert>
void expect_present(C& c, int key, Insert insert) {
  const std::size_t before = c.size();
  const auto result = insert(c, key);
  CHECK(!result.second);
  if (CHECK(result.first != c.end())) {
    CHECK_EQ(key_of(*result.first), key);
    CHECK(result.first == c.find(key));
  }
  CHECK_EQ(c.size(), before);
}

template <typename C, typename Insert>
void expect_full(C& c, int key, Insert insert) {
  const std::size_t before = c.size();
  const auto result = insert(c, key);
  CHECK(!result.second);
  CHECK(result.first == c.end());
  CHECK_EQ(c.size(), before);
  CHECK(!c.contains(key));
}

// Keys 2, 4 and 6 first, then 1, 5 and 3 between them: on a flat container
// every one of those lands in front of an element, so the tail shifts.
// `refuses` is false for emplace, which asserts on a full container instead.
template <typename C, typename Insert>
void walk_outcomes(Insert insert, bool refuses) {
  C c;
  for (int key : {4, 2, 6, 1, 5, 3}) {
    expect_inserted(c, key, insert);
  }
  expect_present(c, 4, insert);
  CHECK(c.full());
  expect_present(c, 1, insert);  // found although full
  if (refuses) {
    expect_full(c, 7, insert);
  }
  for (int key : {1, 2, 3, 4, 5, 6}) {
    CHECK(c.contains(key));
  }
}

const auto map_try_emplace = [](auto& c, int key) { return c.try_emplace(key, key * 10); };
const auto set_try_emplace = [](auto& c, int key) { return c.try_emplace(key); };
const auto map_emplace = [](auto& c, int key) { return c.emplace(key, key * 10); };
const auto set_emplace = [](auto& c, int key) { return c.emplace(key); };

// A present key keeps its value under try_emplace and emplace, and takes the
// new one under try_insert_or_assign, full or not.
template <typename C>
void map_values() {
  C m;
  CHECK(m.try_emplace(1, 10).second);
  CHECK_EQ(m.try_emplace(1, 11).first->value, 10);
  CHECK_EQ(m.emplace(1, 12).first->value, 10);

  const auto inserted = m.try_insert_or_assign(2, 20);
  CHECK(inserted.second);
  CHECK_EQ(inserted.first->value, 20);
  const auto assigned = m.try_insert_or_assign(1, 13);
  CHECK(!assigned.second);
  if (CHECK(assigned.first != m.end())) {
    CHECK_EQ(assigned.first->key, 1);
    CHECK_EQ(assigned.first->value, 13);
  }
  CHECK_EQ(m.size(), std::size_t{2});

  CHECK(m.try_emplace(3, 30).second);
  CHECK(m.try_emplace(4, 40).second);
  CHECK(m.full());
  const auto assigned_full = m.try_insert_or_assign(4, 41);
  CHECK(!assigned_full.second);
  CHECK(assigned_full.first != m.end() && assigned_full.first->value == 41);
  const auto refused = m.try_insert_or_assign(5, 50);
  CHECK(!refused.second);
  CHECK(refused.first == m.end());
  CHECK(!m.contains(5));
}

// The key converts before the search, so the iterator points at the element
// stored under the converted key.
template <typename C>
void converted_key() {
  C m;
  const long key = 7;
  const auto result = m.try_emplace(key, 1);
  CHECK(result.second);
  if (CHECK(result.first != m.end())) {
    CHECK_EQ(result.first->key, 7);
  }
  CHECK(m.emplace(key, 2).first == result.first);
  CHECK(m.try_insert_or_assign(key, 3).first == result.first);
  CHECK_EQ(result.first->value, 3);
}

// Every key hashes alike, so all of them share one probe chain, and an erased
// key in front of a live one leaves a tombstone.
struct one_bucket {
  std::size_t operator()(int) const noexcept { return 0; }
};

// Past an eighth of the table in tombstones, the next new key rebuilds the
// table before it is placed, and the live elements move up the chain into the
// tombstones' slots. The iterator the insert hands back must point at the new
// key's slot after the rebuild, not the slot it was headed for before.
template <typename C, typename Insert>
void after_rebuild(Insert insert) {
  C c;
  for (int key = 1; key <= 8; ++key) {
    CHECK(insert(c, key).second);
  }
  for (int key = 1; key <= 3; ++key) {  // 3 tombstones, over 16 / 8
    CHECK(c.erase(key));
  }
  const auto result = insert(c, 9);
  CHECK(result.second);
  if (CHECK(result.first != c.end())) {
    CHECK_EQ(key_of(*result.first), 9);
  }
  for (int key = 4; key <= 9; ++key) {
    CHECK(c.contains(key));
  }
}

template <typename C, typename Insert>
void capacity_zero(Insert insert) {
  C c;
  const auto result = insert(c, 1);
  CHECK(!result.second);
  CHECK(result.first == c.end());
}

}  // namespace

int main() {
  walk_outcomes<fmap<6>>(map_try_emplace, true);
  walk_outcomes<fmap<6>>(map_emplace, false);
  walk_outcomes<fset<6>>(set_try_emplace, true);
  walk_outcomes<fset<6>>(set_emplace, false);
  walk_outcomes<umap<6>>(map_try_emplace, true);
  walk_outcomes<umap<6>>(map_emplace, false);
  walk_outcomes<uset<6>>(set_try_emplace, true);
  walk_outcomes<uset<6>>(set_emplace, false);

  map_values<fmap<4>>();
  map_values<umap<4>>();

  converted_key<fmap<4>>();
  converted_key<umap<4>>();

  using chained_map = metl::static_unordered_map<int, int, 8, one_bucket>;
  using chained_set = metl::static_unordered_set<int, 8, one_bucket>;
  static_assert(chained_map::bucket_count == 16 && chained_set::bucket_count == 16);
  after_rebuild<chained_map>(map_try_emplace);
  after_rebuild<chained_map>(map_emplace);
  after_rebuild<chained_set>(set_try_emplace);
  after_rebuild<chained_set>(set_emplace);

  capacity_zero<fmap<0>>(map_try_emplace);
  capacity_zero<fset<0>>(set_try_emplace);
  capacity_zero<umap<0>>(map_try_emplace);
  capacity_zero<uset<0>>(set_try_emplace);
  const auto map_try_insert_or_assign = [](auto& c, int key) { return c.try_insert_or_assign(key, key); };
  capacity_zero<fmap<0>>(map_try_insert_or_assign);
  capacity_zero<umap<0>>(map_try_insert_or_assign);

  return metl_test::exit_code();
}
