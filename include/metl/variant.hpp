#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/in_place.hpp"

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace metl {

/// @brief Sentinel index value for a valueless variant (largest size_t).
constexpr std::size_t variant_npos = static_cast<std::size_t>(-1);

template <typename... Ts>
class variant;

namespace detail {

// ---------- Compile-time pack utilities ----------

template <typename... Ts>
struct max_sizeof;

template <typename T>
struct max_sizeof<T> : std::integral_constant<std::size_t, sizeof(T)> {};

template <typename T, typename U, typename... Rest>
struct max_sizeof<T, U, Rest...>
    : std::integral_constant<std::size_t,
                             (sizeof(T) > max_sizeof<U, Rest...>::value ? sizeof(T)
                                                                        : max_sizeof<U, Rest...>::value)> {};

template <typename... Ts>
struct max_alignof;

template <typename T>
struct max_alignof<T> : std::integral_constant<std::size_t, alignof(T)> {};

template <typename T, typename U, typename... Rest>
struct max_alignof<T, U, Rest...>
    : std::integral_constant<std::size_t,
                             (alignof(T) > max_alignof<U, Rest...>::value ? alignof(T)
                                                                          : max_alignof<U, Rest...>::value)> {
};

template <std::size_t Index, typename... Ts>
struct nth_type;

template <typename T, typename... Ts>
struct nth_type<0, T, Ts...> {
  using type = T;
};

template <std::size_t Index, typename T, typename... Ts>
struct nth_type<Index, T, Ts...> {
  using type = typename nth_type<Index - 1, Ts...>::type;
};

template <typename T, typename... Ts>
struct index_of_type;

template <typename T>
struct index_of_type<T> : std::integral_constant<std::size_t, variant_npos> {};

template <typename T, typename U, typename... Ts>
struct index_of_type<T, U, Ts...> {
 private:
  static constexpr std::size_t next = index_of_type<T, Ts...>::value;

 public:
  static constexpr std::size_t value =
      std::is_same<T, U>::value ? 0 : (next == variant_npos ? variant_npos : 1 + next);
};

// Count how many times T appears in Ts...
template <typename T, typename... Ts>
struct count_of_type;

template <typename T>
struct count_of_type<T> : std::integral_constant<std::size_t, 0> {};

template <typename T, typename U, typename... Ts>
struct count_of_type<T, U, Ts...>
    : std::integral_constant<std::size_t,
                             (std::is_same<T, U>::value ? 1 : 0) + count_of_type<T, Ts...>::value> {};

// Trait checks across all alternatives.
template <bool... Bs>
struct all_of;

template <>
struct all_of<> : std::true_type {};

template <bool B, bool... Bs>
struct all_of<B, Bs...> : std::integral_constant<bool, B && all_of<Bs...>::value> {};

template <typename... Ts>
using all_nothrow_default_constructible = all_of<std::is_nothrow_default_constructible<Ts>::value...>;

template <typename... Ts>
using all_nothrow_move_constructible = all_of<std::is_nothrow_move_constructible<Ts>::value...>;

template <typename... Ts>
using all_nothrow_copy_constructible = all_of<std::is_nothrow_copy_constructible<Ts>::value...>;

template <typename... Ts>
using all_nothrow_move_assignable =
    all_of<(std::is_nothrow_move_constructible<Ts>::value && std::is_nothrow_move_assignable<Ts>::value)...>;

template <typename... Ts>
using all_nothrow_copy_assignable =
    all_of<(std::is_nothrow_copy_constructible<Ts>::value && std::is_nothrow_copy_assignable<Ts>::value)...>;

template <typename... Ts>
using all_nothrow_destructible = all_of<std::is_nothrow_destructible<Ts>::value...>;

// ---------- Storage helpers ----------
// Replacement for std::aligned_storage (deprecated in C++23).
// alignas(max alignof) + array of max sizeof unsigned char bytes.
// Accessors apply std::launder for strict-aliasing safety.
template <typename... Ts>
struct alternative_storage {
  alignas(max_alignof<Ts...>::value) unsigned char bytes[max_sizeof<Ts...>::value];
};

template <typename T>
void destroy_value(void* storage) noexcept {
  std::launder(static_cast<T*>(storage))->~T();
}

template <typename T>
void copy_value(void* destination,
                const void* source) noexcept(std::is_nothrow_copy_constructible<T>::value) {
  new (destination) T(*std::launder(static_cast<const T*>(source)));
}

template <typename T>
void move_value(void* destination, void* source) noexcept(std::is_nothrow_move_constructible<T>::value) {
  new (destination) T(static_cast<T&&>(*std::launder(static_cast<T*>(source))));
}

}  // namespace detail

// ---------- variant_size / variant_alternative ----------

/// @brief Provides the number of alternatives of a variant as `value`.
/// @tparam V The variant type (cv-qualified variants are supported).
template <typename V>
struct variant_size;

template <typename... Ts>
struct variant_size<variant<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)> {};

template <typename V>
struct variant_size<const V> : variant_size<V> {};

template <typename V>
struct variant_size<volatile V> : variant_size<V> {};

template <typename V>
struct variant_size<const volatile V> : variant_size<V> {};

/// @brief Convenience variable template for `variant_size<V>::value`.
template <typename V>
inline constexpr std::size_t variant_size_v = variant_size<V>::value;

/// @brief Provides the type of the I-th alternative of a variant as `type`.
/// @tparam I The zero-based alternative index.
/// @tparam V The variant type (cv-qualified variants are supported).
template <std::size_t I, typename V>
struct variant_alternative;

template <std::size_t I, typename... Ts>
struct variant_alternative<I, variant<Ts...>> {
  static_assert(I < sizeof...(Ts), "variant_alternative index out of range");
  using type = typename detail::nth_type<I, Ts...>::type;
};

template <std::size_t I, typename V>
struct variant_alternative<I, const V> {
  using type = typename std::add_const<typename variant_alternative<I, V>::type>::type;
};

template <std::size_t I, typename V>
struct variant_alternative<I, volatile V> {
  using type = typename std::add_volatile<typename variant_alternative<I, V>::type>::type;
};

template <std::size_t I, typename V>
struct variant_alternative<I, const volatile V> {
  using type = typename std::add_cv<typename variant_alternative<I, V>::type>::type;
};

/// @brief Convenience alias for `variant_alternative<I, V>::type`.
template <std::size_t I, typename V>
using variant_alternative_t = typename variant_alternative<I, V>::type;

// ---------- variant ----------

/// @brief A fixed-storage tagged union holding one of `Ts...` (in-place, no heap).
///
/// Stores the active alternative inline in an aligned byte buffer sized/aligned
/// for the largest alternative; it never allocates. Trivially copyable when all
/// alternatives are.
/// @tparam Ts The alternative types (at least one required).
/// @note `get<>()` (both free and by-index) ASSERTS (aborts by default) when the
/// requested alternative is not active; it does NOT throw std::bad_variant_access.
/// `get_if<>()` returns nullptr instead of asserting.
template <typename... Ts>
class variant {
 public:
  static_assert(sizeof...(Ts) > 0, "variant requires at least one alternative");

  using first_type = typename detail::nth_type<0, Ts...>::type;
  static constexpr std::size_t alternative_count = sizeof...(Ts);

  /// @brief Default-constructs, activating the first alternative.
  // Default constructor: requires first alternative to be default-constructible.
  template <typename First = first_type,
            typename = typename std::enable_if<std::is_default_constructible<First>::value>::type>
  constexpr variant() noexcept(std::is_nothrow_default_constructible<First>::value)
      : storage_{}, index_(variant_npos) {
    new (raw_addr()) first_type();
    index_ = 0;
  }

  /// @brief Constructs by selecting the unique alternative matching `value`.
  /// @param value The value forwarded into the matching alternative.
  // Converting constructor.
  template <typename T,
            typename Decayed = typename std::decay<T>::type,
            typename = typename std::enable_if<!std::is_same<Decayed, variant>::value>::type,
            typename =
                typename std::enable_if<(detail::index_of_type<Decayed, Ts...>::value != variant_npos)>::type,
            typename = typename std::enable_if<(detail::count_of_type<Decayed, Ts...>::value == 1)>::type>
  variant(T&& value) noexcept(std::is_nothrow_constructible<Decayed, T&&>::value)
      : storage_{}, index_(variant_npos) {
    construct<Decayed>(std::forward<T>(value));
  }

  /// @brief Constructs the alternative of type `T` in place.
  /// @tparam T The (unique) alternative type to activate.
  /// @tparam Args Constructor argument types forwarded to `T`.
  /// @param args Arguments forwarded to `T`'s constructor.
  // in_place_type constructor.
  template <
      typename T,
      typename... Args,
      typename = typename std::enable_if<(detail::index_of_type<T, Ts...>::value != variant_npos)>::type,
      typename = typename std::enable_if<(detail::count_of_type<T, Ts...>::value == 1)>::type,
      typename = typename std::enable_if<std::is_constructible<T, Args&&...>::value>::type>
  explicit variant(in_place_type_t<T>,
                   Args&&... args) noexcept(std::is_nothrow_constructible<T, Args&&...>::value)
      : storage_{}, index_(variant_npos) {
    construct<T>(std::forward<Args>(args)...);
  }

  /// @brief Constructs the alternative at index `I` in place.
  /// @tparam I The zero-based alternative index to activate.
  /// @tparam Args Constructor argument types forwarded to the alternative.
  /// @param args Arguments forwarded to the alternative's constructor.
  // in_place_index constructor.
  template <std::size_t I,
            typename... Args,
            typename = typename std::enable_if<(I < sizeof...(Ts))>::type,
            typename = typename std::enable_if<
                std::is_constructible<typename detail::nth_type<I, Ts...>::type, Args&&...>::value>::type>
  explicit variant(in_place_index_t<I>, Args&&... args) noexcept(
      std::is_nothrow_constructible<typename detail::nth_type<I, Ts...>::type, Args&&...>::value)
      : storage_{}, index_(variant_npos) {
    using target_type = typename detail::nth_type<I, Ts...>::type;
    new (raw_addr()) target_type(std::forward<Args>(args)...);
    index_ = I;
  }

  /// @brief Copy constructor; copies the active alternative.
  variant(const variant& other) noexcept(detail::all_nothrow_copy_constructible<Ts...>::value)
      : storage_{}, index_(variant_npos) {
    if (other.index_ != variant_npos) {
      copy_ops()[other.index_](raw_addr(), other.raw_addr());
      index_ = other.index_;
    }
  }

  /// @brief Move constructor; moves the active alternative.
  variant(variant&& other) noexcept(detail::all_nothrow_move_constructible<Ts...>::value)
      : storage_{}, index_(variant_npos) {
    if (other.index_ != variant_npos) {
      move_ops()[other.index_](raw_addr(), other.raw_addr());
      index_ = other.index_;
    }
  }

  /// @brief Destroys the active alternative.
  ~variant() {
    if (index_ != variant_npos) {
      destroy_ops()[index_](raw_addr());
    }
  }

  /// @brief Copy-assigns the active alternative from another variant.
  variant& operator=(const variant& other) noexcept(detail::all_nothrow_copy_assignable<Ts...>::value) {
    if (this == &other) {
      return *this;
    }
    if (other.index_ == variant_npos) {
      reset();
      return *this;
    }
    assign_from(other);
    return *this;
  }

  /// @brief Move-assigns the active alternative from another variant.
  variant& operator=(variant&& other) noexcept(detail::all_nothrow_move_assignable<Ts...>::value) {
    if (this == &other) {
      return *this;
    }
    if (other.index_ == variant_npos) {
      reset();
      return *this;
    }
    assign_from(static_cast<variant&&>(other));
    return *this;
  }

  /// @brief Assigns `value`, activating its unique matching alternative.
  /// @param value The value forwarded into the matching alternative.
  template <typename T,
            typename Decayed = typename std::decay<T>::type,
            typename = typename std::enable_if<!std::is_same<Decayed, variant>::value>::type,
            typename =
                typename std::enable_if<(detail::index_of_type<Decayed, Ts...>::value != variant_npos)>::type,
            typename = typename std::enable_if<(detail::count_of_type<Decayed, Ts...>::value == 1)>::type>
  variant& operator=(T&& value) noexcept(std::is_nothrow_constructible<Decayed, T&&>::value &&
                                         std::is_nothrow_assignable<Decayed&, T&&>::value) {
    emplace<Decayed>(std::forward<T>(value));
    return *this;
  }

  /// @brief Returns the zero-based index of the active alternative.
  /// @return The active index, or `variant_npos` if valueless.
  METL_NODISCARD constexpr std::size_t index() const noexcept { return index_; }

  /// @brief Returns true if the variant holds no alternative (valueless).
  METL_NODISCARD constexpr bool valueless_by_exception() const noexcept { return index_ == variant_npos; }

  /// @brief Returns true if alternative `T` is currently active.
  /// @tparam T The alternative type to test for.
  template <typename T>
  METL_NODISCARD constexpr bool holds_alternative() const noexcept {
    return index_ != variant_npos && index_ == detail::index_of_type<T, Ts...>::value;
  }

  /// @brief Destroys the active alternative and constructs `T` in place.
  /// @tparam T The (unique) alternative type to activate.
  /// @tparam Args Constructor argument types forwarded to `T`.
  /// @param args Arguments forwarded to `T`'s constructor.
  /// @return Reference to the newly constructed alternative.
  template <typename T, typename... Args>
  T& emplace(Args&&... args) noexcept(std::is_nothrow_constructible<T, Args&&...>::value &&
                                      detail::all_nothrow_destructible<Ts...>::value) {
    constexpr std::size_t target_index = detail::index_of_type<T, Ts...>::value;
    static_assert(target_index != variant_npos, "type is not an alternative of this variant");
    static_assert(detail::count_of_type<T, Ts...>::value == 1, "emplace<T> requires unique alternative type");
    static_assert(std::is_constructible<T, Args&&...>::value,
                  "T must be constructible from the given arguments");

    reset();
    return construct<T>(std::forward<Args>(args)...);
  }

  /// @brief Destroys the active alternative and constructs alternative `I`.
  /// @tparam I The zero-based alternative index to activate.
  /// @tparam Args Constructor argument types forwarded to the alternative.
  /// @param args Arguments forwarded to the alternative's constructor.
  /// @return Reference to the newly constructed alternative.
  template <std::size_t I, typename... Args>
  variant_alternative_t<I, variant>& emplace(Args&&... args) noexcept(
      std::is_nothrow_constructible<typename detail::nth_type<I, Ts...>::type, Args&&...>::value &&
      detail::all_nothrow_destructible<Ts...>::value) {
    static_assert(I < sizeof...(Ts), "emplace<I> index out of range");
    using target_type = typename detail::nth_type<I, Ts...>::type;
    static_assert(std::is_constructible<target_type, Args&&...>::value,
                  "alternative must be constructible from the given arguments");

    reset();
    new (raw_addr()) target_type(std::forward<Args>(args)...);
    index_ = I;
    return *std::launder(static_cast<target_type*>(raw_addr()));
  }

  // Internal raw storage access (for free function helpers).
  // storage_ptr() returns a laundered pointer to the active alternative storage;
  // callers must know the type (used by get_if which already checks index).
  void* storage_ptr() noexcept { return raw_addr(); }
  const void* storage_ptr() const noexcept { return raw_addr(); }

 private:
  using storage_type = detail::alternative_storage<Ts...>;
  using destroy_op = void (*)(void*);
  using copy_op = void (*)(void*, const void*);
  using move_op = void (*)(void*, void*);

  static const destroy_op* destroy_ops() noexcept {
    static constexpr destroy_op ops[] = {&detail::destroy_value<Ts>...};
    return ops;
  }

  static const copy_op* copy_ops() noexcept {
    static constexpr copy_op ops[] = {&detail::copy_value<Ts>...};
    return ops;
  }

  static const move_op* move_ops() noexcept {
    static constexpr move_op ops[] = {&detail::move_value<Ts>...};
    return ops;
  }

  void* raw_addr() noexcept { return static_cast<void*>(&storage_.bytes[0]); }
  const void* raw_addr() const noexcept { return static_cast<const void*>(&storage_.bytes[0]); }

  void reset() noexcept(detail::all_nothrow_destructible<Ts...>::value) {
    if (index_ != variant_npos) {
      destroy_ops()[index_](raw_addr());
      index_ = variant_npos;
    }
  }

  template <typename T, typename... Args>
  T& construct(Args&&... args) noexcept(std::is_nothrow_constructible<T, Args&&...>::value) {
    new (raw_addr()) T(std::forward<Args>(args)...);
    index_ = detail::index_of_type<T, Ts...>::value;
    return *std::launder(static_cast<T*>(raw_addr()));
  }

  // Cross-alternative assignment: build a backup of the current state first.
  // If the new construction is nothrow, we destroy current then construct directly.
  // Otherwise, attempt to move-construct via a temporary variant first so a
  // construction-time panic leaves the destination untouched.
  // In this no-exception environment, METL_PANIC aborts; valueless_by_exception()
  // remains a valid (but unreachable in normal flow) observable state.
  void assign_from(const variant& other) {
    if (index_ == other.index_) {
      // Same alternative: destroy + copy-construct in place. Mark the variant
      // valueless across the (possibly throwing) copy-construct so that, if it
      // throws, we never leave a destroyed member paired with a stale
      // discriminant (which would double-destroy). On success the discriminant
      // is restored.
      const std::size_t active = index_;
      destroy_ops()[active](raw_addr());
      index_ = variant_npos;
      copy_ops()[active](raw_addr(), other.raw_addr());
      index_ = active;
      return;
    }
    // Different alternative: copy-construct backup first to minimize valueless window.
    variant backup(other);
    reset();
    move_ops()[backup.index_](raw_addr(), backup.raw_addr());
    index_ = backup.index_;
  }

  void assign_from(variant&& other) {
    if (index_ == other.index_) {
      // Same alternative: destroy + move-construct in place, valueless across
      // the (possibly throwing) move-construct — see assign_from(const&).
      const std::size_t active = index_;
      destroy_ops()[active](raw_addr());
      index_ = variant_npos;
      move_ops()[active](raw_addr(), other.raw_addr());
      index_ = active;
      return;
    }
    // Different alternative: move-construct backup first, then assign.
    variant backup(static_cast<variant&&>(other));
    reset();
    move_ops()[backup.index_](raw_addr(), backup.raw_addr());
    index_ = backup.index_;
  }

  // NOTE: alternatives live in laundered aligned storage, which is not
  // constant-evaluable, so the constexpr labels here are effective only outside
  // constant evaluation. Genuine constexpr (cf. metl::optional via
  // metl/detail/construct.hpp) would require a recursive union rewrite;
  // deferred (see docs/AUDIT.md Section A).
  storage_type storage_;
  std::size_t index_;
};

// ---------- holds_alternative ----------

/// @brief Returns true if alternative `T` is active in `value`.
/// @tparam T The (unique) alternative type to test for.
/// @param value The variant to inspect.
template <typename T, typename... Ts>
METL_NODISCARD constexpr bool holds_alternative(const variant<Ts...>& value) noexcept {
  static_assert(detail::count_of_type<T, Ts...>::value == 1,
                "holds_alternative<T> requires unique alternative type");
  return value.template holds_alternative<T>();
}

// ---------- get_if ----------

/// @brief Returns a pointer to the active alternative `T`, or nullptr.
/// @tparam T The (unique) alternative type to retrieve.
/// @param value Pointer to the variant (may be null).
/// @return Pointer to the alternative if active, otherwise nullptr. Never asserts.
template <typename T, typename... Ts>
METL_NODISCARD constexpr T* get_if(variant<Ts...>* value) noexcept {
  static_assert(detail::count_of_type<T, Ts...>::value == 1, "get_if<T> requires unique alternative type");
  if (value == nullptr || !value->template holds_alternative<T>()) {
    return nullptr;
  }
  return std::launder(static_cast<T*>(value->storage_ptr()));
}

template <typename T, typename... Ts>
METL_NODISCARD constexpr const T* get_if(const variant<Ts...>* value) noexcept {
  static_assert(detail::count_of_type<T, Ts...>::value == 1, "get_if<T> requires unique alternative type");
  if (value == nullptr || !value->template holds_alternative<T>()) {
    return nullptr;
  }
  return std::launder(static_cast<const T*>(value->storage_ptr()));
}

/// @brief Returns a pointer to the active alternative at index `I`, or nullptr.
/// @tparam I The zero-based alternative index to retrieve.
/// @param value Pointer to the variant (may be null).
/// @return Pointer to the alternative if active, otherwise nullptr. Never asserts.
template <std::size_t I, typename... Ts>
METL_NODISCARD constexpr variant_alternative_t<I, variant<Ts...>>* get_if(variant<Ts...>* value) noexcept {
  static_assert(I < sizeof...(Ts), "get_if<I> index out of range");
  using alternative_type = variant_alternative_t<I, variant<Ts...>>;
  if (value == nullptr || value->index() != I) {
    return nullptr;
  }
  return std::launder(static_cast<alternative_type*>(value->storage_ptr()));
}

template <std::size_t I, typename... Ts>
METL_NODISCARD constexpr const variant_alternative_t<I, variant<Ts...>>* get_if(
    const variant<Ts...>* value) noexcept {
  static_assert(I < sizeof...(Ts), "get_if<I> index out of range");
  using alternative_type = variant_alternative_t<I, variant<Ts...>>;
  if (value == nullptr || value->index() != I) {
    return nullptr;
  }
  return std::launder(static_cast<const alternative_type*>(value->storage_ptr()));
}

// ---------- get ----------

/// @brief Returns a reference to the active alternative `T`.
/// @tparam T The (unique) alternative type to retrieve.
/// @param value The variant to access.
/// @pre `holds_alternative<T>(value)`
/// @warning Asserts (aborts by default) if `T` is not active; it does NOT throw
/// std::bad_variant_access. Use `get_if<T>` for a checked pointer alternative.
template <typename T, typename... Ts>
METL_NODISCARD constexpr T& get(variant<Ts...>& value) {
  T* pointer = get_if<T>(&value);
  METL_ASSERT(pointer != nullptr);
  return *pointer;
}

template <typename T, typename... Ts>
METL_NODISCARD constexpr const T& get(const variant<Ts...>& value) {
  const T* pointer = get_if<T>(&value);
  METL_ASSERT(pointer != nullptr);
  return *pointer;
}

template <typename T, typename... Ts>
METL_NODISCARD constexpr T&& get(variant<Ts...>&& value) {
  T* pointer = get_if<T>(&value);
  METL_ASSERT(pointer != nullptr);
  return static_cast<T&&>(*pointer);
}

template <typename T, typename... Ts>
METL_NODISCARD constexpr const T&& get(const variant<Ts...>&& value) {
  const T* pointer = get_if<T>(&value);
  METL_ASSERT(pointer != nullptr);
  return static_cast<const T&&>(*pointer);
}

/// @brief Returns a reference to the active alternative at index `I`.
/// @tparam I The zero-based alternative index to retrieve.
/// @param value The variant to access.
/// @pre `value.index() == I`
/// @warning Asserts (aborts by default) if index `I` is not active; it does NOT
/// throw. Use `get_if<I>` for a checked pointer alternative.
template <std::size_t I, typename... Ts>
METL_NODISCARD constexpr variant_alternative_t<I, variant<Ts...>>& get(variant<Ts...>& value) {
  using alternative_type = variant_alternative_t<I, variant<Ts...>>;
  alternative_type* pointer = get_if<I>(&value);
  METL_ASSERT(pointer != nullptr);
  return *pointer;
}

template <std::size_t I, typename... Ts>
METL_NODISCARD constexpr const variant_alternative_t<I, variant<Ts...>>& get(const variant<Ts...>& value) {
  using alternative_type = variant_alternative_t<I, variant<Ts...>>;
  const alternative_type* pointer = get_if<I>(&value);
  METL_ASSERT(pointer != nullptr);
  return *pointer;
}

template <std::size_t I, typename... Ts>
METL_NODISCARD constexpr variant_alternative_t<I, variant<Ts...>>&& get(variant<Ts...>&& value) {
  using alternative_type = variant_alternative_t<I, variant<Ts...>>;
  alternative_type* pointer = get_if<I>(&value);
  METL_ASSERT(pointer != nullptr);
  return static_cast<alternative_type&&>(*pointer);
}

template <std::size_t I, typename... Ts>
METL_NODISCARD constexpr const variant_alternative_t<I, variant<Ts...>>&& get(const variant<Ts...>&& value) {
  using alternative_type = variant_alternative_t<I, variant<Ts...>>;
  const alternative_type* pointer = get_if<I>(&value);
  METL_ASSERT(pointer != nullptr);
  return static_cast<const alternative_type&&>(*pointer);
}

// ---------- visit ----------

namespace detail {

template <typename Visitor, typename... Ts>
using visit_result_lvalue_t =
    decltype(std::declval<Visitor>()(std::declval<variant_alternative_t<0, variant<Ts...>>&>()));

template <typename Visitor, typename... Ts>
using visit_result_const_lvalue_t =
    decltype(std::declval<Visitor>()(std::declval<const variant_alternative_t<0, variant<Ts...>>&>()));

template <typename Visitor, typename... Ts>
using visit_result_rvalue_t =
    decltype(std::declval<Visitor>()(std::declval<variant_alternative_t<0, variant<Ts...>>&&>()));

template <typename Visitor, typename... Ts>
using visit_result_const_rvalue_t =
    decltype(std::declval<Visitor>()(std::declval<const variant_alternative_t<0, variant<Ts...>>&&>()));

template <typename Result, std::size_t Index, typename Visitor, typename... Ts>
constexpr Result visit_impl(Visitor&& visitor, variant<Ts...>& value) {
  if constexpr (Index >= sizeof...(Ts)) {
    METL_PANIC("invalid variant index");
    METL_UNREACHABLE();
  } else {
    if (value.index() == Index) {
      return std::forward<Visitor>(visitor)(get<Index>(value));
    }
    return visit_impl<Result, Index + 1>(std::forward<Visitor>(visitor), value);
  }
}

template <typename Result, std::size_t Index, typename Visitor, typename... Ts>
constexpr Result visit_impl(Visitor&& visitor, const variant<Ts...>& value) {
  if constexpr (Index >= sizeof...(Ts)) {
    METL_PANIC("invalid variant index");
    METL_UNREACHABLE();
  } else {
    if (value.index() == Index) {
      return std::forward<Visitor>(visitor)(get<Index>(value));
    }
    return visit_impl<Result, Index + 1>(std::forward<Visitor>(visitor), value);
  }
}

template <typename Result, std::size_t Index, typename Visitor, typename... Ts>
constexpr Result visit_impl(Visitor&& visitor, variant<Ts...>&& value) {
  if constexpr (Index >= sizeof...(Ts)) {
    METL_PANIC("invalid variant index");
    METL_UNREACHABLE();
  } else {
    if (value.index() == Index) {
      return std::forward<Visitor>(visitor)(get<Index>(static_cast<variant<Ts...>&&>(value)));
    }
    return visit_impl<Result, Index + 1>(std::forward<Visitor>(visitor),
                                         static_cast<variant<Ts...>&&>(value));
  }
}

template <typename Result, std::size_t Index, typename Visitor, typename... Ts>
constexpr Result visit_impl(Visitor&& visitor, const variant<Ts...>&& value) {
  if constexpr (Index >= sizeof...(Ts)) {
    METL_PANIC("invalid variant index");
    METL_UNREACHABLE();
  } else {
    if (value.index() == Index) {
      return std::forward<Visitor>(visitor)(get<Index>(static_cast<const variant<Ts...>&&>(value)));
    }
    return visit_impl<Result, Index + 1>(std::forward<Visitor>(visitor),
                                         static_cast<const variant<Ts...>&&>(value));
  }
}

}  // namespace detail

/// @brief Invokes `visitor` with the active alternative of `value`.
/// @param visitor Callable accepting any alternative; its result is returned.
/// @param value The variant whose active alternative is passed to `visitor`.
/// @pre `!value.valueless_by_exception()`
/// @warning Asserts (aborts by default) if `value` is valueless; it does not throw.
template <typename Visitor, typename... Ts>
constexpr decltype(auto) visit(Visitor&& visitor, variant<Ts...>& value) {
  METL_ASSERT(!value.valueless_by_exception());
  using result_type = detail::visit_result_lvalue_t<Visitor&&, Ts...>;
  return detail::visit_impl<result_type, 0>(std::forward<Visitor>(visitor), value);
}

template <typename Visitor, typename... Ts>
constexpr decltype(auto) visit(Visitor&& visitor, const variant<Ts...>& value) {
  METL_ASSERT(!value.valueless_by_exception());
  using result_type = detail::visit_result_const_lvalue_t<Visitor&&, Ts...>;
  return detail::visit_impl<result_type, 0>(std::forward<Visitor>(visitor), value);
}

template <typename Visitor, typename... Ts>
constexpr decltype(auto) visit(Visitor&& visitor, variant<Ts...>&& value) {
  METL_ASSERT(!value.valueless_by_exception());
  using result_type = detail::visit_result_rvalue_t<Visitor&&, Ts...>;
  return detail::visit_impl<result_type, 0>(std::forward<Visitor>(visitor),
                                            static_cast<variant<Ts...>&&>(value));
}

template <typename Visitor, typename... Ts>
constexpr decltype(auto) visit(Visitor&& visitor, const variant<Ts...>&& value) {
  METL_ASSERT(!value.valueless_by_exception());
  using result_type = detail::visit_result_const_rvalue_t<Visitor&&, Ts...>;
  return detail::visit_impl<result_type, 0>(std::forward<Visitor>(visitor),
                                            static_cast<const variant<Ts...>&&>(value));
}

// ---------- Comparison operators ----------

namespace detail {

// Compare the active alternatives of two same-index variants *by index* rather
// than by type. Comparing by type (get<T>) is ill-formed for a variant with
// duplicate alternative types, since get<T> requires a unique alternative.
// Precondition: lhs.index() == rhs.index() and neither is valueless.
template <typename Op, std::size_t I, typename... Ts>
constexpr bool compare_alternative(const variant<Ts...>& lhs, const variant<Ts...>& rhs) {
  if constexpr (I >= sizeof...(Ts)) {
    (void)lhs;
    (void)rhs;
    return false;  // Unreachable: index() is always a valid alternative here.
  } else {
    if (lhs.index() == I) {
      return Op{}(*::metl::get_if<I>(&lhs), *::metl::get_if<I>(&rhs));
    }
    return compare_alternative<Op, I + 1>(lhs, rhs);
  }
}

struct cmp_eq {
  template <typename A, typename B>
  bool operator()(const A& a, const B& b) const {
    return a == b;
  }
};
struct cmp_ne {
  template <typename A, typename B>
  bool operator()(const A& a, const B& b) const {
    return a != b;
  }
};
struct cmp_lt {
  template <typename A, typename B>
  bool operator()(const A& a, const B& b) const {
    return a < b;
  }
};
struct cmp_le {
  template <typename A, typename B>
  bool operator()(const A& a, const B& b) const {
    return a <= b;
  }
};
struct cmp_gt {
  template <typename A, typename B>
  bool operator()(const A& a, const B& b) const {
    return a > b;
  }
};
struct cmp_ge {
  template <typename A, typename B>
  bool operator()(const A& a, const B& b) const {
    return a >= b;
  }
};

}  // namespace detail

/// @brief Compares two variants: equal indices and equal active alternatives.
template <typename... Ts>
constexpr bool operator==(const variant<Ts...>& lhs, const variant<Ts...>& rhs) {
  if (lhs.index() != rhs.index()) {
    return false;
  }
  if (lhs.valueless_by_exception()) {
    return true;
  }
  return detail::compare_alternative<detail::cmp_eq, 0>(lhs, rhs);
}

template <typename... Ts>
constexpr bool operator!=(const variant<Ts...>& lhs, const variant<Ts...>& rhs) {
  if (lhs.index() != rhs.index()) {
    return true;
  }
  if (lhs.valueless_by_exception()) {
    return false;
  }
  return detail::compare_alternative<detail::cmp_ne, 0>(lhs, rhs);
}

/// @brief Orders two variants by index, then by active alternative value.
/// @note A valueless variant orders before any engaged variant.
template <typename... Ts>
constexpr bool operator<(const variant<Ts...>& lhs, const variant<Ts...>& rhs) {
  if (rhs.valueless_by_exception()) {
    return false;
  }
  if (lhs.valueless_by_exception()) {
    return true;
  }
  if (lhs.index() != rhs.index()) {
    return lhs.index() < rhs.index();
  }
  return detail::compare_alternative<detail::cmp_lt, 0>(lhs, rhs);
}

template <typename... Ts>
constexpr bool operator>(const variant<Ts...>& lhs, const variant<Ts...>& rhs) {
  if (lhs.valueless_by_exception()) {
    return false;
  }
  if (rhs.valueless_by_exception()) {
    return true;
  }
  if (lhs.index() != rhs.index()) {
    return lhs.index() > rhs.index();
  }
  return detail::compare_alternative<detail::cmp_gt, 0>(lhs, rhs);
}

template <typename... Ts>
constexpr bool operator<=(const variant<Ts...>& lhs, const variant<Ts...>& rhs) {
  if (lhs.valueless_by_exception()) {
    return true;
  }
  if (rhs.valueless_by_exception()) {
    return false;
  }
  if (lhs.index() != rhs.index()) {
    return lhs.index() < rhs.index();
  }
  return detail::compare_alternative<detail::cmp_le, 0>(lhs, rhs);
}

template <typename... Ts>
constexpr bool operator>=(const variant<Ts...>& lhs, const variant<Ts...>& rhs) {
  if (rhs.valueless_by_exception()) {
    return true;
  }
  if (lhs.valueless_by_exception()) {
    return false;
  }
  if (lhs.index() != rhs.index()) {
    return lhs.index() > rhs.index();
  }
  return detail::compare_alternative<detail::cmp_ge, 0>(lhs, rhs);
}

}  // namespace metl
