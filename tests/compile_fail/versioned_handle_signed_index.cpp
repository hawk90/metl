// EXPECT-ERROR: versioned_handle IndexT must be unsigned
//
// The index and generation are packed by shifting and masking, and the
// generation counter is expected to WRAP -- that is how a stale handle stops
// matching. Signed overflow is UB rather than wrapping, so a signed field turns
// the one mechanism that makes stale handles detectable into undefined
// behaviour the optimiser is allowed to assume never happens.

#include <cstdint>

#include <metl/versioned_handle.hpp>

namespace {
struct tag {};
}  // namespace

using unsigned_index = metl::versioned_handle<tag, std::uint16_t, std::uint16_t>;
static_assert(unsigned_index::max_index > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using signed_index = metl::versioned_handle<tag, std::int16_t, std::uint16_t>;
static_assert(signed_index::max_index != 0, "forces the instantiation");
#endif
