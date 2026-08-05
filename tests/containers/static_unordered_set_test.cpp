#include <cstddef>
#include <functional>

#include <metl/fixed_string.hpp>
#include <metl/hash.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

using test_string = metl::fixed_string<32>;

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

  friend bool operator==(const tracker& lhs, const tracker& rhs) { return lhs.value == rhs.value; }

  int value;
};

struct tracker_hash {
  std::size_t operator()(const tracker& value) const noexcept {
    return static_cast<std::size_t>(value.value);
  }
};

int tracker::constructions = 0;
int tracker::destructions = 0;

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
  metl::static_unordered_set<int, 4> set;
  if (!set.empty() || set.capacity() != 4) {
    return 1;
  }

  if (!set.try_emplace(1) || !set.try_emplace(5) || !set.try_emplace(9)) {
    return 2;
  }

  if (set.size() != 3 || !set.contains(5) || set.contains(2)) {
    return 3;
  }

  int* found = set.find(9);
  if (found == nullptr || *found != 9) {
    return 4;
  }

  auto found_it = set.find_iterator(5);
  if (found_it == set.end() || *found_it != 5) {
    return 5;
  }

  if (set.try_emplace(5)) {
    return 6;
  }

  if (!set.erase(5) || set.contains(5)) {
    return 7;
  }

  if (!set.try_emplace(13) || !set.contains(13)) {
    return 8;
  }

  int sum = 0;
  int count = 0;
  for (int value : set) {
    sum += value;
    ++count;
  }

  if (count != 3 || sum != 23) {
    return 9;
  }

  if (set.erase(42)) {
    return 10;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::static_unordered_set<tracker, 2, tracker_hash> tracked;
    tracked.emplace(tracker(10));
    tracked.emplace(tracker(20));

    if (!tracked.full() || tracked.try_emplace(tracker(30))) {
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
    using s4 = metl::static_unordered_set<int, 4>;
    static_assert(s4::bucket_count == 8, "bucket_count for Capacity=4 should be 8");
    static_assert((s4::bucket_count & (s4::bucket_count - 1)) == 0, "must be pow2");

    using s3 = metl::static_unordered_set<int, 3>;
    static_assert(s3::bucket_count == 8, "bucket_count for Capacity=3 should ceil to 8");
  }

  // 14: find_iter is an STL-compatible alias for find_iterator.
  {
    metl::static_unordered_set<int, 4> s;
    s.try_emplace(7);
    s.try_emplace(11);
    auto it = s.find_iter(11);
    if (it == s.end() || *it != 11) {
      return 14;
    }
    if (s.find_iter(99) != s.end()) {
      return 14;
    }
  }

  // 15: Heterogeneous lookup.
  {
    metl::static_unordered_set<test_string, 8, string_hash, string_equal> s;
    s.try_emplace(test_string("alpha"));
    s.try_emplace(test_string("beta"));

    const char* lookup = "alpha";
    if (!s.contains(lookup)) {
      return 15;
    }
    if (s.find(lookup) == nullptr) {
      return 15;
    }

    auto it = s.find_iter(static_cast<const char*>("beta"));
    if (it == s.end() || !cstr_equal(it->c_str(), "beta")) {
      return 15;
    }

    if (!s.erase(static_cast<const char*>("alpha")) || s.contains(test_string("alpha"))) {
      return 15;
    }
  }

  // 16: Power-of-2 probing with colliding identity hash.
  {
    struct identity_hash {
      std::size_t operator()(int v) const noexcept { return static_cast<std::size_t>(v); }
    };
    metl::static_unordered_set<int, 4, identity_hash> s;
    // bucket_count == 8, these all start at bucket 0.
    s.try_emplace(0);
    s.try_emplace(8);
    s.try_emplace(16);
    s.try_emplace(24);
    if (s.size() != 4 || !s.full()) {
      return 16;
    }
    if (!s.contains(0) || !s.contains(8) || !s.contains(16) || !s.contains(24)) {
      return 16;
    }
    if (!s.erase(8) || !s.try_emplace(32) || !s.contains(32)) {
      return 16;
    }
  }

  return 0;
}
