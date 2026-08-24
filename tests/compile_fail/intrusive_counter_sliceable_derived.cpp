// EXPECT-ERROR: intrusive_ref_counter<Derived>: the reference-count release destroys the object
//
// The counter releases by calling `delete static_cast<Derived*>(this)`. If
// Derived is neither final nor polymorphic, a further subclass is destroyed
// through the wrong type: its own members are never destroyed and its
// destructor never runs. A leak that only appears in the one arrangement
// nobody wrote a test for, because the base's own tests all use the base.

#include <metl/intrusive_ptr.hpp>

namespace {

struct sealed final : metl::intrusive_ref_counter<sealed> {
  int value = 0;
};

struct open_base : metl::intrusive_ref_counter<open_base> {
  int value = 0;
};

}  // namespace

void control(sealed* object) {
  metl::intrusive_ptr<sealed> owned(object, metl::adopt_ref);
}

#ifdef METL_COMPILE_FAIL
void offender(open_base* object) {
  metl::intrusive_ptr<open_base> owned(object, metl::adopt_ref);
}
#endif
