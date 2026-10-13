// Small paths of the containers that no other test takes: comparisons that
// differ only in size or in the last element, empty and shrinking ranges,
// lookups that land past the end, a range just above a vector's storage, and
// reuse of a tombstone. The comparisons are checked against the std container
// with the same contents, pair by pair.
//
// Self-assignment, self-swap and capacity 0 are properties of every container,
// so they are in the conformance suite (conformance_self, conformance_capacity).

#include "metl_check.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

// Contents that differ in size, in the first element, or only in the last one.
const std::vector<std::vector<int>> kSequences = {{}, {1}, {2}, {1, 2}, {1, 3}, {1, 2, 3}};

template <typename A, typename B, typename SA, typename SB>
void check_ordering(const A& a, const B& b, const SA& sa, const SB& sb) {
  CHECK_EQ(a == b, sa == sb);
  CHECK_EQ(a != b, sa != sb);
  CHECK_EQ(a < b, sa < sb);
  CHECK_EQ(a <= b, sa <= sb);
  CHECK_EQ(a > b, sa > sb);
  CHECK_EQ(a >= b, sa >= sb);
}

// ---- fixed_vector -------------------------------------------------------------

metl::fixed_vector<int, 4> vector_of(const std::vector<int>& values) {
  metl::fixed_vector<int, 4> v;
  for (const int x : values) {
    v.push_back(x);
  }
  return v;
}

void vector_comparisons() {
  for (const auto& a : kSequences) {
    for (const auto& b : kSequences) {
      check_ordering(vector_of(a), vector_of(b), a, b);
    }
  }
}

void vector_erase_empty_range() {
  metl::fixed_vector<int, 4> v = vector_of({1, 2, 3});
  const auto it = v.erase(v.begin() + 1, v.begin() + 1);
  CHECK(it == v.begin() + 1);
  CHECK(v == vector_of({1, 2, 3}));
}

void vector_resize_shrinks() {
  metl::fixed_vector<int, 4> v = vector_of({1, 2, 3});
  v.resize(1, 9);
  CHECK(v == vector_of({1}));
  v.resize(3, 9);
  CHECK(v == vector_of({1, 9, 9}));
}

// A range that lies just above the vector's storage is not its own: assigning
// from it copies, rather than treating it as a self-range.
void vector_assign_from_just_above_storage() {
  struct layout {
    metl::fixed_vector<int, 4> v;
    int above[3] = {7, 8, 9};
  } l;
  CHECK(l.v.try_assign(l.above, l.above + 3));
  CHECK(l.v == vector_of({7, 8, 9}));
}

// ---- flat_map / flat_set -------------------------------------------------------

using map_type = metl::flat_map<int, int, 4, std::less<>>;
using set_type = metl::flat_set<int, 4, std::less<>>;

void map_comparisons() {
  // Same keys with a different value, as well as different keys and sizes.
  const std::vector<std::map<int, int>> contents = {
      {}, {{1, 10}}, {{1, 11}}, {{2, 10}}, {{1, 10}, {2, 20}}, {{1, 10}, {2, 21}}, {{1, 10}, {3, 20}}};
  for (const auto& a : contents) {
    for (const auto& b : contents) {
      map_type ma;
      map_type mb;
      for (const auto& [k, v] : a) {
        CHECK(ma.try_emplace(k, v));
      }
      for (const auto& [k, v] : b) {
        CHECK(mb.try_emplace(k, v));
      }
      check_ordering(ma, mb, a, b);
    }
  }
}

void set_comparisons() {
  for (const auto& a : kSequences) {
    for (const auto& b : kSequences) {
      set_type sa;
      set_type sb;
      for (const int x : a) {
        CHECK(sa.try_emplace(x));
      }
      for (const int x : b) {
        CHECK(sb.try_emplace(x));
      }
      check_ordering(sa, sb, std::set<int>(a.begin(), a.end()), std::set<int>(b.begin(), b.end()));
    }
  }
}

// Transparent lookups that miss: by a key between two keys, which lands on a
// greater key, and by a key above every key, which lands past the end.
void transparent_lookup_misses() {
  map_type m;
  CHECK(m.try_emplace(1, 10));
  CHECK(m.try_emplace(3, 30));
  set_type s;
  CHECK(s.try_emplace(1));
  CHECK(s.try_emplace(3));
  for (const long missing : {2L, 5L}) {
    CHECK(m.find(missing) == nullptr);
    CHECK(!m.erase(missing));
    CHECK(s.find(missing) == nullptr);
    CHECK(!s.erase(missing));
  }
  CHECK_EQ(m.size(), std::size_t{2});
  CHECK_EQ(s.size(), std::size_t{2});
}

// Erasing the last element returns end().
void set_erase_last_returns_end() {
  set_type s;
  CHECK(s.try_emplace(1));
  CHECK(s.try_emplace(2));
  const auto next = s.erase(s.begin() + 1);
  CHECK(next == s.end());
  CHECK_EQ(s.size(), std::size_t{1});
}

// ---- static_unordered_map / static_unordered_set ---------------------------------

// Every key in one bucket, so a lookup walks past each occupied slot and
// tombstone in turn.
struct one_bucket {
  using is_transparent = void;
  std::size_t operator()(int) const noexcept { return 0; }
  std::size_t operator()(long) const noexcept { return 0; }
};

struct same_value {
  using is_transparent = void;
  template <typename A, typename B>
  bool operator()(const A& a, const B& b) const noexcept {
    return static_cast<long>(a) == static_cast<long>(b);
  }
};

using umap_type = metl::static_unordered_map<int, int, 4, one_bucket, same_value>;
using uset_type = metl::static_unordered_set<int, 4, one_bucket, same_value>;

void unordered_lookup_misses() {
  umap_type m;
  CHECK(m.try_emplace(1, 10));
  const long missing = 7;
  CHECK(m.find(missing) == nullptr);
  CHECK(static_cast<const umap_type&>(m).find(missing) == nullptr);
  CHECK(m.find_iterator(missing) == m.end());

  const long present = 1;
  CHECK(static_cast<const umap_type&>(m).find(present) != nullptr);

  uset_type s;
  CHECK(s.try_emplace(1));
  CHECK(s.find(missing) == nullptr);
  CHECK(s.find_iterator(missing) == s.end());
  CHECK(!s.erase(missing));
  CHECK_EQ(s.size(), std::size_t{1});
}

// An insert after an erase in the same probe sequence takes the tombstone the
// erase left, not the next empty slot.
void unordered_set_reuses_a_tombstone() {
  uset_type s;
  CHECK(s.try_emplace(1));
  CHECK(s.try_emplace(2));
  CHECK(s.erase(1));
  CHECK(s.try_emplace(3));
  CHECK_EQ(s.size(), std::size_t{2});
  CHECK(s.contains(2));
  CHECK(s.contains(3));
  CHECK(!s.contains(1));
}

// A key whose move may throw: the set never rebuilds the table for it, so
// tombstones accumulate until no slot is empty.
struct throwing_move_key {
  int value;
  explicit throwing_move_key(int v) noexcept : value(v) {}
  throwing_move_key(const throwing_move_key&) = default;
  throwing_move_key(throwing_move_key&& other) noexcept(false) : value(other.value) {}
  throwing_move_key& operator=(const throwing_move_key&) = default;
  friend bool operator==(const throwing_move_key& a, const throwing_move_key& b) {
    return a.value == b.value;
  }
};

struct throwing_move_key_hash {
  std::size_t operator()(const throwing_move_key& k) const noexcept {
    return static_cast<std::size_t>(k.value);
  }
};

// Insert-then-erase with fresh keys until every slot is a tombstone; the next
// insert finds no empty slot and must take the first tombstone it passed.
void unordered_set_inserts_into_a_table_of_tombstones() {
  metl::static_unordered_set<throwing_move_key, 4, throwing_move_key_hash> s;
  for (int key = 0; key < 64; ++key) {
    CHECK(s.try_emplace(throwing_move_key(key)));
    CHECK(s.erase(throwing_move_key(key)));
  }
  CHECK(s.empty());
  CHECK(s.try_emplace(throwing_move_key(100)));
  CHECK_EQ(s.size(), std::size_t{1});
  CHECK(s.contains(throwing_move_key(100)));
  CHECK(!s.contains(throwing_move_key(63)));
}

// Value-initialized iterators compare equal, as for any forward iterator.
void value_initialized_iterators_compare_equal() {
  CHECK(umap_type::iterator() == umap_type::iterator());
  CHECK(umap_type::const_iterator() == umap_type::const_iterator());
  CHECK(uset_type::iterator() == uset_type::iterator());
  CHECK(uset_type::const_iterator() == uset_type::const_iterator());
}

}  // namespace

int main() {
  vector_comparisons();
  vector_erase_empty_range();
  vector_resize_shrinks();
  vector_assign_from_just_above_storage();
  map_comparisons();
  set_comparisons();
  transparent_lookup_misses();
  set_erase_last_returns_end();
  unordered_lookup_misses();
  unordered_set_reuses_a_tombstone();
  unordered_set_inserts_into_a_table_of_tombstones();
  value_initialized_iterators_compare_equal();
  return metl_test::exit_code();
}
