#include <cstring>
#include <functional>

#include <metl/flat_set.hpp>

namespace {

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

  friend bool operator<(const tracker& lhs, const tracker& rhs) { return lhs.value < rhs.value; }

  int value;
};

int tracker::constructions = 0;
int tracker::destructions = 0;

struct cstr_view {
  const char* ptr;
};

// Transparent comparator over const char* / cstr_view using strcmp.
struct cstr_less {
  using is_transparent = void;

  bool operator()(const char* a, const char* b) const noexcept { return std::strcmp(a, b) < 0; }
  bool operator()(const char* a, cstr_view b) const noexcept { return std::strcmp(a, b.ptr) < 0; }
  bool operator()(cstr_view a, const char* b) const noexcept { return std::strcmp(a.ptr, b) < 0; }
  bool operator()(cstr_view a, cstr_view b) const noexcept { return std::strcmp(a.ptr, b.ptr) < 0; }
};

// Reverse comparator (non-transparent) for testing custom Compare.
struct int_greater {
  bool operator()(int a, int b) const noexcept { return a > b; }
};

}  // namespace

int main() {
  // --- Backward-compatibility tests with default std::less<int> ---
  metl::flat_set<int, 4> set;
  if (!set.empty() || set.capacity() != 4) {
    return 1;
  }

  if (!set.try_emplace(2) || !set.try_emplace(1) || !set.try_emplace(3)) {
    return 2;
  }

  if (set.size() != 3 || set[0] != 1 || set.at(1) != 2 || set[2] != 3) {
    return 3;
  }

  if (!set.contains(2) || set.contains(9)) {
    return 4;
  }

  auto lower = set.lower_bound(2);
  auto upper = set.upper_bound(2);
  if (lower == set.end() || *lower != 2 || upper == set.end() || *upper != 3) {
    return 5;
  }

  auto missing_lower = set.lower_bound(4);
  auto missing_upper = set.upper_bound(0);
  if (missing_lower != set.end() || missing_upper != set.begin()) {
    return 6;
  }

  int* found = set.find(3);
  if (found == nullptr || *found != 3) {
    return 7;
  }

  if (set.try_emplace(2)) {
    return 8;
  }

  if (!set.erase(1) || set.contains(1) || set[0] != 2) {
    return 9;
  }

  if (set.erase(8)) {
    return 10;
  }

  // equal_range tests.
  if (!set.try_emplace(1)) {
    return 11;
  }
  // set is now {1, 2, 3}
  {
    auto rng = set.equal_range(2);
    if (rng.first == set.end() || *rng.first != 2) {
      return 12;
    }
    if (rng.second == set.end() || *rng.second != 3) {
      return 13;
    }
    auto missing = set.equal_range(9);
    if (missing.first != set.end() || missing.second != set.end()) {
      return 14;
    }
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::flat_set<tracker, 2> tracked;
    tracked.emplace(tracker(20));
    tracked.emplace(tracker(10));

    if (tracked[0].value != 10 || tracked[1].value != 20) {
      return 15;
    }

    tracked.clear();
  }

  if (tracker::constructions != tracker::destructions) {
    return 16;
  }

  // --- Custom non-transparent Compare (reverse order) ---
  {
    metl::flat_set<int, 4, int_greater> rset;
    rset.try_emplace(1);
    rset.try_emplace(3);
    rset.try_emplace(2);

    if (rset[0] != 3 || rset[1] != 2 || rset[2] != 1) {
      return 17;
    }

    if (!rset.contains(2) || rset.contains(99)) {
      return 18;
    }

    if (!rset.erase(3) || rset.contains(3) || rset[0] != 2) {
      return 19;
    }
  }

  // --- Heterogeneous lookup with const char* + cstr_less ---
  {
    metl::flat_set<const char*, 4, cstr_less> sset;
    sset.try_emplace("banana");
    sset.try_emplace("apple");
    sset.try_emplace("cherry");

    if (std::strcmp(sset[0], "apple") != 0) {
      return 20;
    }
    if (std::strcmp(sset[1], "banana") != 0) {
      return 21;
    }
    if (std::strcmp(sset[2], "cherry") != 0) {
      return 22;
    }

    // Heterogeneous find/contains via cstr_view.
    cstr_view vb{"banana"};
    const char* const* pb = sset.find(vb);
    if (pb == nullptr || std::strcmp(*pb, "banana") != 0) {
      return 23;
    }

    cstr_view va{"apple"};
    cstr_view vmissing{"kiwi"};
    if (!sset.contains(va) || sset.contains(vmissing)) {
      return 24;
    }

    // Heterogeneous lower_bound / upper_bound.
    cstr_view vc{"cherry"};
    auto lb = sset.lower_bound(vc);
    if (lb == sset.end() || std::strcmp(*lb, "cherry") != 0) {
      return 25;
    }
    auto ub = sset.upper_bound(va);
    if (ub == sset.end() || std::strcmp(*ub, "banana") != 0) {
      return 26;
    }

    // Heterogeneous equal_range.
    auto rng = sset.equal_range(vc);
    if (rng.first == sset.end() || std::strcmp(*rng.first, "cherry") != 0) {
      return 27;
    }
    if (rng.second != sset.end()) {
      return 28;
    }

    // Heterogeneous erase.
    if (!sset.erase(vb)) {
      return 29;
    }
    if (sset.contains(vb)) {
      return 30;
    }
  }

  // --- key_comp / value_comp accessors ---
  {
    metl::flat_set<int, 4> dset;
    auto kc = dset.key_comp();
    auto vc = dset.value_comp();
    if (kc(2, 1) || !kc(1, 2) || vc(2, 1) || !vc(1, 2)) {
      return 31;
    }
  }

  return 0;
}
