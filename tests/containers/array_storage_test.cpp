// The contiguous containers reach their elements as one array
// (include/metl/detail/array_storage.hpp). This test exercises what that
// change must keep working: indexing and iteration through data() + i across
// heavy churn (elements destroyed and re-created in place), element types with
// a const member, an empty container's data(), and Capacity == 0. It runs
// freestanding, so it means the same thing on QEMU.

#include "metl_check.hpp"

#include <cstddef>

#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>

namespace {

// A const member: every re-creation in a slot is a new object the old pointer
// arithmetic had to reach (see the residual note in array_storage.hpp).
struct tagged {
  const int id;
  int value;
  tagged(int i, int v) noexcept : id(i), value(v) {}
  tagged(const tagged&) noexcept = default;
  tagged(tagged&&) noexcept = default;
  tagged& operator=(const tagged&) = delete;
  tagged& operator=(tagged&&) = delete;
  ~tagged() = default;
};

struct ordered {
  int key;
  bool operator<(const ordered& other) const noexcept { return key < other.key; }
};

}  // namespace

int main() {
  // fixed_vector: churn at the back, then walk the whole range by data() + i.
  {
    metl::fixed_vector<tagged, 16> v;
    int expected_id[16] = {};  // the id each slot should hold right now
    for (int round = 0; round < 200; ++round) {
      while (v.size() < 16) {
        const int n = static_cast<int>(v.size());
        expected_id[n] = round * 100 + n;
        v.emplace_back(expected_id[n], n);
      }
      const tagged* base = v.data();
      for (std::size_t i = 0; i < v.size(); ++i) {
        CHECK_EQ(base[i].id, expected_id[i]);
        CHECK_EQ((base + i)->value, static_cast<int>(i));
      }
      int sum = 0;
      for (const tagged& t : v) {
        sum += t.value;
      }
      CHECK_EQ(sum, 120);  // 0 + 1 + ... + 15
      while (v.size() > static_cast<std::size_t>(round % 7)) {
        v.pop_back();
      }
    }
  }

  // flat_map / flat_set: inserts in the middle shift everything; iterate after each.
  {
    metl::flat_map<int, int, 32> m;
    metl::flat_set<ordered, 32> s;
    for (int round = 0; round < 50; ++round) {
      for (int k = 31; k >= 0; k -= 2) {
        (void)m.try_emplace(k, k * 10);
        (void)s.try_emplace(ordered{k});
      }
      for (int k = 0; k < 32; k += 2) {
        (void)m.try_emplace(k, k * 10);
        (void)s.try_emplace(ordered{k});
      }
      CHECK_EQ(m.size(), 32u);
      CHECK_EQ(s.size(), 32u);
      int expected = 0;
      for (const auto& entry : m) {
        CHECK_EQ(entry.key, expected);
        CHECK_EQ(entry.value, expected * 10);
        ++expected;
      }
      expected = 0;
      for (const ordered& entry : s) {
        CHECK_EQ(entry.key, expected);
        ++expected;
      }
      for (int k = 0; k < 32; k += 3) {
        (void)m.erase(k);
        (void)s.erase(ordered{k});
      }
      m.clear();
      s.clear();
    }
  }

  // data() on an empty container is a valid pointer to the (unconstructed)
  // first element: stable, non-null, and equal to begin().
  {
    metl::fixed_vector<int, 4> v;
    CHECK(v.data() != nullptr);
    CHECK(v.data() == v.begin());
    v.push_back(7);
    CHECK(v.data() == &v[0]);
  }

  // Capacity 0 still instantiates (the storage keeps one slot it never uses).
  {
    metl::fixed_vector<int, 0> v;
    CHECK(v.empty());
    CHECK(!v.try_push_back(1));
  }

  return metl_test::exit_code();
}
