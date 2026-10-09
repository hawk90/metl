// Sequence containers when an element's constructor or destructor calls back
// into the container that holds it.
//
//   * Removal ran ~T() before shrinking, so a destructor that looked itself up
//     to unregister still found itself and was destroyed twice; one that
//     cleared the container wrapped size_ to SIZE_MAX.
//   * Insertion built the element before claiming its slot, so a constructor
//     that pushed into the same container got the same slot -- the outer
//     element was overwritten -- and emplace returned back()/front(), which by
//     then was the inner element.
//
// Removal now shrinks first; insertion claims the slot first and returns the
// element it built.

#include "metl_check.hpp"

#include <metl/fixed_deque.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_message_queue.hpp>

namespace {

int g_destroyed = 0;

// --- fixed_vector -----------------------------------------------------------

struct v_item;
metl::fixed_vector<v_item, 8>& vec();

struct v_item {
  int id;
  bool clear_owner = false;
  explicit v_item(int i, int children = 0) : id(i) {
    for (int c = 0; c < children; ++c) {
      vec().emplace_back(100 + c);
    }
  }
  // erase() shifts by assignment; declared, since the destructor is user-provided.
  v_item(const v_item&) = default;
  v_item& operator=(const v_item&) = default;
  ~v_item() {
    ++g_destroyed;
    // Unregister: if this element is still listed, remove it.
    auto& v = vec();
    for (auto it = v.begin(); it != v.end(); ++it) {
      if (&*it == this) {
        v.erase(it);
        break;
      }
    }
    if (clear_owner) {
      v.clear();
    }
  }
};

metl::fixed_vector<v_item, 8>& vec() {
  static metl::fixed_vector<v_item, 8> v;
  return v;
}

void fixed_vector_reentrancy() {
  auto& v = vec();
  v.clear();

  // A constructor that pushes: distinct slots, and emplace_back returns the
  // element it built.
  v_item& outer = v.emplace_back(1, 2);
  CHECK_EQ(outer.id, 1);
  CHECK_EQ(v.size(), 3u);
  CHECK_EQ(v[0].id, 1);
  CHECK_EQ(v[1].id, 100);
  CHECK_EQ(v[2].id, 101);

  // A destructor that unregisters itself: destroyed once.
  g_destroyed = 0;
  v.pop_back();
  CHECK_EQ(g_destroyed, 1);
  CHECK_EQ(v.size(), 2u);

  g_destroyed = 0;
  v.erase(v.begin());
  CHECK_EQ(g_destroyed, 1);
  CHECK_EQ(v.size(), 1u);
  CHECK_EQ(v[0].id, 100);

  // Range erase, then a destructor that clears the whole vector.
  v.emplace_back(2);
  v.emplace_back(3);
  g_destroyed = 0;
  v.erase(v.begin() + 1, v.end());
  CHECK_EQ(g_destroyed, 2);
  CHECK_EQ(v.size(), 1u);

  v.emplace_back(4);
  v.back().clear_owner = true;
  v.pop_back();
  CHECK_EQ(v.size(), 0u);  // not SIZE_MAX
}

// --- ring_buffer / fixed_deque / static_message_queue -------------------------

struct ring_item;
using ring_t = metl::ring_buffer<ring_item, 4>;
ring_t& ring();
struct ring_item {
  int id;
  bool reenter = false;
  explicit ring_item(int i, bool push_child = false) : id(i) {
    if (push_child) {
      ring().emplace_back(200);
    }
  }
  ~ring_item() {
    ++g_destroyed;
    if (reenter && !ring().empty()) {
      ring().pop_front();
    }
  }
};
ring_t& ring() {
  static ring_t instance;
  return instance;
}

struct deque_item;
using deque_t = metl::fixed_deque<deque_item, 4>;
deque_t& deque();
struct deque_item {
  int id;
  bool reenter = false;
  explicit deque_item(int i, bool push_child = false) : id(i) {
    if (push_child) {
      deque().emplace_front(300);
    }
  }
  ~deque_item() {
    ++g_destroyed;
    if (reenter && !deque().empty()) {
      deque().pop_back();
    }
  }
};
deque_t& deque() {
  static deque_t instance;
  return instance;
}

struct queue_item;
using queue_t = metl::static_message_queue<queue_item, 4>;
queue_t& queue();
struct queue_item {
  int id;
  bool reenter = false;
  explicit queue_item(int i, bool push_child = false) : id(i) {
    if (push_child) {
      queue().emplace(400);
    }
  }
  ~queue_item() {
    ++g_destroyed;
    if (reenter && !queue().empty()) {
      queue().pop();
    }
  }
};
queue_t& queue() {
  static queue_t instance;
  return instance;
}

void ring_family_reentrancy() {
  {
    auto& r = ring();
    r.clear();
    ring_item& outer = r.emplace_back(1, true);
    CHECK_EQ(outer.id, 1);
    CHECK_EQ(r.size(), 2u);
    CHECK_EQ(r.front().id, 1);
    CHECK_EQ(r.back().id, 200);
    r.front().reenter = true;
    g_destroyed = 0;
    r.pop_front();  // destroys 1, whose destructor pops 200
    CHECK_EQ(g_destroyed, 2);
    CHECK_EQ(r.size(), 0u);
  }
  {
    auto& d = deque();
    d.clear();
    deque_item& outer = d.emplace_front(1, true);
    CHECK_EQ(outer.id, 1);
    CHECK_EQ(d.size(), 2u);
    CHECK_EQ(d.front().id, 300);
    CHECK_EQ(d.back().id, 1);
    d.back().reenter = true;
    g_destroyed = 0;
    d.pop_back();  // destroys 1, whose destructor pops 300
    CHECK_EQ(g_destroyed, 2);
    CHECK_EQ(d.size(), 0u);
  }
  {
    auto& q = queue();
    q.clear();
    queue_item& outer = q.emplace(1, true);
    CHECK_EQ(outer.id, 1);
    CHECK_EQ(q.size(), 2u);
    CHECK_EQ(q.front().id, 1);
    q.front().reenter = true;
    g_destroyed = 0;
    q.pop();  // destroys 1, whose destructor pops 400
    CHECK_EQ(g_destroyed, 2);
    CHECK_EQ(q.size(), 0u);
  }
}

}  // namespace

int main() {
  fixed_vector_reentrancy();
  ring_family_reentrancy();
  return metl_test::exit_code();
}
