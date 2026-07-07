#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/construct.hpp"
#include "metl/in_place.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace metl {

/// @brief A fixed-storage nullable value wrapper (in-place, no heap).
///
/// Stores an optional `T` inside the object itself using an internal union; it
/// never allocates. Trivially copyable when `T` is. On C++20 it is a literal
/// type and usable in constant expressions.
/// @tparam T The contained value type.
/// @note Accessors like `value()`, `operator*`, and `operator->` ASSERT (abort
/// by default) rather than throwing when the optional is empty. Use `value_or`
/// for a safe alternative that never asserts.
template <typename T>
class optional {
 public:
  using value_type = T;

  // ---- Constructors ---------------------------------------------------------

  /// @brief Constructs an empty optional.
  constexpr optional() noexcept : storage_{}, has_value_(false) {}

  /// @brief Constructs an empty optional from the nullopt tag.
  constexpr optional(nullopt_t) noexcept : storage_{}, has_value_(false) {}

  /// @brief Constructs an engaged optional by forwarding a value into `T`.
  /// @param v The value used to direct-initialize the contained `T`.
  // Converting forwarding constructor. SFINAE prevents hijacking copy/move and
  // in_place_t overloads (e.g. `optional<optional<T>>` should not bind here).
  template <typename U = T,
            typename = std::enable_if_t<!std::is_same<std::decay_t<U>, optional>::value &&
                                        !std::is_same<std::decay_t<U>, in_place_t>::value &&
                                        !std::is_same<std::decay_t<U>, nullopt_t>::value &&
                                        std::is_constructible<T, U>::value>>
  constexpr optional(U&& v) : storage_{}, has_value_(false) {
    construct(std::forward<U>(v));
  }

  /// @brief Constructs an engaged optional, building `T` in place.
  /// @tparam Args Constructor argument types forwarded to `T`.
  /// @param args Arguments forwarded to `T`'s constructor.
  // in_place_t constructor.
  template <typename... Args>
  constexpr explicit optional(in_place_t, Args&&... args) : storage_{}, has_value_(false) {
    construct(std::forward<Args>(args)...);
  }

  /// @brief Copy constructor; copies the contained value if engaged.
  optional(const optional& other) : storage_{}, has_value_(false) {
    if (other.has_value_) {
      construct(*other);
    }
  }

  /// @brief Move constructor; moves the contained value if engaged.
  /// @note The source remains engaged (its value is moved-from), not reset.
  optional(optional&& other) noexcept(std::is_nothrow_move_constructible<T>::value)
      : storage_{}, has_value_(false) {
    if (other.has_value_) {
      construct(static_cast<T&&>(*other));
    }
  }

  /// @brief Destroys the contained value if engaged (constexpr on C++20).
  METL_CONSTEXPR20 ~optional() { reset(); }

  // ---- Assignment -----------------------------------------------------------

  /// @brief Resets to empty from the nullopt tag.
  optional& operator=(nullopt_t) noexcept {
    reset();
    return *this;
  }

  /// @brief Copy-assigns from another optional, matching engaged state.
  optional& operator=(const optional& other) {
    if (this == &other) {
      return *this;
    }

    if (other.has_value_) {
      assign_or_construct(*other);
    } else {
      reset();
    }

    return *this;
  }

  /// @brief Move-assigns from another optional, matching engaged state.
  optional& operator=(optional&& other) noexcept(std::is_nothrow_move_assignable<T>::value &&
                                                 std::is_nothrow_move_constructible<T>::value) {
    if (this == &other) {
      return *this;
    }

    if (other.has_value_) {
      assign_or_construct(static_cast<T&&>(*other));
    } else {
      reset();
    }

    return *this;
  }

  /// @brief Assigns a value, engaging the optional (constructs or assigns `T`).
  /// @param value The value forwarded into the contained `T`.
  template <
      typename U = T,
      typename = std::enable_if_t<!std::is_same<std::decay_t<U>, optional>::value &&
                                  std::is_constructible<T, U>::value && std::is_assignable<T&, U>::value>>
  optional& operator=(U&& value) {
    assign_or_construct(std::forward<U>(value));
    return *this;
  }

  // ---- Observers ------------------------------------------------------------

  /// @brief Accesses a member of the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does not throw.
  METL_NODISCARD constexpr const T* operator->() const noexcept {
    METL_ASSERT(has_value_);
    return data();
  }

  /// @brief Accesses a member of the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does not throw.
  METL_NODISCARD T* operator->() noexcept {
    METL_ASSERT(has_value_);
    return data();
  }

  /// @brief Returns a reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does not throw.
  METL_NODISCARD constexpr const T& operator*() const& noexcept {
    METL_ASSERT(has_value_);
    return *data();
  }

  /// @brief Returns a reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does not throw.
  METL_NODISCARD T& operator*() & noexcept {
    METL_ASSERT(has_value_);
    return *data();
  }

  /// @brief Returns an rvalue reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does not throw.
  METL_NODISCARD const T&& operator*() const&& noexcept {
    METL_ASSERT(has_value_);
    return static_cast<const T&&>(*data());
  }

  /// @brief Returns an rvalue reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does not throw.
  METL_NODISCARD T&& operator*() && noexcept {
    METL_ASSERT(has_value_);
    return static_cast<T&&>(*data());
  }

  /// @brief Returns true if the optional holds a value.
  METL_NODISCARD constexpr explicit operator bool() const noexcept { return has_value_; }
  /// @brief Returns true if the optional holds a value.
  METL_NODISCARD constexpr bool has_value() const noexcept { return has_value_; }

  /// @brief Returns a reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does NOT throw
  /// std::bad_optional_access. Use `value_or` for a checked fallback.
  METL_NODISCARD T& value() & noexcept {
    METL_ASSERT(has_value_);
    return *data();
  }

  /// @brief Returns a reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does NOT throw.
  METL_NODISCARD constexpr const T& value() const& noexcept {
    METL_ASSERT(has_value_);
    return *data();
  }

  /// @brief Returns an rvalue reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does NOT throw.
  METL_NODISCARD T&& value() && noexcept {
    METL_ASSERT(has_value_);
    return static_cast<T&&>(*data());
  }

  /// @brief Returns an rvalue reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does NOT throw.
  METL_NODISCARD constexpr const T&& value() const&& noexcept {
    METL_ASSERT(has_value_);
    return static_cast<const T&&>(*data());
  }

  /// @brief Returns the contained value, or `default_value` if empty.
  /// @param default_value Returned (converted to `T`) when the optional is empty.
  /// @return The contained value or the fallback. Never asserts.
  template <typename U>
  METL_NODISCARD T value_or(U&& default_value) const& {
    return has_value_ ? *data() : static_cast<T>(std::forward<U>(default_value));
  }

  /// @brief Returns the contained value, or `default_value` if empty. Never asserts.
  template <typename U>
  METL_NODISCARD T value_or(U&& default_value) & {
    return has_value_ ? *data() : static_cast<T>(std::forward<U>(default_value));
  }

  /// @brief Returns the contained value, or `default_value` if empty. Never asserts.
  template <typename U>
  METL_NODISCARD T value_or(U&& default_value) && {
    return has_value_ ? static_cast<T&&>(*data()) : static_cast<T>(std::forward<U>(default_value));
  }

  /// @brief Returns the contained value, or `default_value` if empty. Never asserts.
  template <typename U>
  METL_NODISCARD T value_or(U&& default_value) const&& {
    return has_value_ ? static_cast<const T&&>(*data()) : static_cast<T>(std::forward<U>(default_value));
  }

  // ---- Modifiers ------------------------------------------------------------

  /// @brief Destroys any current value and constructs a new one in place.
  /// @tparam Args Constructor argument types forwarded to `T`.
  /// @param args Arguments forwarded to `T`'s constructor.
  /// @return Reference to the newly constructed value.
  template <typename... Args>
  T& emplace(Args&&... args) {
    reset();
    construct(std::forward<Args>(args)...);
    return *data();
  }

  /// @brief Destroys the contained value if engaged, leaving the optional empty.
  METL_CONSTEXPR20 void reset() noexcept {
    if (!has_value_) {
      return;
    }

    detail::destroy_at(data());
    has_value_ = false;
  }

  /// @brief Swaps the contents (and engaged state) with another optional.
  void swap(optional& other) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                      std::is_nothrow_move_assignable<T>::value) {
    if (has_value_ && other.has_value_) {
      using std::swap;
      swap(*data(), *other.data());
    } else if (has_value_ && !other.has_value_) {
      other.construct(static_cast<T&&>(*data()));
      reset();
    } else if (!has_value_ && other.has_value_) {
      construct(static_cast<T&&>(*other.data()));
      other.reset();
    }
    // both empty: nothing to do.
  }

  // ---- Monadic operations (P2505) ------------------------------------------

  /// @brief Invokes `f` with the value if engaged, else returns an empty result.
  /// @param f Callable returning an optional; called only when engaged.
  /// @return `f(value)` if engaged, otherwise a default-constructed result.
  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) & {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    if (has_value_) {
      return std::forward<F>(f)(**this);
    }
    return result_t{};
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) const& {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    if (has_value_) {
      return std::forward<F>(f)(**this);
    }
    return result_t{};
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) && {
    using result_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<T&&>(**this)))>>;
    if (has_value_) {
      return std::forward<F>(f)(static_cast<T&&>(**this));
    }
    return result_t{};
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) const&& {
    using result_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const T&&>(**this)))>>;
    if (has_value_) {
      return std::forward<F>(f)(static_cast<const T&&>(**this));
    }
    return result_t{};
  }

  /// @brief Maps the value through `f`, wrapping the result in an optional.
  /// @param f Callable applied to the value; called only when engaged.
  /// @return `optional<result>` holding `f(value)` if engaged, else empty.
  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) & {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    if (has_value_) {
      return optional<result_value_t>(std::forward<F>(f)(**this));
    }
    return optional<result_value_t>{};
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) const& {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    if (has_value_) {
      return optional<result_value_t>(std::forward<F>(f)(**this));
    }
    return optional<result_value_t>{};
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) && {
    using result_value_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<T&&>(**this)))>>;
    if (has_value_) {
      return optional<result_value_t>(std::forward<F>(f)(static_cast<T&&>(**this)));
    }
    return optional<result_value_t>{};
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) const&& {
    using result_value_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const T&&>(**this)))>>;
    if (has_value_) {
      return optional<result_value_t>(std::forward<F>(f)(static_cast<const T&&>(**this)));
    }
    return optional<result_value_t>{};
  }

  /// @brief Returns `*this` if engaged, otherwise the optional produced by `f`.
  /// @param f Callable returning an optional; called only when empty.
  template <typename F>
  METL_NODISCARD constexpr optional or_else(F&& f) const& {
    if (has_value_) {
      return *this;
    }
    return std::forward<F>(f)();
  }

  template <typename F>
  METL_NODISCARD constexpr optional or_else(F&& f) && {
    if (has_value_) {
      return optional(static_cast<T&&>(**this));
    }
    return std::forward<F>(f)();
  }

 private:
  // Union storage (rather than an aligned byte buffer accessed through
  // std::launder) so the active member can be named directly — a prerequisite
  // for genuine constexpr use: launder/reinterpret_cast are never
  // constant-evaluable, whereas a union's active member is. Same size/alignment
  // as the previous storage_for<T>.
  struct empty_byte {};
  union storage_union {
    empty_byte empty_;
    T value_;

    constexpr storage_union() noexcept : empty_{} {}
    // T's lifetime is managed explicitly by optional; the union itself does
    // nothing on destruction (constexpr in C++20 so optional can be a literal
    // type).
    METL_CONSTEXPR20 ~storage_union() {}
  };

  METL_CONSTEXPR20 T* data() noexcept { return &storage_.value_; }
  constexpr const T* data() const noexcept { return &storage_.value_; }

  template <typename... Args>
  METL_CONSTEXPR20 void construct(Args&&... args) {
    detail::construct_at(&storage_.value_, std::forward<Args>(args)...);
    has_value_ = true;
  }

  template <typename U>
  void assign_or_construct(U&& value) {
    if (has_value_) {
      *data() = std::forward<U>(value);
      return;
    }

    construct(std::forward<U>(value));
  }

  storage_union storage_;
  bool has_value_;
};

// ---- make_optional ----------------------------------------------------------

/// @brief Creates an engaged optional holding a decayed copy of `v`.
/// @param v The value to store.
template <typename T>
constexpr optional<std::decay_t<T>> make_optional(T&& v) {
  return optional<std::decay_t<T>>(std::forward<T>(v));
}

/// @brief Creates an engaged optional, constructing `T` in place.
/// @tparam T The contained value type.
/// @tparam Args Constructor argument types forwarded to `T`.
/// @param args Arguments forwarded to `T`'s constructor.
template <typename T, typename... Args>
constexpr optional<T> make_optional(Args&&... args) {
  return optional<T>(in_place, std::forward<Args>(args)...);
}

// ---- Free swap --------------------------------------------------------------

/// @brief Swaps two optionals.
template <typename T>
void swap(optional<T>& lhs, optional<T>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

// ---- Comparison: optional vs optional --------------------------------------

/// @brief Compares two optionals; empty compares equal only to empty.

template <typename T, typename U>
constexpr auto operator==(const optional<T>& lhs, const optional<U>& rhs) -> decltype(*lhs == *rhs, bool()) {
  if (lhs.has_value() != rhs.has_value())
    return false;
  if (!lhs.has_value())
    return true;
  return *lhs == *rhs;
}

template <typename T, typename U>
constexpr auto operator!=(const optional<T>& lhs, const optional<U>& rhs) -> decltype(*lhs != *rhs, bool()) {
  if (lhs.has_value() != rhs.has_value())
    return true;
  if (!lhs.has_value())
    return false;
  return *lhs != *rhs;
}

template <typename T, typename U>
constexpr auto operator<(const optional<T>& lhs, const optional<U>& rhs) -> decltype(*lhs < *rhs, bool()) {
  if (!rhs.has_value())
    return false;
  if (!lhs.has_value())
    return true;
  return *lhs < *rhs;
}

template <typename T, typename U>
constexpr auto operator<=(const optional<T>& lhs, const optional<U>& rhs) -> decltype(*lhs <= *rhs, bool()) {
  if (!lhs.has_value())
    return true;
  if (!rhs.has_value())
    return false;
  return *lhs <= *rhs;
}

template <typename T, typename U>
constexpr auto operator>(const optional<T>& lhs, const optional<U>& rhs) -> decltype(*lhs > *rhs, bool()) {
  if (!lhs.has_value())
    return false;
  if (!rhs.has_value())
    return true;
  return *lhs > *rhs;
}

template <typename T, typename U>
constexpr auto operator>=(const optional<T>& lhs, const optional<U>& rhs) -> decltype(*lhs >= *rhs, bool()) {
  if (!rhs.has_value())
    return true;
  if (!lhs.has_value())
    return false;
  return *lhs >= *rhs;
}

// ---- Comparison: optional vs nullopt ---------------------------------------

/// @brief Compares an optional against nullopt (empty state check).
template <typename T>
constexpr bool operator==(const optional<T>& lhs, nullopt_t) noexcept {
  return !lhs.has_value();
}

template <typename T>
constexpr bool operator==(nullopt_t, const optional<T>& rhs) noexcept {
  return !rhs.has_value();
}

template <typename T>
constexpr bool operator!=(const optional<T>& lhs, nullopt_t) noexcept {
  return lhs.has_value();
}

template <typename T>
constexpr bool operator!=(nullopt_t, const optional<T>& rhs) noexcept {
  return rhs.has_value();
}

template <typename T>
constexpr bool operator<(const optional<T>&, nullopt_t) noexcept {
  return false;
}

template <typename T>
constexpr bool operator<(nullopt_t, const optional<T>& rhs) noexcept {
  return rhs.has_value();
}

template <typename T>
constexpr bool operator<=(const optional<T>& lhs, nullopt_t) noexcept {
  return !lhs.has_value();
}

template <typename T>
constexpr bool operator<=(nullopt_t, const optional<T>&) noexcept {
  return true;
}

template <typename T>
constexpr bool operator>(const optional<T>& lhs, nullopt_t) noexcept {
  return lhs.has_value();
}

template <typename T>
constexpr bool operator>(nullopt_t, const optional<T>&) noexcept {
  return false;
}

template <typename T>
constexpr bool operator>=(const optional<T>&, nullopt_t) noexcept {
  return true;
}

template <typename T>
constexpr bool operator>=(nullopt_t, const optional<T>& rhs) noexcept {
  return !rhs.has_value();
}

// ---- Comparison: optional vs T ---------------------------------------------

/// @brief Compares an engaged optional against a bare value; empty is unequal.
template <typename T, typename U>
constexpr auto operator==(const optional<T>& lhs, const U& rhs) -> decltype(*lhs == rhs, bool()) {
  return lhs.has_value() ? (*lhs == rhs) : false;
}

template <typename T, typename U>
constexpr auto operator==(const U& lhs, const optional<T>& rhs) -> decltype(lhs == *rhs, bool()) {
  return rhs.has_value() ? (lhs == *rhs) : false;
}

template <typename T, typename U>
constexpr auto operator!=(const optional<T>& lhs, const U& rhs) -> decltype(*lhs != rhs, bool()) {
  return lhs.has_value() ? (*lhs != rhs) : true;
}

template <typename T, typename U>
constexpr auto operator!=(const U& lhs, const optional<T>& rhs) -> decltype(lhs != *rhs, bool()) {
  return rhs.has_value() ? (lhs != *rhs) : true;
}

template <typename T, typename U>
constexpr auto operator<(const optional<T>& lhs, const U& rhs) -> decltype(*lhs < rhs, bool()) {
  return lhs.has_value() ? (*lhs < rhs) : true;
}

template <typename T, typename U>
constexpr auto operator<(const U& lhs, const optional<T>& rhs) -> decltype(lhs < *rhs, bool()) {
  return rhs.has_value() ? (lhs < *rhs) : false;
}

template <typename T, typename U>
constexpr auto operator<=(const optional<T>& lhs, const U& rhs) -> decltype(*lhs <= rhs, bool()) {
  return lhs.has_value() ? (*lhs <= rhs) : true;
}

template <typename T, typename U>
constexpr auto operator<=(const U& lhs, const optional<T>& rhs) -> decltype(lhs <= *rhs, bool()) {
  return rhs.has_value() ? (lhs <= *rhs) : false;
}

template <typename T, typename U>
constexpr auto operator>(const optional<T>& lhs, const U& rhs) -> decltype(*lhs > rhs, bool()) {
  return lhs.has_value() ? (*lhs > rhs) : false;
}

template <typename T, typename U>
constexpr auto operator>(const U& lhs, const optional<T>& rhs) -> decltype(lhs > *rhs, bool()) {
  return rhs.has_value() ? (lhs > *rhs) : true;
}

template <typename T, typename U>
constexpr auto operator>=(const optional<T>& lhs, const U& rhs) -> decltype(*lhs >= rhs, bool()) {
  return lhs.has_value() ? (*lhs >= rhs) : false;
}

template <typename T, typename U>
constexpr auto operator>=(const U& lhs, const optional<T>& rhs) -> decltype(lhs >= *rhs, bool()) {
  return rhs.has_value() ? (lhs >= *rhs) : true;
}

}  // namespace metl

namespace std {

/// @brief std::hash specialization; empty optionals hash to 0.
template <typename T>
struct hash<::metl::optional<T>> {
  size_t operator()(const ::metl::optional<T>& o) const noexcept {
    return o.has_value() ? std::hash<T>{}(*o) : static_cast<size_t>(0);
  }
};

}  // namespace std
