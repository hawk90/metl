// An empty comparator, hasher or key equality takes no room (#318).
//
// Each container holds its functor in a detail::ebo_pair beside a count, or,
// for fixed_priority_queue, beside its elements. Each size is pinned against a
// struct with the layout the container should have without the functor, so
// the test holds on 32- and 64-bit hosts. Cases the empty-base optimization
// must not change are covered too:
//
// - a stateful functor is still held, and its state is used;
// - a `final` empty functor cannot be a base, so it is a member again;
// - one type serving as both hasher and key equality is two subobjects in two
//   pairs, and both still take no room.
//
// The MSVC host-test job builds this too: MSVC gives room to all but the first
// of several empty bases, which is why no class here has two.

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>

#include <metl/fixed_priority_queue.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

using u8 = std::uint8_t;
using u32 = std::uint32_t;

// --- the layouts without the functor ----------------------------------------

template <typename V, std::size_t N>
struct storage_then_size {
  alignas(V) unsigned char bytes[N * sizeof(V)];
  std::size_t size;
};

struct u8_entry {
  u8 key;
  u8 value;
};
struct u32_entry {
  u32 key;
  u32 value;
};

static_assert(sizeof(metl::flat_map<u8, u8, 8>) == sizeof(storage_then_size<u8_entry, 8>),
              "flat_map: the entries and the size, and nothing for std::less");
static_assert(sizeof(metl::flat_map<u32, u32, 8>) == sizeof(storage_then_size<u32_entry, 8>),
              "flat_map: the entries and the size, and nothing for std::less");
static_assert(sizeof(metl::flat_set<u8, 8>) == sizeof(storage_then_size<u8, 8>),
              "flat_set: the keys and the size, and nothing for std::less");
static_assert(sizeof(metl::flat_set<u32, 8>) == sizeof(storage_then_size<u32, 8>),
              "flat_set: the keys and the size, and nothing for std::less");
// fixed_priority_queue is its fixed_vector, and nothing for std::less.
static_assert(sizeof(metl::fixed_priority_queue<u8, 8>) == sizeof(metl::fixed_vector<u8, 8>),
              "fixed_priority_queue: its elements, and nothing for std::less");
static_assert(sizeof(metl::fixed_priority_queue<u32, 8>) == sizeof(metl::fixed_vector<u32, 8>),
              "fixed_priority_queue: its elements, and nothing for std::less");

// The unordered containers: the slots, the two state bitsets, the size and
// the tombstone count, and nothing for std::hash and std::equal_to.
template <typename Table>
struct unordered_without_functors {
  metl::detail::slot_array<metl::storage_for<typename Table::value_type>, Table::bucket_count> slots;
  metl::detail::bitset<Table::bucket_count> occupied;
  metl::detail::bitset<Table::bucket_count> tombstone;
  std::size_t size;
  std::size_t tombstones;
};
using map8 = metl::static_unordered_map<u32, u32, 8>;
using set8 = metl::static_unordered_set<u32, 8>;
static_assert(sizeof(map8) == sizeof(unordered_without_functors<map8>),
              "static_unordered_map: nothing for an empty Hash and KeyEqual");
static_assert(sizeof(set8) == sizeof(unordered_without_functors<set8>),
              "static_unordered_set: nothing for an empty Hash and KeyEqual");

// --- functors the optimization must not change -------------------------------

// Stateful: ascending or descending, chosen at run time.
struct ordered_by {
  bool descending = false;
  bool operator()(int a, int b) const noexcept { return descending ? b < a : a < b; }
};
static_assert(sizeof(metl::flat_set<int, 4, ordered_by>) > sizeof(metl::flat_set<int, 4>),
              "a stateful comparator is still held");

// Empty, but final: it cannot be a base, so it is a member and takes room.
struct final_less final {
  bool operator()(int a, int b) const noexcept { return a < b; }
};
static_assert(sizeof(metl::flat_set<int, 4, final_less>) > sizeof(metl::flat_set<int, 4>),
              "a final comparator cannot be a base, so it is a member");

// One empty type that is both the hasher and the key equality.
struct hash_and_equal {
  std::size_t operator()(u32 key) const noexcept { return key; }
  bool operator()(u32 a, u32 b) const noexcept { return a == b; }
};
using both_map = metl::static_unordered_map<u32, u32, 8, hash_and_equal, hash_and_equal>;
static_assert(sizeof(both_map) == sizeof(unordered_without_functors<both_map>),
              "one empty type as both functors still takes no room: the two are in different pairs");

}  // namespace

int main() {
  // A stateful comparator's state decides the order, through copy and move.
  {
    metl::flat_set<int, 4, ordered_by> set(ordered_by{true});
    CHECK(set.try_emplace(1));
    CHECK(set.try_emplace(3));
    CHECK(set.try_emplace(2));
    CHECK_EQ(*set.begin(), 3);
    metl::flat_set<int, 4, ordered_by> copied(set);
    CHECK_EQ(*copied.begin(), 3);
    metl::flat_set<int, 4, ordered_by> moved(static_cast<metl::flat_set<int, 4, ordered_by>&&>(copied));
    CHECK_EQ(*moved.begin(), 3);
    CHECK(moved.key_comp().descending);
  }
  {
    metl::fixed_priority_queue<int, 4, ordered_by> queue(ordered_by{true});
    CHECK(queue.try_push(1));
    CHECK(queue.try_push(3));
    CHECK(queue.try_push(2));
    CHECK_EQ(queue.top(), 1);  // descending order makes the smallest the top
  }

  // A final comparator works as a member.
  {
    metl::flat_set<int, 4, final_less> set;
    CHECK(set.try_emplace(2));
    CHECK(set.try_emplace(1));
    CHECK_EQ(*set.begin(), 1);
  }

  // One type as both functors.
  {
    both_map map;
    CHECK(map.try_emplace(7U, 70U));
    CHECK(map.contains(7U));
    CHECK(!map.contains(8U));
  }

  return metl_test::exit_code();
}
