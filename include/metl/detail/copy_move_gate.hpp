#pragma once
/// @file
/// @brief Copy and move operations that exist only when the element allows them.
///
/// A container's copy constructor copies its elements, so it is ill-formed for
/// an element that cannot be copied. Declared for every `T`, it still makes
/// `std::is_copy_constructible_v<container<T>>` true, and the error appears only
/// when the body is instantiated, while `std::optional<container<T>>` or any
/// other code that asks the trait gets a hard error instead of a clean `false`
/// (#601). C++20 would put a `requires` clause on each operation; C++17 has two
/// pieces instead:
///
/// - `gated_param<Allowed, C>` is the parameter type of each copy or move
///   operation: `C` itself when the element allows the operation, otherwise a
///   type nothing can create. The declaration then is no copy or move operation
///   at all, only a constructor or assignment nobody can call.
/// - `no_implicit_copy_move` is an empty base whose copy and move operations
///   are deleted. With a gated operation gone, the compiler declares the
///   implicit one, and the base makes it deleted. When the operation is allowed,
///   the container's own one is user-provided and never uses the base's.
///
/// Neither changes a container's layout (an empty base) or the code of an
/// allowed operation (the same declaration as before).
///
/// Progress guarantee: wait-free, bounded (no code at run time).
#include <type_traits>

namespace metl {
namespace detail {

/// @brief A parameter type no caller can produce: it has no constructor.
template <int Which>
struct unusable_copy_move_parameter {
  unusable_copy_move_parameter() = delete;
};

/// @brief `Param` when the element allows the operation; otherwise a type no
///        argument converts to, so the declaration is not a copy or move operation.
/// @tparam Which Tells the four operations' placeholders apart, so no two
///         declarations collide.
template <bool Allowed, typename Param, int Which>
using gated_param = std::conditional_t<Allowed, Param, const unusable_copy_move_parameter<Which>&>;

/// @brief Empty base that deletes the implicit copy and move operations.
/// @tparam Owner The class deriving from it. One type per class, so an
///         `optional<fixed_vector<T, N>>` holds two different empty bases at
///         offset 0 and the empty-base optimization still applies: two
///         subobjects of one type could not share an address.
template <typename Owner>
struct no_implicit_copy_move {
  no_implicit_copy_move() = default;
  no_implicit_copy_move(const no_implicit_copy_move&) = delete;
  no_implicit_copy_move(no_implicit_copy_move&&) = delete;
  no_implicit_copy_move& operator=(const no_implicit_copy_move&) = delete;
  no_implicit_copy_move& operator=(no_implicit_copy_move&&) = delete;
  ~no_implicit_copy_move() = default;
};

}  // namespace detail
}  // namespace metl
