#pragma once

/// @file
/// @brief Holds a container's capacity-sized storage as a base, so that a
///        zero-capacity container takes no room for it (#696).
///
/// An empty member still takes a byte, and the next member's alignment pads it
/// back to a whole word. Measured on a 64-bit host: storage for one `int`
/// followed by a `size_t` is 16 B, and an empty member in its place is also 16 B.
/// An empty BASE takes no room, so the storage lives here:
///
/// - When `Fields` is not empty (`Capacity > 0`), it is the first and only data
///   member of this base. A base sits at offset 0, so the container's layout is
///   what it was with `Fields` as its first member.
/// - When `Fields` is empty (`Capacity == 0`), there is no member at all, and
///   `storage()` returns a fresh `Fields`. The empty storage types answer
///   `nullptr` for every address. A zero-capacity container never dereferences
///   one, because it never holds an element.
///
/// `Base` is the container's existing empty base (`no_implicit_copy_move`), if it
/// has one. It is chained through this class rather than listed beside it: MSVC
/// applies the empty-base optimisation to only one of several empty bases unless
/// the class is marked `__declspec(empty_bases)`, and a chain has only one
/// empty base per class.

#include "metl/compiler.hpp"
#include "metl/detail/abi.hpp"
#include "metl/detail/constant_init.hpp"

#include <type_traits>

// Opens a zero-capacity container's slot accessors, which no call reaches: the
// container holds no element. GCC is told so; otherwise it reports the empty
// storage's null address (-Wnonnull, -Wnull-dereference) on paths it cannot
// see are dead. MSVC is not: its `__assume(0)`, inlined into a caller in a
// Release build, makes it report the caller's code after the call as
// unreachable (C4702), and it has no null-address warning to quiet.
#if METL_COMPILER_MSVC
#define METL_DETAIL_NO_SLOT_IS_REACHED() static_cast<void>(0)
#else
#define METL_DETAIL_NO_SLOT_IS_REACHED() METL_UNREACHABLE()
#endif

namespace metl {
METL_DETAIL_ABI_BEGIN
namespace detail {

/// The `Base` of a container that has no empty base of its own.
struct no_storage_base {};

template <typename Fields, typename Base = no_storage_base, bool = std::is_empty_v<Fields>>
class capacity_storage : Base {
 protected:
  // User-provided, and not `= default`: with a defaulted one, GCC 14 for
  // Cortex-M zero-filled the whole buffer with a memset call each time a
  // fixed_vector was constructed (+16 B of code in the invariant probe). A
  // user-provided constructor leaves the buffer uninitialized, as the member
  // it replaces was. Constant-initialized containers zero it themselves
  // (zero_storage_for_constant_init).
#if METL_DETAIL_LAZY_CONSTANT_INIT
  // NOLINTNEXTLINE(modernize-use-equals-default,hicpp-use-equals-default)
  constexpr capacity_storage() noexcept {}
#else
  // NOLINTNEXTLINE(modernize-use-equals-default,hicpp-use-equals-default)
  capacity_storage() noexcept {}
#endif

  Fields& storage() noexcept { return storage_; }
  const Fields& storage() const noexcept { return storage_; }

  /// Zeroes the storage during constant evaluation (detail/constant_init.hpp).
  constexpr void zero_storage_for_constant_init() noexcept { zero_for_constant_init(storage_); }

 private:
  Fields storage_;
};

template <typename Fields, typename Base>
class capacity_storage<Fields, Base, true> : Base {
 protected:
  capacity_storage() = default;

  static constexpr Fields storage() noexcept { return Fields{}; }
  static constexpr void zero_storage_for_constant_init() noexcept {}
};

}  // namespace detail
METL_DETAIL_ABI_END
}  // namespace metl
