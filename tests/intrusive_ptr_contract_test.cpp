// Regression test for intrusive_ref_counter's destruction contract.
//
// The reference-count release destroys the object through `Derived`, so Derived
// must be the most-derived type. The library now static_asserts that Derived is
// either `final` (concrete leaf) or has a virtual destructor (polymorphic base
// meant to be subclassed). This test exercises both safe forms and verifies the
// correct destructor runs (no slicing / leaks) for a subclassed polymorphic
// base.

#include "metl_check.hpp"

#include <cstddef>
#include <new>

#include <metl/intrusive_ptr.hpp>

namespace {

// Concrete leaf type: marked final -> safe.
struct leaf final : metl::intrusive_ref_counter<leaf, metl::refcount_kind::non_atomic> {
  static int dtor_calls;
  int value = 0;
  explicit leaf(int v) : value(v) {}
  ~leaf() { ++dtor_calls; }
};
int leaf::dtor_calls = 0;

// Polymorphic base with a virtual destructor -> safe to subclass; release
// destroys the real most-derived object via virtual dispatch.
struct shape : metl::intrusive_ref_counter<shape, metl::refcount_kind::non_atomic> {
  static int base_dtor;
  static int circle_dtor;
  virtual ~shape() { ++base_dtor; }
};
int shape::base_dtor = 0;
int shape::circle_dtor = 0;

struct circle : shape {
  int radius = 0;
  explicit circle(int r) : radius(r) {}
  ~circle() override { ++circle_dtor; }
};

// Aligned raw storage so we control (and observe) object lifetime without heap.
template <typename T>
struct raw_storage {
  alignas(T) unsigned char bytes[sizeof(T)];
  template <typename... Args>
  T* make(Args&&... args) {
    return ::new (static_cast<void*>(bytes)) T(static_cast<Args&&>(args)...);
  }
};

}  // namespace

int main() {
  // ---- final leaf type ----
  {
    leaf::dtor_calls = 0;
    static raw_storage<leaf> store;
    leaf* raw = store.make(7);
    {
      metl::intrusive_ptr<leaf> p(raw, metl::retain_ref);
      CHECK_EQ(p->value, 7);
      CHECK_EQ(p->use_count(), std::size_t(1));
    }
    CHECK_EQ(leaf::dtor_calls, 1);
  }

  // ---- polymorphic base subclassed: correct dtor via virtual dispatch ----
  {
    shape::base_dtor = 0;
    shape::circle_dtor = 0;
    static raw_storage<circle> store;
    circle* raw = store.make(3);
    {
      // Hold via a base pointer; release must still destroy the circle.
      metl::intrusive_ptr<shape> p(raw, metl::retain_ref);
      CHECK_EQ(p->use_count(), std::size_t(1));
    }
    CHECK_EQ(shape::circle_dtor, 1);  // most-derived destructor ran
    CHECK_EQ(shape::base_dtor, 1);
  }

  return metl_test::exit_code();
}
