// Containers must stay memory-safe when an element's copy or move throws.
// Exceptions-enabled builds only: METL itself never
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

#include <array>
#include <cstddef>
#include <utility>

#include <metl/detail/ring_core.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/fsm.hpp>
#include <metl/lookup_table.hpp>
#include <metl/mpmc_queue.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/spsc_queue.hpp>
#include <metl/static_message_queue.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

// Every case here throws; without exceptions there is nothing to run.
#if !METL_NO_EXCEPTIONS

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
int g_moves = 0;       ///< move constructions of `armed`, for the rebuild check

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
    ++g_moves;
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

// A hasher that throws on demand through tick().
struct throwing_int_hash {
  std::size_t operator()(int k) const {
    tick();
    return static_cast<std::size_t>(k) * 2654435761u;
  }
};

// A key whose `<` is noexcept but not a scalar: std::less over it must keep the
// containers' lookups noexcept (detail::nothrow_call looks through std::less).
struct ranked {
  int rank;
  bool operator<(const ranked& o) const noexcept { return rank < o.rank; }
};

// Ordering for the priority-queue pop check: throws on demand through tick().
struct throwing_int_less {
  bool operator()(int a, int b) const {
    tick();
    return a < b;
  }
};

// Copy can throw; move cannot -- what the lock-free queues require of T.
struct copy_throws {
  int value = 0;
  copy_throws() = default;
  explicit copy_throws(int v) noexcept : value(v) {}
  copy_throws(const copy_throws& o) : value(o.value) { tick(); }
  copy_throws(copy_throws&&) noexcept = default;
  copy_throws& operator=(const copy_throws& o) {
    tick();
    value = o.value;
    return *this;
  }
  copy_throws& operator=(copy_throws&&) noexcept = default;
  ~copy_throws() = default;
  bool operator==(const copy_throws& o) const { return value == o.value; }
};

enum class fsm_state { idle, busy };
enum class fsm_event { go };
void throwing_action(fsm_state, fsm_event, fsm_state) {
  throw 42;
}

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

  // ---- fixed_vector::swap with unequal sizes, tail move throwing -------
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

  // ---- flat_map / flat_set with a non-move-assignable element ---------
  // Every element move of the insert in turn: building the entry, each step of
  // the shift, and the final move of the entry into the gap. Only throwing at
  // one of them left the last one, whose failure destroyed a dead slot and
  // leaked a live one, untested (#258).
  for (int which = 0; which < 2; ++which) {
    for (int countdown = 0;; ++countdown) {
      g_live = 0;
      g_double_destroys = 0;
      bool threw = false;
      {
        metl::flat_map<int, pinned_armed, 8> map;
        metl::flat_set<pinned_armed, 8> set;
        for (int i = 1; i <= 5; ++i) {
          map.emplace(i * 10, pinned_armed(i));
          set.emplace(pinned_armed(i * 10));
        }
        g_countdown = countdown;
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
        if (threw) {
          // Cleared, or untouched when the throw came before anything moved.
          const std::size_t size = which == 0 ? map.size() : set.size();
          CHECK(size == 0 || size == 5);
        }
      }
      CHECK_EQ(g_live, 0);
      CHECK_EQ(g_double_destroys, 0);
      if (!threw) {
        CHECK(countdown > 3);  // the insert performs at least the moves above
        break;
      }
    }
  }

  // ---- fixed_priority_queue keeps its heap order or empties ----------
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

  // ---- fixed_priority_queue::pop with a throwing comparator --------------
  // pop() was noexcept whenever T's move-assignment was, though it also calls
  // the comparator and T's move constructor. For an int queue with a comparator
  // that throws, the throw terminated instead of clearing and rethrowing.
  static_assert(!noexcept(std::declval<metl::fixed_priority_queue<int, 8, throwing_int_less>&>().pop()),
                "pop must not promise noexcept when the comparator can throw");
  static_assert(noexcept(std::declval<metl::fixed_priority_queue<int, 8>&>().pop()),
                "pop keeps noexcept for int with the default comparator");
  {
    metl::fixed_priority_queue<int, 8, throwing_int_less> queue;
    for (int i = 1; i <= 6; ++i) {
      queue.push(i);
    }
    g_countdown = 1;  // the second comparison inside pop throws
    bool threw = false;
    try {
      queue.pop();
    } catch (int) {
      threw = true;
    }
    g_countdown = -1;
    CHECK(threw);
    CHECK(queue.empty());  // cleared, not left with a broken heap
  }

  // ---- noexcept paths that run user code now propagate -------------
  // Before, each of these was noexcept and a throwing T terminated the program.
  static_assert(!noexcept(std::declval<metl::spsc_queue<copy_throws, 4>&>().try_push(
                    std::declval<const copy_throws&>())),
                "a throwing copy must not be hidden behind noexcept");
  static_assert(noexcept(std::declval<metl::spsc_queue<int, 4>&>().try_push(1)),
                "a nothrow T keeps the noexcept guarantee");
  static_assert(noexcept(std::declval<metl::mpmc_queue<int, 4>&>().try_push(1)),
                "a nothrow T keeps the noexcept guarantee");
  {
    metl::spsc_queue<copy_throws, 4> queue;
    const copy_throws item(5);
    g_countdown = 0;
    bool threw = false;
    try {
      (void)queue.try_push(item);
    } catch (int) {
      threw = true;
    }
    g_countdown = -1;
    CHECK(threw);
    CHECK(queue.empty());  // nothing was published
    CHECK(queue.try_push(copy_throws(7)));
  }
  {
    metl::mpmc_queue<copy_throws, 4> queue;
    const copy_throws item(5);
    g_countdown = 0;
    bool threw = false;
    try {
      (void)queue.try_push(item);
    } catch (int) {
      threw = true;
    }
    g_countdown = -1;
    CHECK(threw);
    // No ticket was claimed: the queue is still usable end to end. (A claimed,
    // never-published slot would make this pop spin as "not ready".)
    CHECK(queue.try_push(copy_throws(7)));
    copy_throws out;
    CHECK(queue.try_pop(out));
    CHECK_EQ(out.value, 7);
  }
  {
    const auto table = metl::make_lookup_table<int, copy_throws, 2>(
        std::array<metl::lookup_entry<int, copy_throws>, 2>{{{1, copy_throws(10)}, {2, copy_throws(20)}}});
    g_countdown = 0;
    bool threw = false;
    try {
      (void)table.value_or(1, copy_throws(0));
    } catch (int) {
      threw = true;
    }
    g_countdown = -1;
    CHECK(threw);
  }
  {
    const std::array<metl::fsm_transition<fsm_state, fsm_event>, 1> table{
        {{fsm_state::idle,
          fsm_event::go,
          fsm_state::busy,
          metl::delegate<void(fsm_state, fsm_event, fsm_state)>::from_function<&throwing_action>()}}};
    metl::fsm<fsm_state, fsm_event, 1> machine(fsm_state::idle, table);
    bool threw = false;
    try {
      (void)machine.dispatch(fsm_event::go);
    } catch (int) {
      threw = true;
    }
    CHECK(threw);
    CHECK(machine.current_state() == fsm_state::busy);
  }
  // An unordered map of a type whose move can throw no longer runs the
  // noexcept rebuild, which terminated the program on such a throw. The
  // rebuild is visible as an insert that moves many elements; an insert that
  // does not rebuild moves exactly one (into its slot).
  {
    g_live = 0;
    g_double_destroys = 0;
    {
      metl::static_unordered_map<int, armed, 16> map;
      int most_moves_in_one_insert = 0;
      for (int round = 0; round < 20; ++round) {
        for (int k = 0; k < 10; ++k) {
          armed value(k);
          const int before = g_moves;
          CHECK(map.try_emplace(round * 100 + k, static_cast<armed&&>(value)));
          const int moved = g_moves - before;
          most_moves_in_one_insert = moved > most_moves_in_one_insert ? moved : most_moves_in_one_insert;
        }
        for (int k = 0; k < 10; ++k) {
          CHECK(map.erase(round * 100 + k));
        }
      }
      CHECK_EQ(most_moves_in_one_insert, 1);  // never rebuilt
      CHECK(map.empty());
      CHECK(map.try_emplace(5, armed(5)));
      CHECK(map.find(5) != nullptr);
    }
    CHECK_EQ(g_live, 0);
  }

  // ---- lookups and erase are noexcept only when user code cannot throw --
  using int_map = metl::flat_map<int, int, 8>;
  using ranked_set = metl::flat_set<ranked, 8>;
  using throwing_map = metl::flat_map<int, int, 8, throwing_int_less>;
  using int_table = metl::static_unordered_map<int, int, 8>;
  using throwing_table = metl::static_unordered_map<int, int, 8, throwing_int_hash>;
  static_assert(noexcept(std::declval<const int_map&>().find(1)), "default comparator keeps noexcept");
  static_assert(noexcept(std::declval<int_map&>().erase(1)), "nothrow-movable entries keep erase noexcept");
  static_assert(noexcept(std::declval<const ranked_set&>().contains(ranked{1})),
                "std::less over a noexcept operator< keeps noexcept");
  static_assert(noexcept(std::declval<const int_table&>().find(1)),
                "std::hash / std::equal_to keep noexcept");
  static_assert(!noexcept(std::declval<const throwing_map&>().find(1)), "a throwing comparator propagates");
  static_assert(!noexcept(std::declval<const throwing_table&>().find(1)), "a throwing hasher propagates");
  static_assert(!noexcept(std::declval<metl::flat_map<int, armed, 8>&>().erase(1)),
                "erase relocating a throwing-move element propagates");
  static_assert(noexcept(std::declval<metl::fixed_priority_queue<ranked, 8>&>().pop()),
                "pop keeps noexcept for std::less over a noexcept operator<");

  // A throwing comparator in a lookup propagates (it terminated before).
  {
    throwing_map map;
    (void)map.try_emplace(1, 10);
    (void)map.try_emplace(2, 20);
    g_countdown = 0;
    bool threw = false;
    try {
      (void)map.find(2);
    } catch (int) {
      threw = true;
    }
    g_countdown = -1;
    CHECK(threw);
    CHECK_EQ(map.size(), 2u);  // a lookup changes nothing
  }

  // flat_map::erase whose relocation throws: cleared, nothing leaked or
  // destroyed twice (the move ran inside a noexcept erase_at before).
  {
    g_live = 0;
    g_double_destroys = 0;
    {
      metl::flat_map<int, armed, 8> map;
      for (int i = 0; i < 5; ++i) {
        map.emplace(i, armed(i));
      }
      g_countdown = 1;  // the second relocation throws
      bool threw = false;
      try {
        (void)map.erase(0);
      } catch (int) {
        threw = true;
      }
      g_countdown = -1;
      CHECK(threw);
      CHECK(map.empty());
    }
    CHECK_EQ(g_live, 0);
    CHECK_EQ(g_double_destroys, 0);
  }

  // A hasher that throws in the middle of the tombstone rebuild: the table is
  // emptied and the exception propagates (rehash_in_place was noexcept).
  {
    throwing_table table;
    for (int k = 0; k < 6; ++k) {
      CHECK(table.try_emplace(k, k));
    }
    for (int k = 0; k < 3; ++k) {
      CHECK(table.erase(k));  // 3 tombstones > bucket_count / 8, so the next new key rebuilds
    }
    g_countdown = 2;  // call 1: locating the new key; calls 2.. : the rebuild -> throws on the 2nd element
    bool threw = false;
    try {
      (void)table.try_emplace(100, 100);
    } catch (int) {
      threw = true;
    }
    g_countdown = -1;
    CHECK(threw);
    CHECK(table.empty());
    CHECK(table.try_emplace(7, 70));  // and it is still usable
    CHECK(table.find(7) != nullptr);
  }

  return metl_test::exit_code();
}

#else

int main() {
  return metl_test::skip("throwing_element_test", "built without exceptions");
}

#endif  // !METL_NO_EXCEPTIONS
