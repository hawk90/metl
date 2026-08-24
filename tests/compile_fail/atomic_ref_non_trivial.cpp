// EXPECT-ERROR: atomic_ref requires a trivially copyable type
//
// An atomic load or store of a non-trivially-copyable type would have to run a
// constructor to produce the value, which is ordinary code the hardware cannot
// make indivisible. There is no way to implement it correctly, so accepting the
// type would mean a load that is atomic in name and torn in fact.

#include <cstdint>

#include <metl/atomic_ref.hpp>

namespace {

struct trivial {
  std::uint32_t value;
};

struct has_user_copy {
  std::uint32_t value;
  has_user_copy(const has_user_copy& other) : value(other.value) {}
};

}  // namespace

void control(trivial& slot) {
  metl::atomic_ref<trivial> ref(slot);
  (void)ref;
}

#ifdef METL_COMPILE_FAIL
void offender(has_user_copy& slot) {
  metl::atomic_ref<has_user_copy> ref(slot);
  (void)ref;
}
#endif
