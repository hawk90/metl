#pragma once

#include <cstddef>
#include <new>
#include <type_traits>

/// @file
/// @brief Metaprogramming helpers: thin, C++17-friendly aliases over `<type_traits>` plus a few
///        original utilities (`storage_for`, `is_detected`). Public traits live in namespace `metl`;
///        `detail::` entries are internal.

namespace metl {

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
using remove_cv_t = typename std::remove_cv<T>::type;

/// @brief Removes top-level const from `T`.
template <typename T>
using remove_const_t = typename std::remove_const<T>::type;

/// @brief Removes top-level volatile from `T`.
template <typename T>
using remove_volatile_t = typename std::remove_volatile<T>::type;

/// @brief Removes a reference from `T`.
template <typename T>
using remove_reference_t = typename std::remove_reference<T>::type;

/// @brief Adds an lvalue reference to `T` (reference-collapsing rules apply).
template <typename T>
using add_lvalue_reference_t = typename std::add_lvalue_reference<T>::type;

/// @brief Adds an rvalue reference to `T` (reference-collapsing rules apply).
template <typename T>
using add_rvalue_reference_t = typename std::add_rvalue_reference<T>::type;

/// @brief Removes reference then cv-qualifiers (the C++20 `remove_cvref_t` for C++17).
template <typename T>
using remove_cvref_t = remove_cv_t<remove_reference_t<T>>;

/// @brief Applies the decay conversions (array/function-to-pointer, cv/ref removal) to `T`.
template <typename T>
using decay_t = typename std::decay<T>::type;

/// @brief Underlying integer type of an enum `T`.
template <typename T>
using underlying_type_t = typename std::underlying_type<T>::type;

/// @brief Byte storage type sized and aligned for `T` (alias for `std::aligned_storage`).
template <typename T>
using aligned_storage_t = typename std::aligned_storage<sizeof(T), alignof(T)>::type;

/// @brief True if `T` and `U` are the same type.
template <typename T, typename U>
inline constexpr bool is_same_v = std::is_same<T, U>::value;

/// @brief True if `Base` is a base class of `Derived`.
template <typename Base, typename Derived>
inline constexpr bool is_base_of_v = std::is_base_of<Base, Derived>::value;

/// @brief True if `T` is const-qualified.
template <typename T>
inline constexpr bool is_const_v = std::is_const<T>::value;

/// @brief True if `T` is a reference type.
template <typename T>
inline constexpr bool is_reference_v = std::is_reference<T>::value;

/// @brief True if `T` is an lvalue reference.
template <typename T>
inline constexpr bool is_lvalue_reference_v = std::is_lvalue_reference<T>::value;

/// @brief True if `T` is an rvalue reference.
template <typename T>
inline constexpr bool is_rvalue_reference_v = std::is_rvalue_reference<T>::value;

/// @brief True if `T` is a pointer type.
template <typename T>
inline constexpr bool is_pointer_v = std::is_pointer<T>::value;

/// @brief True if `T` is an integral type.
template <typename T>
inline constexpr bool is_integral_v = std::is_integral<T>::value;

/// @brief True if `T` is an unsigned arithmetic type.
template <typename T>
inline constexpr bool is_unsigned_v = std::is_unsigned<T>::value;

/// @brief True if `T` is a signed arithmetic type.
template <typename T>
inline constexpr bool is_signed_v = std::is_signed<T>::value;

/// @brief True if `T` is an enumeration type.
template <typename T>
inline constexpr bool is_enum_v = std::is_enum<T>::value;

/// @brief True if `T` is a union type.
template <typename T>
inline constexpr bool is_union_v = std::is_union<T>::value;

/// @brief True if `T` is a non-union class type.
template <typename T>
inline constexpr bool is_class_v = std::is_class<T>::value;

/// @brief True if `T` is a trivial type.
template <typename T>
inline constexpr bool is_trivial_v = std::is_trivial<T>::value;

/// @brief True if `T` is trivially copyable.
template <typename T>
inline constexpr bool is_trivially_copyable_v = std::is_trivially_copyable<T>::value;

/// @brief True if `T` is trivially destructible.
template <typename T>
inline constexpr bool is_trivially_destructible_v = std::is_trivially_destructible<T>::value;

/// @brief True if `T` is default-constructible.
template <typename T>
inline constexpr bool is_default_constructible_v = std::is_default_constructible<T>::value;

/// @brief True if `T` is copy-constructible.
template <typename T>
inline constexpr bool is_copy_constructible_v = std::is_copy_constructible<T>::value;

/// @brief True if `T` is move-constructible.
template <typename T>
inline constexpr bool is_move_constructible_v = std::is_move_constructible<T>::value;

/// @brief True if `T` is copy-assignable.
template <typename T>
inline constexpr bool is_copy_assignable_v = std::is_copy_assignable<T>::value;

/// @brief True if `T` is move-assignable.
template <typename T>
inline constexpr bool is_move_assignable_v = std::is_move_assignable<T>::value;

/// @brief True if `T` is nothrow move-constructible.
template <typename T>
inline constexpr bool is_nothrow_move_constructible_v = std::is_nothrow_move_constructible<T>::value;

/// @brief True if `T` is nothrow move-assignable.
template <typename T>
inline constexpr bool is_nothrow_move_assignable_v = std::is_nothrow_move_assignable<T>::value;

/// @brief True if `T` is default-constructible (single-argument form of `std::is_constructible`).
template <typename T>
inline constexpr bool is_constructible_v = std::is_constructible<T>::value;

/// @brief True if `T` is constructible from `Args...`.
template <typename T, typename... Args>
inline constexpr bool is_constructible_from_v = std::is_constructible<T, Args...>::value;

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

}  // namespace detail

/// @brief Detection idiom: true_type if `Operation<Args...>` is a valid type, else false_type.
/// @tparam Operation An alias template probed for well-formedness against `Args...`.
/// @tparam Args Arguments applied to `Operation`.
template <template <typename...> class Operation, typename... Args>
struct is_detected : detail::is_detected_impl<void, Operation, Args...> {};

/// @brief Value of `is_detected<Operation, Args...>`.
template <template <typename...> class Operation, typename... Args>
inline constexpr bool is_detected_v = is_detected<Operation, Args...>::value;

}  // namespace metl
