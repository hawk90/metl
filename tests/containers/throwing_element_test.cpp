// Containers must stay memory-safe when an element's copy or move throws
// (docs/AUDIT.md G.5). Exceptions-enabled builds only: METL itself never
// throws, but a user's T may, and these paths used to
//
//   - leak every element already copied, when a copy or move CONSTRUCTOR of
//     the container threw part-way (the destructor never runs on an object
//     whose constructor did not finish);
//   - destroy a slot twice, when flat_map / flat_set's insert shifted elements
//     with construct-then-destroy and a move threw mid-shift.
//
// `armed` registers its own address while alive. A leak leaves an address
// registered; a double destroy is a destructor for an address that is not.
// (A magic field poisoned in the destructor does not work: that store is dead
// at the end of the lifetime and the optimiser removes it. And a plain live
// count can hide a leak and a double destroy that cancel out -- exactly what
// the old shift did.)

#include "metl_check.hpp"

#include <cstddef>

#include <metl/detail/ring_core.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_message_queue.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

int g_live = 0;
int g_double_destroys = 0;
const void* g_registry[64] = {};

void enroll(const void* self) {
  for (auto& slot : g_registry) {
    if (slot == nullptr) {
      slot = self;
      ++g_live;
      return;
    }
  }
}

void retire(const void* self) {
  for (auto& slot : g_registry) {
    if (slot == self) {
      slot = nullptr;
      --g_live;
      return;
    }
  }
  ++g_double_destroys;  // destroying something that is not alive
}
int g_countdown = -1;  ///< throws when it reaches 0; negative = never

void tick() {
  if (g_countdown > 0) {
    --g_countdown;
  } else if (g_countdown == 0) {
    g_countdown = -1;
    throw 42;
  }
}

struct armed {
  int value;

  explicit armed(int v = 0) : value(v) { enroll(this); }
  armed(const armed& o) : value(o.value) {
    tick();
    enroll(this);
  }
  armed(armed&& o) noexcept(false) : value(o.value) {
    tick();
    enroll(this);
  }
  armed& operator=(const armed& o) {
    tick();
    value = o.value;
    return *this;
  }
  armed& operator=(armed&& o) noexcept(false) {
    tick();
    value = o.value;
    return *this;
  }
  ~armed() { retire(this); }
  bool operator<(const armed& o) const { return value < o.value; }
  bool operator==(const armed& o) const { return value == o.value; }
};

struct armed_hash {
  std::size_t operator()(const armed& a) const noexcept {
    return static_cast<std::size_t>(a.value) * 2654435761u;
  }
  std::size_t operator()(int v) const noexcept { return static_cast<std::size_t>(v) * 2654435761u; }
};

// Builds `source` with five elements, then copy- or move-constructs from it
// with the third element's copy throwing. Nothing may leak.
template <typename Container, typename Fill>
void check_constructor_throw(Fill fill) {
  for (int move = 0; move < 2; ++move) {
    g_live = 0;
    g_double_destroys = 0;
    {
      Container source;
      fill(source);
      const int before = g_live;
      g_countdown = 2;
      bool threw = false;
      try {
        if (move != 0) {
          Container target(static_cast<Container&&>(source));
          (void)target;
        } else {
          Container target(source);
          (void)target;
        }
      } catch (int) {
        threw = true;
      }
      g_countdown = -1;
      CHECK(threw);
      CHECK_EQ(g_live, before);  // the partly-built target released everything
    }
    CHECK_EQ(g_live, 0);
    CHECK_EQ(g_double_destroys, 0);
  }
}

}  // namespace

int main() {
  // ---- constructors --------------------------------------------------------
  check_constructor_throw<metl::fixed_vector<armed, 8>>([](auto& c) {
    for (int i = 0; i < 5; ++i)
      c.emplace_back(i);
  });
  check_constructor_throw<metl::ring_buffer<armed, 8>>([](auto& c) {
    for (int i = 0; i < 5; ++i)
      c.emplace_back(i);
  });
  check_constructor_throw<metl::fixed_deque<armed, 8>>([](auto& c) {
    for (int i = 0; i < 5; ++i)
      c.emplace_back(i);
  });
  check_constructor_throw<metl::static_message_queue<armed, 8>>([](auto& c) {
    for (int i = 0; i < 5; ++i)
      c.emplace(i);
  });
  check_constructor_throw<metl::flat_map<int, armed, 8>>([](auto& c) {
    for (int i = 0; i < 5; ++i)
      c.emplace(i, armed(i));
  });
  check_constructor_throw<metl::flat_set<armed, 8>>([](auto& c) {
    for (int i = 0; i < 5; ++i)
      c.emplace(armed(i));
  });
  check_constructor_throw<metl::static_unordered_map<int, armed, 8>>([](auto& c) {
    for (int i = 0; i < 5; ++i)
      c.emplace(i, armed(i));
  });
  check_constructor_throw<metl::static_unordered_set<armed, 8, armed_hash>>([](auto& c) {
    for (int i = 0; i < 5; ++i)
      c.emplace(armed(i));
  });

  // ---- flat_map / flat_set insert with a move throwing mid-shift -----------
  // Insert at the front of five elements, failing on the third relocation.
  for (int which = 0; which < 2; ++which) {
    g_live = 0;
    g_double_destroys = 0;
    {
      metl::flat_map<int, armed, 8> map;
      metl::flat_set<armed, 8> set;
      for (int i = 1; i <= 5; ++i) {
        map.emplace(i * 10, armed(i));
        set.emplace(armed(i * 10));
      }
      g_countdown = 3;
      bool threw = false;
      try {
        if (which == 0) {
          map.emplace(0, armed(0));
        } else {
          set.emplace(armed(0));
        }
      } catch (int) {
        threw = true;
      }
      g_countdown = -1;
      CHECK(threw);
      // The invariant is restored by clearing, as std::flat_map does.
      if (which == 0) {
        CHECK(map.empty());
      } else {
        CHECK(set.empty());
      }
    }
    CHECK_EQ(g_live, 0);
    CHECK_EQ(g_double_destroys, 0);
  }

  return metl_test::exit_code();
}
