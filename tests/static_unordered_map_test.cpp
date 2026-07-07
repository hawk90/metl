#include <cstddef>
#include <functional>

#include <metl/fixed_string.hpp>
#include <metl/hash.hpp>
#include <metl/static_unordered_map.hpp>

namespace {

using test_string = metl::fixed_string<32>;

// FNV-1a hash over a NUL-terminated byte range. Used for both fixed_string
// and const char* so that transparent lookup yields identical hashes.
inline std::size_t fnv1a_cstr(const char* s) noexcept {
  std::size_t len = 0;
  while (s[len] != '\0') {
    ++len;
  }
  return metl::fnv1a(reinterpret_cast<const unsigned char*>(s), len);
}

inline bool cstr_equal(const char* a, const char* b) noexcept {
  std::size_t i = 0;
  while (a[i] != '\0' && b[i] != '\0') {
    if (a[i] != b[i])
      return false;
    ++i;
  }
  return a[i] == b[i];
}

struct tracker {
  static int constructions;
  static int destructions;

  tracker() : value(0) { ++constructions; }
  explicit tracker(int input) : value(input) { ++constructions; }
  tracker(const tracker& other) : value(other.value) { ++constructions; }
  tracker(tracker&& other) noexcept : value(other.value) { ++constructions; }
  ~tracker() { ++destructions; }

  tracker& operator=(const tracker& other) {
    value = other.value;
    return *this;
  }

  tracker& operator=(tracker&& other) noexcept {
    value = other.value;
    return *this;
  }

  int value;
};

int tracker::constructions = 0;
int tracker::destructions = 0;

// Transparent hash + equality usable with both fixed_string and const char*.
struct string_hash {
  using is_transparent = void;

  std::size_t operator()(const test_string& s) const noexcept {
    return metl::fnv1a(reinterpret_cast<const unsigned char*>(s.data()), s.size());
  }
  std::size_t operator()(const char* s) const noexcept { return fnv1a_cstr(s); }
};

struct string_equal {
  using is_transparent = void;

  bool operator()(const test_string& a, const test_string& b) const noexcept { return a == b; }
  bool operator()(const test_string& a, const char* b) const noexcept { return cstr_equal(a.c_str(), b); }
  bool operator()(const char* a, const test_string& b) const noexcept { return cstr_equal(a, b.c_str()); }
};

}  // namespace

int main() {
  metl::static_unordered_map<int, int, 4> map;
  if (!map.empty() || map.capacity() != 4) {
    return 1;
  }

  if (!map.try_emplace(1, 10) || !map.try_emplace(5, 50) || !map.try_emplace(9, 90)) {
    return 2;
  }

  if (map.size() != 3 || !map.contains(5) || map.contains(2)) {
    return 3;
  }

  int* found = map.find(9);
  if (found == nullptr || *found != 90) {
    return 4;
  }

  if (map.try_emplace(5, 55)) {
    return 5;
  }

  if (!map.insert_or_assign(5, 55) || *map.find(5) != 55) {
    return 6;
  }

  if (!map.erase(5) || map.contains(5)) {
    return 7;
  }

  if (!map.try_emplace(13, 130) || *map.find(13) != 130) {
    return 8;
  }

  int sum = 0;
  int count = 0;
  for (const auto& item : map) {
    sum += item.value;
    ++count;
  }

  if (count != 3 || sum != 230) {
    return 9;
  }

  if (!map.erase(42)) {
    if (map.size() != 3) {
      return 10;
    }
  } else {
    return 10;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::static_unordered_map<int, tracker, 2> tracked;
    tracked.emplace(1, tracker(10));
    tracked.emplace(2, tracker(20));

    if (tracked.full()) {
      if (tracked.try_emplace(3, tracker(30))) {
        return 11;
      }
    } else {
      return 11;
    }

    tracked.clear();
  }

  if (tracker::constructions != tracker::destructions) {
    return 12;
  }

  // -------- Extended tests --------

  // 13: bucket_count must be a power of two and at least Capacity * 2.
  {
    using map4 = metl::static_unordered_map<int, int, 4>;
    static_assert(map4::bucket_count == 8, "bucket_count for Capacity=4 should be 8");
    static_assert((map4::bucket_count & (map4::bucket_count - 1)) == 0, "must be pow2");

    using map3 = metl::static_unordered_map<int, int, 3>;
    static_assert(map3::bucket_count == 8, "bucket_count for Capacity=3 should ceil to 8");

    using map1 = metl::static_unordered_map<int, int, 1>;
    static_assert(map1::bucket_count == 2, "bucket_count for Capacity=1 should be 2");
  }

  // 14: operator[] inserts a default-constructed value if missing.
  {
    metl::static_unordered_map<int, int, 4> m;
    int& slot = m[7];
    if (slot != 0 || m.size() != 1 || !m.contains(7)) {
      return 14;
    }
    slot = 42;
    if (*m.find(7) != 42) {
      return 14;
    }

    // operator[] on existing key just returns the reference, no growth.
    int& again = m[7];
    if (again != 42 || m.size() != 1) {
      return 14;
    }
  }

  // 15: find_iter is an STL-compatible iterator-returning lookup.
  {
    metl::static_unordered_map<int, int, 4> m;
    m.try_emplace(1, 11);
    m.try_emplace(2, 22);

    auto it = m.find_iter(2);
    if (it == m.end() || it->key != 2 || it->value != 22) {
      return 15;
    }

    auto miss = m.find_iter(99);
    if (miss != m.end()) {
      return 15;
    }
  }

  // 16: Heterogeneous lookup with transparent hasher / equal_to.
  {
    metl::static_unordered_map<test_string, int, 8, string_hash, string_equal> m;
    m.try_emplace(test_string("alpha"), 1);
    m.try_emplace(test_string("beta"), 2);

    const char* lookup = "alpha";
    if (!m.contains(lookup)) {
      return 16;
    }

    int* found_het = m.find(lookup);
    if (found_het == nullptr || *found_het != 1) {
      return 16;
    }

    auto it = m.find_iter(static_cast<const char*>("beta"));
    if (it == m.end() || it->value != 2) {
      return 16;
    }

    if (!m.erase(static_cast<const char*>("alpha"))) {
      return 16;
    }
    if (m.contains(test_string("alpha"))) {
      return 16;
    }
  }

  // 17: power-of-2 probing must wrap correctly under collisions (struct identity hash).
  {
    struct identity_hash {
      std::size_t operator()(int v) const noexcept { return static_cast<std::size_t>(v); }
    };
    metl::static_unordered_map<int, int, 4, identity_hash> m;
    // bucket_count == 8, all of these hash to bucket 0 then collide.
    m.try_emplace(0, 100);
    m.try_emplace(8, 108);
    m.try_emplace(16, 116);
    m.try_emplace(24, 124);
    if (m.size() != 4 || !m.full()) {
      return 17;
    }
    if (*m.find(0) != 100 || *m.find(8) != 108 || *m.find(16) != 116 || *m.find(24) != 124) {
      return 17;
    }
    if (!m.erase(8) || !m.try_emplace(32, 132) || *m.find(32) != 132) {
      return 17;
    }
  }

  return 0;
}
