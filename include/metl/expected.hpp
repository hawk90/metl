#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/in_place.hpp"
#include "metl/type_traits.hpp"

#include <new>
#include <type_traits>
#include <utility>

namespace metl {

// =============================================================================
// unexpected<E>
// =============================================================================

template <typename E>
class unexpected {
 public:
  using error_type = E;

  // std::unexpected has no default constructor; copy/move are defaulted.
  constexpr unexpected(const unexpected&) = default;
  constexpr unexpected(unexpected&&) = default;
  unexpected& operator=(const unexpected&) = default;
  unexpected& operator=(unexpected&&) = default;

  // P0323 requires these to be explicit (per LWG / std::unexpected).
  template <typename Err = E,
            typename = std::enable_if_t<!std::is_same<std::decay_t<Err>, unexpected>::value &&
                                        !std::is_same<std::decay_t<Err>, in_place_t>::value &&
                                        std::is_constructible<E, Err>::value>>
  constexpr explicit unexpected(Err&& e) : value_(std::forward<Err>(e)) {}

  template <typename... Args, typename = std::enable_if_t<std::is_constructible<E, Args...>::value>>
  constexpr explicit unexpected(in_place_t, Args&&... args) : value_(std::forward<Args>(args)...) {}

  // error() accessors (std::unexpected interface).
  constexpr const E& error() const& noexcept { return value_; }
  constexpr E& error() & noexcept { return value_; }
  constexpr const E&& error() const&& noexcept { return static_cast<const E&&>(value_); }
  constexpr E&& error() && noexcept { return static_cast<E&&>(value_); }

  // Legacy `value()` accessors (kept for backwards compatibility with the
  // previous metl::unexpected interface).
  constexpr const E& value() const& noexcept { return value_; }
  constexpr E& value() & noexcept { return value_; }
  constexpr const E&& value() const&& noexcept { return static_cast<const E&&>(value_); }
  constexpr E&& value() && noexcept { return static_cast<E&&>(value_); }

  void swap(unexpected& other) noexcept(std::is_nothrow_move_constructible<E>::value &&
                                        std::is_nothrow_move_assignable<E>::value) {
    using std::swap;
    swap(value_, other.value_);
  }

 private:
  E value_;
};

// CTAD guide.
template <typename E>
unexpected(E) -> unexpected<E>;

// Comparisons.
template <typename E1, typename E2>
constexpr auto operator==(const unexpected<E1>& lhs,
                          const unexpected<E2>& rhs) -> decltype(lhs.error() == rhs.error(), bool()) {
  return lhs.error() == rhs.error();
}

template <typename E1, typename E2>
constexpr auto operator!=(const unexpected<E1>& lhs,
                          const unexpected<E2>& rhs) -> decltype(lhs.error() != rhs.error(), bool()) {
  return lhs.error() != rhs.error();
}

// Free swap for unexpected.
template <typename E>
void swap(unexpected<E>& lhs, unexpected<E>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

template <typename E>
constexpr unexpected<typename std::decay<E>::type> make_unexpected(E&& error) {
  return unexpected<typename std::decay<E>::type>(std::forward<E>(error));
}

// =============================================================================
// expected<T, E>
// =============================================================================

template <typename T, typename E>
class expected {
 public:
  using value_type = T;
  using error_type = E;
  using unexpected_type = unexpected<E>;

  template <typename U>
  using rebind = expected<U, E>;

  // ---- Constructors --------------------------------------------------------

  // Default constructor (requires T default-constructible).
  template <typename U = T, typename = std::enable_if_t<std::is_default_constructible<U>::value>>
  constexpr expected() : has_value_(true) {
    construct_value();
  }

  expected(const T& value) : has_value_(true) { construct_value(value); }
  expected(T&& value) : has_value_(true) { construct_value(static_cast<T&&>(value)); }

  // unexpected conversion constructors.
  template <typename G = E, typename = std::enable_if_t<std::is_constructible<E, const G&>::value>>
  expected(const unexpected<G>& u) : has_value_(false) {
    construct_error(u.error());
  }

  template <typename G = E, typename = std::enable_if_t<std::is_constructible<E, G&&>::value>>
  expected(unexpected<G>&& u) : has_value_(false) {
    construct_error(static_cast<G&&>(u.error()));
  }

  // in_place_t and unexpect_t tag constructors.
  template <typename... Args, typename = std::enable_if_t<std::is_constructible<T, Args...>::value>>
  constexpr explicit expected(in_place_t, Args&&... args) : has_value_(true) {
    construct_value(std::forward<Args>(args)...);
  }

  template <typename... Args, typename = std::enable_if_t<std::is_constructible<E, Args...>::value>>
  constexpr explicit expected(unexpect_t, Args&&... args) : has_value_(false) {
    construct_error(std::forward<Args>(args)...);
  }

  expected(const expected& other) : has_value_(other.has_value_) {
    if (has_value_) {
      construct_value(other.value_unchecked());
    } else {
      construct_error(other.error_unchecked());
    }
  }

  expected(expected&& other) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                      std::is_nothrow_move_constructible<E>::value)
      : has_value_(other.has_value_) {
    if (has_value_) {
      construct_value(static_cast<T&&>(other.value_unchecked()));
    } else {
      construct_error(static_cast<E&&>(other.error_unchecked()));
    }
  }

  ~expected() { destroy_active(); }

  // ---- Assignment ----------------------------------------------------------

  expected& operator=(const expected& other) {
    if (this == &other) {
      return *this;
    }

    if (other.has_value_) {
      assign_value_or_rebind(other.value_unchecked());
    } else {
      assign_error_or_rebind(other.error_unchecked());
    }

    return *this;
  }

  expected& operator=(expected&& other) noexcept(std::is_nothrow_move_assignable<T>::value &&
                                                 std::is_nothrow_move_constructible<T>::value &&
                                                 std::is_nothrow_move_assignable<E>::value &&
                                                 std::is_nothrow_move_constructible<E>::value) {
    if (this == &other) {
      return *this;
    }

    if (other.has_value_) {
      assign_value_or_rebind(static_cast<T&&>(other.value_unchecked()));
    } else {
      assign_error_or_rebind(static_cast<E&&>(other.error_unchecked()));
    }

    return *this;
  }

  template <
      typename U = T,
      typename = std::enable_if_t<!std::is_same<std::decay_t<U>, expected>::value &&
                                  std::is_constructible<T, U>::value && std::is_assignable<T&, U>::value>>
  expected& operator=(U&& value) {
    assign_value_or_rebind(std::forward<U>(value));
    return *this;
  }

  template <typename G = E,
            typename = std::enable_if_t<std::is_constructible<E, const G&>::value &&
                                        std::is_assignable<E&, const G&>::value>>
  expected& operator=(const unexpected<G>& error) {
    assign_error_or_rebind(error.error());
    return *this;
  }

  template <
      typename G = E,
      typename = std::enable_if_t<std::is_constructible<E, G&&>::value && std::is_assignable<E&, G&&>::value>>
  expected& operator=(unexpected<G>&& error) {
    assign_error_or_rebind(static_cast<G&&>(error.error()));
    return *this;
  }

  // ---- Observers -----------------------------------------------------------

  METL_NODISCARD constexpr explicit operator bool() const noexcept { return has_value_; }
  METL_NODISCARD constexpr bool has_value() const noexcept { return has_value_; }

  METL_NODISCARD const T* operator->() const noexcept {
    METL_ASSERT(has_value_);
    return value_ptr();
  }

  METL_NODISCARD T* operator->() noexcept {
    METL_ASSERT(has_value_);
    return value_ptr();
  }

  METL_NODISCARD const T& operator*() const& noexcept {
    METL_ASSERT(has_value_);
    return *value_ptr();
  }

  METL_NODISCARD T& operator*() & noexcept {
    METL_ASSERT(has_value_);
    return *value_ptr();
  }

  METL_NODISCARD const T&& operator*() const&& noexcept {
    METL_ASSERT(has_value_);
    return static_cast<const T&&>(*value_ptr());
  }

  METL_NODISCARD T&& operator*() && noexcept {
    METL_ASSERT(has_value_);
    return static_cast<T&&>(*value_ptr());
  }

  METL_NODISCARD T& value() & noexcept {
    METL_ASSERT(has_value_);
    return *value_ptr();
  }

  METL_NODISCARD const T& value() const& noexcept {
    METL_ASSERT(has_value_);
    return *value_ptr();
  }

  METL_NODISCARD T&& value() && noexcept {
    METL_ASSERT(has_value_);
    return static_cast<T&&>(*value_ptr());
  }

  METL_NODISCARD const T&& value() const&& noexcept {
    METL_ASSERT(has_value_);
    return static_cast<const T&&>(*value_ptr());
  }

  METL_NODISCARD E& error() & noexcept {
    METL_ASSERT(!has_value_);
    return *error_ptr();
  }

  METL_NODISCARD const E& error() const& noexcept {
    METL_ASSERT(!has_value_);
    return *error_ptr();
  }

  METL_NODISCARD E&& error() && noexcept {
    METL_ASSERT(!has_value_);
    return static_cast<E&&>(*error_ptr());
  }

  METL_NODISCARD const E&& error() const&& noexcept {
    METL_ASSERT(!has_value_);
    return static_cast<const E&&>(*error_ptr());
  }

  template <typename U>
  METL_NODISCARD T value_or(U&& default_value) const& {
    return has_value_ ? *value_ptr() : static_cast<T>(std::forward<U>(default_value));
  }

  template <typename U>
  METL_NODISCARD T value_or(U&& default_value) && {
    return has_value_ ? static_cast<T&&>(*value_ptr()) : static_cast<T>(std::forward<U>(default_value));
  }

  template <typename G>
  METL_NODISCARD E error_or(G&& default_value) const& {
    return has_value_ ? static_cast<E>(std::forward<G>(default_value)) : *error_ptr();
  }

  template <typename G>
  METL_NODISCARD E error_or(G&& default_value) && {
    return has_value_ ? static_cast<E>(std::forward<G>(default_value)) : static_cast<E&&>(*error_ptr());
  }

  // ---- Modifiers -----------------------------------------------------------

  template <typename... Args>
  T& emplace(Args&&... args) {
    destroy_active();
    construct_value(std::forward<Args>(args)...);
    has_value_ = true;
    return *value_ptr();
  }

  template <typename... Args>
  E& emplace_error(Args&&... args) {
    destroy_active();
    construct_error(std::forward<Args>(args)...);
    has_value_ = false;
    return *error_ptr();
  }

  // ---- swap ----------------------------------------------------------------

  void swap(expected& other) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                      std::is_nothrow_move_constructible<E>::value &&
                                      std::is_nothrow_move_assignable<T>::value &&
                                      std::is_nothrow_move_assignable<E>::value) {
    if (has_value_ && other.has_value_) {
      using std::swap;
      swap(*value_ptr(), *other.value_ptr());
    } else if (!has_value_ && !other.has_value_) {
      using std::swap;
      swap(*error_ptr(), *other.error_ptr());
    } else if (has_value_ && !other.has_value_) {
      swap_value_error(*this, other);
    } else {
      swap_value_error(other, *this);
    }
  }

  // ---- Monadic operations (P2505 / std::expected) --------------------------

  // and_then: F(T) -> expected<U, E>.
  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) & {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    if (has_value_) {
      return std::forward<F>(f)(**this);
    }
    return result_t(unexpected<E>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) const& {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    if (has_value_) {
      return std::forward<F>(f)(**this);
    }
    return result_t(unexpected<E>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) && {
    using result_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<T&&>(**this)))>>;
    if (has_value_) {
      return std::forward<F>(f)(static_cast<T&&>(**this));
    }
    return result_t(unexpected<E>(static_cast<E&&>(*error_ptr())));
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) const&& {
    using result_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const T&&>(**this)))>>;
    if (has_value_) {
      return std::forward<F>(f)(static_cast<const T&&>(**this));
    }
    return result_t(unexpected<E>(static_cast<const E&&>(*error_ptr())));
  }

  // transform: F(T) -> U  =>  expected<U, E>.
  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) & {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    if (has_value_) {
      return expected<result_value_t, E>(in_place, std::forward<F>(f)(**this));
    }
    return expected<result_value_t, E>(unexpect, *error_ptr());
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) const& {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    if (has_value_) {
      return expected<result_value_t, E>(in_place, std::forward<F>(f)(**this));
    }
    return expected<result_value_t, E>(unexpect, *error_ptr());
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) && {
    using result_value_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<T&&>(**this)))>>;
    if (has_value_) {
      return expected<result_value_t, E>(in_place, std::forward<F>(f)(static_cast<T&&>(**this)));
    }
    return expected<result_value_t, E>(unexpect, static_cast<E&&>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) const&& {
    using result_value_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const T&&>(**this)))>>;
    if (has_value_) {
      return expected<result_value_t, E>(in_place, std::forward<F>(f)(static_cast<const T&&>(**this)));
    }
    return expected<result_value_t, E>(unexpect, static_cast<const E&&>(*error_ptr()));
  }

  // or_else: F(E) -> expected<T, G>.
  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) & {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    if (has_value_) {
      return result_t(in_place, *value_ptr());
    }
    return std::forward<F>(f)(*error_ptr());
  }

  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) const& {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    if (has_value_) {
      return result_t(in_place, *value_ptr());
    }
    return std::forward<F>(f)(*error_ptr());
  }

  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) && {
    using result_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<E&&>(*error_ptr())))>>;
    if (has_value_) {
      return result_t(in_place, static_cast<T&&>(*value_ptr()));
    }
    return std::forward<F>(f)(static_cast<E&&>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) const&& {
    using result_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const E&&>(*error_ptr())))>>;
    if (has_value_) {
      return result_t(in_place, static_cast<const T&&>(*value_ptr()));
    }
    return std::forward<F>(f)(static_cast<const E&&>(*error_ptr()));
  }

  // transform_error: F(E) -> G  =>  expected<T, G>.
  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) & {
    using result_error_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    if (has_value_) {
      return expected<T, result_error_t>(in_place, *value_ptr());
    }
    return expected<T, result_error_t>(unexpect, std::forward<F>(f)(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) const& {
    using result_error_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    if (has_value_) {
      return expected<T, result_error_t>(in_place, *value_ptr());
    }
    return expected<T, result_error_t>(unexpect, std::forward<F>(f)(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) && {
    using result_error_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<E&&>(*error_ptr())))>>;
    if (has_value_) {
      return expected<T, result_error_t>(in_place, static_cast<T&&>(*value_ptr()));
    }
    return expected<T, result_error_t>(unexpect, std::forward<F>(f)(static_cast<E&&>(*error_ptr())));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) const&& {
    using result_error_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const E&&>(*error_ptr())))>>;
    if (has_value_) {
      return expected<T, result_error_t>(in_place, static_cast<const T&&>(*value_ptr()));
    }
    return expected<T, result_error_t>(unexpect, std::forward<F>(f)(static_cast<const E&&>(*error_ptr())));
  }

 private:
  T* value_ptr() noexcept { return storage_.value_storage.ptr(); }
  const T* value_ptr() const noexcept { return storage_.value_storage.ptr(); }

  E* error_ptr() noexcept { return storage_.error_storage.ptr(); }
  const E* error_ptr() const noexcept { return storage_.error_storage.ptr(); }

  // Unchecked accessors used internally where active-member state is known.
  T& value_unchecked() noexcept { return *value_ptr(); }
  const T& value_unchecked() const noexcept { return *value_ptr(); }
  E& error_unchecked() noexcept { return *error_ptr(); }
  const E& error_unchecked() const noexcept { return *error_ptr(); }

  template <typename... Args>
  void construct_value(Args&&... args) {
    ::new (storage_.value_storage.addr()) T(std::forward<Args>(args)...);
  }

  template <typename... Args>
  void construct_error(Args&&... args) {
    ::new (storage_.error_storage.addr()) E(std::forward<Args>(args)...);
  }

  void destroy_active() noexcept {
    if (has_value_) {
      value_ptr()->~T();
    } else {
      error_ptr()->~E();
    }
  }

  template <typename U>
  void assign_value_or_rebind(U&& value) {
    if (has_value_) {
      *value_ptr() = std::forward<U>(value);
      return;
    }
    // Currently holds an error; switch to the value state exception-safely.
    reinit_as_value(std::forward<U>(value));
  }

  template <typename G>
  void assign_error_or_rebind(G&& error) {
    if (!has_value_) {
      *error_ptr() = std::forward<G>(error);
      return;
    }
    // Currently holds a value; switch to the error state exception-safely.
    reinit_as_error(std::forward<G>(error));
  }

  // Cross-state reinitialization (std::expected's "reinit-expected" pattern).
  // Precondition: currently in the error state; switch to the value state.
  // Guarantees a throwing T constructor never destroys the error while leaving
  // has_value_ unchanged (which would double-destroy the error on ~expected).
  template <typename... Args>
  void reinit_as_value(Args&&... args) {
    if constexpr (std::is_nothrow_constructible<T, Args&&...>::value) {
      error_ptr()->~E();
      construct_value(std::forward<Args>(args)...);
    } else if constexpr (std::is_nothrow_move_constructible<T>::value) {
      // Build the new value first (may throw; error stays intact), then commit.
      T tmp(std::forward<Args>(args)...);
      error_ptr()->~E();
      construct_value(static_cast<T&&>(tmp));
    } else {
#if METL_NO_EXCEPTIONS
      error_ptr()->~E();
      construct_value(std::forward<Args>(args)...);
#else
      E backup(static_cast<E&&>(*error_ptr()));
      error_ptr()->~E();
      try {
        construct_value(std::forward<Args>(args)...);
      } catch (...) {
        construct_error(static_cast<E&&>(backup));  // restore the error state
        throw;
      }
#endif
    }
    has_value_ = true;
  }

  // Precondition: currently in the value state; switch to the error state.
  template <typename... Args>
  void reinit_as_error(Args&&... args) {
    if constexpr (std::is_nothrow_constructible<E, Args&&...>::value) {
      value_ptr()->~T();
      construct_error(std::forward<Args>(args)...);
    } else if constexpr (std::is_nothrow_move_constructible<E>::value) {
      E tmp(std::forward<Args>(args)...);
      value_ptr()->~T();
      construct_error(static_cast<E&&>(tmp));
    } else {
#if METL_NO_EXCEPTIONS
      value_ptr()->~T();
      construct_error(std::forward<Args>(args)...);
#else
      T backup(static_cast<T&&>(*value_ptr()));
      value_ptr()->~T();
      try {
        construct_error(std::forward<Args>(args)...);
      } catch (...) {
        construct_value(static_cast<T&&>(backup));  // restore the value state
        throw;
      }
#endif
    }
    has_value_ = false;
  }

  // Exception-safe cross-state swap: `v` holds a value, `e` holds an error.
  // On return `v` holds the error and `e` holds the value. If a move-construct
  // throws, both operands are rolled back to their original active member so
  // neither is left with a destroyed member under a stale discriminant.
  static void swap_value_error(expected& v,
                               expected& e) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                                     std::is_nothrow_move_constructible<E>::value) {
    if constexpr (std::is_nothrow_move_constructible<E>::value) {
      E tmp(static_cast<E&&>(*e.error_ptr()));
      e.error_ptr()->~E();
#if !METL_NO_EXCEPTIONS
      try {
#endif
        ::new (e.storage_.value_storage.addr()) T(static_cast<T&&>(*v.value_ptr()));
#if !METL_NO_EXCEPTIONS
      } catch (...) {
        ::new (e.storage_.error_storage.addr()) E(static_cast<E&&>(tmp));  // restore e
        throw;
      }
#endif
      v.value_ptr()->~T();
      ::new (v.storage_.error_storage.addr()) E(static_cast<E&&>(tmp));
    } else {
      T tmp(static_cast<T&&>(*v.value_ptr()));
      v.value_ptr()->~T();
#if !METL_NO_EXCEPTIONS
      try {
#endif
        ::new (v.storage_.error_storage.addr()) E(static_cast<E&&>(*e.error_ptr()));
#if !METL_NO_EXCEPTIONS
      } catch (...) {
        ::new (v.storage_.value_storage.addr()) T(static_cast<T&&>(tmp));  // restore v
        throw;
      }
#endif
      e.error_ptr()->~E();
      ::new (e.storage_.value_storage.addr()) T(static_cast<T&&>(tmp));
    }
    v.has_value_ = false;
    e.has_value_ = true;
  }

  union storage_union {
    storage_union() {}
    ~storage_union() {}

    storage_for<T> value_storage;
    storage_for<E> error_storage;
  } storage_;

  bool has_value_;
};

// =============================================================================
// expected<void, E> partial specialization
// =============================================================================

template <typename E>
class expected<void, E> {
 public:
  using value_type = void;
  using error_type = E;
  using unexpected_type = unexpected<E>;

  template <typename U>
  using rebind = expected<U, E>;

  // ---- Constructors --------------------------------------------------------

  constexpr expected() noexcept : has_value_(true) {}

  // in_place_t: produces a value-state expected<void, E> (no T to construct).
  constexpr explicit expected(in_place_t) noexcept : has_value_(true) {}

  template <typename... Args, typename = std::enable_if_t<std::is_constructible<E, Args...>::value>>
  constexpr explicit expected(unexpect_t, Args&&... args) : has_value_(false) {
    construct_error(std::forward<Args>(args)...);
  }

  template <typename G = E, typename = std::enable_if_t<std::is_constructible<E, const G&>::value>>
  expected(const unexpected<G>& u) : has_value_(false) {
    construct_error(u.error());
  }

  template <typename G = E, typename = std::enable_if_t<std::is_constructible<E, G&&>::value>>
  expected(unexpected<G>&& u) : has_value_(false) {
    construct_error(static_cast<G&&>(u.error()));
  }

  expected(const expected& other) : has_value_(other.has_value_) {
    if (!has_value_) {
      construct_error(*other.error_ptr());
    }
  }

  expected(expected&& other) noexcept(std::is_nothrow_move_constructible<E>::value)
      : has_value_(other.has_value_) {
    if (!has_value_) {
      construct_error(static_cast<E&&>(*other.error_ptr()));
    }
  }

  ~expected() {
    if (!has_value_) {
      error_ptr()->~E();
    }
  }

  // ---- Assignment ----------------------------------------------------------

  expected& operator=(const expected& other) {
    if (this == &other) {
      return *this;
    }

    if (other.has_value_) {
      if (!has_value_) {
        error_ptr()->~E();
        has_value_ = true;
      }
    } else {
      if (has_value_) {
        construct_error(*other.error_ptr());
        has_value_ = false;
      } else {
        *error_ptr() = *other.error_ptr();
      }
    }
    return *this;
  }

  expected& operator=(expected&& other) noexcept(std::is_nothrow_move_constructible<E>::value &&
                                                 std::is_nothrow_move_assignable<E>::value) {
    if (this == &other) {
      return *this;
    }

    if (other.has_value_) {
      if (!has_value_) {
        error_ptr()->~E();
        has_value_ = true;
      }
    } else {
      if (has_value_) {
        construct_error(static_cast<E&&>(*other.error_ptr()));
        has_value_ = false;
      } else {
        *error_ptr() = static_cast<E&&>(*other.error_ptr());
      }
    }
    return *this;
  }

  template <typename G = E,
            typename = std::enable_if_t<std::is_constructible<E, const G&>::value &&
                                        std::is_assignable<E&, const G&>::value>>
  expected& operator=(const unexpected<G>& error) {
    if (has_value_) {
      construct_error(error.error());
      has_value_ = false;
    } else {
      *error_ptr() = error.error();
    }
    return *this;
  }

  template <
      typename G = E,
      typename = std::enable_if_t<std::is_constructible<E, G&&>::value && std::is_assignable<E&, G&&>::value>>
  expected& operator=(unexpected<G>&& error) {
    if (has_value_) {
      construct_error(static_cast<G&&>(error.error()));
      has_value_ = false;
    } else {
      *error_ptr() = static_cast<G&&>(error.error());
    }
    return *this;
  }

  // ---- Observers -----------------------------------------------------------

  METL_NODISCARD constexpr explicit operator bool() const noexcept { return has_value_; }
  METL_NODISCARD constexpr bool has_value() const noexcept { return has_value_; }

  // value() returns void; asserts on error state.
  void value() const& noexcept { METL_ASSERT(has_value_); }
  void value() && noexcept { METL_ASSERT(has_value_); }

  // operator*() returns void.
  void operator*() const noexcept { METL_ASSERT(has_value_); }

  METL_NODISCARD E& error() & noexcept {
    METL_ASSERT(!has_value_);
    return *error_ptr();
  }

  METL_NODISCARD const E& error() const& noexcept {
    METL_ASSERT(!has_value_);
    return *error_ptr();
  }

  METL_NODISCARD E&& error() && noexcept {
    METL_ASSERT(!has_value_);
    return static_cast<E&&>(*error_ptr());
  }

  METL_NODISCARD const E&& error() const&& noexcept {
    METL_ASSERT(!has_value_);
    return static_cast<const E&&>(*error_ptr());
  }

  template <typename G>
  METL_NODISCARD E error_or(G&& default_value) const& {
    return has_value_ ? static_cast<E>(std::forward<G>(default_value)) : *error_ptr();
  }

  template <typename G>
  METL_NODISCARD E error_or(G&& default_value) && {
    return has_value_ ? static_cast<E>(std::forward<G>(default_value)) : static_cast<E&&>(*error_ptr());
  }

  // ---- Modifiers -----------------------------------------------------------

  void emplace() noexcept {
    if (!has_value_) {
      error_ptr()->~E();
      has_value_ = true;
    }
  }

  template <typename... Args>
  E& emplace_error(Args&&... args) {
    if (!has_value_) {
      error_ptr()->~E();
    }
    construct_error(std::forward<Args>(args)...);
    has_value_ = false;
    return *error_ptr();
  }

  void swap(expected& other) noexcept(std::is_nothrow_move_constructible<E>::value &&
                                      std::is_nothrow_move_assignable<E>::value) {
    if (has_value_ && other.has_value_) {
      // nothing to swap.
    } else if (!has_value_ && !other.has_value_) {
      using std::swap;
      swap(*error_ptr(), *other.error_ptr());
    } else if (has_value_ && !other.has_value_) {
      construct_error(static_cast<E&&>(*other.error_ptr()));
      other.error_ptr()->~E();
      has_value_ = false;
      other.has_value_ = true;
    } else {
      other.swap(*this);
    }
  }

  // ---- Monadic operations --------------------------------------------------

  // and_then: F() -> expected<U, E>.
  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) & {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    if (has_value_) {
      return std::forward<F>(f)();
    }
    return result_t(unexpected<E>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) const& {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    if (has_value_) {
      return std::forward<F>(f)();
    }
    return result_t(unexpected<E>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) && {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    if (has_value_) {
      return std::forward<F>(f)();
    }
    return result_t(unexpected<E>(static_cast<E&&>(*error_ptr())));
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) const&& {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    if (has_value_) {
      return std::forward<F>(f)();
    }
    return result_t(unexpected<E>(static_cast<const E&&>(*error_ptr())));
  }

  // transform: F() -> U  =>  expected<U, E>. (U may be void.)
  template <typename F>
  METL_NODISCARD auto transform(F&& f) & {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    if constexpr (std::is_void<result_value_t>::value) {
      if (has_value_) {
        std::forward<F>(f)();
        return expected<void, E>();
      }
      return expected<void, E>(unexpect, *error_ptr());
    } else {
      if (has_value_) {
        return expected<result_value_t, E>(in_place, std::forward<F>(f)());
      }
      return expected<result_value_t, E>(unexpect, *error_ptr());
    }
  }

  template <typename F>
  METL_NODISCARD auto transform(F&& f) const& {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    if constexpr (std::is_void<result_value_t>::value) {
      if (has_value_) {
        std::forward<F>(f)();
        return expected<void, E>();
      }
      return expected<void, E>(unexpect, *error_ptr());
    } else {
      if (has_value_) {
        return expected<result_value_t, E>(in_place, std::forward<F>(f)());
      }
      return expected<result_value_t, E>(unexpect, *error_ptr());
    }
  }

  template <typename F>
  METL_NODISCARD auto transform(F&& f) && {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    if constexpr (std::is_void<result_value_t>::value) {
      if (has_value_) {
        std::forward<F>(f)();
        return expected<void, E>();
      }
      return expected<void, E>(unexpect, static_cast<E&&>(*error_ptr()));
    } else {
      if (has_value_) {
        return expected<result_value_t, E>(in_place, std::forward<F>(f)());
      }
      return expected<result_value_t, E>(unexpect, static_cast<E&&>(*error_ptr()));
    }
  }

  template <typename F>
  METL_NODISCARD auto transform(F&& f) const&& {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    if constexpr (std::is_void<result_value_t>::value) {
      if (has_value_) {
        std::forward<F>(f)();
        return expected<void, E>();
      }
      return expected<void, E>(unexpect, static_cast<const E&&>(*error_ptr()));
    } else {
      if (has_value_) {
        return expected<result_value_t, E>(in_place, std::forward<F>(f)());
      }
      return expected<result_value_t, E>(unexpect, static_cast<const E&&>(*error_ptr()));
    }
  }

  // or_else: F(E) -> expected<void, G>.
  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) & {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    if (has_value_) {
      return result_t();
    }
    return std::forward<F>(f)(*error_ptr());
  }

  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) const& {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    if (has_value_) {
      return result_t();
    }
    return std::forward<F>(f)(*error_ptr());
  }

  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) && {
    using result_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<E&&>(*error_ptr())))>>;
    if (has_value_) {
      return result_t();
    }
    return std::forward<F>(f)(static_cast<E&&>(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto or_else(F&& f) const&& {
    using result_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const E&&>(*error_ptr())))>>;
    if (has_value_) {
      return result_t();
    }
    return std::forward<F>(f)(static_cast<const E&&>(*error_ptr()));
  }

  // transform_error: F(E) -> G  =>  expected<void, G>.
  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) & {
    using result_error_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    if (has_value_) {
      return expected<void, result_error_t>();
    }
    return expected<void, result_error_t>(unexpect, std::forward<F>(f)(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) const& {
    using result_error_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(*error_ptr()))>>;
    if (has_value_) {
      return expected<void, result_error_t>();
    }
    return expected<void, result_error_t>(unexpect, std::forward<F>(f)(*error_ptr()));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) && {
    using result_error_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<E&&>(*error_ptr())))>>;
    if (has_value_) {
      return expected<void, result_error_t>();
    }
    return expected<void, result_error_t>(unexpect, std::forward<F>(f)(static_cast<E&&>(*error_ptr())));
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform_error(F&& f) const&& {
    using result_error_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const E&&>(*error_ptr())))>>;
    if (has_value_) {
      return expected<void, result_error_t>();
    }
    return expected<void, result_error_t>(unexpect, std::forward<F>(f)(static_cast<const E&&>(*error_ptr())));
  }

 private:
  E* error_ptr() noexcept { return error_storage_.ptr(); }
  const E* error_ptr() const noexcept { return error_storage_.ptr(); }

  template <typename... Args>
  void construct_error(Args&&... args) {
    ::new (error_storage_.addr()) E(std::forward<Args>(args)...);
  }

  // storage_for<E> is a raw byte buffer with correct alignment; leaving it
  // uninitialized in the value state is OK (no E construction required).
  storage_for<E> error_storage_;
  bool has_value_;
};

// =============================================================================
// Free swap
// =============================================================================

template <typename T, typename E>
void swap(expected<T, E>& lhs, expected<T, E>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

// =============================================================================
// Comparison operators
// =============================================================================

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

// expected<void, E1> vs expected<void, E2>.
template <typename E1, typename E2>
constexpr auto operator==(const expected<void, E1>& lhs,
                          const expected<void, E2>& rhs) -> decltype(lhs.error() == rhs.error(), bool()) {
  if (lhs.has_value() != rhs.has_value()) {
    return false;
  }
  return lhs.has_value() ? true : (lhs.error() == rhs.error());
}

template <typename E1, typename E2>
constexpr auto operator!=(const expected<void, E1>& lhs,
                          const expected<void, E2>& rhs) -> decltype(lhs.error() == rhs.error(), bool()) {
  return !(lhs == rhs);
}

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

// expected vs unexpected.
template <typename T, typename E, typename E2>
constexpr auto operator==(const expected<T, E>& lhs,
                          const unexpected<E2>& rhs) -> decltype(lhs.error() == rhs.error(), bool()) {
  return !lhs.has_value() && (lhs.error() == rhs.error());
}

template <typename T, typename E, typename E2>
constexpr auto operator==(const unexpected<E2>& lhs,
                          const expected<T, E>& rhs) -> decltype(lhs.error() == rhs.error(), bool()) {
  return !rhs.has_value() && (lhs.error() == rhs.error());
}

template <typename T, typename E, typename E2>
constexpr auto operator!=(const expected<T, E>& lhs,
                          const unexpected<E2>& rhs) -> decltype(lhs.error() != rhs.error(), bool()) {
  return lhs.has_value() || (lhs.error() != rhs.error());
}

template <typename T, typename E, typename E2>
constexpr auto operator!=(const unexpected<E2>& lhs,
                          const expected<T, E>& rhs) -> decltype(lhs.error() != rhs.error(), bool()) {
  return rhs.has_value() || (lhs.error() != rhs.error());
}

}  // namespace metl
