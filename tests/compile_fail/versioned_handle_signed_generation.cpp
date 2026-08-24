// EXPECT-ERROR: versioned_handle GenT must be unsigned
//
// The companion to the IndexT case: the generation is the half that is MEANT
// to wrap, so a signed type makes the wrap undefined behaviour rather than the
// mechanism. Pinned separately because it is a separate assertion -- a caller
// can get the index type right and the generation type wrong.

#include <cstdint>

#include <metl/versioned_handle.hpp>

namespace {
struct tag {};
}  // namespace

using unsigned_generation = metl::versioned_handle<tag, std::uint16_t, std::uint16_t>;
static_assert(unsigned_generation::max_index > 0, "control instantiation");

#ifdef METL_COMPILE_FAIL
using signed_generation = metl::versioned_handle<tag, std::uint16_t, std::int16_t>;
static_assert(signed_generation::max_index != 0, "forces the instantiation");
#endif
