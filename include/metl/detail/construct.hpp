#pragma once

// Internal object-lifetime helpers: metl::detail::construct_at / destroy_at.
//
// Placement-new and an explicit `->~T()` are NEVER usable in constant
// evaluation (C++17 or C++20), so a `constexpr` label on a constructor/accessor
// that routes through them is non-functional — the type cannot actually be
// constant-initialized.
//
// std::construct_at / std::destroy_at, by contrast, are `constexpr` since C++20
// (feature-test macro __cpp_lib_constexpr_dynamic_alloc). Types that want to be
// genuinely constexpr on a C++20 toolchain — and byte-for-byte unchanged on
// C++17 — route their storage lifetime through these helpers instead of raw
// placement-new:
//
//   * C++20: forwards to std::construct_at / std::destroy_at (constant-eval OK).
//   * C++17: falls back to placement-new / explicit destructor call (identical
//            to the previous hand-rolled code; still not constant-evaluable, so
//            behavior is unchanged).
//
// METL_CONSTEXPR20 expands to `constexpr` only when the C++20 constexpr path is
// active (constexpr destructors + constexpr construct_at), and to nothing on
// C++17. Use it on the members that drive an object's storage lifetime
// (constructors that construct into storage, reset/destroy, the destructor).

#include <new>
#include <utility>

#if defined(__cpp_lib_constexpr_dynamic_alloc) && __cpp_lib_constexpr_dynamic_alloc >= 201907L && \
    defined(__cpp_constexpr) && __cpp_constexpr >= 201907L
#include <memory>  // std::construct_at, constexpr std::destroy_at
#define METL_DETAIL_CONSTEXPR_LIFETIME 1
#define METL_CONSTEXPR20 constexpr
#else
#define METL_DETAIL_CONSTEXPR_LIFETIME 0
#define METL_CONSTEXPR20
#endif

namespace metl {
namespace detail {

#if METL_DETAIL_CONSTEXPR_LIFETIME

template <typename T, typename... Args>
constexpr T* construct_at(T* location, Args&&... args) {
  return std::construct_at(location, std::forward<Args>(args)...);
}

template <typename T>
constexpr void destroy_at(T* location) noexcept {
  std::destroy_at(location);
}

#else

template <typename T, typename... Args>
T* construct_at(T* location, Args&&... args) {
  return ::new (static_cast<void*>(location)) T(std::forward<Args>(args)...);
}

template <typename T>
void destroy_at(T* location) noexcept {
  location->~T();
}

#endif

}  // namespace detail
}  // namespace metl
