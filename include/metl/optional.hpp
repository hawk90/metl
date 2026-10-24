#pragma once

/// @file
/// @brief Progress guarantees for `metl::optional` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | construct, assign, `emplace`, `reset`, `swap`, observers | wait-free, bounded |
///   | `and_then`, `or_else`, `transform` | bounded by the callable you pass |
///
/// The value lives inline, so engaging and disengaging is one construction or one
/// destruction plus a flag -- no loop, no allocation. The bound is therefore `T`'s
/// own bound.
///
/// The monadic operations add one call; their bound is the bound of the callable,
/// which is yours to keep.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/addressof.hpp"
#include "metl/detail/construct.hpp"
#include "metl/detail/three_way.hpp"
#include "metl/in_place.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace metl {

template <typename T>
class optional;

namespace detail {

/// @brief Detects whether `T` is a specialization of `metl::optional`.
/// @note Callers should strip cv/ref (e.g. via `remove_cvref_t`) before probing,
/// since only the bare specialization matches.
template <typename T>
struct is_metl_optional : false_type {};

template <typename U>
struct is_metl_optional<optional<U>> : true_type {};

}  // namespace detail

/// @brief A fixed-storage nullable value wrapper (in-place, no heap).
///
/// Stores an optional `T` inside the object itself using an internal union; it
/// never allocates. Copyable when `T` is, but never *trivially* copyable (it has a
/// user-provided copy constructor and destructor). On C++20 it is a literal
/// type, and these are usable in constant expressions: the empty, value and
/// `in_place` constructors, `reset`, the destructor, `has_value` / `operator
/// bool`, the const overloads of `operator*`, `operator->` and `value()`, and
/// the comparisons. Copy, move, assignment, `emplace`, `value_or` and the
/// non-const observers are not.
/// @par Reference invalidation
/// A reference to the value stays valid until that object ends: `reset`,
/// `= nullopt`, `emplace` (which replaces it), assigning an empty optional, or
/// destruction. Assigning a value while engaged assigns to the same object,
/// except for a `const T`, which cannot be assigned and is replaced.
///
/// @tparam T The contained value type.
/// @note Accessors like `value()`, `operator*`, and `operator->` ASSERT (abort
/// by default) rather than throwing when the optional is empty. Use `value_or`
/// for a safe alternative that never asserts.
template <typename T>
class optional {
  static_assert(std::is_object_v<T> && !std::is_array_v<T>, METL_DETAIL_OBJECT_TYPE_MESSAGE);

 public:
  using value_type = T;

  // ---- Constructors ---------------------------------------------------------

  /// @brief Constructs an empty optional.
  // See the converting constructor below.
  template <typename U>
  static constexpr bool bool_from_optional_v =
      std::is_same_v<std::remove_cv_t<T>, bool> &&
      detail::is_metl_optional<std::remove_cv_t<std::remove_reference_t<U>>>::value;

  constexpr optional() noexcept : storage_{}, has_value_(false) {}

  /// @brief Constructs an empty optional from the nullopt tag.
  constexpr optional(nullopt_t) noexcept : storage_{}, has_value_(false) {}

  /// @brief Constructs an engaged optional by forwarding a value into `T`.
  /// @param v The value used to direct-initialize the contained `T`.
  // Converting forwarding constructor. SFINAE prevents hijacking copy/move and
  // in_place_t overloads (e.g. `optional<optional<T>>` should not bind here).
  //
  // Not for `optional<bool>` from another optional (LWG 3836): `bool` is
  // constructible from `optional<U>` through its explicit operator bool, so the
  // source's has_value() would become the stored value -- an empty
  // optional<int> producing an engaged optional<bool> holding false.
  template <typename U = T,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<U>, optional> &&
                                        !std::is_same_v<std::decay_t<U>, in_place_t> &&
                                        !std::is_same_v<std::decay_t<U>, nullopt_t> &&
                                        !bool_from_optional_v<U> && std::is_constructible_v<T, U>>>
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
  optional(optional&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
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
  optional& operator=(optional&& other) noexcept(std::is_nothrow_move_assignable_v<T> &&
                                                 std::is_nothrow_move_constructible_v<T>) {
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
  /// @note Disabled for a scalar `T` assigned its own type, as in `std::optional`. That case is
  ///       served by the converting constructor plus move assignment, and the exclusion is what
  ///       makes `o = {}` RESET a scalar optional: otherwise `{}` binds to `U = T` here and
  ///       engages it with `T{}`.
  template <typename U = T,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<U>, optional> &&
                                        !(std::is_scalar_v<T> && std::is_same_v<T, std::decay_t<U>>) &&
                                        !bool_from_optional_v<U> && std::is_constructible_v<T, U> &&
                                        std::is_assignable_v<T&, U>>>
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
  METL_NODISCARD std::remove_cv_t<T> value_or(U&& default_value) const& {
    return has_value_ ? *data() : static_cast<std::remove_cv_t<T>>(std::forward<U>(default_value));
  }

  /// @brief Returns the contained value, or `default_value` if empty. Never asserts.
  template <typename U>
  METL_NODISCARD std::remove_cv_t<T> value_or(U&& default_value) & {
    return has_value_ ? *data() : static_cast<std::remove_cv_t<T>>(std::forward<U>(default_value));
  }

  /// @brief Returns the contained value, or `default_value` if empty. Never asserts.
  template <typename U>
  METL_NODISCARD std::remove_cv_t<T> value_or(U&& default_value) && {
    return has_value_ ? static_cast<T&&>(*data())
                      : static_cast<std::remove_cv_t<T>>(std::forward<U>(default_value));
  }

  /// @brief Returns the contained value, or `default_value` if empty. Never asserts.
  template <typename U>
  METL_NODISCARD std::remove_cv_t<T> value_or(U&& default_value) const&& {
    return has_value_ ? static_cast<const T&&>(*data())
                      : static_cast<std::remove_cv_t<T>>(std::forward<U>(default_value));
  }

  // ---- Modifiers ------------------------------------------------------------

  /// @brief Replaces any current value with one constructed from `args`.
  /// @tparam Args Constructor argument types forwarded to `T`.
  /// @param args Arguments forwarded to `T`'s constructor.
  /// @return Reference to the newly constructed value.
  /// @note `args` may refer into the current value (`o.emplace(*o)`,
  ///       `o.emplace(o->member)`): when engaged and `T` is move constructible,
  ///       the new value is built before the old one is destroyed, at the cost
  ///       of one move. A throwing constructor then leaves
  ///       the old value in place.
  template <typename... Args>
  T& emplace(Args&&... args) {
    if constexpr (std::is_move_constructible_v<T>) {
      if (has_value_) {
        T incoming(std::forward<Args>(args)...);
        reset();
        construct(static_cast<T&&>(incoming));
        return *data();
      }
    }
    // Disengaged: nothing for `args` to alias. Non-movable T: no build-first
    // path exists, so it is constructed directly, as std does.
    reset();
    construct(std::forward<Args>(args)...);
    return *data();
  }

  /// @brief Destroys the contained value if engaged, leaving the optional empty.
  /// @note The optional is empty before the value's destructor runs, so that
  ///       destructor may call reset() or has_value(). It must not emplace into
  ///       this optional: the storage being destroyed is the storage it would use.
  METL_CONSTEXPR20 void reset() noexcept {
    if (!has_value_) {
      return;
    }

    // Disengage first, then destroy: a destructor that calls back into this
    // optional (reset(), has_value()) must find it empty, not destroy the value
    // a second time.
    has_value_ = false;
    detail::destroy_at(data());
  }

  /// @brief Swaps the contents (and engaged state) with another optional.
  void swap(optional& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                      std::is_nothrow_move_assignable_v<T>) {
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

  // ---- Monadic operations (P0798) ------------------------------------------

  /// @brief Invokes `f` with the value if engaged, else returns an empty result.
  /// @param f Callable returning an optional; called only when engaged.
  /// @return `f(value)` if engaged, otherwise a default-constructed result.
  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) & {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    static_assert(detail::is_metl_optional<result_t>::value,
                  "optional::and_then(F): F must return a metl::optional");
    if (has_value_) {
      return std::forward<F>(f)(**this);
    }
    return result_t{};
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) const& {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    static_assert(detail::is_metl_optional<result_t>::value,
                  "optional::and_then(F): F must return a metl::optional");
    if (has_value_) {
      return std::forward<F>(f)(**this);
    }
    return result_t{};
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) && {
    using result_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<T&&>(**this)))>>;
    static_assert(detail::is_metl_optional<result_t>::value,
                  "optional::and_then(F): F must return a metl::optional");
    if (has_value_) {
      return std::forward<F>(f)(static_cast<T&&>(**this));
    }
    return result_t{};
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) const&& {
    using result_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const T&&>(**this)))>>;
    static_assert(detail::is_metl_optional<result_t>::value,
                  "optional::and_then(F): F must return a metl::optional");
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
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    static_assert(detail::is_metl_optional<result_t>::value,
                  "optional::or_else(F): F must return a metl::optional");
    if (has_value_) {
      return *this;
    }
    return std::forward<F>(f)();
  }

  template <typename F>
  METL_NODISCARD constexpr optional or_else(F&& f) && {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    static_assert(detail::is_metl_optional<result_t>::value,
                  "optional::or_else(F): F must return a metl::optional");
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
    // Unqualified: a const T is stored as T and handed out as `const T`, so the
    // placement new that builds it never needs a const T* turned into void*
    // (which neither a plain static_cast nor libc++'s std::construct_at does).
    std::remove_cv_t<T> value_;

    constexpr storage_union() noexcept : empty_{} {}
    // T's lifetime is managed explicitly by optional; the union itself does
    // nothing on destruction (constexpr in C++20 so optional can be a literal
    // type).
    METL_CONSTEXPR20 ~storage_union() {}
  };

  // For a T with a const or reference member, C++17 [basic.life]/8 does not let
  // the name `storage_.value_` reach a T re-created by emplace or assignment
  // after a reset without std::launder, which is not constant-evaluable. C++20
  // removed that rule (P1971) and no compiler optimises on it, so the union
  // keeps optional constexpr; detail/array_storage.hpp makes the same trade.
  METL_NODISCARD METL_CONSTEXPR20 T* data() noexcept { return detail::addressof(storage_.value_); }
  METL_NODISCARD constexpr const T* data() const noexcept { return detail::addressof(storage_.value_); }

  template <typename... Args>
  METL_CONSTEXPR20 void construct(Args&&... args) {
    detail::construct_at(detail::addressof(storage_.value_), std::forward<Args>(args)...);
    has_value_ = true;
  }

  template <typename U>
  void assign_or_construct(U&& value) {
    if (has_value_) {
      if constexpr (std::is_assignable_v<T&, U>) {
        *data() = std::forward<U>(value);
        return;
      } else {
        // A const T has no assignment: replace the object, as variant does
        // for an alternative it cannot assign.
        reset();
      }
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
#if METL_DETAIL_HAS_THREE_WAY
// clang-format off
/// Three-way comparison (C++20), as std::optional's: an empty optional orders
/// before any engaged one, and two engaged ones by their values.
template <typename T, typename U>
  requires std::three_way_comparable_with<T, U>
constexpr std::compare_three_way_result_t<T, U> operator<=>(const optional<T>& lhs, const optional<U>& rhs) {
  if (lhs.has_value() && rhs.has_value()) {
    return *lhs <=> *rhs;
  }
  return lhs.has_value() <=> rhs.has_value();
}

/// An optional against nullopt (C++20): empty is equal, engaged is greater.
template <typename T>
constexpr std::strong_ordering operator<=>(const optional<T>& lhs, nullopt_t) noexcept {
  return lhs.has_value() <=> false;
}
// clang-format on
#endif

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

namespace metl {
namespace detail {

// std::hash<optional<T>> is enabled exactly when std::hash<remove_const_t<T>>
// is, as for std::optional. A disabled specialization has no call operator and
// cannot be constructed, so detection (is_invocable, is_default_constructible)
// says no instead of picking it and failing inside the body (#316).
template <typename T, typename = void>
struct optional_hash {
  optional_hash() = delete;
  optional_hash(const optional_hash&) = delete;
  optional_hash(optional_hash&&) = delete;
  optional_hash& operator=(const optional_hash&) = delete;
  optional_hash& operator=(optional_hash&&) = delete;
  ~optional_hash() = default;
};

template <typename T>
struct optional_hash<T,
                     std::void_t<decltype(std::hash<std::remove_const_t<T>>{}(
                         std::declval<const std::remove_const_t<T>&>()))>> {
  std::size_t operator()(const ::metl::optional<T>& o) const
      noexcept(noexcept(std::hash<std::remove_const_t<T>>{}(std::declval<const std::remove_const_t<T>&>()))) {
    return o.has_value() ? std::hash<std::remove_const_t<T>>{}(*o) : static_cast<std::size_t>(0);
  }
};

}  // namespace detail
}  // namespace metl

namespace std {

/// @brief std::hash specialization; empty optionals hash to 0. Enabled only
///        when `std::hash<std::remove_const_t<T>>` is, and `noexcept` only
///        when that hash is.
template <typename T>
struct hash<::metl::optional<T>> : ::metl::detail::optional_hash<T> {};

}  // namespace std
