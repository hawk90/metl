// EXPECT-ERROR: versioned_handle IndexT must not be bool
//
// A bool index addresses two slots and packs as one bit of a byte-sized
// field; it is never what a caller meant.

#include <metl/versioned_handle.hpp>

struct tag {};

inline bool control() {
  metl::versioned_handle<tag, std::uint8_t, std::uint8_t> handle{};
  return handle.valid();
}

#ifdef METL_COMPILE_FAIL
inline bool uses_a_bool_index() {
  metl::versioned_handle<tag, bool, std::uint8_t> handle{};
  return handle.valid();
}
#endif
