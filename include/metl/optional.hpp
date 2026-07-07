#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/in_place.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

template <typename T>
class optional {
 public:
  using value_type = T;

  // ---- Constructors ---------------------------------------------------------

  constexpr optional() noexcept : storage_{}, has_value_(false) {}

  constexpr optional(nullopt_t) noexcept : storage_{}, has_value_(false) {}

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

  // in_place_t constructor.
  template <typename... Args>
  constexpr explicit optional(in_place_t, Args&&... args) : storage_{}, has_value_(false) {
    construct(std::forward<Args>(args)...);
  }

  optional(const optional& other) : storage_{}, has_value_(false) {
    if (other.has_value_) {
      construct(*other);
    }
  }

  optional(optional&& other) noexcept(std::is_nothrow_move_constructible<T>::value)
      : storage_{}, has_value_(false) {
    if (other.has_value_) {
      construct(static_cast<T&&>(*other));
    }
  }

  ~optional() { reset(); }

  // ---- Assignment -----------------------------------------------------------

  optional& operator=(nullopt_t) noexcept {
    reset();
    return *this;
  }

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

  template <
      typename U = T,
      typename = std::enable_if_t<!std::is_same<std::decay_t<U>, optional>::value &&
                                  std::is_constructible<T, U>::value && std::is_assignable<T&, U>::value>>
  optional& operator=(U&& value) {
    assign_or_construct(std::forward<U>(value));
    return *this;
  }

  // ---- Observers ------------------------------------------------------------

  METL_NODISCARD constexpr const T* operator->() const noexcept {
    METL_ASSERT(has_value_);
    return data();
  }

  METL_NODISCARD T* operator->() noexcept {
    METL_ASSERT(has_value_);
    return data();
  }

  METL_NODISCARD constexpr const T& operator*() const& noexcept {
    METL_ASSERT(has_value_);
    return *data();
  }

  METL_NODISCARD T& operator*() & noexcept {
    METL_ASSERT(has_value_);
    return *data();
  }

  METL_NODISCARD const T&& operator*() const&& noexcept {
    METL_ASSERT(has_value_);
    return static_cast<const T&&>(*data());
  }

  METL_NODISCARD T&& operator*() && noexcept {
    METL_ASSERT(has_value_);
    return static_cast<T&&>(*data());
  }

  METL_NODISCARD constexpr explicit operator bool() const noexcept { return has_value_; }
  METL_NODISCARD constexpr bool has_value() const noexcept { return has_value_; }

  METL_NODISCARD T& value() & noexcept {
    METL_ASSERT(has_value_);
    return *data();
  }

  METL_NODISCARD const T& value() const& noexcept {
    METL_ASSERT(has_value_);
    return *data();
  }

  METL_NODISCARD T&& value() && noexcept {
    METL_ASSERT(has_value_);
    return static_cast<T&&>(*data());
  }

  METL_NODISCARD const T&& value() const&& noexcept {
    METL_ASSERT(has_value_);
    return static_cast<const T&&>(*data());
  }

  template <typename U>
  METL_NODISCARD T value_or(U&& default_value) const& {
    return has_value_ ? *data() : static_cast<T>(std::forward<U>(default_value));
  }

  template <typename U>
  METL_NODISCARD T value_or(U&& default_value) & {
    return has_value_ ? *data() : static_cast<T>(std::forward<U>(default_value));
  }

  template <typename U>
  METL_NODISCARD T value_or(U&& default_value) && {
    return has_value_ ? static_cast<T&&>(*data()) : static_cast<T>(std::forward<U>(default_value));
  }

  template <typename U>
  METL_NODISCARD T value_or(U&& default_value) const&& {
    return has_value_ ? static_cast<const T&&>(*data()) : static_cast<T>(std::forward<U>(default_value));
  }

  // ---- Modifiers ------------------------------------------------------------

  template <typename... Args>
  T& emplace(Args&&... args) {
    reset();
    construct(std::forward<Args>(args)...);
    return *data();
  }

  void reset() noexcept {
    if (!has_value_) {
      return;
    }

    data()->~T();
    has_value_ = false;
  }

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
  T* data() noexcept { return storage_.ptr(); }
  const T* data() const noexcept { return storage_.ptr(); }

  template <typename... Args>
  void construct(Args&&... args) {
    ::new (storage_.addr()) T(std::forward<Args>(args)...);
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

  storage_for<T> storage_;
  bool has_value_;
};

// ---- make_optional ----------------------------------------------------------

template <typename T>
constexpr optional<std::decay_t<T>> make_optional(T&& v) {
  return optional<std::decay_t<T>>(std::forward<T>(v));
}

template <typename T, typename... Args>
constexpr optional<T> make_optional(Args&&... args) {
  return optional<T>(in_place, std::forward<Args>(args)...);
}

// ---- Free swap --------------------------------------------------------------

template <typename T>
void swap(optional<T>& lhs, optional<T>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

// ---- Comparison: optional vs optional --------------------------------------

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

template <typename T>
struct hash<::metl::optional<T>> {
  size_t operator()(const ::metl::optional<T>& o) const noexcept {
    return o.has_value() ? std::hash<T>{}(*o) : static_cast<size_t>(0);
  }
};

}  // namespace std
