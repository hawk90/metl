// metl/exp/bitset.hpp (#527): metl::exp::bitset is metl::bitset, not a copy of
// it, so code that still names it there passes its sets to code that does not.

#include "metl_check.hpp"

#include <cstddef>
#include <type_traits>

#include <metl/exp/bitset.hpp>

namespace {

static_assert(std::is_same<metl::exp::bitset<1>, metl::bitset<1>>::value, "");
static_assert(std::is_same<metl::exp::bitset<33>, metl::bitset<33>>::value, "");

std::size_t lowest(const metl::bitset<40>& bits) {
  return bits.find_first_set();
}

}  // namespace

int main() {
  metl::exp::bitset<40> bits;
  bits.set(35);
  CHECK_EQ(lowest(bits), 35u);
  return metl_test::exit_code();
}
