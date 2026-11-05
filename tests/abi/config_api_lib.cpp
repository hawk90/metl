// A library function that takes a METL type, as a prebuilt SDK would.
// tools/check_abi_tag.py builds it with one configuration and
// config_api_main.cpp with another: the program must link only when the two
// agree on everything the ABI tag encodes.

#include <metl/fixed_vector.hpp>

int api(metl::fixed_vector<int, 4>& values) noexcept {
  return values.empty() ? -1 : values[0];
}
