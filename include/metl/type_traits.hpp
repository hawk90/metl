#pragma once

#include "metl/detail/abi.hpp"

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

/// @file
/// @brief Metaprogramming helpers: thin, C++17-friendly aliases over `<type_traits>` plus a few
///        original utilities (`storage_for`, `is_detected`). Public traits live in namespace `metl`;
///        `detail::` entries are internal.

namespace metl {
METL_DETAIL_ABI_BEGIN

/// @brief Correctly aligned raw byte buffer for one `T`; a replacement for the deprecated
///        `std::aligned_storage`.
/// @tparam T The type the storage is sized and aligned for.
/// @note Access the object via `ptr()`/`ref()`, which `std::launder` for strict-aliasing safety.
///       Does not construct or destroy the `T`; that is the caller's responsibility.
template <typename T>
struct storage_for {
  alignas(T) unsigned char bytes[sizeof(T)];

  /// @brief Laundered pointer to the stored object.
  T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(&bytes[0])); }
  /// @brief Laundered const pointer to the stored object.
  const T* ptr() const noexcept { return std::launder(reinterpret_cast<const T*>(&bytes[0])); }

  /// @brief Reference to the stored object (assumes it has been constructed).
  T& ref() noexcept { return *ptr(); }
  /// @brief Const reference to the stored object (assumes it has been constructed).
  const T& ref() const noexcept { return *ptr(); }

  /// @brief Raw address of the buffer, suitable for placement-new.
  void* addr() noexcept { return &bytes[0]; }
  /// @brief Raw const address of the buffer.
  const void* addr() const noexcept { return &bytes[0]; }
};

/// @brief Alias for `std::integral_constant`: a compile-time constant of type `T`.
template <typename T, T Value>
using integral_constant = std::integral_constant<T, Value>;

/// @brief Compile-time boolean constant.
template <bool Value>
using bool_constant = integral_constant<bool, Value>;

/// @brief Alias for `std::true_type`.
using true_type = std::true_type;
/// @brief Alias for `std::false_type`.
using false_type = std::false_type;

/// @brief Alias for `std::enable_if` (the trait, exposing a nested type).
template <bool Condition, typename T = void>
using enable_if = std::enable_if<Condition, T>;

/// @brief SFINAE helper: `T` when `Condition` is true, otherwise ill-formed.
template <bool Condition, typename T = void>
using enable_if_t = typename enable_if<Condition, T>::type;

/// @brief Maps any pack of types to `void`; the detection-idiom / SFINAE building block.
template <typename... Ts>
using void_t = void;

/// @brief Removes top-level const and volatile from `T`.
template <typename T>
using remove_cv_t = std::remove_cv_t<T>;

/// @brief Removes top-level const from `T`.
template <typename T>
using remove_const_t = std::remove_const_t<T>;

/// @brief Removes top-level volatile from `T`.
template <typename T>
using remove_volatile_t = std::remove_volatile_t<T>;

/// @brief Removes a reference from `T`.
template <typename T>
using remove_reference_t = std::remove_reference_t<T>;

/// @brief Adds an lvalue reference to `T` (reference-collapsing rules apply).
template <typename T>
using add_lvalue_reference_t = std::add_lvalue_reference_t<T>;

/// @brief Adds an rvalue reference to `T` (reference-collapsing rules apply).
template <typename T>
using add_rvalue_reference_t = std::add_rvalue_reference_t<T>;

/// @brief Removes reference then cv-qualifiers (the C++20 `remove_cvref_t` for C++17).
template <typename T>
using remove_cvref_t = remove_cv_t<remove_reference_t<T>>;

/// @brief Applies the decay conversions (array/function-to-pointer, cv/ref removal) to `T`.
template <typename T>
using decay_t = std::decay_t<T>;

/// @brief Underlying integer type of an enum `T`.
template <typename T>
using underlying_type_t = std::underlying_type_t<T>;

/// @brief Byte storage sized and aligned for `T`. Aliases metl's own
/// `storage_for<T>` rather than the C++23-deprecated `std::aligned_storage`.
template <typename T>
using aligned_storage_t = storage_for<T>;

/// @brief True if `T` and `U` are the same type.
template <typename T, typename U>
inline constexpr bool is_same_v = std::is_same_v<T, U>;

/// @brief True if `Base` is a base class of `Derived`.
template <typename Base, typename Derived>
inline constexpr bool is_base_of_v = std::is_base_of_v<Base, Derived>;

/// @brief True if `T` is const-qualified.
template <typename T>
inline constexpr bool is_const_v = std::is_const_v<T>;

/// @brief True if `T` is a reference type.
template <typename T>
inline constexpr bool is_reference_v = std::is_reference_v<T>;

/// @brief True if `T` is an lvalue reference.
template <typename T>
inline constexpr bool is_lvalue_reference_v = std::is_lvalue_reference_v<T>;

/// @brief True if `T` is an rvalue reference.
template <typename T>
inline constexpr bool is_rvalue_reference_v = std::is_rvalue_reference_v<T>;

/// @brief True if `T` is a pointer type.
template <typename T>
inline constexpr bool is_pointer_v = std::is_pointer_v<T>;

/// @brief True if `T` is an integral type.
template <typename T>
inline constexpr bool is_integral_v = std::is_integral_v<T>;

/// @brief True if `T` is an unsigned arithmetic type.
template <typename T>
inline constexpr bool is_unsigned_v = std::is_unsigned_v<T>;

/// @brief True if `T` is a signed arithmetic type.
template <typename T>
inline constexpr bool is_signed_v = std::is_signed_v<T>;

/// @brief True if `T` is an enumeration type.
template <typename T>
inline constexpr bool is_enum_v = std::is_enum_v<T>;

/// @brief True if `T` is a union type.
template <typename T>
inline constexpr bool is_union_v = std::is_union_v<T>;

/// @brief True if `T` is a non-union class type.
template <typename T>
inline constexpr bool is_class_v = std::is_class_v<T>;

/// @brief True if `T` is a trivial type.
template <typename T>
inline constexpr bool is_trivial_v = std::is_trivial_v<T>;

/// @brief True if `T` is trivially copyable.
template <typename T>
inline constexpr bool is_trivially_copyable_v = std::is_trivially_copyable_v<T>;

/// @brief True if `T` is trivially destructible.
template <typename T>
inline constexpr bool is_trivially_destructible_v = std::is_trivially_destructible_v<T>;

/// @brief True if `T` is default-constructible.
template <typename T>
inline constexpr bool is_default_constructible_v = std::is_default_constructible_v<T>;

/// @brief True if `T` is copy-constructible.
template <typename T>
inline constexpr bool is_copy_constructible_v = std::is_copy_constructible_v<T>;

/// @brief True if `T` is move-constructible.
template <typename T>
inline constexpr bool is_move_constructible_v = std::is_move_constructible_v<T>;

/// @brief True if `T` is copy-assignable.
template <typename T>
inline constexpr bool is_copy_assignable_v = std::is_copy_assignable_v<T>;

/// @brief True if `T` is move-assignable.
template <typename T>
inline constexpr bool is_move_assignable_v = std::is_move_assignable_v<T>;

/// @brief True if `T` is nothrow move-constructible.
template <typename T>
inline constexpr bool is_nothrow_move_constructible_v = std::is_nothrow_move_constructible_v<T>;

/// @brief True if `T` is nothrow move-assignable.
template <typename T>
inline constexpr bool is_nothrow_move_assignable_v = std::is_nothrow_move_assignable_v<T>;

/// @brief True if `T` is default-constructible (single-argument form of `std::is_constructible`).
template <typename T>
inline constexpr bool is_constructible_v = std::is_constructible_v<T>;

/// @brief True if `T` is constructible from `Args...`.
template <typename T, typename... Args>
inline constexpr bool is_constructible_from_v = std::is_constructible_v<T, Args...>;

/// @brief Short-circuiting logical AND over a pack of traits (C++17 `std::conjunction`).
template <typename...>
struct conjunction : true_type {};

template <typename Trait>
struct conjunction<Trait> : Trait {};

template <typename First, typename... Rest>
struct conjunction<First, Rest...> : std::conditional_t<bool(First::value), conjunction<Rest...>, First> {};

/// @brief Short-circuiting logical OR over a pack of traits (C++17 `std::disjunction`).
template <typename...>
struct disjunction : false_type {};

template <typename Trait>
struct disjunction<Trait> : Trait {};

template <typename First, typename... Rest>
struct disjunction<First, Rest...> : std::conditional_t<bool(First::value), First, disjunction<Rest...>> {};

/// @brief Logical NOT of a trait's boolean value member (C++17 `std::negation`).
template <typename Trait>
struct negation : bool_constant<!bool(Trait::value)> {};

/// @brief Value of `conjunction<Traits...>`.
template <typename... Traits>
inline constexpr bool conjunction_v = conjunction<Traits...>::value;

/// @brief Value of `disjunction<Traits...>`.
template <typename... Traits>
inline constexpr bool disjunction_v = disjunction<Traits...>::value;

/// @brief Value of `negation<Trait>`.
template <typename Trait>
inline constexpr bool negation_v = negation<Trait>::value;

namespace detail {

template <typename, template <typename...> class, typename...>
struct is_detected_impl : false_type {};

template <template <typename...> class Operation, typename... Args>
struct is_detected_impl<void_t<Operation<Args...>>, Operation, Args...> : true_type {};

// Always true. Naming a template parameter in a static_assert condition
// defers the check to instantiation.
template <typename...>
inline constexpr bool dependent_true_v = true;

// T, or int when T is not an object type (a reference, function, array or
// void). A class that keeps its body in a base builds the base from this, so
// that its own METL_DETAIL_OBJECT_TYPE_MESSAGE static_assert is the only error
// for such a T, instead of the base failing first on a member it cannot form.
template <typename T>
using object_or_placeholder_t = std::conditional_t<std::is_object_v<T> && !std::is_array_v<T>, T, int>;

// The unsigned type of exactly Bytes bytes (1, 2, 4 or 8).
template <std::size_t Bytes>
using unsigned_of_size_t =
    std::conditional_t<Bytes == 1,
                       std::uint8_t,
                       std::conditional_t<Bytes == 2,
                                          std::uint16_t,
                                          std::conditional_t<Bytes == 4, std::uint32_t, std::uint64_t>>>;

// How many bytes a count up to n needs. Compared as 64-bit, so a 32-bit
// size_t does not make the last test a tautology.
constexpr std::size_t count_bytes(std::size_t n) noexcept {
  const auto wide = static_cast<std::uint64_t>(n);
  if (wide <= 0xFFU) {
    return 1;
  }
  if (wide <= 0xFFFFU) {
    return 2;
  }
  if (wide <= 0xFFFFFFFFU) {
    return 4;
  }
  return 8;
}
template <std::size_t N>
inline constexpr std::size_t count_bytes_v = count_bytes(N);

// How wide a fixed-capacity container's stored size is, in bytes: enough for
// Capacity, but not narrower than T's alignment (capped at size_t's width).
// Narrower than that would only turn into padding.
constexpr std::size_t stored_size_bytes(std::size_t count_bytes, std::size_t align) noexcept {
  const std::size_t floor = align < sizeof(std::size_t) ? align : sizeof(std::size_t);
  return count_bytes > floor ? count_bytes : floor;
}
template <std::size_t Capacity, typename T>
inline constexpr std::size_t stored_size_bytes_v = stored_size_bytes(count_bytes_v<Capacity>, alignof(T));

// The type a fixed-capacity container stores its size in, and a ring its head
// (#318 section 3). It narrows only where that makes the container smaller: a
// container of std::uint8_t stores a byte, one of int keeps a size_t. At
// size_t's width it is size_t itself, so those containers' code does not change.
// The interface still speaks size_type.
template <std::size_t Capacity, typename T>
using stored_size_t = std::conditional_t<stored_size_bytes_v<Capacity, T> == sizeof(std::size_t),
                                         std::size_t,
                                         unsigned_of_size_t<stored_size_bytes_v<Capacity, T>>>;

}  // namespace detail

/// @brief Detection idiom: true_type if `Operation<Args...>` is a valid type, else false_type.
/// @tparam Operation An alias template probed for well-formedness against `Args...`.
/// @tparam Args Arguments applied to `Operation`.
template <template <typename...> class Operation, typename... Args>
struct is_detected : detail::is_detected_impl<void, Operation, Args...> {};

/// @brief Value of `is_detected<Operation, Args...>`.
template <template <typename...> class Operation, typename... Args>
inline constexpr bool is_detected_v = is_detected<Operation, Args...>::value;

METL_DETAIL_ABI_END
}  // namespace metl
