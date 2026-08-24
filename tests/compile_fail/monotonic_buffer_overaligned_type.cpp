// EXPECT-ERROR: type alignment exceeds monotonic_buffer maximum alignment
//
// Same bound as the arena's, in a separate header with its own assertion --
// pinned separately because a shared reason is not a shared line of code, and
// #92's census counts the two messages separately for exactly that reason.

#include <cstddef>
#include <cstdint>

#include <metl/monotonic_buffer.hpp>

namespace {

struct normal {
  std::uint64_t value;
};

struct alignas(2 * alignof(std::max_align_t)) overaligned {
  std::uint64_t value;
};

}  // namespace

normal* control(metl::monotonic_buffer<256>& buffer) {
  return buffer.try_emplace<normal>();
}

#ifdef METL_COMPILE_FAIL
overaligned* offender(metl::monotonic_buffer<256>& buffer) {
  return buffer.try_emplace<overaligned>();
}
#endif
