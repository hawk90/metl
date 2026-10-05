// Regression test for intrusive_ptr assignment from a source that the release
// destroys (docs/AUDIT.md, Section G).
//
// `p = p->next` is the list-walk idiom: `p` holds the only reference to the
// head, and the head's `next` holds the only reference to the second node.
// Assignment used to `reset()` first, which destroyed the head -- and with it
// `p->next`, the very pointer being assigned from. The second node was then
// released by the head's destructor and `p` was left null.
//
// Uses refcount_kind::non_atomic so it builds and runs on ARMv6-M too.

#include "metl_check.hpp"

#include <new>

#include <metl/intrusive_ptr.hpp>

namespace {

struct node final : metl::intrusive_ref_counter<node, metl::refcount_kind::non_atomic> {
  explicit node(int v) noexcept : value(v) {}
  ~node() { ++destroyed; }

  int value;
  metl::intrusive_ptr<node> next;
  static int destroyed;
};

int node::destroyed = 0;

template <typename T>
struct slot {
  alignas(T) unsigned char buf[sizeof(T)];
  template <typename... Args>
  T* make(Args&&... args) {
    return ::new (static_cast<void*>(buf)) T(static_cast<Args&&>(args)...);
  }
};

}  // namespace

int main() {
  // Copy assignment: p = p->next.
  {
    node::destroyed = 0;
    slot<node> a_slot;
    slot<node> b_slot;
    node* b = b_slot.make(2);
    metl::intrusive_ptr<node> p(a_slot.make(1), metl::retain_ref);
    p->next = metl::intrusive_ptr<node>(b, metl::retain_ref);

    p = p->next;

    CHECK_EQ(node::destroyed, 1);  // the head only
    CHECK(p.get() == b);
    CHECK_EQ(b->use_count(), 1u);
    CHECK_EQ(p->value, 2);
  }
  CHECK_EQ(node::destroyed, 2);

  // Move assignment: p = std::move(p->next).
  {
    node::destroyed = 0;
    slot<node> a_slot;
    slot<node> b_slot;
    node* b = b_slot.make(2);
    metl::intrusive_ptr<node> p(a_slot.make(1), metl::retain_ref);
    p->next = metl::intrusive_ptr<node>(b, metl::retain_ref);

    p = static_cast<metl::intrusive_ptr<node>&&>(p->next);

    CHECK_EQ(node::destroyed, 1);
    CHECK(p.get() == b);
    CHECK_EQ(b->use_count(), 1u);
  }
  CHECK_EQ(node::destroyed, 2);

  // Walking a three-node list to its end releases each node exactly once.
  {
    node::destroyed = 0;
    slot<node> s[3];
    metl::intrusive_ptr<node> p(s[0].make(0), metl::retain_ref);
    p->next = metl::intrusive_ptr<node>(s[1].make(1), metl::retain_ref);
    p->next->next = metl::intrusive_ptr<node>(s[2].make(2), metl::retain_ref);

    int visited = 0;
    while (p) {
      CHECK_EQ(p->value, visited);
      ++visited;
      p = p->next;
    }
    CHECK_EQ(visited, 3);
    CHECK_EQ(node::destroyed, 3);
  }

  // Self-assignment still keeps the object alive.
  {
    node::destroyed = 0;
    slot<node> a_slot;
    metl::intrusive_ptr<node> p(a_slot.make(1), metl::retain_ref);
    metl::intrusive_ptr<node>& alias = p;
    p = alias;
    CHECK_EQ(node::destroyed, 0);
    CHECK_EQ(p->use_count(), 1u);
  }

  return metl_test::exit_code();
}
