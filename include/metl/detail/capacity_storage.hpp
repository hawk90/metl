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

#include "metl/detail/abi.hpp"
#include "metl/detail/constant_init.hpp"

#include <type_traits>

namespace metl {
METL_DETAIL_ABI_BEGIN
namespace detail {

/// The `Base` of a container that has no empty base of its own.
struct no_storage_base {};

template <typename Fields, typename Base = no_storage_base, bool = std::is_empty_v<Fields>>
class capacity_storage : Base {
 protected:
  capacity_storage() = default;

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
