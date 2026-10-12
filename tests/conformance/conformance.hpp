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

#include <cstddef>
#include <cstdio>
#include <functional>
#include <tuple>
#include <utility>

#include <metl/arena_allocator.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/handle_pool.hpp>
#include <metl/object_pool.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_message_queue.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

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
//   fixed_count              held() is an exact element count and capacity a
//                            number of elements, not of bytes
//
// and optionally `gap_<property>`, the issue that tracks a known failure.

// conformance-header: fixed_vector.hpp
struct fixed_vector_adapter {
  using type = metl::fixed_vector<probe, 4>;
  static constexpr const char* name = "fixed_vector";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool fixed_count = true;
  static bool try_add(type& c, int id) { return c.try_emplace_back(id); }
  static std::size_t held(const type& c) { return c.size(); }
  static void remove_one(type& c) { c.pop_back(); }
  static void clear(type& c) { c.clear(); }
};

// conformance-header: fixed_deque.hpp
struct fixed_deque_adapter {
  using type = metl::fixed_deque<probe, 4>;
  static constexpr const char* name = "fixed_deque";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool fixed_count = true;
  static bool try_add(type& c, int id) { return c.try_emplace_back(id); }
  static std::size_t held(const type& c) { return c.size(); }
  static void remove_one(type& c) { c.pop_front(); }
  static void clear(type& c) { c.clear(); }
};

// conformance-header: ring_buffer.hpp
struct ring_buffer_adapter {
  using type = metl::ring_buffer<probe, 4>;
  static constexpr const char* name = "ring_buffer";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool fixed_count = true;
  static bool try_add(type& c, int id) { return c.try_emplace_back(id); }
  static std::size_t held(const type& c) { return c.size(); }
  static void remove_one(type& c) { c.pop_front(); }
  static void clear(type& c) { c.clear(); }
};

// conformance-header: fixed_queue.hpp
struct fixed_queue_adapter {
  using type = metl::fixed_queue<probe, 4>;
  static constexpr const char* name = "fixed_queue";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool fixed_count = true;
  static bool try_add(type& c, int id) { return c.try_emplace(id); }
  static std::size_t held(const type& c) { return c.size(); }
  static void remove_one(type& c) { c.pop(); }
  static void clear(type& c) { c.clear(); }
};

// conformance-header: fixed_stack.hpp
struct fixed_stack_adapter {
  using type = metl::fixed_stack<probe, 4>;
  static constexpr const char* name = "fixed_stack";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool fixed_count = true;
  static bool try_add(type& c, int id) { return c.try_emplace(id); }
  static std::size_t held(const type& c) { return c.size(); }
  static void remove_one(type& c) { c.pop(); }
  static void clear(type& c) { c.clear(); }
};

// conformance-header: fixed_priority_queue.hpp
struct fixed_priority_queue_adapter {
  using type = metl::fixed_priority_queue<probe, 4>;
  static constexpr const char* name = "fixed_priority_queue";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool fixed_count = true;
  static bool try_add(type& c, int id) { return c.try_emplace(id); }
  static std::size_t held(const type& c) { return c.size(); }
  static void remove_one(type& c) { c.pop(); }
  static void clear(type& c) { c.clear(); }
};

// conformance-header: static_message_queue.hpp
struct static_message_queue_adapter {
  using type = metl::static_message_queue<probe, 4>;
  static constexpr const char* name = "static_message_queue";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool fixed_count = true;
  static bool try_add(type& c, int id) { return c.try_emplace(id); }
  static std::size_t held(const type& c) { return c.size(); }
  static void remove_one(type& c) { c.pop(); }
  static void clear(type& c) { c.clear(); }
};

// conformance-header: flat_map.hpp
struct flat_map_adapter {
  using type = metl::flat_map<int, probe, 4>;
  static constexpr const char* name = "flat_map";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool fixed_count = true;
  static bool try_add(type& c, int id) { return c.try_emplace(id, probe(id)); }
  static std::size_t held(const type& c) { return c.size(); }
  static void remove_one(type& c) { (void)c.erase(c.begin()->key); }
  static void clear(type& c) { c.clear(); }
};

// conformance-header: flat_set.hpp
struct flat_set_adapter {
  using type = metl::flat_set<probe, 4>;
  static constexpr const char* name = "flat_set";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool fixed_count = true;
  static bool try_add(type& c, int id) { return c.try_emplace(probe(id)); }
  static std::size_t held(const type& c) { return c.size(); }
  static void remove_one(type& c) { (void)c.erase(probe::key(c.begin()->id)); }
  static void clear(type& c) { c.clear(); }
};

// conformance-header: static_unordered_map.hpp
struct static_unordered_map_adapter {
  using type = metl::static_unordered_map<int, probe, 4>;
  static constexpr const char* name = "static_unordered_map";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool fixed_count = true;
  static bool try_add(type& c, int id) { return c.try_emplace(id, probe(id)); }
  static std::size_t held(const type& c) { return c.size(); }
  static void remove_one(type& c) { (void)c.erase(c.begin()->key); }
  static void clear(type& c) { c.clear(); }
};

// conformance-header: static_unordered_set.hpp
struct static_unordered_set_adapter {
  using type = metl::static_unordered_set<probe, 4, probe_hash>;
  static constexpr const char* name = "static_unordered_set";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = true;
  static constexpr bool fixed_count = true;
  static bool try_add(type& c, int id) { return c.try_emplace(probe(id)); }
  static std::size_t held(const type& c) { return c.size(); }
  static void remove_one(type& c) { (void)c.erase(probe::key(c.begin()->id)); }
  static void clear(type& c) { c.clear(); }
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
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = false;
  static constexpr bool fixed_count = true;
  static handed_out<probe*, 4>& given() {
    static handed_out<probe*, 4> list;
    return list;
  }
  static bool try_add(type& c, int id) {
    probe* object = c.try_emplace(id);
    if (object != nullptr) {
      given().push(object);
    }
    return object != nullptr;
  }
  static std::size_t held(const type& c) { return c.size(); }
  static void remove_one(type& c) { (void)c.destroy(given().pop()); }
  static void clear(type& c) {
    given().count = 0;
    c.clear();
  }
};

// conformance-header: handle_pool.hpp
struct handle_pool_adapter {
  using type = metl::handle_pool<probe, 4>;
  static constexpr const char* name = "handle_pool";
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = false;
  static constexpr bool fixed_count = true;
  static handed_out<type::handle_type, 4>& given() {
    static handed_out<type::handle_type, 4> list;
    return list;
  }
  static bool try_add(type& c, int id) {
    const type::handle_type handle = c.try_emplace(id);
    if (handle.valid()) {
      given().push(handle);
    }
    return handle.valid();
  }
  static std::size_t held(const type& c) { return c.size(); }
  static void remove_one(type& c) { (void)c.destroy(given().pop()); }
  static void clear(type& c) {
    given().count = 0;
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
  static constexpr std::size_t capacity = 4;
  static constexpr bool movable = false;
  static constexpr bool fixed_count = false;
  static constexpr const char* gap_reentrant_clear_destroys_once = "#312";
  static handed_out<type::mark_type, 4>& given() {
    static handed_out<type::mark_type, 4> list;
    return list;
  }
  static bool try_add(type& c, int id) {
    if (given().count == capacity) {
      return false;
    }
    const type::mark_type before = c.mark();
    if (c.try_emplace<probe>(id) == nullptr) {
      return false;
    }
    given().push(before);
    return true;
  }
  static std::size_t held(const type& c) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < given().count; ++i) {
      count += c.used() > given().items[i].offset ? std::size_t{1} : std::size_t{0};
    }
    return count;
  }
  static void remove_one(type& c) {
    const type::mark_type target = given().items[given().count - 1];
    c.rewind(target);
    --given().count;
  }
  static void clear(type& c) {
    c.reset();
    given().count = 0;
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
                            arena_allocator_adapter>;

template <typename Adapter>
typename Adapter::type& instance() {
  static typename Adapter::type object;
  return object;
}

// ---- Running a property ------------------------------------------------------

// A property is a callable template `bool operator()(Adapter) const` that
// returns whether the type met it, printing what it saw when it did not.
// `gap` is the adapter's known-gap issue for this property, or nullptr.
template <typename Adapter, typename Property>
void run_one(const char* property_name, const char* gap, Property property) {
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
