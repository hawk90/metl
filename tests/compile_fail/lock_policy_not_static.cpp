// EXPECT-ERROR: metl lock policy needs a state_type
//
// A lock with member lock() and unlock(), as an RTOS mutex object has. The policy
// is static, so without this check the error comes from inside the type.

#include <metl/lock.hpp>

struct member_lock {
  using state_type = int;
  int lock() { return 0; }
  void unlock(int) {}
};

metl::scoped_lock<metl::null_lock> control;

#ifdef METL_COMPILE_FAIL
metl::scoped_lock<member_lock> offender;
#endif
