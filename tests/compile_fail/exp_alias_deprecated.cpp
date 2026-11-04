// EXPECT-WARNING: metl::exp::bitset is removed in 2.0
//
// The metl::exp forwarding names are deprecated for 2.0 (#645): a use warns
// and names the metl:: type and its header.

#include <metl/exp/bitset.hpp>

bool control(const metl::bitset<8>& b) {
  return b.test(0);
}

#ifdef METL_COMPILE_FAIL
bool offender(const metl::exp::bitset<8>& b) {
  return b.test(0);
}
#endif
