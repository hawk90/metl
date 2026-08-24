// EXPECT-ERROR: metl::scope_exit requires the stored callable to be noexcept
//
// The guard runs its callable from a destructor. A destructor that lets an
// exception escape during unwinding calls std::terminate, and METL is a
// no-exception library, so there is no recovery path to fall back on. The
// requirement has to be checked where the callable is stored, not where it
// runs -- by then the type is gone.

#include <metl/scope_exit.hpp>

namespace {

struct never_throws {
  void operator()() const noexcept {}
};

struct may_throw {
  void operator()() const {}
};

}  // namespace

void control() {
  metl::scope_exit<never_throws> guard{never_throws{}};
  (void)guard;
}

#ifdef METL_COMPILE_FAIL
void offender() {
  metl::scope_exit<may_throw> guard{may_throw{}};
  (void)guard;
}
#endif
