// EXPECT-ERROR: type alignment exceeds arena_allocator maximum alignment
//
// The arena hands out storage aligned to max_align_t. A type wanting more than
// that gets a pointer that is merely mis-aligned -- which on x86 is a slower
// load nobody notices, and on the Cortex-M targets METL exists for is a
// HardFault at whatever unrelated moment the object is first touched.

#include <cstddef>
#include <cstdint>

#include <metl/arena_allocator.hpp>

namespace {

struct normal {
  std::uint64_t value;
};

struct alignas(2 * alignof(std::max_align_t)) overaligned {
  std::uint64_t value;
};

}  // namespace

normal* control(metl::arena_allocator<256>& arena) {
  return arena.try_emplace<normal>();
}

#ifdef METL_COMPILE_FAIL
overaligned* offender(metl::arena_allocator<256>& arena) {
  return arena.try_emplace<overaligned>();
}
#endif
