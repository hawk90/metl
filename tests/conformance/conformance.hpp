#pragma once

// The conformance suite (#320): one property, written once, checked against
// every type that holds user objects.
//
// Earlier fixes landed in the types a bug was reported against, and the same
// class turned up again in a sibling: destruction before the removal was
// committed was fixed for the containers (#190) and found again in the arena
// (#312). Each property here runs over the whole registry below, so a type
// added to METL is checked for every property by default, and
// tools/check_conformance.py fails when a public header is neither registered
// nor listed with a reason.
//
// A registered type that does not yet meet a property names the issue in its
// adapter (`gap_<property>`). The property still runs. While it fails, the
// failure is reported as that known gap and does not fail the test. Once it
// passes, the test fails until the entry is deleted, so the list stays true.
// Every property must therefore observe without crashing, even on the broken
// type: it records what it sees and checks afterwards.
//
// Adding a type: write an adapter in this file, add it to `registry`, and tag
// its header with a `// conformance-header:` line next to the adapter.

#include "metl_check.hpp"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include <metl/arena_allocator.hpp>
#include <metl/expected.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_function.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/handle_pool.hpp>
#include <metl/mpmc_queue.hpp>
#include <metl/object_pool.hpp>
#include <metl/optional.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/spsc_queue.hpp>
#include <metl/static_message_queue.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>
#include <metl/variant.hpp>

namespace conformance {

// ---- The element ------------------------------------------------------------

// A user object that counts itself. Only an armed probe is live: moving one
// disarms the source, so relocation inside a container is not counted, and a
// probe built only to look one up (`probe::key`) is never armed.
struct probe {
  int id = 0;
  bool armed = false;
  // Set while the object is within its lifetime. A destructor that finds it
  // cleared is running a second time on the same object.
  int alive = alive_tag;
  static constexpr int alive_tag = 0x5AFE;

  // How many armed probes exist.
  static int& live() {
    static int count = 0;
    return count;
  }
  // How many destructor calls ran on an object already destroyed.
  static int& destroyed_twice() {
    static int count = 0;
    return count;
  }
  // Called from every armed probe's destructor, after `live()` has dropped by one.
  static void (*&on_destroy())(const probe&) {
    static void (*hook)(const probe&) = nullptr;
    return hook;
  }

  probe() = default;
  explicit probe(int i) : id(i), armed(true) { ++live(); }
  probe(const probe& other) : id(other.id), armed(other.armed) {
    if (armed) {
      ++live();
    }
  }
  probe(probe&& other) noexcept : id(other.id), armed(other.armed) { other.armed = false; }
  probe& operator=(const probe& other) {
    if (this != &other) {
      drop();
      id = other.id;
      armed = other.armed;
      if (armed) {
        ++live();
      }
    }
    return *this;
  }
  probe& operator=(probe&& other) noexcept {
    if (this != &other) {
      drop();
      id = other.id;
      armed = other.armed;
      other.armed = false;
    }
    return *this;
  }
// GCC 16 at -O2 reports the reads below as maybe-uninitialized when the probe
// lives in an optional's union storage: std::optional<probe> warns after just
// emplace() and reset(), and metl::optional the same. The probe was built; the
// volatile read is what detects a second destruction, so it stays.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
  ~probe() {
    if (*static_cast<volatile int*>(&alive) != alive_tag) {
      ++destroyed_twice();
      return;
    }
    const bool was_armed = armed;
    drop();
    // Ends the lifetime before the hook re-enters the container. Touching a
    // member after the hook would be equally valid, but fixed_vector's ASan
    // annotations then report a false use-after-poison (#321). Volatile, so
    // the store survives GCC's dead-store elimination of a dying object.
    *static_cast<volatile int*>(&alive) = 0;
    if (was_armed && on_destroy() != nullptr) {
      on_destroy()(*this);
    }
  }

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

  static probe key(int i) {
    probe unarmed;
    unarmed.id = i;
    return unarmed;
  }

  friend bool operator<(const probe& lhs, const probe& rhs) { return lhs.id < rhs.id; }
  friend bool operator==(const probe& lhs, const probe& rhs) { return lhs.id == rhs.id; }

 private:
  // Assigning over a probe ends the value it held but runs no destructor, so
  // it counts as one fewer live probe without calling the hook. The properties
  // are about destructor calls; pop on a heap overwrites the top by design.
  void drop() noexcept {
    if (armed) {
      armed = false;
      --live();
    }
  }
};

struct probe_hash {
  std::size_t operator()(const probe& p) const noexcept { return std::hash<int>{}(p.id); }
};

// ---- Adapters -----------------------------------------------------------------
//
// Each adapter gives the properties one vocabulary:
//
//   type, name, capacity     the instantiation under test, at a small capacity
//   instance()               a function-local static, so a destructor can find it
//   try_add(c, id)           add probe(id); false when full
//   held(c)                  how many probes `c` still counts as its own
//   remove_one(c)            remove exactly one probe, whichever the type picks
//   clear(c)                 remove every probe
//   movable                  move construction is part of the type's contract
//   move_empties_source      moving leaves the source holding nothing; the
//                            vocabulary types keep the source's state, as std
//                            does, and only its value is moved from
//   fixed_count              held() is an exact element count and capacity a
//                            number of elements, not of bytes
//   zero_type                the same type at capacity 0, or instead
//   zero_capacity_refused    how the type refuses capacity 0
//   available                optional, true by default: false where the type
//                            does not build for this target
//
// The functions are templates, so they serve `type` and `zero_type` alike.
//
// and optionally `gap_<property>`, the issue that tracks a known failure.

// conformance-header: fixed_vector.hpp
struct fixed_vector_adapter {
  using type = metl::fixed_vector<probe, 4>;
  static constexpr const char* name = "fixed_vector";
  using zero_type = metl::fixed_vector<probe, 0>;
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = true;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    return c.try_emplace_back(id);
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size();
  }
  template <typename C>
  static void remove_one(C& c) {
    c.pop_back();
  }
  template <typename C>
  static void clear(C& c) {
    c.clear();
  }
};

// conformance-header: fixed_deque.hpp
struct fixed_deque_adapter {
  using type = metl::fixed_deque<probe, 4>;
  static constexpr const char* name = "fixed_deque";
  using zero_type = metl::fixed_deque<probe, 0>;
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = true;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    return c.try_emplace_back(id);
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size();
  }
  template <typename C>
  static void remove_one(C& c) {
    c.pop_front();
  }
  template <typename C>
  static void clear(C& c) {
    c.clear();
  }
};

// conformance-header: ring_buffer.hpp
struct ring_buffer_adapter {
  using type = metl::ring_buffer<probe, 4>;
  static constexpr const char* name = "ring_buffer";
  using zero_type = metl::ring_buffer<probe, 0>;
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = true;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    return c.try_emplace_back(id);
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size();
  }
  template <typename C>
  static void remove_one(C& c) {
    c.pop_front();
  }
  template <typename C>
  static void clear(C& c) {
    c.clear();
  }
};

// conformance-header: fixed_queue.hpp
struct fixed_queue_adapter {
  using type = metl::fixed_queue<probe, 4>;
  static constexpr const char* name = "fixed_queue";
  using zero_type = metl::fixed_queue<probe, 0>;
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = true;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    return c.try_emplace(id);
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size();
  }
  template <typename C>
  static void remove_one(C& c) {
    c.pop();
  }
  template <typename C>
  static void clear(C& c) {
    c.clear();
  }
};

// conformance-header: fixed_stack.hpp
struct fixed_stack_adapter {
  using type = metl::fixed_stack<probe, 4>;
  static constexpr const char* name = "fixed_stack";
  using zero_type = metl::fixed_stack<probe, 0>;
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = true;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    return c.try_emplace(id);
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size();
  }
  template <typename C>
  static void remove_one(C& c) {
    c.pop();
  }
  template <typename C>
  static void clear(C& c) {
    c.clear();
  }
};

// conformance-header: fixed_priority_queue.hpp
struct fixed_priority_queue_adapter {
  using type = metl::fixed_priority_queue<probe, 4>;
  static constexpr const char* name = "fixed_priority_queue";
  using zero_type = metl::fixed_priority_queue<probe, 0>;
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = true;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    return c.try_emplace(id);
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size();
  }
  template <typename C>
  static void remove_one(C& c) {
    c.pop();
  }
  template <typename C>
  static void clear(C& c) {
    c.clear();
  }
};

// conformance-header: static_message_queue.hpp
struct static_message_queue_adapter {
  using type = metl::static_message_queue<probe, 4>;
  static constexpr const char* name = "static_message_queue";
  static constexpr const char* zero_capacity_refused = "a static_assert: it could hold nothing";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = true;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    return c.try_emplace(id);
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size();
  }
  template <typename C>
  static void remove_one(C& c) {
    c.pop();
  }
  template <typename C>
  static void clear(C& c) {
    c.clear();
  }
};

// conformance-header: flat_map.hpp
struct flat_map_adapter {
  using type = metl::flat_map<int, probe, 4>;
  static constexpr const char* name = "flat_map";
  using zero_type = metl::flat_map<int, probe, 0>;
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = true;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    return c.try_emplace(id, probe(id));
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size();
  }
  template <typename C>
  static void remove_one(C& c) {
    (void)c.erase(c.begin()->key);
  }
  template <typename C>
  static void clear(C& c) {
    c.clear();
  }
};

// conformance-header: flat_set.hpp
struct flat_set_adapter {
  using type = metl::flat_set<probe, 4>;
  static constexpr const char* name = "flat_set";
  using zero_type = metl::flat_set<probe, 0>;
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = true;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    return c.try_emplace(probe(id));
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size();
  }
  template <typename C>
  static void remove_one(C& c) {
    (void)c.erase(probe::key(c.begin()->id));
  }
  template <typename C>
  static void clear(C& c) {
    c.clear();
  }
};

// conformance-header: static_unordered_map.hpp
struct static_unordered_map_adapter {
  using type = metl::static_unordered_map<int, probe, 4>;
  static constexpr const char* name = "static_unordered_map";
  using zero_type = metl::static_unordered_map<int, probe, 0>;
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = true;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    return c.try_emplace(id, probe(id));
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size();
  }
  template <typename C>
  static void remove_one(C& c) {
    (void)c.erase(c.begin()->key);
  }
  template <typename C>
  static void clear(C& c) {
    c.clear();
  }
};

// conformance-header: static_unordered_set.hpp
struct static_unordered_set_adapter {
  using type = metl::static_unordered_set<probe, 4, probe_hash>;
  static constexpr const char* name = "static_unordered_set";
  using zero_type = metl::static_unordered_set<probe, 0, probe_hash>;
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = true;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    return c.try_emplace(probe(id));
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size();
  }
  template <typename C>
  static void remove_one(C& c) {
    (void)c.erase(probe::key(c.begin()->id));
  }
  template <typename C>
  static void clear(C& c) {
    c.clear();
  }
};

// The pools and the arena hand out addresses instead of holding a sequence, so
// their adapters remember what they were given, newest last.
template <typename Handle, std::size_t N>
struct handed_out {
  Handle items[N] = {};
  std::size_t count = 0;
  void push(Handle h) { items[count++] = h; }
  Handle pop() { return items[--count]; }
};

// conformance-header: object_pool.hpp
struct object_pool_adapter {
  using type = metl::object_pool<probe, 4>;
  static constexpr const char* name = "object_pool";
  using zero_type = metl::object_pool<probe, 0>;
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = false;
  static constexpr bool move_empties_source = false;
  static constexpr bool fixed_count = true;
  // One list per instantiation: `type` and `zero_type` hand out separately.
  template <typename C>
  static handed_out<probe*, 4>& given() {
    static handed_out<probe*, 4> list;
    return list;
  }
  template <typename C>
  static bool try_add(C& c, int id) {
    probe* object = c.try_emplace(id);
    if (object != nullptr) {
      given<C>().push(object);
    }
    return object != nullptr;
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size();
  }
  template <typename C>
  static void remove_one(C& c) {
    (void)c.destroy(given<C>().pop());
  }
  template <typename C>
  static void clear(C& c) {
    given<C>().count = 0;
    c.clear();
  }
};

// conformance-header: handle_pool.hpp
struct handle_pool_adapter {
  using type = metl::handle_pool<probe, 4>;
  static constexpr const char* name = "handle_pool";
  using zero_type = metl::handle_pool<probe, 0>;
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = false;
  static constexpr bool move_empties_source = false;
  static constexpr bool fixed_count = true;
  template <typename C>
  static handed_out<typename C::handle_type, 4>& given() {
    static handed_out<typename C::handle_type, 4> list;
    return list;
  }
  template <typename C>
  static bool try_add(C& c, int id) {
    const typename C::handle_type handle = c.try_emplace(id);
    if (handle.valid()) {
      given<C>().push(handle);
    }
    return handle.valid();
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size();
  }
  template <typename C>
  static void remove_one(C& c) {
    (void)c.destroy(given<C>().pop());
  }
  template <typename C>
  static void clear(C& c) {
    given<C>().count = 0;
    c.clear();
  }
};

// The arena counts bytes, not objects. held() approximates: an object is held
// while the offset is above the mark taken just before it was allocated. The
// storage can only be released after the destructor returns, so the arena
// cannot meet removal_commits_first by that measure; what it must meet is
// reentrant_clear_destroys_once.
// conformance-header: arena_allocator.hpp
struct arena_allocator_adapter {
  using type = metl::arena_allocator<256>;
  static constexpr const char* name = "arena_allocator";
  using zero_type = metl::arena_allocator<0>;
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = false;
  static constexpr bool move_empties_source = false;
  static constexpr bool fixed_count = false;
  template <typename C>
  static handed_out<typename C::mark_type, 4>& given() {
    static handed_out<typename C::mark_type, 4> list;
    return list;
  }
  template <typename C>
  static bool try_add(C& c, int id) {
    if (given<C>().count == capacity) {
      return false;
    }
    const typename C::mark_type before = c.mark();
    if (c.template try_emplace<probe>(id) == nullptr) {
      return false;
    }
    given<C>().push(before);
    return true;
  }
  template <typename C>
  static std::size_t held(const C& c) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < given<C>().count; ++i) {
      count += c.used() > given<C>().items[i].offset ? std::size_t{1} : std::size_t{0};
    }
    return count;
  }
  template <typename C>
  static void remove_one(C& c) {
    const typename C::mark_type target = given<C>().items[given<C>().count - 1];
    c.rewind(target);
    --given<C>().count;
  }
  template <typename C>
  static void clear(C& c) {
    c.reset();
    given<C>().count = 0;
  }
};

// The queues: a try_pop moves the element out, and it ends when the local it
// was moved into does.
template <typename Queue>
void pop_one(Queue& c) {
  probe out;
  (void)c.try_pop(out);
}

// conformance-header: spsc_queue.hpp
struct spsc_queue_adapter {
  using type = metl::spsc_queue<probe, 4>;
  static constexpr const char* name = "spsc_queue";
  static constexpr const char* zero_capacity_refused = "a static_assert: Capacity is at least 2";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = false;
  static constexpr bool move_empties_source = false;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    return c.try_emplace(id);
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size_approx();
  }
  template <typename C>
  static void remove_one(C& c) {
    pop_one(c);
  }
  template <typename C>
  static void clear(C& c) {
    while (!c.empty()) {
      pop_one(c);
    }
  }
};

// conformance-header: mpmc_queue.hpp
struct mpmc_queue_adapter {
  using type = metl::mpmc_queue<probe, 4>;
  static constexpr const char* name = "mpmc_queue";
  // mpmc_queue refuses, at compile time, a target with no lock-free atomic
  // read-modify-write on std::size_t (ARMv6-M); it is checked where it builds.
  static constexpr bool available = std::atomic<std::size_t>::is_always_lock_free;
  static constexpr const char* zero_capacity_refused = "a static_assert: Capacity is at least 2";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = false;
  static constexpr bool move_empties_source = false;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    return c.try_emplace(id);
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.size_approx();
  }
  template <typename C>
  static void remove_one(C& c) {
    pop_one(c);
  }
  template <typename C>
  static void clear(C& c) {
    while (!c.empty()) {
      pop_one(c);
    }
  }
};

// The vocabulary types hold at most one object: capacity 1, and try_add fails
// while one is held, as for a full container. They have no capacity parameter.

// conformance-header: optional.hpp
struct optional_adapter {
  using type = metl::optional<probe>;
  static constexpr const char* name = "optional";
  static constexpr const char* zero_capacity_refused = "no capacity parameter: it holds at most one";
  static constexpr std::size_t capacity = 1;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = false;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    if (c.has_value()) {
      return false;
    }
    c.emplace(id);
    return true;
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.has_value() ? 1 : 0;
  }
  template <typename C>
  static void remove_one(C& c) {
    c.reset();
  }
  template <typename C>
  static void clear(C& c) {
    c.reset();
  }
};

// An expected that starts in the error state, which here means "holds no
// object": a default-constructed expected<probe, int> would hold a probe.
struct expected_slot : metl::expected<probe, int> {
  expected_slot() noexcept : metl::expected<probe, int>(metl::unexpect, 0) {}
};

// conformance-header: expected.hpp
struct expected_adapter {
  using type = expected_slot;
  static constexpr const char* name = "expected";
  // Switching state destroys the old member while has_value() still names it.
  static constexpr const char* gap_removal_commits_first = "#374";
  static constexpr const char* gap_reentrant_clear_destroys_once = "#374";
  static constexpr const char* zero_capacity_refused = "no capacity parameter: it holds at most one";
  static constexpr std::size_t capacity = 1;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = false;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    if (c.has_value()) {
      return false;
    }
    c.emplace(id);
    return true;
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c.has_value() ? 1 : 0;
  }
  template <typename C>
  static void remove_one(C& c) {
    c.emplace_error(0);
  }
  template <typename C>
  static void clear(C& c) {
    c.emplace_error(0);
  }
};

// conformance-header: variant.hpp
struct variant_adapter {
  using type = metl::variant<int, probe>;
  static constexpr const char* name = "variant";
  static constexpr const char* zero_capacity_refused = "no capacity parameter: it holds at most one";
  static constexpr std::size_t capacity = 1;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = false;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    if (metl::holds_alternative<probe>(c)) {
      return false;
    }
    c.template emplace<probe>(id);
    return true;
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return metl::holds_alternative<probe>(c) ? 1 : 0;
  }
  template <typename C>
  static void remove_one(C& c) {
    c.template emplace<int>(0);
  }
  template <typename C>
  static void clear(C& c) {
    c.template emplace<int>(0);
  }
};

// A probe the wrapper can call.
struct callable_probe {
  probe held;
  void operator()() const noexcept {}
};

// conformance-header: fixed_function.hpp
struct fixed_function_adapter {
  using type = metl::fixed_function<void(), 32>;
  static constexpr const char* name = "fixed_function";
  static constexpr const char* zero_capacity_refused = "its storage holds one target";
  static constexpr std::size_t capacity = 1;
  static constexpr bool movable = true;
  static constexpr bool move_empties_source = true;
  static constexpr bool fixed_count = true;
  template <typename C>
  static bool try_add(C& c, int id) {
    if (c) {
      return false;
    }
    c = callable_probe{probe(id)};
    return true;
  }
  template <typename C>
  static std::size_t held(const C& c) {
    return c ? 1 : 0;
  }
  template <typename C>
  static void remove_one(C& c) {
    c = nullptr;
  }
  template <typename C>
  static void clear(C& c) {
    c = nullptr;
  }
};

using registry = std::tuple<fixed_vector_adapter,
                            fixed_deque_adapter,
                            ring_buffer_adapter,
                            fixed_queue_adapter,
                            fixed_stack_adapter,
                            fixed_priority_queue_adapter,
                            static_message_queue_adapter,
                            flat_map_adapter,
                            flat_set_adapter,
                            static_unordered_map_adapter,
                            static_unordered_set_adapter,
                            object_pool_adapter,
                            handle_pool_adapter,
                            arena_allocator_adapter,
                            spsc_queue_adapter,
                            mpmc_queue_adapter,
                            optional_adapter,
                            expected_adapter,
                            variant_adapter,
                            fixed_function_adapter>;

// How many elements a property fills: three, or fewer for a type that holds
// fewer.
template <typename Adapter>
inline constexpr int filled = Adapter::capacity < 3 ? static_cast<int>(Adapter::capacity) : 3;

template <typename Adapter>
typename Adapter::type& instance() {
  static typename Adapter::type object;
  return object;
}

// ---- Running a property ------------------------------------------------------

// A property is a callable template `bool operator()(Adapter) const` that
// returns whether the type met it, printing what it saw when it did not.
// `gap` is the adapter's known-gap issue for this property, or nullptr.
template <typename A, typename = void>
struct is_available : std::true_type {};
template <typename A>
struct is_available<A, std::void_t<decltype(A::available)>> : std::bool_constant<A::available> {};

template <typename Adapter, typename Property>
void run_available(const char* property_name, const char* gap, Property property);

template <typename Adapter, typename Property>
void run_one(const char* property_name, const char* gap, Property property) {
  if constexpr (!is_available<Adapter>::value) {
    std::printf("%s: %s: not built for this target\n", Adapter::name, property_name);
    (void)gap;
    (void)property;
  } else {
    run_available<Adapter>(property_name, gap, property);
  }
}

template <typename Adapter, typename Property>
void run_available(const char* property_name, const char* gap, Property property) {
  const bool met = property(Adapter{});
  if (gap == nullptr) {
    if (!met) {
      std::fprintf(stderr, "%s: %s: FAILED\n", Adapter::name, property_name);
      CHECK(met);
    }
    return;
  }
  if (met) {
    std::fprintf(stderr,
                 "%s: %s: passes now; delete gap_%s (%s) from its adapter\n",
                 Adapter::name,
                 property_name,
                 property_name,
                 gap);
    CHECK(!met);
    return;
  }
  std::printf("%s: %s: known gap %s\n", Adapter::name, property_name, gap);
}

}  // namespace conformance

// Declares a detector for `Adapter::gap_<property>`, usable as
// `METL_CONFORMANCE_GAP(property, Adapter)`.
#define METL_CONFORMANCE_DECLARE_GAP(property)                         \
  namespace conformance {                                              \
  template <typename A, typename = void>                               \
  struct gap_##property {                                              \
    static constexpr const char* value = nullptr;                      \
  };                                                                   \
  template <typename A>                                                \
  struct gap_##property<A, std::void_t<decltype(A::gap_##property)>> { \
    static constexpr const char* value = A::gap_##property;            \
  };                                                                   \
  }
#define METL_CONFORMANCE_GAP(property, Adapter) ::conformance::gap_##property<Adapter>::value
