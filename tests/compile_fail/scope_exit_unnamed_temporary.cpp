// EXPECT-WARNING: declared with 'nodiscard'
// WARNS-ON: clang
//
// An unnamed scope_exit is a temporary: it runs its callable at the semicolon,
// not at the end of the scope (#640). The class is nodiscard, so clang warns.
// GCC 16 gives no warning for a discarded temporary in C++17, hence WARNS-ON.

#include <metl/scope_exit.hpp>

void control(int& n) {
  metl::scope_exit guard([&n]() noexcept { ++n; });
}

#ifdef METL_COMPILE_FAIL
void offender(int& n) {
  metl::scope_exit([&n]() noexcept { ++n; });
}
#endif
