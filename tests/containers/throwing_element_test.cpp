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
#include <metl/fixed_priority_queue.hpp>
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

// Like `armed`, but its move can throw and it cannot be move-assigned, which
// sends flat_map / flat_set down their construct-and-destroy shift path.
struct pinned_armed {
  int value;

  explicit pinned_armed(int v = 0) : value(v) { enroll(this); }
  pinned_armed(const pinned_armed& o) : value(o.value) {
    tick();
    enroll(this);
  }
  pinned_armed(pinned_armed&& o) noexcept(false) : value(o.value) {
    tick();
    enroll(this);
  }
  pinned_armed& operator=(const pinned_armed&) = delete;
  pinned_armed& operator=(pinned_armed&&) = delete;
  ~pinned_armed() { retire(this); }
  bool operator<(const pinned_armed& o) const { return value < o.value; }
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

  // ---- G.6: fixed_vector::swap with unequal sizes, tail move throwing -------
  for (int direction = 0; direction < 2; ++direction) {
    g_live = 0;
    g_double_destroys = 0;
    {
      metl::fixed_vector<armed, 8> shorter;
      metl::fixed_vector<armed, 8> longer;
      shorter.emplace_back(1);
      for (int i = 0; i < 5; ++i) {
        longer.emplace_back(10 + i);
      }
      // Swapping the one common element costs three operations (a move and two
      // move-assignments); the next two tail moves succeed and the third throws.
      g_countdown = 5;
      bool threw = false;
      try {
        if (direction == 0) {
          shorter.swap(longer);
        } else {
          longer.swap(shorter);
        }
      } catch (int) {
        threw = true;
      }
      g_countdown = -1;
      CHECK(threw);
    }
    CHECK_EQ(g_live, 0);  // used to leak the two elements moved before the throw
    CHECK_EQ(g_double_destroys, 0);
  }

  // ---- G.6: flat_map / flat_set with a non-move-assignable element ---------
  for (int which = 0; which < 2; ++which) {
    g_live = 0;
    g_double_destroys = 0;
    {
      metl::flat_map<int, pinned_armed, 8> map;
      metl::flat_set<pinned_armed, 8> set;
      for (int i = 1; i <= 5; ++i) {
        map.emplace(i * 10, pinned_armed(i));
        set.emplace(pinned_armed(i * 10));
      }
      g_countdown = 3;
      bool threw = false;
      try {
        if (which == 0) {
          map.emplace(0, pinned_armed(0));
        } else {
          set.emplace(pinned_armed(0));
        }
      } catch (int) {
        threw = true;
      }
      g_countdown = -1;
      CHECK(threw);
      CHECK(which == 0 ? map.empty() : set.empty());
    }
    CHECK_EQ(g_live, 0);
    CHECK_EQ(g_double_destroys, 0);
  }

  // ---- G.6: fixed_priority_queue keeps its heap order or empties ----------
  {
    g_live = 0;
    g_double_destroys = 0;
    {
      metl::fixed_priority_queue<armed, 16> queue;
      for (int i = 1; i <= 7; ++i) {
        queue.push(armed(i));
      }
      g_countdown = 3;  // throws inside the sift
      bool threw = false;
      try {
        queue.push(armed(100));
      } catch (int) {
        threw = true;
      }
      g_countdown = -1;
      CHECK(threw);
      // Either empty, or every pop comes out in non-increasing order.
      int previous = 1 << 30;
      bool ordered = true;
      while (!queue.empty()) {
        const int top = queue.top().value;
        ordered = ordered && top <= previous;
        previous = top;
        queue.pop();
      }
      CHECK(ordered);
    }
    CHECK_EQ(g_live, 0);
    CHECK_EQ(g_double_destroys, 0);
  }

  return metl_test::exit_code();
}
