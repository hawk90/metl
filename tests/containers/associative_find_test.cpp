// find returns an iterator on the four associative containers, as std's do,
// and the pointer form is find_value (#162, #641).
//
// Pinned here:
// - the std idiom `c.find(k) != c.end()` answers correctly on all four. On
//   flat_set it used to be true for every key, because find returned nullptr
//   and the iterator was a pointer (#641);
// - code written for a pointer-returning find does not compile: find's result
//   neither compares with nullptr nor converts to bool. A silent change of
//   meaning would be worse than the break;
// - find_value returns a pointer to the mapped value, or nullptr, on the two
//   maps and on lookup_table; the sets and lookup_table have nothing else.

#include "metl_check.hpp"

#include <cstddef>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>

#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/lookup_table.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

using fmap = metl::flat_map<int, int, 8>;
using fset = metl::flat_set<int, 8>;
using umap = metl::static_unordered_map<int, int, 8>;
using uset = metl::static_unordered_set<int, 8>;
using table = metl::lookup_table<int, int, 2>;

// ---- what compiles ----------------------------------------------------------

template <typename C, typename = void>
struct find_compares_with_nullptr : std::false_type {};
template <typename C>
struct find_compares_with_nullptr<C, std::void_t<decltype(std::declval<C&>().find(0) != nullptr)>>
    : std::true_type {};

template <typename C, typename = void>
struct has_find : std::false_type {};
template <typename C>
struct has_find<C, std::void_t<decltype(std::declval<C&>().find(0))>> : std::true_type {};

template <typename C, typename = void>
struct has_find_value : std::false_type {};
template <typename C>
struct has_find_value<C, std::void_t<decltype(std::declval<C&>().find_value(0))>> : std::true_type {};

template <typename C, typename = void>
struct has_find_iterator : std::false_type {};
template <typename C>
struct has_find_iterator<C, std::void_t<decltype(std::declval<C&>().find_iterator(0))>> : std::true_type {};

template <typename C>
constexpr bool find_is_std_shaped =
    std::is_same_v<decltype(std::declval<C&>().find(0)), typename C::iterator> &&
    std::is_same_v<decltype(std::declval<const C&>().find(0)), typename C::const_iterator> &&
    !find_compares_with_nullptr<C>::value && !find_compares_with_nullptr<const C>::value &&
    !std::is_constructible_v<bool, decltype(std::declval<C&>().find(0))> &&
    !std::is_constructible_v<bool, decltype(std::declval<const C&>().find(0))> &&
    !has_find_iterator<C>::value;

static_assert(find_is_std_shaped<fmap>, "flat_map::find returns an iterator");
static_assert(find_is_std_shaped<fset>, "flat_set::find returns an iterator");
static_assert(find_is_std_shaped<umap>, "static_unordered_map::find returns an iterator");
static_assert(find_is_std_shaped<uset>, "static_unordered_set::find returns an iterator");

// A flat iterator is no longer a pointer, in either direction.
static_assert(!std::is_convertible_v<fset::iterator, const int*>, "no flat_set iterator -> pointer");
static_assert(!std::is_convertible_v<int*, fset::iterator>, "no pointer -> flat_set iterator");
static_assert(!std::is_convertible_v<fmap::const_iterator, const fmap::value_type*>,
              "no flat_map iterator -> pointer");
static_assert(std::is_convertible_v<fset::iterator, fset::const_iterator>,
              "iterator -> const_iterator stays");
static_assert(sizeof(fset::iterator) == sizeof(int*), "the class is a pointer in size");

static_assert(std::is_same_v<decltype(std::declval<fmap&>().find_value(0)), int*>, "flat_map::find_value");
static_assert(std::is_same_v<decltype(std::declval<const fmap&>().find_value(0)), const int*>, "const");
static_assert(std::is_same_v<decltype(std::declval<umap&>().find_value(0)), int*>,
              "unordered_map::find_value");
static_assert(std::is_same_v<decltype(std::declval<const umap&>().find_value(0)), const int*>, "const");
static_assert(std::is_same_v<decltype(std::declval<const table&>().find_value(0)), const int*>,
              "lookup_table");
static_assert(!has_find_value<fset>::value && !has_find_value<uset>::value,
              "sets have contains, not find_value");
static_assert(!has_find<const table>::value, "lookup_table has no iterators, so no find");

// ---- what it answers --------------------------------------------------------

template <typename Map>
void map_answers(Map& m) {
  CHECK(m.try_emplace(1, 10));
  CHECK(m.try_emplace(3, 30));
  const Map& cm = m;

  const auto hit = m.find(3);
  CHECK(hit != m.end());
  if (hit != m.end()) {
    CHECK(hit->key == 3 && hit->value == 30);
  }
  CHECK(m.find(2) == m.end());
  CHECK(m.find(9) == m.end());
  const auto const_hit = cm.find(1);
  CHECK(const_hit != cm.end());
  if (const_hit != cm.end()) {
    CHECK(const_hit->value == 10);
  }
  CHECK(cm.find(0) == cm.end());

  int* const value = m.find_value(3);
  CHECK_DEREF_EQ(value, 30);
  CHECK_DEREF_EQ(m.find_value(1), 10);  // not the last element: a neighbour would show
  CHECK(m.find_value(2) == nullptr);
  CHECK_DEREF_EQ(cm.find_value(1), 10);
  CHECK(cm.find_value(9) == nullptr);

  // find_value is the value of find's element, not a copy.
  if (value != nullptr && hit != m.end()) {
    CHECK(value == &hit->value);
    *value = 31;
    CHECK(hit->value == 31);
  }
}

template <typename Set>
void set_answers(Set& s) {
  CHECK(s.try_emplace(1));
  CHECK(s.try_emplace(3));
  const Set& cs = s;
  const auto hit = s.find(3);
  CHECK(hit != s.end());
  if (hit != s.end()) {
    CHECK(*hit == 3);
  }
  CHECK(s.find(2) == s.end());  // #641: was true on flat_set
  CHECK(s.find(9) == s.end());
  const auto const_hit = cs.find(1);
  CHECK(const_hit != cs.end());
  if (const_hit != cs.end()) {
    CHECK(*const_hit == 1);
  }
  CHECK(cs.find(0) == cs.end());
  CHECK(s.contains(3) && !s.contains(2));
}

void std_idiom_on_all_four() {
  fmap fm;
  umap um;
  fset fs;
  uset us;
  map_answers(fm);
  map_answers(um);
  set_answers(fs);
  set_answers(us);
}

void erase_by_find() {
  fset s;
  CHECK(s.try_emplace(1));
  CHECK(s.try_emplace(2));
  const auto next = s.erase(s.find(1));
  CHECK(next != s.end());
  if (next != s.end()) {
    CHECK(*next == 2);
  }
  CHECK(s.size() == 1);

  umap m;
  CHECK(m.try_emplace(5, 50));
  (void)m.erase(m.find(5));
  CHECK(m.find(5) == m.end() && m.empty());
}

// Heterogeneous keys take the same path: std::less<> on the flat map, and a
// transparent hash and equality on the unordered one.
struct long_hash {
  using is_transparent = void;
  std::size_t operator()(long v) const noexcept { return static_cast<std::size_t>(v); }
};
struct long_equal {
  using is_transparent = void;
  bool operator()(long a, long b) const noexcept { return a == b; }
};

void heterogeneous() {
  metl::flat_map<int, int, 4, std::less<>> fm;
  CHECK(fm.try_emplace(2, 20));
  const auto& cfm = fm;
  const long present = 2;
  const long absent = 7;
  CHECK(fm.find(present) != fm.end());
  const auto fm_hit = cfm.find(present);
  if (fm_hit != cfm.end()) {
    CHECK(fm_hit->value == 20);
  }
  CHECK(fm.find(absent) == fm.end() && cfm.find(absent) == cfm.end());
  CHECK(fm.find_value(present) != nullptr);
  CHECK_DEREF_EQ(cfm.find_value(present), 20);
  CHECK(fm.find_value(absent) == nullptr && cfm.find_value(absent) == nullptr);

  metl::static_unordered_map<int, int, 4, long_hash, long_equal> um;
  CHECK(um.try_emplace(2, 20));
  const auto& cum = um;
  CHECK(um.find(present) != um.end());
  const auto um_hit = cum.find(present);
  if (um_hit != cum.end()) {
    CHECK(um_hit->value == 20);
  }
  CHECK(um.find(absent) == um.end() && cum.find(absent) == cum.end());
  CHECK(um.find_value(present) != nullptr);
  CHECK_DEREF_EQ(cum.find_value(present), 20);
  CHECK(um.find_value(absent) == nullptr && cum.find_value(absent) == nullptr);
}

// Every lookup overload against every position a key can take: before the
// first element, between two, past the last (index == size on the flat ones),
// and on each element. Key is the lookup type: the key type itself, or a
// heterogeneous one. Elements are 2 and 4, and a map holds key * 10.
template <typename Key, typename C>
void every_position(C& c) {
  const C& cc = c;
  for (const int k : {1, 3, 5}) {
    const Key key = static_cast<Key>(k);
    CHECK(c.find(key) == c.end());
    CHECK(cc.find(key) == cc.end());
    if constexpr (has_find_value<C>::value) {
      CHECK(c.find_value(key) == nullptr);
      CHECK(cc.find_value(key) == nullptr);
    }
  }
  for (const int k : {2, 4}) {
    const Key key = static_cast<Key>(k);
    const auto it = c.find(key);
    const auto cit = cc.find(key);
    CHECK(it != c.end() && cit != cc.end());
    if constexpr (has_find_value<C>::value) {
      if (it != c.end() && cit != cc.end()) {
        CHECK(it->key == k && cit->key == k);
      }
      CHECK_DEREF_EQ(c.find_value(key), k * 10);
      CHECK_DEREF_EQ(cc.find_value(key), k * 10);
    } else if (it != c.end() && cit != cc.end()) {
      CHECK(*it == k && *cit == k);
    }
  }
}

template <typename Key, typename Map>
void every_position_map() {
  Map m;
  CHECK(m.try_emplace(4, 40));
  CHECK(m.try_emplace(2, 20));
  every_position<Key>(m);
}

template <typename Key, typename Set>
void every_position_set() {
  Set s;
  CHECK(s.try_emplace(4));
  CHECK(s.try_emplace(2));
  every_position<Key>(s);
}

void lookup_table_find_value() {
  static constexpr table t{{{{1, 10}, {2, 20}}}};
  static_assert(t.find_value(2) != nullptr && *t.find_value(2) == 20, "constant-evaluated hit");
  static_assert(t.find_value(3) == nullptr, "constant-evaluated miss");
  CHECK_DEREF_EQ(t.find_value(1), 10);
  CHECK(t.find_value(0) == nullptr);
}

// Every operation of the flat iterator, on its own. It is a class now, so no
// operation may lean on a pointer's, and coverage of each must not depend on
// which algorithm some other test happens to call.
template <typename It>
void walk_one_iterator_type(It first, It last) {
  // first..last hold 10, 20, 30, 40 (a set) or keys 1..4 (a map, through key_of).
  It it = first;
  CHECK(it == first && !(it != first));
  CHECK(&*it == it.operator->());

  CHECK(&*++it == &first[1]);  // pre-increment returns the moved iterator
  CHECK(&*it++ == &first[1]);  // post-increment returns the old one
  CHECK(&*it == &first[2]);
  CHECK(&*--it == &first[1]);  // pre-decrement
  CHECK(&*it-- == &first[1]);  // post-decrement
  CHECK(it == first);

  it += 3;
  CHECK(&*it == &first[3]);
  it -= 2;
  CHECK(&*it == &first[1]);
  CHECK(first + 4 == last && 4 + first == last && last - 4 == first);
  CHECK(last - first == 4 && first - last == -4);
  CHECK(&first[3] == &*(last - 1));

  CHECK(first < last && !(last < first) && last > first && !(first > last));
  // <= and >= at one position, through a copy, as a caller compares two
  // iterators that happen to be equal.
  const It also_first = first;
  const It also_last = last;
  CHECK(first <= also_first && first <= last && !(last <= first));
  CHECK(last >= also_last && last >= first && !(first >= last));

  CHECK(It() == It());
}

void flat_iterator_operations() {
  fset s;
  for (const int k : {40, 10, 30, 20}) {
    CHECK(s.try_emplace(k));
  }
  walk_one_iterator_type(s.begin(), s.end());
  walk_one_iterator_type(s.cbegin(), s.cend());
  CHECK(*s.begin() == 10 && s.begin()[3] == 40);

  fmap m;
  for (const int k : {4, 1, 3, 2}) {
    CHECK(m.try_emplace(k, k * 10));
  }
  walk_one_iterator_type(m.begin(), m.end());
  walk_one_iterator_type(m.cbegin(), m.cend());
  CHECK(m.begin()->key == 1 && m.begin()[3].value == 40);

  // iterator -> const_iterator, and both compare as one position.
  const fmap::const_iterator converted = m.begin() + 2;
  CHECK(converted == m.cbegin() + 2);
  CHECK(converted->key == 3);
  // Writes go through the mutable iterator.
  m.begin()[1].value = 21;
  CHECK(m.find(2)->value == 21);

  // Standard algorithms see a random-access iterator.
  static_assert(std::is_same_v<std::iterator_traits<fset::iterator>::iterator_category,
                               std::random_access_iterator_tag>,
                "random access");
#if __cplusplus >= 202002L
  static_assert(std::contiguous_iterator<fset::iterator> && std::contiguous_iterator<fmap::const_iterator>,
                "contiguous in C++20");
#endif
  CHECK(std::distance(s.begin(), s.end()) == 4);
  CHECK(*std::prev(s.end()) == 40);
  CHECK(std::reverse_iterator<fset::const_iterator>(s.cend())[0] == 40);
}

void every_position_everywhere() {
  every_position_map<int, fmap>();
  every_position_map<int, umap>();
  every_position_set<int, fset>();
  every_position_set<int, uset>();
  every_position_map<long, metl::flat_map<int, int, 4, std::less<>>>();
  every_position_set<long, metl::flat_set<int, 4, std::less<>>>();
  every_position_map<long, metl::static_unordered_map<int, int, 4, long_hash, long_equal>>();
  every_position_set<long, metl::static_unordered_set<int, 4, long_hash, long_equal>>();
}

}  // namespace

int main() {
  std_idiom_on_all_four();
  erase_by_find();
  heterogeneous();
  lookup_table_find_value();
  every_position_everywhere();
  flat_iterator_operations();
  return metl_test::exit_code();
}
