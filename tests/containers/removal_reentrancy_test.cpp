// Associative containers and vocabulary types when an element's destructor
// calls back into the object that holds it.
//
// Removal ran ~T() first and updated size, slot state or the engaged flag
// afterwards. A destructor that unregistered itself (erase by its own key)
// therefore found itself still present and was destroyed twice, and one that
// asked "am I still here?" was told yes. Removal now commits first and destroys
// after. (The sequence containers are in sequence_reentrancy_test.cpp.)

#include "metl_check.hpp"

#include <cstddef>
#include <functional>

#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/optional.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>
#include <metl/variant.hpp>

namespace {

constexpr int kIds = 8;
int g_destroyed[kIds] = {};

void reset_counts() {
  for (int& count : g_destroyed) {
    count = 0;
  }
}

// Counts its own destruction and then runs `on_destroy`. A moved-from shell is
// disarmed, so relocation inside a container does not count.
struct tracked {
  int id = 0;
  bool armed = false;
  void (*on_destroy)(int id) = nullptr;

  tracked() = default;
  tracked(int i, void (*hook)(int)) : id(i), armed(true), on_destroy(hook) {}
  tracked(tracked&& other) noexcept : id(other.id), armed(other.armed), on_destroy(other.on_destroy) {
    other.armed = false;
  }
  tracked& operator=(tracked&& other) noexcept {
    id = other.id;
    armed = other.armed;
    on_destroy = other.on_destroy;
    other.armed = false;
    return *this;
  }
  tracked(const tracked&) = delete;
  tracked& operator=(const tracked&) = delete;
  ~tracked() {
    if (armed) {
      armed = false;
      ++g_destroyed[id];
      if (on_destroy != nullptr) {
        on_destroy(id);
      }
    }
  }
  friend bool operator<(const tracked& lhs, const tracked& rhs) { return lhs.id < rhs.id; }
  friend bool operator==(const tracked& lhs, const tracked& rhs) { return lhs.id == rhs.id; }
};

// An unarmed key, for erasing a set element by value without counting.
tracked key(int id) {
  tracked probe;
  probe.id = id;
  return probe;
}

struct tracked_hash {
  std::size_t operator()(const tracked& t) const noexcept { return std::hash<int>{}(t.id); }
};

// --- flat_map ------------------------------------------------------------------

metl::flat_map<int, tracked, 4>& fmap() {
  static metl::flat_map<int, tracked, 4> m;
  return m;
}

void flat_map_unregisters() {
  reset_counts();
  auto& m = fmap();
  m.clear();
  const auto erase_self = [](int id) { (void)fmap().erase(id); };
  m.emplace(1, tracked(1, erase_self));
  m.emplace(2, tracked(2, erase_self));
  m.emplace(3, tracked(3, erase_self));

  CHECK(m.erase(2));
  CHECK_EQ(g_destroyed[2], 1);  // not twice
  CHECK_EQ(m.size(), 2u);
  CHECK(m.contains(1));
  CHECK(m.contains(3));

  m.clear();
  CHECK_EQ(g_destroyed[1], 1);
  CHECK_EQ(g_destroyed[3], 1);
  CHECK_EQ(m.size(), 0u);
}

// --- flat_set ------------------------------------------------------------------

metl::flat_set<tracked, 4>& fset() {
  static metl::flat_set<tracked, 4> s;
  return s;
}

void flat_set_unregisters() {
  reset_counts();
  auto& s = fset();
  s.clear();
  const auto erase_self = [](int id) { (void)fset().erase(key(id)); };
  s.emplace(tracked(4, erase_self));
  s.emplace(tracked(5, erase_self));
  CHECK(s.erase(key(4)));
  CHECK_EQ(g_destroyed[4], 1);  // not twice
  CHECK_EQ(s.size(), 1u);
  s.clear();
  CHECK_EQ(g_destroyed[5], 1);
  CHECK_EQ(s.size(), 0u);
}

// --- static_unordered_map / static_unordered_set -------------------------------

metl::static_unordered_map<int, tracked, 8>& umap() {
  static metl::static_unordered_map<int, tracked, 8> m;
  return m;
}

void unordered_map_unregisters() {
  reset_counts();
  auto& m = umap();
  m.clear();
  const auto erase_self = [](int id) { (void)umap().erase(id); };
  m.emplace(1, tracked(1, erase_self));
  m.emplace(2, tracked(2, erase_self));

  CHECK(m.erase(1));
  CHECK_EQ(g_destroyed[1], 1);
  CHECK_EQ(m.size(), 1u);
  CHECK(m.contains(2));

  m.clear();
  CHECK_EQ(g_destroyed[2], 1);
  CHECK_EQ(m.size(), 0u);
}

metl::static_unordered_set<tracked, 8, tracked_hash>& uset() {
  static metl::static_unordered_set<tracked, 8, tracked_hash> s;
  return s;
}

void unordered_set_unregisters() {
  reset_counts();
  auto& s = uset();
  s.clear();
  const auto erase_self = [](int id) { (void)uset().erase(key(id)); };
  s.emplace(tracked(6, erase_self));
  s.emplace(tracked(7, erase_self));
  CHECK(s.erase(key(6)));
  CHECK_EQ(g_destroyed[6], 1);
  CHECK_EQ(s.size(), 1u);
  s.clear();
  CHECK_EQ(g_destroyed[7], 1);
  CHECK_EQ(s.size(), 0u);
}

// --- optional / variant ---------------------------------------------------------

metl::optional<tracked>& opt() {
  static metl::optional<tracked> o;
  return o;
}
bool g_still_engaged = false;

void optional_disengages_first() {
  reset_counts();
  opt().emplace(1, [](int) {
    g_still_engaged = opt().has_value();
    opt().reset();  // must be a no-op, not a second destruction
  });
  opt().reset();
  CHECK(!g_still_engaged);
  CHECK_EQ(g_destroyed[1], 1);
  CHECK(!opt().has_value());
}

metl::variant<int, tracked>& var() {
  static metl::variant<int, tracked> v;
  return v;
}
bool g_still_active = false;

void variant_leaves_the_alternative_first() {
  reset_counts();
  var().emplace<tracked>(2, [](int) { g_still_active = metl::holds_alternative<tracked>(var()); });
  var().emplace<int>(5);
  CHECK(!g_still_active);
  CHECK_EQ(g_destroyed[2], 1);
  CHECK(metl::holds_alternative<int>(var()));
}

}  // namespace

int main() {
  flat_map_unregisters();
  flat_set_unregisters();
  unordered_map_unregisters();
  unordered_set_unregisters();
  optional_disengages_first();
  variant_leaves_the_alternative_first();
  return metl_test::exit_code();
}
