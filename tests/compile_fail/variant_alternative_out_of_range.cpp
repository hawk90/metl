// EXPECT-ERROR: variant_alternative index out of range
//
// Without the assertion this is a recursive nth_type instantiation that runs
// off the end of the pack, and the caller gets a page of template backtrace
// instead of a sentence.

#include <metl/variant.hpp>

using control = metl::variant_alternative_t<1, metl::variant<int, long>>;

#ifdef METL_COMPILE_FAIL
using offender = metl::variant_alternative_t<5, metl::variant<int, long>>;
#endif
