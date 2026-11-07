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
#include "metl/detail/enable_copy_move.hpp"
#include "metl/detail/three_way.hpp"
#include "metl/in_place.hpp"
#include "metl/type_traits.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace metl {
METL_DETAIL_ABI_BEGIN

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

// ---- Storage layers ---------------------------------------------------------
//
// One layer per special member, each trivial when `T`'s is and user-provided
// otherwise, so `optional`'s defaulted members are trivial exactly where
// `std::optional`'s are (#163). The layers never change the layout: the union
// and the flag live in `optional_storage`, and the others add no members.

struct optional_empty_byte {};

// Union storage (rather than an aligned byte buffer) so the active member can
// be named directly -- a prerequisite for genuine constexpr use: reaching a
// byte buffer needs a reinterpret_cast, which is never constant-evaluable,
// whereas a union's active member is.
template <typename T, bool = std::is_trivially_destructible_v<T>>
union optional_union {
  optional_empty_byte empty;
  // Unqualified: a const T is stored as T and handed out as `const T`, so the
  // placement new that builds it never needs a const T* turned into void*
  // (which neither a plain static_cast nor libc++'s std::construct_at does).
  std::remove_cv_t<T> value;

  constexpr optional_union() noexcept : empty{} {}
};

template <typename T>
union optional_union<T, false> {
  optional_empty_byte empty;
  std::remove_cv_t<T> value;

  constexpr optional_union() noexcept : empty{} {}
  optional_union(const optional_union&) = default;
  optional_union(optional_union&&) = default;
  optional_union& operator=(const optional_union&) = default;
  optional_union& operator=(optional_union&&) = default;
  // T's lifetime is managed by optional; the union itself does nothing on
  // destruction (constexpr in C++20 so optional can be a literal type).
  METL_DETAIL_CONSTEXPR20 ~optional_union() {}
};

template <typename T>
struct optional_storage_common {
  // For a T with a const or reference member, C++17 [basic.life]/8 does not let
  // the name `storage.value` reach a T re-created by emplace or assignment
  // after a reset without std::launder. C++20 removed that rule (P1971) and no
  // compiler optimises on it, so optional does not launder here. std::launder
  // is constexpr, so this is not a constant-evaluation constraint: `expected`
  // launders in its accessors and stays constant-evaluable (#584).
  METL_NODISCARD METL_DETAIL_CONSTEXPR20 T* data() noexcept { return detail::addressof(storage.value); }
  METL_NODISCARD constexpr const T* data() const noexcept { return detail::addressof(storage.value); }

  template <typename... Args>
  METL_DETAIL_CONSTEXPR20 void construct(Args&&... args) {
    detail::construct_at(detail::addressof(storage.value), std::forward<Args>(args)...);
    engaged = true;
  }

  // Disengage first, then destroy: a destructor that calls back into this
  // optional (reset(), has_value()) must find it empty, not destroy the value
  // a second time.
  METL_DETAIL_CONSTEXPR20 void destroy_value() noexcept {
    if (!engaged) {
      return;
    }
    engaged = false;
    detail::destroy_at(data());
  }

  template <typename U>
  void assign_or_construct(U&& value) {
    if (engaged) {
      if constexpr (std::is_assignable_v<T&, U>) {
        *data() = std::forward<U>(value);
        return;
      } else {
        // A const T has no assignment: replace the object.
        destroy_value();
      }
    }

    construct(std::forward<U>(value));
  }

  optional_union<T> storage{};
  bool engaged = false;
};

// Destructor: trivial when T's is.
template <typename T, bool = std::is_trivially_destructible_v<T>>
struct optional_storage : optional_storage_common<T> {};

template <typename T>
struct optional_storage<T, false> : optional_storage_common<T> {
  optional_storage() = default;
  optional_storage(const optional_storage&) = default;
  optional_storage(optional_storage&&) = default;
  optional_storage& operator=(const optional_storage&) = default;
  optional_storage& operator=(optional_storage&&) = default;
  METL_DETAIL_CONSTEXPR20 ~optional_storage() { this->destroy_value(); }
};

// Copy constructor: trivial when T's is.
template <typename T, bool = std::is_trivially_copy_constructible_v<T>>
struct optional_copy : optional_storage<T> {};

template <typename T>
struct optional_copy<T, false> : optional_storage<T> {
  optional_copy() = default;
  optional_copy(const optional_copy& other) : optional_storage<T>() {
    if (other.engaged) {
      this->construct(*other.data());
    }
  }
  optional_copy(optional_copy&&) = default;
  optional_copy& operator=(const optional_copy&) = default;
  optional_copy& operator=(optional_copy&&) = default;
  ~optional_copy() = default;
};

// Move constructor: trivial when T's is.
// The source remains engaged (its value is moved-from), not reset.
template <typename T, bool = std::is_trivially_move_constructible_v<T>>
struct optional_move : optional_copy<T> {};

template <typename T>
struct optional_move<T, false> : optional_copy<T> {
  optional_move() = default;
  optional_move(const optional_move&) = default;
  optional_move(optional_move&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
      : optional_copy<T>() {
    if (other.engaged) {
      this->construct(static_cast<T&&>(*other.data()));
    }
  }
  optional_move& operator=(const optional_move&) = default;
  optional_move& operator=(optional_move&&) = default;
  ~optional_move() = default;
};

// Copy assignment: trivial when T's copy constructor, copy assignment and
// destructor all are, as for std::optional.
template <typename T,
          bool = std::is_trivially_copy_constructible_v<T> && std::is_trivially_copy_assignable_v<T> &&
                 std::is_trivially_destructible_v<T>>
struct optional_copy_assign : optional_move<T> {};

template <typename T>
struct optional_copy_assign<T, false> : optional_move<T> {
  optional_copy_assign() = default;
  optional_copy_assign(const optional_copy_assign&) = default;
  optional_copy_assign(optional_copy_assign&&) = default;
  optional_copy_assign& operator=(const optional_copy_assign& other) {
    if (this == &other) {
      return *this;
    }
    if (other.engaged) {
      this->assign_or_construct(*other.data());
    } else {
      this->destroy_value();
    }
    return *this;
  }
  optional_copy_assign& operator=(optional_copy_assign&&) = default;
  ~optional_copy_assign() = default;
};

// Move assignment: trivial when T's move constructor, move assignment and
// destructor all are.
template <typename T,
          bool = std::is_trivially_move_constructible_v<T> && std::is_trivially_move_assignable_v<T> &&
                 std::is_trivially_destructible_v<T>>
struct optional_move_assign : optional_copy_assign<T> {};

template <typename T>
struct optional_move_assign<T, false> : optional_copy_assign<T> {
  optional_move_assign() = default;
  optional_move_assign(const optional_move_assign&) = default;
  optional_move_assign(optional_move_assign&&) = default;
  optional_move_assign& operator=(const optional_move_assign&) = default;
  optional_move_assign& operator=(optional_move_assign&& other) noexcept(
      std::is_nothrow_move_assignable_v<T> && std::is_nothrow_move_constructible_v<T>) {
    if (this == &other) {
      return *this;
    }
    if (other.engaged) {
      this->assign_or_construct(static_cast<T&&>(*other.data()));
    } else {
      this->destroy_value();
    }
    return *this;
  }
  ~optional_move_assign() = default;
};

// The layers above, with the operations T does not allow deleted
// (detail/enable_copy_move.hpp). Copy assignment needs only a copy constructor:
// a const T is replaced instead of assigned.
template <typename T>
using optional_base = enable_copy_move<optional_move_assign<T>,
                                       std::is_copy_constructible_v<T>,
                                       std::is_copy_constructible_v<T>,
                                       std::is_move_constructible_v<T>,
                                       std::is_move_constructible_v<T>>;

}  // namespace detail

/// @brief A fixed-storage nullable value wrapper (in-place, no heap).
///
/// Stores an optional `T` inside the object itself using an internal union; it
/// never allocates. Copyable when `T` is. Each copy and move operation and the
/// destructor is trivial when `T`'s is, as for `std::optional`, so
/// `optional<int>` is trivially copyable and returns in registers. On C++20 it is a literal
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
class optional : detail::optional_base<T> {
  static_assert(std::is_object_v<T> && !std::is_array_v<T>, METL_DETAIL_OBJECT_TYPE_MESSAGE);

  using base = detail::optional_base<T>;
  using base::assign_or_construct;
  using base::construct;
  using base::data;
  using base::engaged;

 public:
  using value_type = T;

  // ---- Constructors ---------------------------------------------------------

  /// @brief Constructs an empty optional.
  // See the converting constructor below.
  template <typename U>
  static constexpr bool bool_from_optional_v =
      std::is_same_v<std::remove_cv_t<T>, bool> &&
      detail::is_metl_optional<std::remove_cv_t<std::remove_reference_t<U>>>::value;

  constexpr optional() noexcept = default;

  /// @brief Constructs an empty optional from the nullopt tag.
  constexpr optional(nullopt_t) noexcept {}

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
  constexpr optional(U&& v) {
    construct(std::forward<U>(v));
  }

  /// @brief Constructs an engaged optional, building `T` in place.
  /// @tparam Args Constructor argument types forwarded to `T`.
  /// @param args Arguments forwarded to `T`'s constructor.
  // in_place_t constructor.
  template <typename... Args>
  constexpr explicit optional(in_place_t, Args&&... args) {
    construct(std::forward<Args>(args)...);
  }

  /// @brief Copy constructor; copies the contained value if engaged.
  optional(const optional&) = default;

  /// @brief Move constructor; moves the contained value if engaged.
  /// @note The source remains engaged (its value is moved-from), not reset.
  optional(optional&&) = default;

  /// @brief Copy-assigns from another optional, matching engaged state.
  optional& operator=(const optional&) = default;

  /// @brief Move-assigns from another optional, matching engaged state.
  optional& operator=(optional&&) = default;

  /// @brief Destroys the contained value if engaged (constexpr on C++20).
  ~optional() = default;

  // ---- Assignment -----------------------------------------------------------

  /// @brief Resets to empty from the nullopt tag.
  optional& operator=(nullopt_t) noexcept {
    reset();
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
    METL_ASSERT(engaged);
    return data();
  }

  /// @brief Accesses a member of the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does not throw.
  METL_NODISCARD T* operator->() noexcept {
    METL_ASSERT(engaged);
    return data();
  }

  /// @brief Returns a reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does not throw.
  METL_NODISCARD constexpr const T& operator*() const& noexcept {
    METL_ASSERT(engaged);
    return *data();
  }

  /// @brief Returns a reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does not throw.
  METL_NODISCARD T& operator*() & noexcept {
    METL_ASSERT(engaged);
    return *data();
  }

  /// @brief Returns an rvalue reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does not throw.
  METL_NODISCARD const T&& operator*() const&& noexcept {
    METL_ASSERT(engaged);
    return static_cast<const T&&>(*data());
  }

  /// @brief Returns an rvalue reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does not throw.
  METL_NODISCARD T&& operator*() && noexcept {
    METL_ASSERT(engaged);
    return static_cast<T&&>(*data());
  }

  /// @brief Returns true if the optional holds a value.
  METL_NODISCARD constexpr explicit operator bool() const noexcept { return engaged; }
  /// @brief Returns true if the optional holds a value.
  METL_NODISCARD constexpr bool has_value() const noexcept { return engaged; }

  /// @brief Returns a reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does NOT throw
  /// std::bad_optional_access. Use `value_or` for a checked fallback.
  METL_NODISCARD T& value() & noexcept {
    METL_ASSERT(engaged);
    return *data();
  }

  /// @brief Returns a reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does NOT throw.
  METL_NODISCARD constexpr const T& value() const& noexcept {
    METL_ASSERT(engaged);
    return *data();
  }

  /// @brief Returns an rvalue reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does NOT throw.
  METL_NODISCARD T&& value() && noexcept {
    METL_ASSERT(engaged);
    return static_cast<T&&>(*data());
  }

  /// @brief Returns an rvalue reference to the contained value.
  /// @pre has_value()
  /// @warning Asserts (aborts by default) if empty; it does NOT throw.
  METL_NODISCARD constexpr const T&& value() const&& noexcept {
    METL_ASSERT(engaged);
    return static_cast<const T&&>(*data());
  }

  /// @brief Returns the contained value, or `default_value` if empty.
  /// @param default_value Returned (converted to `T`) when the optional is empty.
  /// @return The contained value or the fallback. Never asserts.
  template <typename U>
  METL_NODISCARD std::remove_cv_t<T> value_or(U&& default_value) const& {
    return engaged ? *data() : static_cast<std::remove_cv_t<T>>(std::forward<U>(default_value));
  }

  /// @brief Returns the contained value, or `default_value` if empty. Never asserts.
  template <typename U>
  METL_NODISCARD std::remove_cv_t<T> value_or(U&& default_value) & {
    return engaged ? *data() : static_cast<std::remove_cv_t<T>>(std::forward<U>(default_value));
  }

  /// @brief Returns the contained value, or `default_value` if empty. Never asserts.
  template <typename U>
  METL_NODISCARD std::remove_cv_t<T> value_or(U&& default_value) && {
    return engaged ? static_cast<T&&>(*data())
                   : static_cast<std::remove_cv_t<T>>(std::forward<U>(default_value));
  }

  /// @brief Returns the contained value, or `default_value` if empty. Never asserts.
  template <typename U>
  METL_NODISCARD std::remove_cv_t<T> value_or(U&& default_value) const&& {
    return engaged ? static_cast<const T&&>(*data())
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
      if (engaged) {
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
  METL_DETAIL_CONSTEXPR20 void reset() noexcept { this->destroy_value(); }

  /// @brief Swaps the contents (and engaged state) with another optional.
  void swap(optional& other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                      std::is_nothrow_move_assignable_v<T> &&
                                      std::is_nothrow_swappable_v<T>) {
    if (engaged && other.engaged) {
      using std::swap;
      swap(*data(), *other.data());
    } else if (engaged && !other.engaged) {
      other.construct(static_cast<T&&>(*data()));
      reset();
    } else if (!engaged && other.engaged) {
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
    if (engaged) {
      return std::forward<F>(f)(**this);
    }
    return result_t{};
  }

  template <typename F>
  METL_NODISCARD constexpr auto and_then(F&& f) const& {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    static_assert(detail::is_metl_optional<result_t>::value,
                  "optional::and_then(F): F must return a metl::optional");
    if (engaged) {
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
    if (engaged) {
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
    if (engaged) {
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
    if (engaged) {
      return optional<result_value_t>(std::forward<F>(f)(**this));
    }
    return optional<result_value_t>{};
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) const& {
    using result_value_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(**this))>>;
    if (engaged) {
      return optional<result_value_t>(std::forward<F>(f)(**this));
    }
    return optional<result_value_t>{};
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) && {
    using result_value_t =
        std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<T&&>(**this)))>>;
    if (engaged) {
      return optional<result_value_t>(std::forward<F>(f)(static_cast<T&&>(**this)));
    }
    return optional<result_value_t>{};
  }

  template <typename F>
  METL_NODISCARD constexpr auto transform(F&& f) const&& {
    using result_value_t = std::remove_cv_t<
        std::remove_reference_t<decltype(std::forward<F>(f)(static_cast<const T&&>(**this)))>>;
    if (engaged) {
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
    if (engaged) {
      return *this;
    }
    return std::forward<F>(f)();
  }

  template <typename F>
  METL_NODISCARD constexpr optional or_else(F&& f) && {
    using result_t = std::remove_cv_t<std::remove_reference_t<decltype(std::forward<F>(f)())>>;
    static_assert(detail::is_metl_optional<result_t>::value,
                  "optional::or_else(F): F must return a metl::optional");
    if (engaged) {
      return optional(static_cast<T&&>(**this));
    }
    return std::forward<F>(f)();
  }
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

/// @brief Swaps two optionals. Declared only for a swappable `T`.
template <typename T, std::enable_if_t<std::is_swappable_v<T>, int> = 0>
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

METL_DETAIL_ABI_END
}  // namespace metl

namespace metl {
METL_DETAIL_ABI_BEGIN
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
METL_DETAIL_ABI_END
}  // namespace metl

namespace std {

/// @brief std::hash specialization; empty optionals hash to 0. Enabled only
///        when `std::hash<std::remove_const_t<T>>` is, and `noexcept` only
///        when that hash is.
template <typename T>
struct hash<::metl::optional<T>> : ::metl::detail::optional_hash<T> {};

}  // namespace std
