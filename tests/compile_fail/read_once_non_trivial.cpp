// EXPECT-ERROR: read_once requires a trivially copyable type
//
// read_once exists to make ONE volatile access the compiler may not split,
// duplicate or reorder. A non-trivially-copyable type is read by running a copy
// constructor, which is an arbitrary amount of ordinary code over a volatile
// object -- so the single-access guarantee the function's whole name is about
// quietly stops holding.

#include <cstdint>

#include <metl/register_access.hpp>

namespace {

struct has_copy_ctor {
  std::uint32_t value;
  has_copy_ctor(const volatile has_copy_ctor& other) : value(other.value) {}
};

}  // namespace

std::uint32_t control(const volatile std::uint32_t* reg) {
  return metl::read_once(reg);
}

#ifdef METL_COMPILE_FAIL
std::uint32_t offender(const volatile has_copy_ctor* reg) {
  return metl::read_once(reg).value;
}
#endif
