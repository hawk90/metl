#pragma once

/// @file
/// @brief Progress guarantees for `metl::expected` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | construct, assign, `emplace`, `emplace_error`, `swap`, observers | wait-free, bounded |
///   | `and_then`, `or_else`, `transform`, `transform_error` | bounded by the callable you pass |
///
/// `expected` holds one `T` or one `E` inline and switches between them by
/// constructing the new member before destroying the old one (so a throwing
/// constructor leaves the old state), so every operation is a fixed number
/// of steps plus whatever `T` and `E` cost. There is no loop and no allocation.
///
/// The monadic operations add one call; their bound is the bound of the callable,
/// which is yours to keep.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/detail/addressof.hpp"
#include "metl/detail/construct.hpp"
#include "metl/in_place.hpp"
#include "metl/type_traits.hpp"

#include <new>
#include <type_traits>
#include <utility>

namespace metl {

// =============================================================================
// unexpected<E>
// =============================================================================

/// @brief In-place wrapper marking a value as an error for expected.
///
/// Holds an `E` inside the object (no heap allocation). Used to construct or
/// assign the error state of an expected. Trivially copyable when `E` is.
/// @tparam E The error type.
template <typename E>
class unexpected {
 public:
  using error_type = E;

  // std::unexpected has no default constructor; copy/move are defaulted.
  constexpr unexpected(const unexpected&) = default;
  constexpr unexpected(unexpected&&) = default;
  unexpected& operator=(const unexpected&) = default;
  unexpected& operator=(unexpected&&) = default;

  /// @brief Constructs the wrapper by forwarding a value into `E`.
  /// @param e The value used to direct-initialize the contained error.
  // P0323 requires these to be explicit (per LWG / std::unexpected).
  template <typename Err = E,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<Err>, unexpected> &&
                                        !std::is_same_v<std::decay_t<Err>, in_place_t> &&
                                        std::is_constructible_v<E, Err>>>
  constexpr explicit unexpected(Err&& e) : value_(std::forward<Err>(e)) {}

  /// @brief Constructs the wrapped error in place from `args`.
  /// @tparam Args Constructor argument types forwarded to `E`.
  /// @param args Arguments forwarded to `E`'s constructor.
  template <typename... Args, typename = std::enable_if_t<std::is_constructible_v<E, Args...>>>
  constexpr explicit unexpected(in_place_t, Args&&... args) : value_(std::forward<Args>(args)...) {}

  /// @brief Returns a reference to the wrapped error value.
  // error() accessors (std::unexpected interface).
  constexpr const E& error() const& noexcept { return value_; }
  constexpr E& error() & noexcept { return value_; }
  constexpr const E&& error() const&& noexcept { return static_cast<const E&&>(value_); }
  constexpr E&& error() && noexcept { return static_cast<E&&>(value_); }

  /// @brief Returns the wrapped error (legacy alias for `error()`).
  // Legacy `value()` accessors (kept for backwards compatibility with the
  // previous metl::unexpected interface).
  constexpr const E& value() const& noexcept { return value_; }
  constexpr E& value() & noexcept { return value_; }
  constexpr const E&& value() const&& noexcept { return static_cast<const E&&>(value_); }
  constexpr E&& value() && noexcept { return static_cast<E&&>(value_); }

  /// @brief Swaps the wrapped error with another unexpected.
  void swap(unexpected& other) noexcept(std::is_nothrow_move_constructible_v<E> &&
                                        std::is_nothrow_move_assignable_v<E>) {
    using std::swap;
    swap(value_, other.value_);
  }

 private:
  E value_;
};

// CTAD guide.
template <typename E>
unexpected(E) -> unexpected<E>;

/// @brief Equality comparison of two unexpected wrappers by error value.
// Comparisons.
template <typename E1, typename E2>
constexpr auto operator==(const unexpected<E1>& lhs, const unexpected<E2>& rhs)
    -> decltype(lhs.error() == rhs.error(), bool()) {
  return lhs.error() == rhs.error();
}

template <typename E1, typename E2>
constexpr auto operator!=(const unexpected<E1>& lhs, const unexpected<E2>& rhs)
    -> decltype(lhs.error() != rhs.error(), bool()) {
  return lhs.error() != rhs.error();
}

/// @brief Swaps two unexpected wrappers.
// Free swap for unexpected.
template <typename E>
void swap(unexpected<E>& lhs, unexpected<E>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

/// @brief Creates an unexpected wrapper holding a decayed copy of `error`.
/// @param error The error value to wrap.
template <typename E>
constexpr unexpected<std::decay_t<E>> make_unexpected(E&& error) {
  return unexpected<std::decay_t<E>>(std::forward<E>(error));
}

// =============================================================================
// expected<T, E>
// =============================================================================

template <typename T, typename E>
class expected;

namespace detail {

/// @brief Detects whether `R` is a specialization of `metl::expected`.
/// @note Callers should strip cv/ref (e.g. via `remove_cvref_t`) before probing.
template <typename R>
struct is_metl_expected : false_type {};

template <typename T, typename E>
struct is_metl_expected<expected<T, E>> : true_type {};

/// @brief True if `R` is a `metl::expected` whose error type is exactly `E`.
template <typename R, typename E>
struct is_metl_expected_with_error : false_type {};

template <typename U, typename E>
struct is_metl_expected_with_error<expected<U, E>, E> : true_type {};

/// @brief True if `R` is a `metl::expected` whose value type is exactly `T`.
template <typename R, typename T>
struct is_metl_expected_with_value : false_type {};

template <typename T, typename G>
struct is_metl_expected_with_value<expected<T, G>, T> : true_type {};

// Which member an expected holds. `switching` is the stretch in which the old
// member is being destroyed and the new one is not built yet, so neither is
// live: a destructor that reaches back into the expected there finds no value
// (has_value() is false), and an attempt to change it fails METL_HARDEN rather
// than destroying the old member a second time (#374).
enum class expected_state : unsigned char { error, value, switching };

struct expected_empty {};

// The storage of expected<T, E>: a union, so a constructor builds the value or
// the error in a member initializer. A constant expression allows that, and
// never allows placement new, so a namespace-scope expected is
// constant-initialized. Members are unqualified, as in optional: a const one is
// built through a non-const pointer and handed out as const.
template <typename T, typename E>
union expected_union {
  expected_empty empty;
  std::remove_cv_t<T> value;
  std::remove_cv_t<E> error;

  constexpr expected_union() noexcept : empty{} {}
  template <typename... Args>
  constexpr explicit expected_union(in_place_t, Args&&... args) : value(std::forward<Args>(args)...) {}
  template <typename... Args>
  constexpr explicit expected_union(unexpect_t, Args&&... args) : error(std::forward<Args>(args)...) {}
  // expected copies, moves and ends the active member itself.
  expected_union(const expected_union&) = delete;
  expected_union(expected_union&&) = delete;
  expected_union& operator=(const expected_union&) = delete;
  expected_union& operator=(expected_union&&) = delete;
  METL_CONSTEXPR20 ~expected_union() {}
};

// The storage of expected<void, E>: the error, or nothing.
template <typename E>
union expected_error_union {
  expected_empty empty;
  std::remove_cv_t<E> error;

  constexpr expected_error_union() noexcept : empty{} {}
  template <typename... Args>
  constexpr explicit expected_error_union(unexpect_t, Args&&... args) : error(std::forward<Args>(args)...) {}
  expected_error_union(const expected_error_union&) = delete;
  expected_error_union(expected_error_union&&) = delete;
  expected_error_union& operator=(const expected_error_union&) = delete;
  expected_error_union& operator=(expected_error_union&&) = delete;
  METL_CONSTEXPR20 ~expected_error_union() {}
};

// Replaces the live `U` at `slot` with one built from `args`. `args` may refer
// into `*slot` (`e.emplace(e.value())`), so a movable `U` is always built
// before the old one is touched. If anything throws, `slot`
// still holds a live `U` -- the old object, or a moved-back copy of it -- so
// `state` is back at `settled`, true to what is stored, and the owning
// expected's destructor destroys exactly once. While the old `U` is destroyed,
// `state` reads `switching`.
template <typename U, typename... Args>
void replace_live(U* slot, expected_state& state, expected_state settled, Args&&... args) {
  if constexpr (!std::is_move_constructible_v<U>) {
    // Nothing can be built aside or set aside for a rollback -- std::expected::
    // emplace rejects such a U outright; METL keeps accepting it, without the
    // aliasing support or the guarantee.
    state = expected_state::switching;
    slot->~U();
    ::new (static_cast<void*>(slot)) U(std::forward<Args>(args)...);
    state = settled;
  } else if constexpr (std::is_nothrow_move_constructible_v<U> || METL_NO_EXCEPTIONS) {
    // Only the constructor can throw, and it runs while the old value is intact.
    U incoming(std::forward<Args>(args)...);
    state = expected_state::switching;
    slot->~U();
    ::new (static_cast<void*>(slot)) U(static_cast<U&&>(incoming));
    state = settled;
  }
#if !METL_NO_EXCEPTIONS
  else {
    // The final move can throw: keep the old value in `backup` to move back.
    U incoming(std::forward<Args>(args)...);
    U backup(static_cast<U&&>(*slot));
    state = expected_state::switching;
    slot->~U();
    try {
      ::new (static_cast<void*>(slot)) U(static_cast<U&&>(incoming));
    } catch (...) {
      ::new (static_cast<void*>(slot)) U(static_cast<U&&>(backup));
      state = settled;
      throw;
    }
    state = settled;
  }
#endif
}

}  // namespace detail

/// @brief A fixed-storage value-or-error result type (in-place, no heap).
///
/// Holds either a value `T` or an error `E` in an internal union; it never
/// allocates. Copyable when both `T` and `E` are, but never *trivially* copyable
/// (user-provided copy constructor and destructor).
/// @par Constant initialization
/// The default, value, error, `unexpected` and in-place constructors are
/// constant-evaluable in every standard, C++17 included, whenever the
/// constructor of `T` or `E` they call is. A namespace-scope expected built by
/// one is then constant-initialized (it can be declared `constinit`), so nothing
/// that runs before `main()` sees it before its constructor ran. The copy and
/// move constructors, assignment and the observers are not constant-evaluable.
/// @par Reference invalidation
/// A reference to the value or the error stays valid until that member ends:
/// a switch between value and error (by assignment, `emplace`/`emplace_error`
/// or `swap`), an `emplace` within the same state (which replaces it), or
/// destruction. Assignment within the same state assigns to the same object.
///
/// @tparam T The expected (success) value type.
/// @tparam E The error type.
/// @note `value()`, `operator*`, `operator->`, and `error()` ASSERT (abort by
/// default) when accessed in the wrong state; they do NOT throw
/// std::bad_expected_access. Use `value_or`/`error_or` for checked fallbacks.
/// @pre `T`'s and `E`'s destructors do not change the expected that holds
///      them. While a member is destroyed -- on a switch between value and
///      error, an `emplace`, or the expected's own destruction -- the expected
///      holds neither member: `has_value()` is false, and any change, copy or
///      move of it fails METL_HARDEN, never stripped, instead of destroying the
///      old member twice (#374). std::expected leaves the same case undefined.
template <typename T, typename E>
class expected {
  static_assert(std::is_object_v<T> && !std::is_array_v<T>, METL_DETAIL_OBJECT_TYPE_MESSAGE);
  static_assert(std::is_object_v<E> && !std::is_array_v<E>, METL_DETAIL_OBJECT_TYPE_MESSAGE);

 public:
  using value_type = T;
  using error_type = E;
  using unexpected_type = unexpected<E>;

  template <typename U>
  using rebind = expected<U, E>;

  // ---- Constructors --------------------------------------------------------

  /// @brief Constructs a value-state expected by default-constructing `T`.
  // Default constructor (requires T default-constructible).
  template <typename U = T, typename = std::enable_if_t<std::is_default_constructible_v<U>>>
  constexpr expected() : storage_(in_place), state_(detail::expected_state::value) {}

  /// @brief Constructs a value-state expected from a copied value.
  constexpr expected(const T& value) : storage_(in_place, value), state_(detail::expected_state::value) {}
  /// @brief Constructs a value-state expected from a moved value.
  constexpr expected(T&& value)
      : storage_(in_place, static_cast<T&&>(value)), state_(detail::expected_state::value) {}

  /// @brief Constructs an error-state expected from an unexpected wrapper.
  // unexpected conversion constructors.
  template <typename G = E, typename = std::enable_if_t<std::is_constructible_v<E, const G&>>>
  constexpr expected(const unexpected<G>& u)
      : storage_(unexpect, u.error()), state_(detail::expected_state::error) {}

  template <typename G = E, typename = std::enable_if_t<std::is_constructible_v<E, G&&>>>
  constexpr expected(unexpected<G>&& u)
      : storage_(unexpect, static_cast<G&&>(u.error())), state_(detail::expected_state::error) {}

  /// @brief Constructs a value-state expected, building `T` in place.
  /// @tparam Args Constructor argument types forwarded to `T`.
  /// @param args Arguments forwarded to `T`'s constructor.
  // in_place_t and unexpect_t tag constructors.
  template <typename... Args, typename = std::enable_if_t<std::is_constructible_v<T, Args...>>>
  constexpr explicit expected(in_place_t, Args&&... args)
      : storage_(in_place, std::forward<Args>(args)...), state_(detail::expected_state::value) {}

  /// @brief Constructs an error-state expected, building `E` in place.
  /// @tparam Args Constructor argument types forwarded to `E`.
  /// @param args Arguments forwarded to `E`'s constructor.
  template <typename... Args, typename = std::enable_if_t<std::is_constructible_v<E, Args...>>>
  constexpr explicit expected(unexpect_t, Args&&... args)
      : storage_(unexpect, std::forward<Args>(args)...), state_(detail::expected_state::error) {}

  /// @brief Copy constructor; copies the active value or error.
  expected(const expected& other) : state_(other.state_) {
    other.require_settled();
    if (holds_value()) {
      construct_value(other.value_unchecked());
    } else {
      construct_error(other.error_unchecked());
    }
  }

  /// @brief Move constructor; moves the active value or error.
  expected(expected&& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                      std::is_nothrow_move_constructible_v<E>)
      : state_(other.state_) {
    other.require_settled();
    if (holds_value()) {
      construct_value(static_cast<T&&>(other.value_unchecked()));
    } else {
      construct_error(static_cast<E&&>(other.error_unchecked()));
    }
  }

  /// @brief Destroys the active value or error.
  ~expected() {
    require_settled();
    destroy_active();
  }

  // ---- Assignment ----------------------------------------------------------

  /// @brief Copy-assigns, switching state if needed (exception-safe).
  expected& operator=(const expected& other) {
    require_settled();
    other.require_settled();
    if (this == &other) {
      return *this;
    }

    if (other.holds_value()) {
      assign_value_or_rebind(other.value_unchecked());
    } else {
      assign_error_or_rebind(other.error_unchecked());
    }

    return *this;
  }

  /// @brief Move-assigns, switching state if needed (exception-safe).
  expected& operator=(expected&& other) noexcept(std::is_nothrow_move_assignable_v<T> &&
                                                 std::is_nothrow_move_constructible_v<T> &&
                                                 std::is_nothrow_move_assignable_v<E> &&
                                                 std::is_nothrow_move_constructible_v<E>) {
    require_settled();
    other.require_settled();
    if (this == &other) {
      return *this;
    }

    if (other.holds_value()) {
      assign_value_or_rebind(static_cast<T&&>(other.value_unchecked()));
    } else {
      assign_error_or_rebind(static_cast<E&&>(other.error_unchecked()));
    }

    return *this;
  }

  /// @brief Assigns a value, switching to the value state if needed.
  /// @param value The value forwarded into the contained `T`.
  template <typename U = T,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<U>, expected> &&
                                        std::is_constructible_v<T, U> && std::is_assignable_v<T&, U>>>
  expected& operator=(U&& value) {
    assign_value_or_rebind(std::forward<U>(value));
    return *this;
  }

  /// @brief Assigns an error, switching to the error state if needed.
  template <
      typename G = E,
      typename = std::enable_if_t<std::is_constructible_v<E, const G&> && std::is_assignable_v<E&, const G&>>>
  expected& operator=(const unexpected<G>& error) {
    assign_error_or_rebind(error.error());
    return *this;
  }

  /// @brief Assigns an error, switching to the error state if needed.
  template <typename G = E,
            typename = std::enable_if_t<std::is_constructible_v<E, G&&> && std::is_assignable_v<E&, G&&>>>
  expected& operator=(unexpected<G>&& error) {
    assign_error_or_rebind(static_cast<G&&>(error.error()));
    return *this;
  }

  // ---- Observers -----------------------------------------------------------

  /// @brief Returns true if the expected holds a value (not an error).
  METL_NODISCARD constexpr explicit operator bool() const noexcept { return holds_value(); }
  /// @brief Returns true if the expected holds a value (not an error).
  METL_NODISCARD constexpr bool has_value() const noexcept { return holds_value(); }

  /// @brief Accesses a member of the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) in the error state; it does not throw.
  METL_NODISCARD const T* operator->() const noexcept {
    METL_ASSERT(holds_value());
    return value_ptr();
  }

  /// @brief Accesses a member of the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) in the error state; it does not throw.
  METL_NODISCARD T* operator->() noexcept {
    METL_ASSERT(holds_value());
    return value_ptr();
  }

  /// @brief Returns a reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) in the error state; it does not throw.
  METL_NODISCARD const T& operator*() const& noexcept {
    METL_ASSERT(holds_value());
    return *value_ptr();
  }

  /// @brief Returns a reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) in the error state; it does not throw.
  METL_NODISCARD T& operator*() & noexcept {
    METL_ASSERT(holds_value());
    return *value_ptr();
  }

  /// @brief Returns an rvalue reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) in the error state; it does not throw.
  METL_NODISCARD const T&& operator*() const&& noexcept {
    METL_ASSERT(holds_value());
    return static_cast<const T&&>(*value_ptr());
  }

  /// @brief Returns an rvalue reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) in the error state; it does not throw.
  METL_NODISCARD T&& operator*() && noexcept {
    METL_ASSERT(holds_value());
    return static_cast<T&&>(*value_ptr());
  }

  /// @brief Returns a reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) in the error state; it does NOT throw
  /// std::bad_expected_access. Use `value_or` for a checked fallback.
  METL_NODISCARD T& value() & noexcept {
    METL_ASSERT(holds_value());
    return *value_ptr();
  }

  /// @brief Returns a reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) in the error state; it does NOT throw.
  METL_NODISCARD const T& value() const& noexcept {
    METL_ASSERT(holds_value());
    return *value_ptr();
  }

  /// @brief Returns an rvalue reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) in the error state; it does NOT throw.
  METL_NODISCARD T&& value() && noexcept {
    METL_ASSERT(holds_value());
    return static_cast<T&&>(*value_ptr());
  }

  /// @brief Returns an rvalue reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) in the error state; it does NOT throw.
  METL_NODISCARD const T&& value() const&& noexcept {
    METL_ASSERT(holds_value());
    return static_cast<const T&&>(*value_ptr());
  }

  /// @brief Returns a reference to the contained error.
  /// @pre !has_value()
  /// @warning Asserts (aborts by default) in the value state; it does NOT throw.
  /// Use `error_or` for a checked fallback.
  METL_NODISCARD E& error() & noexcept {
    METL_ASSERT(holds_error());
    return *error_ptr();
  }

  /// @brief Returns a reference to the contained error.
  /// @pre !has_value()
  /// @warning Asserts (aborts by default) in the value state; it does NOT throw.
  METL_NODISCARD const E& error() const& noexcept {
    METL_ASSERT(holds_error());
    return *error_ptr();
  }

  /// @brief Returns an rvalue reference to the contained error.
  /// @pre !has_value()
  /// @warning Asserts (aborts by default) in the value state; it does NOT throw.
  METL_NODISCARD E&& error() && noexcept {
    METL_ASSERT(holds_error());
    return static_cast<E&&>(*error_ptr());
  }

  /// @brief Returns an rvalue reference to the contained error.
  /// @pre !has_value()
  /// @warning Asserts (aborts by default) in the value state; it does NOT throw.
  METL_NODISCARD const E&& error() const&& noexcept {
    METL_ASSERT(holds_error());
    return static_cast<const E&&>(*error_ptr());
  }

  /// @brief Returns the value if present, else `default_value`. Never asserts.
  /// @param default_value Returned (converted to `T`) in the error state.
  template <typename U>
  METL_NODISCARD std::remove_cv_t<T> value_or(U&& default_value) const& {
    return holds_value() ? *value_ptr() : static_cast<std::remove_cv_t<T>>(std::forward<U>(default_value));
  }

  /// @brief Returns the value if present, else `default_value`. Never asserts.
  template <typename U>
  METL_NODISCARD std::remove_cv_t<T> value_or(U&& default_value) && {
    return holds_value() ? static_cast<T&&>(*value_ptr())
                         : static_cast<std::remove_cv_t<T>>(std::forward<U>(default_value));
  }

  /// @brief Returns the error if present, else `default_value`. Never asserts.
  /// @param default_value Returned (converted to `E`) in the value state.
  template <typename G>
  METL_NODISCARD E error_or(G&& default_value) const& {
    return holds_error() ? *error_ptr() : static_cast<E>(std::forward<G>(default_value));
  }

  /// @brief Returns the error if present, else `default_value`. Never asserts.
  template <typename G>
  METL_NODISCARD E error_or(G&& default_value) && {
    return holds_error() ? static_cast<E&&>(*error_ptr()) : static_cast<E>(std::forward<G>(default_value));
  }

  // ---- Modifiers -----------------------------------------------------------

  /// @brief Replaces the contents with a value built in place.
  /// @tparam Args Constructor argument types forwarded to `T`.
  /// @param args Arguments forwarded to `T`'s constructor.
  /// @return Reference to the newly constructed value.
  /// @note `args` may refer into the current value or error (`e.emplace(e.value())`):
  ///       a movable new value is built before the old member is destroyed, at
  ///       the cost of one move.
  template <typename... Args>
  T& emplace(Args&&... args) {
    require_settled();
    // Never destroy-then-construct: a throwing constructor would leave the
    // discriminant naming a destroyed member, destroyed again by ~expected.
    if (holds_value()) {
      detail::replace_live(value_ptr(), state_, detail::expected_state::value, std::forward<Args>(args)...);
    } else {
      reinit_as_value(std::forward<Args>(args)...);
    }
    return *value_ptr();
  }

  /// @brief Replaces the contents with an error built in place.
  /// @tparam Args Constructor argument types forwarded to `E`.
  /// @param args Arguments forwarded to `E`'s constructor.
  /// @return Reference to the newly constructed error.
  template <typename... Args>
  E& emplace_error(Args&&... args) {
    require_settled();
    if (holds_value()) {
      reinit_as_error(std::forward<Args>(args)...);
    } else {
      detail::replace_live(error_ptr(), state_, detail::expected_state::error, std::forward<Args>(args)...);
    }
    return *error_ptr();
  }

  // ---- swap ----------------------------------------------------------------

  /// @brief Swaps contents and state with another expected (exception-safe).
  /// @pre `T` or `E` is nothrow move constructible (static_assert).
  void swap(expected& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                      std::is_nothrow_move_constructible_v<E> &&
                                      std::is_nothrow_move_assignable_v<T> &&
                                      std::is_nothrow_move_assignable_v<E>) {
    require_settled();
    other.require_settled();
    if (holds_value() && other.holds_value()) {
      using std::swap;
      swap(*value_ptr(), *other.value_ptr());
    } else if (!holds_value() && !other.holds_value()) {
      using std::swap;
      swap(*error_ptr(), *other.error_ptr());
    } else if (holds_value() && !other.holds_value()) {
      swap_value_error(*this, other);
    } else {
      swap_value_error(other, *this);
    }
  }

  // ---- Monadic operations (P2505 / std::expected) --------------------------

  /// @brief Chains `f` on the value; forwards the error unchanged.
  /// @param f Callable `T -> expected<U, E>`; called only in the value state.
  // and_then: F(T) -> expected<U, E>.
  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) & {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    static_assert(detail::is_metl_expected_with_error<result_t, E>::value,
                  "expected::and_then(F): F must return a metl::expected with the same error type E");
    if (holds_value()) {
      return std::forward<F>(f)(**this);
    }
    METL_ASSERT(holds_error());
    return result_t(unexpected<E>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) const& {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    static_assert(detail::is_metl_expected_with_error<result_t, E>::value,
                  "expected::and_then(F): F must return a metl::expected with the same error type E");
    if (holds_value()) {
      return std::forward<F>(f)(**this);
    }
    METL_ASSERT(holds_error());
    return result_t(unexpected<E>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) && {
    using result_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<T&&>(**this)))>>;
    static_assert(detail::is_metl_expected_with_error<result_t, E>::value,
                  "expected::and_then(F): F must return a metl::expected with the same error type E");
    if (holds_value()) {
      return std::forward<F>(f)(static_cast<T&&>(**this));
    }
    METL_ASSERT(holds_error());
    return result_t(unexpected<E>(static_cast<E&&>(*error_ptr())));
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) const&& {
    using result_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const T&&>(**this)))>>;
    static_assert(detail::is_metl_expected_with_error<result_t, E>::value,
                  "expected::and_then(F): F must return a metl::expected with the same error type E");
    if (holds_value()) {
      return std::forward<F>(f)(static_cast<const T&&>(**this));
    }
    METL_ASSERT(holds_error());
    return result_t(unexpected<E>(static_cast<const E&&>(*error_ptr())));
  }

  /// @brief Maps the value through `f`; forwards the error unchanged.
  /// @param f Callable `T -> U`; called only in the value state.
  // transform: F(T) -> U  =>  expected<U, E>.
  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) & {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    if (holds_value()) {
      return expected<result_value_t, E>(in_place, std::forward<F>(f)(**this));
    }
    METL_ASSERT(holds_error());
    return expected<result_value_t, E>(unexpect, *error_ptr());
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) const& {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    if (holds_value()) {
      return expected<result_value_t, E>(in_place, std::forward<F>(f)(**this));
    }
    METL_ASSERT(holds_error());
    return expected<result_value_t, E>(unexpect, *error_ptr());
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) && {
    using result_value_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<T&&>(**this)))>>;
    if (holds_value()) {
      return expected<result_value_t, E>(in_place, std::forward<F>(f)(static_cast<T&&>(**this)));
    }
    METL_ASSERT(holds_error());
    return expected<result_value_t, E>(unexpect, static_cast<E&&>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) const&& {
    using result_value_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const T&&>(**this)))>>;
    if (holds_value()) {
      return expected<result_value_t, E>(in_place, std::forward<F>(f)(static_cast<const T&&>(**this)));
    }
    METL_ASSERT(holds_error());
    return expected<result_value_t, E>(unexpect, static_cast<const E&&>(*error_ptr()));
  }

  /// @brief Recovers from the error via `f`; forwards the value unchanged.
  /// @param f Callable `E -> expected<T, G>`; called only in the error state.
  // or_else: F(E) -> expected<T, G>.
  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) & {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    static_assert(detail::is_metl_expected_with_value<result_t, T>::value,
                  "expected::or_else(F): F must return a metl::expected with the same value type T");
    if (holds_value()) {
      return result_t(in_place, *value_ptr());
    }
    METL_ASSERT(holds_error());
    return std::forward<F>(f)(*error_ptr());
  }

  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) const& {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    static_assert(detail::is_metl_expected_with_value<result_t, T>::value,
                  "expected::or_else(F): F must return a metl::expected with the same value type T");
    if (holds_value()) {
      return result_t(in_place, *value_ptr());
    }
    METL_ASSERT(holds_error());
    return std::forward<F>(f)(*error_ptr());
  }

  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) && {
    using result_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<E&&>(*error_ptr())))>>;
    static_assert(detail::is_metl_expected_with_value<result_t, T>::value,
                  "expected::or_else(F): F must return a metl::expected with the same value type T");
    if (holds_value()) {
      return result_t(in_place, static_cast<T&&>(*value_ptr()));
    }
    METL_ASSERT(holds_error());
    return std::forward<F>(f)(static_cast<E&&>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) const&& {
    using result_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const E&&>(*error_ptr())))>>;
    static_assert(detail::is_metl_expected_with_value<result_t, T>::value,
                  "expected::or_else(F): F must return a metl::expected with the same value type T");
    if (holds_value()) {
      return result_t(in_place, static_cast<const T&&>(*value_ptr()));
    }
    METL_ASSERT(holds_error());
    return std::forward<F>(f)(static_cast<const E&&>(*error_ptr()));
  }

  /// @brief Maps the error through `f`; forwards the value unchanged.
  /// @param f Callable `E -> G`; called only in the error state.
  // transform_error: F(E) -> G  =>  expected<T, G>.
  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) & {
    using result_error_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    if (holds_value()) {
      return expected<T, result_error_t>(in_place, *value_ptr());
    }
    METL_ASSERT(holds_error());
    return expected<T, result_error_t>(unexpect, std::forward<F>(f)(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) const& {
    using result_error_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    if (holds_value()) {
      return expected<T, result_error_t>(in_place, *value_ptr());
    }
    METL_ASSERT(holds_error());
    return expected<T, result_error_t>(unexpect, std::forward<F>(f)(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) && {
    using result_error_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<E&&>(*error_ptr())))>>;
    if (holds_value()) {
      return expected<T, result_error_t>(in_place, static_cast<T&&>(*value_ptr()));
    }
    METL_ASSERT(holds_error());
    return expected<T, result_error_t>(unexpect, std::forward<F>(f)(static_cast<E&&>(*error_ptr())));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) const&& {
    using result_error_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const E&&>(*error_ptr())))>>;
    if (holds_value()) {
      return expected<T, result_error_t>(in_place, static_cast<const T&&>(*value_ptr()));
    }
    METL_ASSERT(holds_error());
    return expected<T, result_error_t>(unexpect, std::forward<F>(f)(static_cast<const E&&>(*error_ptr())));
  }

 private:
  // Laundered: emplace and assignment can end a member and build a new one in
  // its place, which C++17 [basic.life]/8 does not let the member's name reach
  // when T or E has a const or reference member.
  T* value_ptr() noexcept { return std::launder(detail::addressof(storage_.value)); }
  const T* value_ptr() const noexcept { return std::launder(detail::addressof(storage_.value)); }

  E* error_ptr() noexcept { return std::launder(detail::addressof(storage_.error)); }
  const E* error_ptr() const noexcept { return std::launder(detail::addressof(storage_.error)); }

  // Unchecked accessors used internally where active-member state is known.
  T& value_unchecked() noexcept { return *value_ptr(); }
  const T& value_unchecked() const noexcept { return *value_ptr(); }
  E& error_unchecked() noexcept { return *error_ptr(); }
  const E& error_unchecked() const noexcept { return *error_ptr(); }

  constexpr bool holds_value() const noexcept { return state_ == detail::expected_state::value; }
  constexpr bool holds_error() const noexcept { return state_ == detail::expected_state::error; }
  // A change while the old member is being destroyed would destroy it again.
  // The condition is a call so that the expression text METL_HARDEN stores in
  // every binary stays short.
  constexpr bool settled() const noexcept { return state_ != detail::expected_state::switching; }
  void require_settled() const noexcept { METL_HARDEN(settled()); }

  template <typename... Args>
  void construct_value(Args&&... args) {
    detail::construct_at(detail::addressof(storage_.value), std::forward<Args>(args)...);
  }

  template <typename... Args>
  void construct_error(Args&&... args) {
    detail::construct_at(detail::addressof(storage_.error), std::forward<Args>(args)...);
  }

  // Only the destructor calls this; a member's destructor that reaches back
  // finds the expected switching.
  void destroy_active() noexcept {
    const bool had_value = holds_value();
    state_ = detail::expected_state::switching;
    if (had_value) {
      value_ptr()->~T();
    } else {
      error_ptr()->~E();
    }
  }

  template <typename U>
  void assign_value_or_rebind(U&& value) {
    require_settled();
    if (holds_value()) {
      *value_ptr() = std::forward<U>(value);
      return;
    }
    // Currently holds an error; switch to the value state exception-safely.
    reinit_as_value(std::forward<U>(value));
  }

  template <typename G>
  void assign_error_or_rebind(G&& error) {
    require_settled();
    if (!holds_value()) {
      *error_ptr() = std::forward<G>(error);
      return;
    }
    // Currently holds a value; switch to the error state exception-safely.
    reinit_as_error(std::forward<G>(error));
  }

  // Cross-state reinitialization (std::expected's "reinit-expected" pattern).
  // reinit_as_value: precondition, currently in the error state; switch to the
  // value state (reinit_as_error is the mirror image). A throwing constructor
  // never destroys the old member while leaving the discriminant unchanged (which
  // would double-destroy it on ~expected).
  template <typename... Args>
  void reinit_as_value(Args&&... args) {
    if constexpr (std::is_move_constructible_v<T>) {
      // Build the new value BEFORE touching the old member: `args` may refer
      // into it (`e = e.error().fallback`), and destroying first read a dead
      // object (variant's converting assignment had the same bug).
      // A throw here leaves the state untouched.
      T incoming(std::forward<Args>(args)...);
      commit_value(static_cast<T&&>(incoming));
    } else if constexpr (std::is_nothrow_constructible_v<T, Args&&...>) {
      // A non-movable T cannot be built aside; aliasing the old member is a
      // precondition violation here, as it is for std::expected.
      state_ = detail::expected_state::switching;
      error_ptr()->~E();
      construct_value(std::forward<Args>(args)...);
    } else {
#if METL_NO_EXCEPTIONS
      state_ = detail::expected_state::switching;
      error_ptr()->~E();
      construct_value(std::forward<Args>(args)...);
#else
      E backup(static_cast<E&&>(*error_ptr()));
      state_ = detail::expected_state::switching;
      error_ptr()->~E();
      try {
        construct_value(std::forward<Args>(args)...);
      } catch (...) {
        construct_error(static_cast<E&&>(backup));  // restore the previous state
        state_ = detail::expected_state::error;
        throw;
      }
#endif
    }
    state_ = detail::expected_state::value;
  }

  // Replaces the old member with an already-built `incoming`. Only the move
  // can throw now; if it does, the old member is restored from a backup.
  void commit_value(T&& incoming) {
    if constexpr (std::is_nothrow_move_constructible_v<T>) {
      state_ = detail::expected_state::switching;
      error_ptr()->~E();
      construct_value(std::move(incoming));
    } else {
#if METL_NO_EXCEPTIONS
      state_ = detail::expected_state::switching;
      error_ptr()->~E();
      construct_value(std::move(incoming));
#else
      E backup(static_cast<E&&>(*error_ptr()));
      state_ = detail::expected_state::switching;
      error_ptr()->~E();
      try {
        construct_value(std::move(incoming));
      } catch (...) {
        construct_error(static_cast<E&&>(backup));
        state_ = detail::expected_state::error;
        throw;
      }
#endif
    }
  }

  template <typename... Args>
  void reinit_as_error(Args&&... args) {
    if constexpr (std::is_move_constructible_v<E>) {
      // Build the new error BEFORE touching the old member: `args` may refer
      // into it (`e = e.value().code`), and destroying first read a dead
      // object (variant's converting assignment had the same bug).
      // A throw here leaves the state untouched.
      E incoming(std::forward<Args>(args)...);
      commit_error(static_cast<E&&>(incoming));
    } else if constexpr (std::is_nothrow_constructible_v<E, Args&&...>) {
      // A non-movable E cannot be built aside; aliasing the old member is a
      // precondition violation here, as it is for std::expected.
      state_ = detail::expected_state::switching;
      value_ptr()->~T();
      construct_error(std::forward<Args>(args)...);
    } else {
#if METL_NO_EXCEPTIONS
      state_ = detail::expected_state::switching;
      value_ptr()->~T();
      construct_error(std::forward<Args>(args)...);
#else
      T backup(static_cast<T&&>(*value_ptr()));
      state_ = detail::expected_state::switching;
      value_ptr()->~T();
      try {
        construct_error(std::forward<Args>(args)...);
      } catch (...) {
        construct_value(static_cast<T&&>(backup));  // restore the previous state
        state_ = detail::expected_state::value;
        throw;
      }
#endif
    }
    state_ = detail::expected_state::error;
  }

  // Replaces the old member with an already-built `incoming`. Only the move
  // can throw now; if it does, the old member is restored from a backup.
  void commit_error(E&& incoming) {
    if constexpr (std::is_nothrow_move_constructible_v<E>) {
      state_ = detail::expected_state::switching;
      value_ptr()->~T();
      construct_error(std::move(incoming));
    } else {
#if METL_NO_EXCEPTIONS
      state_ = detail::expected_state::switching;
      value_ptr()->~T();
      construct_error(std::move(incoming));
#else
      T backup(static_cast<T&&>(*value_ptr()));
      state_ = detail::expected_state::switching;
      value_ptr()->~T();
      try {
        construct_error(std::move(incoming));
      } catch (...) {
        construct_value(static_cast<T&&>(backup));
        state_ = detail::expected_state::value;
        throw;
      }
#endif
    }
  }

  // Exception-safe cross-state swap: `v` holds a value, `e` holds an error.
  // On return `v` holds the error and `e` holds the value. If a move-construct
  // throws, both operands are rolled back to their original active member so
  // neither is left with a destroyed member under a stale discriminant.
  static void swap_value_error(expected& v, expected& e) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                                                  std::is_nothrow_move_constructible_v<E>) {
    // Both branches below end with one unguarded move. With one nothrow move a
    // rollback is always possible; with two throwing moves the last step could
    // fail after both originals are gone, leaving a destroyed member under a
    // stale discriminant. std::expected::swap has the same
    // requirement.
    static_assert(std::is_nothrow_move_constructible_v<T> || std::is_nothrow_move_constructible_v<E>,
                  "metl::expected::swap requires T or E to be nothrow move constructible");
    // Both read as switching until both members are in place again.
    v.state_ = detail::expected_state::switching;
    e.state_ = detail::expected_state::switching;
    if constexpr (std::is_nothrow_move_constructible_v<E>) {
      E tmp(static_cast<E&&>(*e.error_ptr()));
      e.error_ptr()->~E();
      // The rollback exists for the case where T's move CAN throw. In the
      // instantiation where it cannot, this function is noexcept (see the
      // signature) and the handler is unreachable -- GCC reports exactly that as
      // -Wterminate, since it sees a `throw` inside a noexcept function. Saying
      // so with `if constexpr` compiles the handler only where it can run,
      // which is what the code already meant.
#if METL_NO_EXCEPTIONS
      e.construct_value(static_cast<T&&>(*v.value_ptr()));
#else
      if constexpr (std::is_nothrow_move_constructible_v<T>) {
        e.construct_value(static_cast<T&&>(*v.value_ptr()));
      } else {
        try {
          e.construct_value(static_cast<T&&>(*v.value_ptr()));
        } catch (...) {
          e.construct_error(static_cast<E&&>(tmp));  // restore e
          v.state_ = detail::expected_state::value;
          e.state_ = detail::expected_state::error;
          throw;
        }
      }
#endif
      v.value_ptr()->~T();
      v.construct_error(static_cast<E&&>(tmp));
    } else {
      T tmp(static_cast<T&&>(*v.value_ptr()));
      v.value_ptr()->~T();
#if !METL_NO_EXCEPTIONS
      try {
#endif
        v.construct_error(static_cast<E&&>(*e.error_ptr()));
#if !METL_NO_EXCEPTIONS
      } catch (...) {
        v.construct_value(static_cast<T&&>(tmp));  // restore v
        v.state_ = detail::expected_state::value;
        e.state_ = detail::expected_state::error;
        throw;
      }
#endif
      e.error_ptr()->~E();
      e.construct_value(static_cast<T&&>(tmp));
    }
    v.state_ = detail::expected_state::error;
    e.state_ = detail::expected_state::value;
  }

  // The larger member, rounded up to the larger alignment (detail::expected_union).
  detail::expected_union<T, E> storage_;

  detail::expected_state state_;
};

// =============================================================================
// expected<void, E> partial specialization
// =============================================================================

/// @brief Void specialization of expected: success carries no value.
///
/// Holds either a valueless "success" state or an error `E` (in-place, no heap).
/// @tparam E The error type.
/// @note `value()` and `operator*` return void and ASSERT (abort by default) in
/// the error state; `error()` asserts in the success state. Neither throws.
/// @pre `E`'s destructor does not change the expected that holds it; see the
///      primary template (#374).
template <typename E>
class expected<void, E> {
  static_assert(std::is_object_v<E> && !std::is_array_v<E>, METL_DETAIL_OBJECT_TYPE_MESSAGE);

 public:
  using value_type = void;
  using error_type = E;
  using unexpected_type = unexpected<E>;

  template <typename U>
  using rebind = expected<U, E>;

  // ---- Constructors --------------------------------------------------------

  /// @brief Constructs a success-state expected<void, E>.
  constexpr expected() noexcept : storage_(), state_(detail::expected_state::value) {}

  /// @brief Constructs a success-state expected<void, E> (no value to build).
  // in_place_t: produces a value-state expected<void, E> (no T to construct).
  constexpr explicit expected(in_place_t) noexcept : storage_(), state_(detail::expected_state::value) {}

  /// @brief Constructs an error-state expected, building `E` in place.
  /// @tparam Args Constructor argument types forwarded to `E`.
  /// @param args Arguments forwarded to `E`'s constructor.
  template <typename... Args, typename = std::enable_if_t<std::is_constructible_v<E, Args...>>>
  constexpr explicit expected(unexpect_t, Args&&... args)
      : storage_(unexpect, std::forward<Args>(args)...), state_(detail::expected_state::error) {}

  /// @brief Constructs an error-state expected from an unexpected wrapper.
  template <typename G = E, typename = std::enable_if_t<std::is_constructible_v<E, const G&>>>
  constexpr expected(const unexpected<G>& u)
      : storage_(unexpect, u.error()), state_(detail::expected_state::error) {}

  template <typename G = E, typename = std::enable_if_t<std::is_constructible_v<E, G&&>>>
  constexpr expected(unexpected<G>&& u)
      : storage_(unexpect, static_cast<G&&>(u.error())), state_(detail::expected_state::error) {}

  /// @brief Copy constructor; copies the error if in the error state.
  expected(const expected& other) : state_(other.state_) {
    other.require_settled();
    if (!holds_value()) {
      construct_error(*other.error_ptr());
    }
  }

  /// @brief Move constructor; moves the error if in the error state.
  expected(expected&& other) noexcept(std::is_nothrow_move_constructible_v<E>) : state_(other.state_) {
    other.require_settled();
    if (!holds_value()) {
      construct_error(static_cast<E&&>(*other.error_ptr()));
    }
  }

  /// @brief Destroys the error if in the error state.
  ~expected() {
    require_settled();
    if (!holds_value()) {
      state_ = detail::expected_state::switching;
      error_ptr()->~E();
    }
  }

  // ---- Assignment ----------------------------------------------------------

  /// @brief Copy-assigns, switching between success and error as needed.
  expected& operator=(const expected& other) {
    require_settled();
    other.require_settled();
    if (this == &other) {
      return *this;
    }

    if (other.holds_value()) {
      if (!holds_value()) {
        state_ = detail::expected_state::switching;
        error_ptr()->~E();
        state_ = detail::expected_state::value;
      }
    } else {
      if (holds_value()) {
        construct_error(*other.error_ptr());
        state_ = detail::expected_state::error;
      } else {
        *error_ptr() = *other.error_ptr();
      }
    }
    return *this;
  }

  /// @brief Move-assigns, switching between success and error as needed.
  expected& operator=(expected&& other) noexcept(std::is_nothrow_move_constructible_v<E> &&
                                                 std::is_nothrow_move_assignable_v<E>) {
    require_settled();
    other.require_settled();
    if (this == &other) {
      return *this;
    }

    if (other.holds_value()) {
      if (!holds_value()) {
        state_ = detail::expected_state::switching;
        error_ptr()->~E();
        state_ = detail::expected_state::value;
      }
    } else {
      if (holds_value()) {
        construct_error(static_cast<E&&>(*other.error_ptr()));
        state_ = detail::expected_state::error;
      } else {
        *error_ptr() = static_cast<E&&>(*other.error_ptr());
      }
    }
    return *this;
  }

  /// @brief Assigns an error, switching to the error state if needed.
  template <
      typename G = E,
      typename = std::enable_if_t<std::is_constructible_v<E, const G&> && std::is_assignable_v<E&, const G&>>>
  expected& operator=(const unexpected<G>& error) {
    require_settled();
    if (holds_value()) {
      construct_error(error.error());
      state_ = detail::expected_state::error;
    } else {
      *error_ptr() = error.error();
    }
    return *this;
  }

  /// @brief Assigns an error, switching to the error state if needed.
  template <typename G = E,
            typename = std::enable_if_t<std::is_constructible_v<E, G&&> && std::is_assignable_v<E&, G&&>>>
  expected& operator=(unexpected<G>&& error) {
    require_settled();
    if (holds_value()) {
      construct_error(static_cast<G&&>(error.error()));
      state_ = detail::expected_state::error;
    } else {
      *error_ptr() = static_cast<G&&>(error.error());
    }
    return *this;
  }

  // ---- Observers -----------------------------------------------------------

  /// @brief Returns true if in the success state (not an error).
  METL_NODISCARD constexpr explicit operator bool() const noexcept { return holds_value(); }
  /// @brief Returns true if in the success state (not an error).
  METL_NODISCARD constexpr bool has_value() const noexcept { return holds_value(); }

  /// @brief Verifies the success state (returns void).
  /// @pre has_value()
  /// @warning Asserts (aborts by default) in the error state; it does not throw.
  // value() returns void; asserts on error state.
  void value() const& noexcept { METL_ASSERT(holds_value()); }
  /// @brief Verifies the success state (returns void).
  /// @pre has_value()
  /// @warning Asserts (aborts by default) in the error state; it does not throw.
  void value() && noexcept { METL_ASSERT(holds_value()); }

  /// @brief Verifies the success state (returns void).
  /// @pre has_value()
  /// @warning Asserts (aborts by default) in the error state; it does not throw.
  // operator*() returns void.
  void operator*() const noexcept { METL_ASSERT(holds_value()); }

  /// @brief Returns a reference to the contained error.
  /// @pre !has_value()
  /// @warning Asserts (aborts by default) in the success state; it does not throw.
  METL_NODISCARD E& error() & noexcept {
    METL_ASSERT(holds_error());
    return *error_ptr();
  }

  /// @brief Returns a reference to the contained error.
  /// @pre !has_value()
  /// @warning Asserts (aborts by default) in the success state; it does not throw.
  METL_NODISCARD const E& error() const& noexcept {
    METL_ASSERT(holds_error());
    return *error_ptr();
  }

  /// @brief Returns an rvalue reference to the contained error.
  /// @pre !has_value()
  /// @warning Asserts (aborts by default) in the success state; it does not throw.
  METL_NODISCARD E&& error() && noexcept {
    METL_ASSERT(holds_error());
    return static_cast<E&&>(*error_ptr());
  }

  /// @brief Returns an rvalue reference to the contained error.
  /// @pre !has_value()
  /// @warning Asserts (aborts by default) in the success state; it does not throw.
  METL_NODISCARD const E&& error() const&& noexcept {
    METL_ASSERT(holds_error());
    return static_cast<const E&&>(*error_ptr());
  }

  /// @brief Returns the error if present, else `default_value`. Never asserts.
  template <typename G>
  METL_NODISCARD E error_or(G&& default_value) const& {
    return holds_error() ? *error_ptr() : static_cast<E>(std::forward<G>(default_value));
  }

  /// @brief Returns the error if present, else `default_value`. Never asserts.
  template <typename G>
  METL_NODISCARD E error_or(G&& default_value) && {
    return holds_error() ? static_cast<E&&>(*error_ptr()) : static_cast<E>(std::forward<G>(default_value));
  }

  // ---- Modifiers -----------------------------------------------------------

  /// @brief Sets the success state, destroying any current error.
  void emplace() noexcept {
    require_settled();
    if (!holds_value()) {
      state_ = detail::expected_state::switching;
      error_ptr()->~E();
      state_ = detail::expected_state::value;
    }
  }

  /// @brief Replaces the contents with an error built in place.
  /// @tparam Args Constructor argument types forwarded to `E`.
  /// @param args Arguments forwarded to `E`'s constructor.
  /// @return Reference to the newly constructed error.
  template <typename... Args>
  E& emplace_error(Args&&... args) {
    require_settled();
    if (holds_value()) {
      construct_error(std::forward<Args>(args)...);
      state_ = detail::expected_state::error;
    } else {
      // Same hazard as the primary template's emplace.
      detail::replace_live(error_ptr(), state_, detail::expected_state::error, std::forward<Args>(args)...);
    }
    return *error_ptr();
  }

  /// @brief Swaps contents and state with another expected<void, E>.
  void swap(expected& other) noexcept(std::is_nothrow_move_constructible_v<E> &&
                                      std::is_nothrow_move_assignable_v<E>) {
    require_settled();
    other.require_settled();
    if (holds_value() && other.holds_value()) {
      // nothing to swap.
    } else if (!holds_value() && !other.holds_value()) {
      using std::swap;
      swap(*error_ptr(), *other.error_ptr());
    } else if (holds_value() && !other.holds_value()) {
      construct_error(static_cast<E&&>(*other.error_ptr()));
      state_ = detail::expected_state::error;
      other.state_ = detail::expected_state::switching;
      other.error_ptr()->~E();
      other.state_ = detail::expected_state::value;
    } else {
      other.swap(*this);
    }
  }

  // ---- Monadic operations --------------------------------------------------

  /// @brief Chains `f` on success; forwards the error unchanged.
  /// @param f Callable `() -> expected<U, E>`; called only in the success state.
  // and_then: F() -> expected<U, E>.
  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) & {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    static_assert(detail::is_metl_expected_with_error<result_t, E>::value,
                  "expected<void, E>::and_then(F): F must return a metl::expected<U, E>");
    if (holds_value()) {
      return std::forward<F>(f)();
    }
    METL_ASSERT(holds_error());
    return result_t(unexpected<E>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) const& {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    static_assert(detail::is_metl_expected_with_error<result_t, E>::value,
                  "expected<void, E>::and_then(F): F must return a metl::expected<U, E>");
    if (holds_value()) {
      return std::forward<F>(f)();
    }
    METL_ASSERT(holds_error());
    return result_t(unexpected<E>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) && {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    static_assert(detail::is_metl_expected_with_error<result_t, E>::value,
                  "expected<void, E>::and_then(F): F must return a metl::expected<U, E>");
    if (holds_value()) {
      return std::forward<F>(f)();
    }
    METL_ASSERT(holds_error());
    return result_t(unexpected<E>(static_cast<E&&>(*error_ptr())));
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) const&& {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    static_assert(detail::is_metl_expected_with_error<result_t, E>::value,
                  "expected<void, E>::and_then(F): F must return a metl::expected<U, E>");
    if (holds_value()) {
      return std::forward<F>(f)();
    }
    METL_ASSERT(holds_error());
    return result_t(unexpected<E>(static_cast<const E&&>(*error_ptr())));
  }

  /// @brief Maps success through `f`; forwards the error unchanged.
  /// @param f Callable `() -> U` (U may be void); called only on success.
  // transform: F() -> U  =>  expected<U, E>. (U may be void.)
  template <typename F>
  METL_NODISCARD auto transform(F&& f) & {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    if constexpr (std::is_void_v<result_value_t>) {
      if (holds_value()) {
        std::forward<F>(f)();
        return expected<void, E>();
      }
      METL_ASSERT(holds_error());
      return expected<void, E>(unexpect, *error_ptr());
    } else {
      if (holds_value()) {
        return expected<result_value_t, E>(in_place, std::forward<F>(f)());
      }
      METL_ASSERT(holds_error());
      return expected<result_value_t, E>(unexpect, *error_ptr());
    }
  }

  template <typename F>
  METL_NODISCARD auto transform(F&& f) const& {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    if constexpr (std::is_void_v<result_value_t>) {
      if (holds_value()) {
        std::forward<F>(f)();
        return expected<void, E>();
      }
      METL_ASSERT(holds_error());
      return expected<void, E>(unexpect, *error_ptr());
    } else {
      if (holds_value()) {
        return expected<result_value_t, E>(in_place, std::forward<F>(f)());
      }
      METL_ASSERT(holds_error());
      return expected<result_value_t, E>(unexpect, *error_ptr());
    }
  }

  template <typename F>
  METL_NODISCARD auto transform(F&& f) && {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    if constexpr (std::is_void_v<result_value_t>) {
      if (holds_value()) {
        std::forward<F>(f)();
        return expected<void, E>();
      }
      METL_ASSERT(holds_error());
      return expected<void, E>(unexpect, static_cast<E&&>(*error_ptr()));
    } else {
      if (holds_value()) {
        return expected<result_value_t, E>(in_place, std::forward<F>(f)());
      }
      METL_ASSERT(holds_error());
      return expected<result_value_t, E>(unexpect, static_cast<E&&>(*error_ptr()));
    }
  }

  template <typename F>
  METL_NODISCARD auto transform(F&& f) const&& {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    if constexpr (std::is_void_v<result_value_t>) {
      if (holds_value()) {
        std::forward<F>(f)();
        return expected<void, E>();
      }
      METL_ASSERT(holds_error());
      return expected<void, E>(unexpect, static_cast<const E&&>(*error_ptr()));
    } else {
      if (holds_value()) {
        return expected<result_value_t, E>(in_place, std::forward<F>(f)());
      }
      METL_ASSERT(holds_error());
      return expected<result_value_t, E>(unexpect, static_cast<const E&&>(*error_ptr()));
    }
  }

  /// @brief Recovers from the error via `f`; success passes through.
  /// @param f Callable `E -> expected<void, G>`; called only in the error state.
  // or_else: F(E) -> expected<void, G>.
  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) & {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    static_assert(detail::is_metl_expected_with_value<result_t, void>::value,
                  "expected<void, E>::or_else(F): F must return a metl::expected<void, G>");
    if (holds_value()) {
      return result_t();
    }
    METL_ASSERT(holds_error());
    return std::forward<F>(f)(*error_ptr());
  }

  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) const& {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    static_assert(detail::is_metl_expected_with_value<result_t, void>::value,
                  "expected<void, E>::or_else(F): F must return a metl::expected<void, G>");
    if (holds_value()) {
      return result_t();
    }
    METL_ASSERT(holds_error());
    return std::forward<F>(f)(*error_ptr());
  }

  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) && {
    using result_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<E&&>(*error_ptr())))>>;
    static_assert(detail::is_metl_expected_with_value<result_t, void>::value,
                  "expected<void, E>::or_else(F): F must return a metl::expected<void, G>");
    if (holds_value()) {
      return result_t();
    }
    METL_ASSERT(holds_error());
    return std::forward<F>(f)(static_cast<E&&>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) const&& {
    using result_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const E&&>(*error_ptr())))>>;
    static_assert(detail::is_metl_expected_with_value<result_t, void>::value,
                  "expected<void, E>::or_else(F): F must return a metl::expected<void, G>");
    if (holds_value()) {
      return result_t();
    }
    METL_ASSERT(holds_error());
    return std::forward<F>(f)(static_cast<const E&&>(*error_ptr()));
  }

  /// @brief Maps the error through `f`; success passes through.
  /// @param f Callable `E -> G`; called only in the error state.
  // transform_error: F(E) -> G  =>  expected<void, G>.
  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) & {
    using result_error_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    if (holds_value()) {
      return expected<void, result_error_t>();
    }
    METL_ASSERT(holds_error());
    return expected<void, result_error_t>(unexpect, std::forward<F>(f)(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) const& {
    using result_error_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    if (holds_value()) {
      return expected<void, result_error_t>();
    }
    METL_ASSERT(holds_error());
    return expected<void, result_error_t>(unexpect, std::forward<F>(f)(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) && {
    using result_error_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<E&&>(*error_ptr())))>>;
    if (holds_value()) {
      return expected<void, result_error_t>();
    }
    METL_ASSERT(holds_error());
    return expected<void, result_error_t>(unexpect, std::forward<F>(f)(static_cast<E&&>(*error_ptr())));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) const&& {
    using result_error_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const E&&>(*error_ptr())))>>;
    if (holds_value()) {
      return expected<void, result_error_t>();
    }
    METL_ASSERT(holds_error());
    return expected<void, result_error_t>(unexpect, std::forward<F>(f)(static_cast<const E&&>(*error_ptr())));
  }

 private:
  E* error_ptr() noexcept { return std::launder(detail::addressof(storage_.error)); }
  const E* error_ptr() const noexcept { return std::launder(detail::addressof(storage_.error)); }

  constexpr bool holds_value() const noexcept { return state_ == detail::expected_state::value; }
  constexpr bool holds_error() const noexcept { return state_ == detail::expected_state::error; }
  // A change while the error is being destroyed would destroy it again.
  constexpr bool settled() const noexcept { return state_ != detail::expected_state::switching; }
  void require_settled() const noexcept { METL_HARDEN(settled()); }

  template <typename... Args>
  void construct_error(Args&&... args) {
    detail::construct_at(detail::addressof(storage_.error), std::forward<Args>(args)...);
  }

  // The success state leaves the empty member active: nothing to build, and a
  // constant-initialized object has no indeterminate bytes.
  detail::expected_error_union<E> storage_;
  detail::expected_state state_;
};

// =============================================================================
// Free swap
// =============================================================================

/// @brief Swaps two expected objects.
template <typename T, typename E>
void swap(expected<T, E>& lhs, expected<T, E>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

// =============================================================================
// Comparison operators
// =============================================================================

/// @brief Compares two expected objects (equal state and equal contents).
// expected vs expected.
template <typename T1, typename E1, typename T2, typename E2>
constexpr auto operator==(const expected<T1, E1>& lhs, const expected<T2, E2>& rhs)
    -> decltype(*lhs == *rhs, lhs.error() == rhs.error(), bool()) {
  if (lhs.has_value() != rhs.has_value()) {
    return false;
  }
  return lhs.has_value() ? (*lhs == *rhs) : (lhs.error() == rhs.error());
}

template <typename T1, typename E1, typename T2, typename E2>
constexpr auto operator!=(const expected<T1, E1>& lhs, const expected<T2, E2>& rhs)
    -> decltype(*lhs == *rhs, lhs.error() == rhs.error(), bool()) {
  return !(lhs == rhs);
}

/// @brief Compares two void-expected objects (equal state, equal error if any).
// expected<void, E1> vs expected<void, E2>.
template <typename E1, typename E2>
constexpr auto operator==(const expected<void, E1>& lhs, const expected<void, E2>& rhs)
    -> decltype(lhs.error() == rhs.error(), bool()) {
  if (lhs.has_value() != rhs.has_value()) {
    return false;
  }
  return lhs.has_value() ? true : (lhs.error() == rhs.error());
}

template <typename E1, typename E2>
constexpr auto operator!=(const expected<void, E1>& lhs, const expected<void, E2>& rhs)
    -> decltype(lhs.error() == rhs.error(), bool()) {
  return !(lhs == rhs);
}

/// @brief Compares an expected against a bare value (true only in value state).
// expected vs T (value comparison).
template <typename T, typename E, typename U>
constexpr auto operator==(const expected<T, E>& lhs, const U& rhs) -> decltype(*lhs == rhs, bool()) {
  return lhs.has_value() && (*lhs == rhs);
}

template <typename T, typename E, typename U>
constexpr auto operator==(const U& lhs, const expected<T, E>& rhs) -> decltype(lhs == *rhs, bool()) {
  return rhs.has_value() && (lhs == *rhs);
}

template <typename T, typename E, typename U>
constexpr auto operator!=(const expected<T, E>& lhs, const U& rhs) -> decltype(*lhs != rhs, bool()) {
  return !lhs.has_value() || (*lhs != rhs);
}

template <typename T, typename E, typename U>
constexpr auto operator!=(const U& lhs, const expected<T, E>& rhs) -> decltype(lhs != *rhs, bool()) {
  return !rhs.has_value() || (lhs != *rhs);
}

/// @brief Compares an expected against an unexpected (true only in error state).
// expected vs unexpected.
template <typename T, typename E, typename E2>
constexpr auto operator==(const expected<T, E>& lhs, const unexpected<E2>& rhs)
    -> decltype(lhs.error() == rhs.error(), bool()) {
  return !lhs.has_value() && (lhs.error() == rhs.error());
}

template <typename T, typename E, typename E2>
constexpr auto operator==(const unexpected<E2>& lhs, const expected<T, E>& rhs)
    -> decltype(lhs.error() == rhs.error(), bool()) {
  return !rhs.has_value() && (lhs.error() == rhs.error());
}

template <typename T, typename E, typename E2>
constexpr auto operator!=(const expected<T, E>& lhs, const unexpected<E2>& rhs)
    -> decltype(lhs.error() != rhs.error(), bool()) {
  return lhs.has_value() || (lhs.error() != rhs.error());
}

template <typename T, typename E, typename E2>
constexpr auto operator!=(const unexpected<E2>& lhs, const expected<T, E>& rhs)
    -> decltype(lhs.error() != rhs.error(), bool()) {
  return rhs.has_value() || (lhs.error() != rhs.error());
}

}  // namespace metl
