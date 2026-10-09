#pragma once

#include "metl/compiler.hpp"

namespace metl {
namespace detail {

/// @brief `std::addressof` without pulling in all of `<memory>`.
///
/// Library code that takes the address of a user's object must not write `&obj`:
/// a `T` with its own `operator&` would hand back whatever that returns, and a
/// `T` that deletes it would not compile. `__builtin_addressof` is available on
/// every compiler in the CI matrix (GCC, Clang, MSVC) and is constant-evaluable;
/// the fallback is the standard cast that sidesteps an overloaded `operator&`.
///
/// Progress guarantee: wait-free, bounded.
template <typename T>
constexpr T* addressof(T& object) noexcept {
#if METL_HAS_BUILTIN(__builtin_addressof) || defined(__GNUC__) || defined(_MSC_VER)
  return __builtin_addressof(object);
#else
  return reinterpret_cast<T*>(&const_cast<char&>(reinterpret_cast<const volatile char&>(object)));
#endif
}

template <typename T>
const T* addressof(const T&&) = delete;

}  // namespace detail
}  // namespace metl
