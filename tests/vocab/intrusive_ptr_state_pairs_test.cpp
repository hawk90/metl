// Every pair of states for intrusive_ptr's construction, assignment, swap and
// reset: null, or holding one of two objects.
//
// Taking or dropping a reference when either side may be null is where a count
// goes wrong: a reference added for nothing, or released twice. Each case
// checks both objects' counts after the operation, and that an object is
// destroyed exactly when its last reference goes.

#include "metl_check.hpp"

#include <cstddef>
#include <new>
#include <utility>

#include <metl/intrusive_ptr.hpp>

namespace {

struct object : metl::intrusive_ref_counter<object> {
  static int destroyed;
  int value;
  explicit object(int v) : value(v) {}
  virtual ~object() { ++destroyed; }
};

int object::destroyed = 0;

struct derived final : object {
  using object::object;
};

using ptr = metl::intrusive_ptr<object>;

// The objects live in local storage; a test builds them fresh for each case.
struct pool {
  alignas(derived) unsigned char bytes[2][sizeof(derived)];
  derived* objects[2];

  pool()
      : objects{::new (static_cast<void*>(bytes[0])) derived(1),
                ::new (static_cast<void*>(bytes[1])) derived(2)} {}

  // A state is 0 for null, or 1 or 2 for an object.
  derived* at(int state) const { return state == 0 ? nullptr : objects[state - 1]; }
  std::size_t count(int state) const { return objects[state - 1]->use_count(); }
};

constexpr int states[] = {0, 1, 2};

bool holds(const ptr& p, const pool& objects, int state) {
  return p.get() == objects.at(state);
}

// Each object's count, which the test also holds one reference of.
bool counts_are(const pool& objects, std::size_t first, std::size_t second) {
  return objects.count(1) == first + 1 && objects.count(2) == second + 1;
}

std::size_t refs(int state, int which) {
  return state == which ? 1 : 0;
}

void construction(int s) {
  pool objects;
  {
    const ptr keep1(objects.at(1), metl::retain_ref);
    const ptr keep2(objects.at(2), metl::retain_ref);
    {
      const ptr retained(objects.at(s), metl::retain_ref);
      CHECK(holds(retained, objects, s));
      const ptr copy(retained);
      CHECK(holds(copy, objects, s));
      CHECK(counts_are(objects, 2 * refs(s, 1), 2 * refs(s, 2)));
      metl::intrusive_ptr<derived> exact(objects.at(s), metl::retain_ref);
      const ptr converted(exact);
      CHECK(holds(converted, objects, s));
      const ptr moved_in(std::move(exact));
      CHECK(holds(moved_in, objects, s));
      CHECK(exact.get() == nullptr);
      CHECK(counts_are(objects, 4 * refs(s, 1), 4 * refs(s, 2)));
    }
    CHECK(counts_are(objects, 0, 0));
  }
  CHECK_EQ(object::destroyed, 2);
  object::destroyed = 0;
}

void assignment_swap_reset(int a, int b) {
  pool objects;
  {
    const ptr keep1(objects.at(1), metl::retain_ref);
    const ptr keep2(objects.at(2), metl::retain_ref);
    {
      ptr x(objects.at(a), metl::retain_ref);
      const ptr y(objects.at(b), metl::retain_ref);
      x = y;
      CHECK(holds(x, objects, b));
      CHECK(counts_are(objects, 2 * refs(b, 1), 2 * refs(b, 2)));
    }
    {
      ptr x(objects.at(a), metl::retain_ref);
      ptr y(objects.at(b), metl::retain_ref);
      x = std::move(y);
      CHECK(holds(x, objects, b));
      CHECK(y.get() == nullptr);
      CHECK(counts_are(objects, refs(b, 1), refs(b, 2)));
    }
    {
      ptr x(objects.at(a), metl::retain_ref);
      ptr y(objects.at(b), metl::retain_ref);
      x.swap(y);
      CHECK(holds(x, objects, b));
      CHECK(holds(y, objects, a));
      CHECK(counts_are(objects, refs(a, 1) + refs(b, 1), refs(a, 2) + refs(b, 2)));
      x.reset();
      y = nullptr;
      CHECK(x.get() == nullptr);
      CHECK(y.get() == nullptr);
    }
    {
      ptr x(objects.at(a), metl::retain_ref);
      ptr& alias = x;
      x = alias;
      CHECK(holds(x, objects, a));
      x = std::move(alias);
      CHECK(holds(x, objects, a));
      CHECK(counts_are(objects, refs(a, 1), refs(a, 2)));
    }
    CHECK(counts_are(objects, 0, 0));
    CHECK_EQ(object::destroyed, 0);
  }
  CHECK_EQ(object::destroyed, 2);  // each exactly once, when the last reference went
  object::destroyed = 0;
}

}  // namespace

int main() {
  for (const int a : states) {
    construction(a);
    for (const int b : states) {
      assignment_swap_reset(a, b);
    }
  }
  return metl_test::exit_code();
}
