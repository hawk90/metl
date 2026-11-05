#pragma once

/// @file
/// @brief Zeroing a container's storage only when it is being constant-initialized.
///
/// A namespace-scope container that an interrupt can reach before `main()` must
/// be valid when that interrupt fires. If it is dynamically initialized, its
/// constructor runs later, in link order, and resets whatever was pushed.
/// Constant initialization avoids that, but a constant-initialized object must
/// have every byte of its storage initialized, while a container's default
/// constructor deliberately leaves the element buffer alone, so that a local one
/// costs no `Capacity * sizeof(T)` stores.
///
/// `zero_for_constant_init` zeroes the buffer only during constant evaluation.
/// Since C++20 a `constexpr` constructor may leave members to its body, so a
/// namespace-scope container is constant-initialized, and lands in `.bss`,
/// while a local one still runs no zeroing code. In C++17 a `constexpr`
/// constructor must initialize every member up front, so these containers stay
/// dynamically initialized there, like `std::vector`.
///
/// Progress guarantee: wait-free, bounded (no code at run time).

#include "metl/compiler.hpp"
#include "metl/detail/abi.hpp"

#include <cstddef>
#include <type_traits>

namespace metl {
METL_DETAIL_ABI_BEGIN
namespace detail {

/// @brief True while the call is being constant-evaluated. False where the
///        compiler cannot tell, which only costs constant initialization.
METL_NODISCARD constexpr bool in_constant_evaluation() noexcept {
#ifdef __cpp_lib_is_constant_evaluated
  return std::is_constant_evaluated();
#elif METL_HAS_BUILTIN(__builtin_is_constant_evaluated) || METL_COMPILER_MSVC_VERSION >= 1925
  return __builtin_is_constant_evaluated();
#else
  return false;
#endif
}

/// 1 where a `constexpr` constructor may leave a member uninitialized until its
/// body (C++20, P1331), so it can zero storage only during constant evaluation.
/// 0 before that, where a type that promises constant initialization must
/// initialize the member up front, and so on every construction.
#if defined(__cpp_constexpr) && __cpp_constexpr >= 201907L
#define METL_DETAIL_LAZY_CONSTANT_INIT 1
#else
#define METL_DETAIL_LAZY_CONSTANT_INIT 0
#endif

/// @brief `N` bytes aligned for any scalar, in a struct so that
///        `zero_for_constant_init` clears them in one assignment (#628).
///        Converts to `unsigned char*`, so `storage_[i]` and `storage_ + n`
///        read as they do on the bare array it replaces under C++20; the C++17
///        build keeps that array, and its code, as it was.
template <std::size_t N>
struct max_aligned_bytes {
  alignas(std::max_align_t) unsigned char bytes[N];

  // Implicit on purpose: the containing class indexes it as it did the array.
  // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,google-explicit-constructor)
  constexpr operator unsigned char*() noexcept { return bytes; }
  // NOLINTNEXTLINE(cppcoreguidelines-explicit-constructor,google-explicit-constructor)
  constexpr operator const unsigned char*() const noexcept { return bytes; }
};

/// @brief Value-initializes `storage`, a buffer wrapped in a struct, during
///        constant evaluation; does nothing at run time.
///
/// One assignment of a value-initialized `S`, not a loop over its bytes: the
/// constant evaluator counts loop iterations, and GCC stops at 262144 and Clang
/// near a million, so a byte loop left every container of more than 256 KiB
/// dynamically initialized without a word (#613). Whole-buffer assignment was
/// measured to 16 MiB on both.
template <typename S, typename = std::enable_if_t<std::is_class_v<S>>>
constexpr void zero_for_constant_init(S& storage) noexcept {
  if (in_constant_evaluation()) {
    storage = S{};
  }
}

/// @brief An array of storage slots wrapped in a struct, so
///        `zero_for_constant_init` clears it in one assignment rather than one
///        iteration per slot (#613). Same layout as `S[N]`. Containers index
///        `items` directly: a subscript operator here changed GCC's code for
///        object_pool at -Os (+4 bytes on Cortex-M3), and a bare array
///        expression keeps the code what it was.
template <typename S, std::size_t N>
struct slot_array {
  S items[N];
};

/// @brief Zeroes `bytes` during constant evaluation; does nothing at run time.
///        For a bare array, which cannot be assigned: it loops, so it is for
///        small buffers only (`fixed_function`'s).
template <std::size_t N>
constexpr void zero_for_constant_init(unsigned char (&bytes)[N]) noexcept {
  if (in_constant_evaluation()) {
    for (std::size_t i = 0; i < N; ++i) {
      bytes[i] = 0;
    }
  }
}

}  // namespace detail
METL_DETAIL_ABI_END
}  // namespace metl
