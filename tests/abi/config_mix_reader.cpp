// Compiled twice into one program by tools/check_abi_tag.py, each time at a
// different METL_HARDENING level and with METL_ABI_READER naming the function.
// fixed_vector stays inside each translation unit: no METL type crosses into
// the other, so mixing the levels is allowed, and each copy must keep the
// checks its own level compiles in, whichever object the linker sees first.

#include <cstddef>

#include <metl/fixed_vector.hpp>

// A default name, so the file also compiles on its own, as every test does
// under the consumer-warning-flags check.
#ifndef METL_ABI_READER
#define METL_ABI_READER read_unchecked
#endif

int METL_ABI_READER(int index) noexcept {
  metl::fixed_vector<int, 4> values;
  for (int i = 0; i < 4; ++i) {
    values.push_back(7 + i);
  }
  values.resize(1);
  // Past size() but inside the storage: METL_ASSERT stops it at FAST and DEBUG,
  // and at NONE operator[] reads the slot like std::vector's.
  return values[static_cast<std::size_t>(index)];
}
