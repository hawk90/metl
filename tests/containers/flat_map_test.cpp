#include <cstring>
#include <functional>

#include <metl/flat_map.hpp>

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

  int value;
};

int tracker::constructions = 0;
int tracker::destructions = 0;

// Small string view used to exercise heterogeneous lookup.
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
  metl::flat_map<int, int, 4> map;
  if (!map.empty() || map.capacity() != 4) {
    return 1;
  }

  if (!map.try_emplace(2, 20) || !map.try_emplace(1, 10) || !map.try_emplace(3, 30)) {
    return 2;
  }

  if (map.size() != 3 || map[0].key != 1 || map.at(1).key != 2 || map[2].key != 3) {
    return 3;
  }

  if (!map.contains(2) || map.contains(9)) {
    return 4;
  }

  auto lower = map.lower_bound(2);
  auto upper = map.upper_bound(2);
  if (lower == map.end() || lower->key != 2 || upper == map.end() || upper->key != 3) {
    return 5;
  }

  auto missing_lower = map.lower_bound(4);
  auto missing_upper = map.upper_bound(0);
  if (missing_lower != map.end() || missing_upper != map.begin()) {
    return 6;
  }

  int* found = map.find(3);
  if (found == nullptr || *found != 30) {
    return 7;
  }

  if (map.try_emplace(2, 99)) {
    return 8;
  }

  if (!map.insert_or_assign(2, 25) || *map.find(2) != 25) {
    return 9;
  }

  if (!map.erase(1) || map.contains(1) || map[0].key != 2) {
    return 10;
  }

  if (map.erase(8)) {
    return 11;
  }

  // equal_range tests on default-compared map.
  if (!map.try_emplace(1, 11)) {
    return 12;
  }
  // map keys are now {1, 2, 3}
  {
    auto rng = map.equal_range(2);
    if (rng.first == map.end() || rng.first->key != 2) {
      return 13;
    }
    if (rng.second == map.end() || rng.second->key != 3) {
      return 14;
    }
    auto missing = map.equal_range(9);
    if (missing.first != map.end() || missing.second != map.end()) {
      return 15;
    }
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::flat_map<int, tracker, 2> tracked;
    tracked.emplace(2, tracker(20));
    tracked.emplace(1, tracker(10));

    if (tracked[0].key != 1 || tracked[0].value.value != 10 || tracked[1].value.value != 20) {
      return 16;
    }

    tracked.clear();
  }

  if (tracker::constructions != tracker::destructions) {
    return 17;
  }

  // --- Custom non-transparent Compare (reverse order) ---
  {
    metl::flat_map<int, int, 4, int_greater> rmap;
    rmap.try_emplace(1, 100);
    rmap.try_emplace(3, 300);
    rmap.try_emplace(2, 200);

    // Stored in descending order.
    if (rmap[0].key != 3 || rmap[1].key != 2 || rmap[2].key != 1) {
      return 18;
    }

    if (!rmap.contains(2) || rmap.contains(99)) {
      return 19;
    }

    const int* p = rmap.find(2);
    if (p == nullptr || *p != 200) {
      return 20;
    }

    // lower_bound with reverse: first element NOT > key.
    auto lb = rmap.lower_bound(2);
    if (lb == rmap.end() || lb->key != 2) {
      return 21;
    }

    if (!rmap.erase(3) || rmap.contains(3) || rmap[0].key != 2) {
      return 22;
    }
  }

  // --- Heterogeneous lookup with const char* + cstr_less ---
  {
    metl::flat_map<const char*, int, 4, cstr_less> smap;
    smap.try_emplace("banana", 2);
    smap.try_emplace("apple", 1);
    smap.try_emplace("cherry", 3);

    // Sorted in strcmp order: apple, banana, cherry.
    if (std::strcmp(smap[0].key, "apple") != 0) {
      return 23;
    }
    if (std::strcmp(smap[1].key, "banana") != 0) {
      return 24;
    }
    if (std::strcmp(smap[2].key, "cherry") != 0) {
      return 25;
    }

    // Heterogeneous lookup via cstr_view (a different type than const char*).
    cstr_view vb{"banana"};
    const int* pb = smap.find(vb);
    if (pb == nullptr || *pb != 2) {
      return 26;
    }

    cstr_view va{"apple"};
    cstr_view vmissing{"kiwi"};
    if (!smap.contains(va) || smap.contains(vmissing)) {
      return 27;
    }

    // Heterogeneous lower_bound / upper_bound.
    cstr_view vc{"cherry"};
    auto lb = smap.lower_bound(vc);
    if (lb == smap.end() || std::strcmp(lb->key, "cherry") != 0) {
      return 28;
    }
    auto ub = smap.upper_bound(va);
    if (ub == smap.end() || std::strcmp(ub->key, "banana") != 0) {
      return 29;
    }

    // Heterogeneous equal_range.
    auto rng = smap.equal_range(vc);
    if (rng.first == smap.end() || std::strcmp(rng.first->key, "cherry") != 0) {
      return 30;
    }
    if (rng.second != smap.end()) {
      return 31;
    }

    // Heterogeneous erase.
    if (!smap.erase(vb)) {
      return 32;
    }
    if (smap.contains(vb)) {
      return 33;
    }
  }

  // --- key_comp accessor ---
  {
    metl::flat_map<int, int, 4> dmap;
    auto kc = dmap.key_comp();
    if (kc(2, 1) || !kc(1, 2)) {
      return 34;
    }
  }

  return 0;
}
