// EXPECT-ERROR: versioned_handle GenT must not be bool
//
// bool passes std::is_unsigned, but `true + 1` converts back to `true`: the
// generation never advances, so every stale handle stays valid -- the one
// thing handle_pool exists to prevent.

#include <metl/handle_pool.hpp>

inline int control() {
  metl::handle_pool<int, 2, std::uint8_t> pool;  // an 8-bit generation is fine
  return pool.contains(pool.emplace(1)) ? 0 : 1;
}

#ifdef METL_COMPILE_FAIL
inline int uses_a_bool_generation() {
  metl::handle_pool<int, 2, bool> pool;
  return pool.contains(pool.emplace(1)) ? 0 : 1;
}
#endif
