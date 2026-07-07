#pragma once

#include <cstddef>
#include <new>
#include <type_traits>

namespace metl {

// Replacement for std::aligned_storage (deprecated in C++23).
// Use `storage_for<T>` as a raw bytes buffer with correct alignment.
// Access via `storage.ptr()` which performs std::launder for strict-aliasing safety.
template <typename T>
struct storage_for {
  alignas(T) unsigned char bytes[sizeof(T)];

  T* ptr() noexcept { return std::launder(reinterpret_cast<T*>(&bytes[0])); }
  const T* ptr() const noexcept { return std::launder(reinterpret_cast<const T*>(&bytes[0])); }

  T& ref() noexcept { return *ptr(); }
  const T& ref() const noexcept { return *ptr(); }

  void* addr() noexcept { return &bytes[0]; }
  const void* addr() const noexcept { return &bytes[0]; }
};

template <typename T, T Value>
using integral_constant = std::integral_constant<T, Value>;

template <bool Value>
using bool_constant = integral_constant<bool, Value>;

using true_type = std::true_type;
using false_type = std::false_type;

template <bool Condition, typename T = void>
using enable_if = std::enable_if<Condition, T>;

template <bool Condition, typename T = void>
using enable_if_t = typename enable_if<Condition, T>::type;

template <typename... Ts>
using void_t = void;

template <typename T>
using remove_cv_t = typename std::remove_cv<T>::type;

template <typename T>
using remove_const_t = typename std::remove_const<T>::type;

template <typename T>
using remove_volatile_t = typename std::remove_volatile<T>::type;

template <typename T>
using remove_reference_t = typename std::remove_reference<T>::type;

template <typename T>
using add_lvalue_reference_t = typename std::add_lvalue_reference<T>::type;

template <typename T>
using add_rvalue_reference_t = typename std::add_rvalue_reference<T>::type;

template <typename T>
using remove_cvref_t = remove_cv_t<remove_reference_t<T>>;

template <typename T>
using decay_t = typename std::decay<T>::type;

template <typename T>
using underlying_type_t = typename std::underlying_type<T>::type;

template <typename T>
using aligned_storage_t = typename std::aligned_storage<sizeof(T), alignof(T)>::type;

template <typename T, typename U>
inline constexpr bool is_same_v = std::is_same<T, U>::value;

template <typename Base, typename Derived>
inline constexpr bool is_base_of_v = std::is_base_of<Base, Derived>::value;

template <typename T>
inline constexpr bool is_const_v = std::is_const<T>::value;

template <typename T>
inline constexpr bool is_reference_v = std::is_reference<T>::value;

template <typename T>
inline constexpr bool is_lvalue_reference_v = std::is_lvalue_reference<T>::value;

template <typename T>
inline constexpr bool is_rvalue_reference_v = std::is_rvalue_reference<T>::value;

template <typename T>
inline constexpr bool is_pointer_v = std::is_pointer<T>::value;

template <typename T>
inline constexpr bool is_integral_v = std::is_integral<T>::value;

template <typename T>
inline constexpr bool is_unsigned_v = std::is_unsigned<T>::value;

template <typename T>
inline constexpr bool is_signed_v = std::is_signed<T>::value;

template <typename T>
inline constexpr bool is_enum_v = std::is_enum<T>::value;

template <typename T>
inline constexpr bool is_union_v = std::is_union<T>::value;

template <typename T>
inline constexpr bool is_class_v = std::is_class<T>::value;

template <typename T>
inline constexpr bool is_trivial_v = std::is_trivial<T>::value;

template <typename T>
inline constexpr bool is_trivially_copyable_v = std::is_trivially_copyable<T>::value;

template <typename T>
inline constexpr bool is_trivially_destructible_v = std::is_trivially_destructible<T>::value;

template <typename T>
inline constexpr bool is_default_constructible_v = std::is_default_constructible<T>::value;

template <typename T>
inline constexpr bool is_copy_constructible_v = std::is_copy_constructible<T>::value;

template <typename T>
inline constexpr bool is_move_constructible_v = std::is_move_constructible<T>::value;

template <typename T>
inline constexpr bool is_copy_assignable_v = std::is_copy_assignable<T>::value;

template <typename T>
inline constexpr bool is_move_assignable_v = std::is_move_assignable<T>::value;

template <typename T>
inline constexpr bool is_nothrow_move_constructible_v = std::is_nothrow_move_constructible<T>::value;

template <typename T>
inline constexpr bool is_nothrow_move_assignable_v = std::is_nothrow_move_assignable<T>::value;

template <typename T>
inline constexpr bool is_constructible_v = std::is_constructible<T>::value;

template <typename T, typename... Args>
inline constexpr bool is_constructible_from_v = std::is_constructible<T, Args...>::value;

template <typename...>
struct conjunction : true_type {};

template <typename Trait>
struct conjunction<Trait> : Trait {};

template <typename First, typename... Rest>
struct conjunction<First, Rest...> : std::conditional_t<bool(First::value), conjunction<Rest...>, First> {};

template <typename...>
struct disjunction : false_type {};

template <typename Trait>
struct disjunction<Trait> : Trait {};

template <typename First, typename... Rest>
struct disjunction<First, Rest...> : std::conditional_t<bool(First::value), First, disjunction<Rest...>> {};

template <typename Trait>
struct negation : bool_constant<!bool(Trait::value)> {};

template <typename... Traits>
inline constexpr bool conjunction_v = conjunction<Traits...>::value;

template <typename... Traits>
inline constexpr bool disjunction_v = disjunction<Traits...>::value;

template <typename Trait>
inline constexpr bool negation_v = negation<Trait>::value;

namespace detail {

template <typename, template <typename...> class, typename...>
struct is_detected_impl : false_type {};

template <template <typename...> class Operation, typename... Args>
struct is_detected_impl<void_t<Operation<Args...>>, Operation, Args...> : true_type {};

}  // namespace detail

template <template <typename...> class Operation, typename... Args>
struct is_detected : detail::is_detected_impl<void, Operation, Args...> {};

template <template <typename...> class Operation, typename... Args>
inline constexpr bool is_detected_v = is_detected<Operation, Args...>::value;

}  // namespace metl
