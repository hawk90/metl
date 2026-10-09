// Pools and the arena when the element's own constructor or destructor calls
// back into the same container.
//
//   * arena_allocator: a constructor that allocates from the arena used to get
//     ITS destructor registered on the inner allocation's record, so reset()
//     ran ~Outer() on the inner object and never ran ~Inner().
//   * object_pool / handle_pool: the slot was claimed only after the
//     constructor returned, so a constructor that emplaced got the same slot
//     -- two objects, one slot.
//   * object_pool / handle_pool: destroy ran ~T() while the slot still looked
//     live, so a self-unregistering destructor destroyed it a second time; and
//     a destructor that emplaced could be handed the slot still being torn down.
//   * object_pool: a pointer into the middle of a slot was accepted as the slot.

#include "metl_check.hpp"

#include <cstdint>

#include <metl/arena_allocator.hpp>
#include <metl/config.hpp>
#include <metl/handle_pool.hpp>
#include <metl/object_pool.hpp>
#include <metl/versioned_handle.hpp>

namespace {

// --- arena ------------------------------------------------------------------

metl::arena_allocator<256> g_arena;
int g_inner_destroyed = 0;
int g_outer_destroyed = 0;
bool g_outer_saw_own_tag = true;

struct inner {
  int value;
  explicit inner(int v) : value(v) {}
  ~inner() { ++g_inner_destroyed; }
};

struct outer {
  inner* child;
  int tag = 7;
  outer() : child(g_arena.emplace<inner>(42)) {}
  ~outer() {
    ++g_outer_destroyed;
    g_outer_saw_own_tag = g_outer_saw_own_tag && tag == 7;
  }
};

void arena_constructor_allocates() {
  outer* o = g_arena.emplace<outer>();
  CHECK_EQ(o->child->value, 42);
  g_arena.reset();
  CHECK_EQ(g_outer_destroyed, 1);
  CHECK_EQ(g_inner_destroyed, 1);
  CHECK(g_outer_saw_own_tag);  // ~outer ran on the outer object, not the inner
}

// --- object_pool ------------------------------------------------------------

struct node;
// Each pool is a function-local static, so the elements reach it without a
// global holding the address of a stack object.
metl::object_pool<node, 4>& node_pool();
int g_node_destroyed = 0;

struct node {
  node* child = nullptr;
  int depth;
  explicit node(int d) : depth(d) {
    if (d > 0) {
      child = node_pool().emplace(d - 1);
    }
  }
  ~node() { ++g_node_destroyed; }
};

metl::object_pool<node, 4>& node_pool() {
  static metl::object_pool<node, 4> pool;
  return pool;
}

void object_pool_constructor_emplaces() {
  auto& pool = node_pool();
  node* parent = pool.emplace(1);
  CHECK(parent != parent->child);
  CHECK_EQ(parent->depth, 1);
  CHECK_EQ(parent->child->depth, 0);
  CHECK_EQ(pool.size(), 2u);
  g_node_destroyed = 0;
  pool.clear();
  CHECK_EQ(g_node_destroyed, 2);
  CHECK_EQ(pool.size(), 0u);
}

struct self_unregistering;
metl::object_pool<self_unregistering, 4>& registry();
int g_unregistered = 0;

struct self_unregistering {
  bool replace_on_destroy = false;
  ~self_unregistering() {
    ++g_unregistered;
    // A handle that removes itself: must report "not live", not destroy again.
    CHECK(!registry().destroy(this));
    CHECK(!registry().contains(this));
    if (replace_on_destroy) {
      // Must land in a free slot, not over the object being destroyed.
      self_unregistering* replacement = registry().try_emplace();
      CHECK(replacement != nullptr);
      CHECK(replacement != this);
    }
  }
};

metl::object_pool<self_unregistering, 4>& registry() {
  static metl::object_pool<self_unregistering, 4> pool;
  return pool;
}

void object_pool_destructor_reenters() {
  auto& pool = registry();
  self_unregistering* a = pool.emplace();
  g_unregistered = 0;
  CHECK(pool.destroy(a));
  CHECK_EQ(g_unregistered, 1);
  CHECK_EQ(pool.size(), 0u);

  self_unregistering* b = pool.emplace();
  b->replace_on_destroy = true;
  CHECK(pool.destroy(b));
  CHECK_EQ(pool.size(), 1u);  // the replacement is live and counted
  pool.clear();
  CHECK_EQ(pool.size(), 0u);
}

void object_pool_interior_pointer() {
  struct pair {
    int first = 1;
    int second = 2;
  };
  metl::object_pool<pair, 2> pool;
  pair* p = pool.emplace();
  int* member = &p->second;
  const auto* interior = reinterpret_cast<const pair*>(member);
  CHECK(!pool.contains(interior));
  CHECK(!pool.destroy(const_cast<pair*>(interior)));
  CHECK(pool.contains(p));
  CHECK_EQ(pool.size(), 1u);
}

// --- handle_pool ------------------------------------------------------------

struct h_node;
using h_node_pool = metl::handle_pool<h_node, 4>;
// h_node_pool::handle_type, spelled out: naming the member would instantiate
// the pool while h_node is still incomplete.
using h_node_handle = metl::versioned_handle<h_node_pool, std::uint16_t, std::uint16_t>;
h_node_pool& handles();

struct h_node {
  h_node_handle child{};
  explicit h_node(int d) {
    if (d > 0) {
      child = handles().emplace(d - 1);
    }
  }
};

h_node_pool& handles() {
  static h_node_pool pool;
  return pool;
}

void handle_pool_constructor_emplaces() {
  auto& pool = handles();
  const auto parent = pool.emplace(1);
  const h_node* p = pool.get(parent);
  if (CHECK(p != nullptr)) {
    CHECK(parent != p->child);
    CHECK(pool.contains(p->child));
  }
  CHECK_EQ(pool.size(), 2u);
}

struct h_self;
using h_self_pool = metl::handle_pool<h_self, 4>;
using h_self_handle = metl::versioned_handle<h_self_pool, std::uint16_t, std::uint16_t>;
h_self_pool& h_registry();
int g_h_destroyed = 0;

struct h_self {
  h_self_handle me{};
  bool replace_on_destroy = false;
  ~h_self() {
    ++g_h_destroyed;
    CHECK(!h_registry().destroy(me));
    if (replace_on_destroy) {
      const auto replacement = h_registry().try_emplace();
      CHECK(replacement.valid());
      CHECK(replacement.index() != me.index());
    }
  }
};

h_self_pool& h_registry() {
  static h_self_pool pool;
  return pool;
}

void handle_pool_destructor_reenters() {
  auto& pool = h_registry();
  const auto a = pool.emplace();
  if (h_self* object = pool.get(a); CHECK(object != nullptr)) {
    object->me = a;
  }
  g_h_destroyed = 0;
  CHECK(pool.destroy(a));
  CHECK_EQ(g_h_destroyed, 1);
  CHECK_EQ(pool.size(), 0u);

  const auto b = pool.emplace();
  if (h_self* object = pool.get(b); CHECK(object != nullptr)) {
    object->me = b;
    object->replace_on_destroy = true;
  }
  CHECK(pool.destroy(b));
  CHECK_EQ(pool.size(), 1u);
}

#if !METL_NO_EXCEPTIONS
struct throws_once {
  static inline bool arm = false;
  throws_once() {
    if (arm) {
      arm = false;
      throw 1;
    }
  }
};

void a_throwing_constructor_releases_the_claim() {
  metl::object_pool<throws_once, 2> pool;
  throws_once::arm = true;
  bool threw = false;
  try {
    (void)pool.try_emplace();
  } catch (int) {
    threw = true;
  }
  CHECK(threw);  // otherwise the claim below was never put to the test
  CHECK_EQ(pool.size(), 0u);
  CHECK(pool.try_emplace() != nullptr);
  CHECK(pool.try_emplace() != nullptr);  // both slots are still usable
  CHECK_EQ(pool.size(), 2u);

  metl::handle_pool<throws_once, 2> handles;
  throws_once::arm = true;
  threw = false;
  try {
    (void)handles.try_emplace();
  } catch (int) {
    threw = true;
  }
  CHECK(threw);
  CHECK_EQ(handles.size(), 0u);
  CHECK(handles.try_emplace().valid());
  CHECK(handles.try_emplace().valid());
}
#endif

}  // namespace

int main() {
  arena_constructor_allocates();
  object_pool_constructor_emplaces();
  object_pool_destructor_reenters();
  object_pool_interior_pointer();
  handle_pool_constructor_emplaces();
  handle_pool_destructor_reenters();
#if !METL_NO_EXCEPTIONS
  a_throwing_constructor_releases_the_claim();
#endif
  return metl_test::exit_code();
}
