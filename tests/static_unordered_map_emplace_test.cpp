// Regression test: static_unordered_map::emplace must find an existing key
// first. The previous implementation constructed unconditionally, so a
// duplicate-key emplace double-constructed over a live element and incremented
// size_ twice (object leak + size corruption). std::unordered_map semantics:
// emplace does NOT overwrite an existing key.

#include "metl_check.hpp"

#include <cstddef>

#include <metl/static_unordered_map.hpp>

namespace {

struct counted {
  static int live;
  int value = 0;

  counted() { ++live; }
  explicit counted(int v) : value(v) { ++live; }
  counted(const counted& o) : value(o.value) { ++live; }
  counted(counted&& o) noexcept : value(o.value) { ++live; }
  counted& operator=(const counted&) = default;
  counted& operator=(counted&&) noexcept = default;
  ~counted() { --live; }
};

int counted::live = 0;

}  // namespace

int main() {
  {
    metl::static_unordered_map<int, counted, 8> map;

    auto& first = map.emplace(1, counted{10});
    CHECK_EQ(map.size(), std::size_t(1));
    CHECK_EQ(first.value.value, 10);
    const int live_after_first = counted::live;

    // Duplicate key: must be a no-op returning the *existing* element, no
    // second construction, no size change.
    auto& again = map.emplace(1, counted{999});
    CHECK_EQ(map.size(), std::size_t(1));
    CHECK_EQ(again.value.value, 10);            // NOT overwritten
    CHECK_EQ(&again, &first);                   // same element
    CHECK_EQ(counted::live, live_after_first);  // no leaked extra construction

    // A genuinely new key still inserts.
    map.emplace(2, counted{20});
    CHECK_EQ(map.size(), std::size_t(2));
    auto* p = map.find(2);
    CHECK(p != nullptr);
    CHECK_EQ(p->value, 20);
  }
  // All elements destroyed with the map: no leaks.
  CHECK_EQ(counted::live, 0);

  return metl_test::exit_code();
}
