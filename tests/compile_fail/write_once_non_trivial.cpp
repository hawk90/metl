// EXPECT-ERROR: write_once requires a trivially copyable type
//
// The companion to read_once, and the more dangerous direction: a write that
// runs a copy assignment over a volatile object is an arbitrary number of
// stores to a peripheral register, in an order the compiler chose. Half-written
// control registers are how a peripheral ends up in a state its datasheet does
// not describe.

#include <cstdint>

#include <metl/register_access.hpp>

namespace {

struct has_user_dtor {
  std::uint32_t value{};
  // A user-provided destructor is non-trivial, so the type is not trivially
  // copyable -- without adding a copy operation that would fail on its own and
  // mask the assertion under test.
  ~has_user_dtor() {}
};

}  // namespace

void control(volatile std::uint32_t* reg) {
  metl::write_once(reg, std::uint32_t{1});
}

#ifdef METL_COMPILE_FAIL
void offender(volatile has_user_dtor* reg) {
  metl::write_once(reg, has_user_dtor{});
}
#endif
