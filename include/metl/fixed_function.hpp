#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

// fixed_function<Sig, Capacity>
//   - SBO-based type-erased callable, copyable+movable.
//   - Specialized for both `R(Args...)` and `R(Args...) noexcept` signatures.
//
// fixed_any_invocable<Sig, Capacity>
//   - Move-only counterpart that accepts non-copyable callables.
//   - Specialized for both `R(Args...)` and `R(Args...) noexcept`.
//
// Notes:
//   - No exceptions, no heap, no RTTI.
//   - target<T>() / target_type() are intentionally omitted because they
//     require RTTI to be useful in the general case.
//   - Storage is aligned to alignof(std::max_align_t); callables with stricter
//     alignment requirements fail a static_assert at assign time.

namespace metl {

namespace detail {

// -------- common operations table & helpers ---------------------------------

template <bool IsNoexcept, typename R, typename... Args>
struct invoke_traits;

template <typename R, typename... Args>
struct invoke_traits<false, R, Args...> {
  using invoke_fn = R (*)(void*, Args&&...);
};

template <typename R, typename... Args>
struct invoke_traits<true, R, Args...> {
  using invoke_fn = R (*)(void*, Args&&...) noexcept;
};

// Operations table for copyable callables.
template <bool IsNoexcept, typename R, typename... Args>
struct copyable_ops {
  typename invoke_traits<IsNoexcept, R, Args...>::invoke_fn invoke;
  void (*copy)(void*, const void*);
  void (*move)(void*, void*) noexcept;
  void (*destroy)(void*) noexcept;
};

// Operations table for move-only callables (any_invocable).
template <bool IsNoexcept, typename R, typename... Args>
struct moveonly_ops {
  typename invoke_traits<IsNoexcept, R, Args...>::invoke_fn invoke;
  void (*move)(void*, void*) noexcept;
  void (*destroy)(void*) noexcept;
};

template <typename F, typename R, typename... Args>
R invoke_object(void* storage, Args&&... args) {
  auto* function = static_cast<F*>(storage);
  return (*function)(std::forward<Args>(args)...);
}

template <typename F, typename R, typename... Args>
R invoke_object_nx(void* storage, Args&&... args) noexcept {
  static_assert(noexcept((*static_cast<F*>(storage))(std::forward<Args>(args)...)),
                "callable must be noexcept to satisfy noexcept signature");
  auto* function = static_cast<F*>(storage);
  return (*function)(std::forward<Args>(args)...);
}

template <typename F>
void copy_object(void* destination, const void* source) {
  new (destination) F(*static_cast<const F*>(source));
}

template <typename F>
void move_object(void* destination, void* source) noexcept {
  new (destination) F(static_cast<F&&>(*static_cast<F*>(source)));
}

template <typename F>
void destroy_object(void* storage) noexcept {
  static_cast<F*>(storage)->~F();
}

// Per-callable ops tables, specialized on noexcept-ness because a function
// pointer of `R(...) noexcept` is not convertible to `R(...)` in initializer
// lists.
template <typename F, bool IsNoexcept, typename R, typename... Args>
struct copyable_ops_for_t;

template <typename F, typename R, typename... Args>
struct copyable_ops_for_t<F, true, R, Args...> {
  static constexpr copyable_ops<true, R, Args...> value = {
      &invoke_object_nx<F, R, Args...>,
      &copy_object<F>,
      &move_object<F>,
      &destroy_object<F>,
  };
};

template <typename F, typename R, typename... Args>
struct copyable_ops_for_t<F, false, R, Args...> {
  static constexpr copyable_ops<false, R, Args...> value = {
      &invoke_object<F, R, Args...>,
      &copy_object<F>,
      &move_object<F>,
      &destroy_object<F>,
  };
};

template <typename F, bool IsNoexcept, typename R, typename... Args>
struct moveonly_ops_for_t;

template <typename F, typename R, typename... Args>
struct moveonly_ops_for_t<F, true, R, Args...> {
  static constexpr moveonly_ops<true, R, Args...> value = {
      &invoke_object_nx<F, R, Args...>,
      &move_object<F>,
      &destroy_object<F>,
  };
};

template <typename F, typename R, typename... Args>
struct moveonly_ops_for_t<F, false, R, Args...> {
  static constexpr moveonly_ops<false, R, Args...> value = {
      &invoke_object<F, R, Args...>,
      &move_object<F>,
      &destroy_object<F>,
  };
};

// -------- fixed_function implementation core --------------------------------

template <bool IsNoexcept, std::size_t Capacity, typename R, typename... Args>
class fixed_function_impl {
 public:
  using ops_type = copyable_ops<IsNoexcept, R, Args...>;

  constexpr fixed_function_impl() noexcept : ops_(nullptr) {}
  constexpr fixed_function_impl(std::nullptr_t) noexcept : ops_(nullptr) {}

  fixed_function_impl(const fixed_function_impl& other) : ops_(nullptr) {
    if (other.has_value()) {
      other.ops_->copy(storage_ptr(), other.storage_ptr_const());
      ops_ = other.ops_;
    }
  }

  fixed_function_impl(fixed_function_impl&& other) noexcept : ops_(nullptr) {
    if (other.has_value()) {
      other.ops_->move(storage_ptr(), other.storage_ptr());
      ops_ = other.ops_;
      other.reset();
    }
  }

  ~fixed_function_impl() { reset(); }

  fixed_function_impl& operator=(std::nullptr_t) noexcept {
    reset();
    return *this;
  }

  fixed_function_impl& operator=(const fixed_function_impl& other) {
    if (this == &other) {
      return *this;
    }
    if (!other.has_value()) {
      reset();
      return *this;
    }
    reset();
    other.ops_->copy(storage_ptr(), other.storage_ptr_const());
    ops_ = other.ops_;
    return *this;
  }

  fixed_function_impl& operator=(fixed_function_impl&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (!other.has_value()) {
      reset();
      return *this;
    }
    reset();
    other.ops_->move(storage_ptr(), other.storage_ptr());
    ops_ = other.ops_;
    other.reset();
    return *this;
  }

  METL_NODISCARD constexpr explicit operator bool() const noexcept { return ops_ != nullptr; }
  METL_NODISCARD constexpr bool has_value() const noexcept { return ops_ != nullptr; }

  void reset() noexcept {
    if (ops_ == nullptr) {
      return;
    }
    ops_->destroy(storage_ptr());
    ops_ = nullptr;
  }

  void swap(fixed_function_impl& other) noexcept {
    if (this == &other) {
      return;
    }
    if (has_value() && other.has_value()) {
      // Temporary in local storage; constrained to Capacity.
      alignas(std::max_align_t) unsigned char tmp[Capacity];
      ops_->move(static_cast<void*>(&tmp), storage_ptr());
      const ops_type* tmp_ops = ops_;
      ops_->destroy(storage_ptr());

      other.ops_->move(storage_ptr(), other.storage_ptr());
      ops_ = other.ops_;
      other.ops_->destroy(other.storage_ptr());

      tmp_ops->move(other.storage_ptr(), static_cast<void*>(&tmp));
      other.ops_ = tmp_ops;
      tmp_ops->destroy(static_cast<void*>(&tmp));
    } else if (has_value()) {
      ops_->move(other.storage_ptr(), storage_ptr());
      other.ops_ = ops_;
      ops_->destroy(storage_ptr());
      ops_ = nullptr;
    } else if (other.has_value()) {
      other.ops_->move(storage_ptr(), other.storage_ptr());
      ops_ = other.ops_;
      other.ops_->destroy(other.storage_ptr());
      other.ops_ = nullptr;
    }
  }

 protected:
  template <typename F>
  bool try_assign_callable(F&& function) {
    using decayed_type = typename std::decay<F>::type;

    static_assert(std::is_invocable_r<R, decayed_type&, Args...>::value,
                  "callable signature does not match fixed_function");
    static_assert(std::is_copy_constructible<decayed_type>::value,
                  "fixed_function requires a copy-constructible callable");
    static_assert(!IsNoexcept || std::is_nothrow_invocable_r<R, decayed_type&, Args...>::value,
                  "noexcept fixed_function requires a noexcept-invocable callable");
    static_assert(alignof(decayed_type) <= alignof(std::max_align_t),
                  "callable alignment exceeds fixed_function storage alignment");

    if (sizeof(decayed_type) > Capacity) {
      return false;
    }

    reset();
    new (storage_ptr()) decayed_type(std::forward<F>(function));
    ops_ = &copyable_ops_for_t<decayed_type, IsNoexcept, R, Args...>::value;
    return true;
  }

  void* storage_ptr() noexcept { return static_cast<void*>(&storage_[0]); }
  const void* storage_ptr_const() const noexcept { return static_cast<const void*>(&storage_[0]); }
  void* storage_ptr() const noexcept {
    // Used for invoke(); the called function receives non-const access to the
    // type-erased payload (mirrors std::function, whose operator() is const but
    // may call a mutable target). `storage_` is `mutable`, so obtaining a
    // non-const pointer to it from a const fixed_function is well-defined — even
    // for a const fixed_function, storage_ is not a const subobject.
    return const_cast<void*>(static_cast<const void*>(&storage_[0]));
  }

  alignas(std::max_align_t) mutable unsigned char storage_[Capacity];
  const ops_type* ops_;
};

// -------- fixed_any_invocable implementation core ---------------------------

template <bool IsNoexcept, std::size_t Capacity, typename R, typename... Args>
class fixed_any_invocable_impl {
 public:
  using ops_type = moveonly_ops<IsNoexcept, R, Args...>;

  constexpr fixed_any_invocable_impl() noexcept : ops_(nullptr) {}
  constexpr fixed_any_invocable_impl(std::nullptr_t) noexcept : ops_(nullptr) {}

  fixed_any_invocable_impl(const fixed_any_invocable_impl&) = delete;
  fixed_any_invocable_impl& operator=(const fixed_any_invocable_impl&) = delete;

  fixed_any_invocable_impl(fixed_any_invocable_impl&& other) noexcept : ops_(nullptr) {
    if (other.has_value()) {
      other.ops_->move(storage_ptr(), other.storage_ptr());
      ops_ = other.ops_;
      other.reset();
    }
  }

  ~fixed_any_invocable_impl() { reset(); }

  fixed_any_invocable_impl& operator=(std::nullptr_t) noexcept {
    reset();
    return *this;
  }

  fixed_any_invocable_impl& operator=(fixed_any_invocable_impl&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (!other.has_value()) {
      reset();
      return *this;
    }
    reset();
    other.ops_->move(storage_ptr(), other.storage_ptr());
    ops_ = other.ops_;
    other.reset();
    return *this;
  }

  METL_NODISCARD constexpr explicit operator bool() const noexcept { return ops_ != nullptr; }
  METL_NODISCARD constexpr bool has_value() const noexcept { return ops_ != nullptr; }

  void reset() noexcept {
    if (ops_ == nullptr) {
      return;
    }
    ops_->destroy(storage_ptr());
    ops_ = nullptr;
  }

  void swap(fixed_any_invocable_impl& other) noexcept {
    if (this == &other) {
      return;
    }
    if (has_value() && other.has_value()) {
      alignas(std::max_align_t) unsigned char tmp[Capacity];
      ops_->move(static_cast<void*>(&tmp), storage_ptr());
      const ops_type* tmp_ops = ops_;
      ops_->destroy(storage_ptr());

      other.ops_->move(storage_ptr(), other.storage_ptr());
      ops_ = other.ops_;
      other.ops_->destroy(other.storage_ptr());

      tmp_ops->move(other.storage_ptr(), static_cast<void*>(&tmp));
      other.ops_ = tmp_ops;
      tmp_ops->destroy(static_cast<void*>(&tmp));
    } else if (has_value()) {
      ops_->move(other.storage_ptr(), storage_ptr());
      other.ops_ = ops_;
      ops_->destroy(storage_ptr());
      ops_ = nullptr;
    } else if (other.has_value()) {
      other.ops_->move(storage_ptr(), other.storage_ptr());
      ops_ = other.ops_;
      other.ops_->destroy(other.storage_ptr());
      other.ops_ = nullptr;
    }
  }

 protected:
  template <typename F>
  bool try_assign_callable(F&& function) {
    using decayed_type = typename std::decay<F>::type;

    static_assert(std::is_invocable_r<R, decayed_type&, Args...>::value,
                  "callable signature does not match fixed_any_invocable");
    static_assert(std::is_move_constructible<decayed_type>::value,
                  "fixed_any_invocable requires a move-constructible callable");
    static_assert(!IsNoexcept || std::is_nothrow_invocable_r<R, decayed_type&, Args...>::value,
                  "noexcept fixed_any_invocable requires a noexcept-invocable callable");
    static_assert(alignof(decayed_type) <= alignof(std::max_align_t),
                  "callable alignment exceeds fixed_any_invocable storage alignment");

    if (sizeof(decayed_type) > Capacity) {
      return false;
    }

    reset();
    new (storage_ptr()) decayed_type(std::forward<F>(function));
    ops_ = &moveonly_ops_for_t<decayed_type, IsNoexcept, R, Args...>::value;
    return true;
  }

  void* storage_ptr() noexcept { return static_cast<void*>(&storage_[0]); }
  // `storage_` is `mutable`, so the const overload (used by the const
  // operator()) yields legitimate non-const access without UB — see the
  // fixed_function_impl overload for the full rationale.
  void* storage_ptr() const noexcept { return const_cast<void*>(static_cast<const void*>(&storage_[0])); }

  alignas(std::max_align_t) mutable unsigned char storage_[Capacity];
  const ops_type* ops_;
};

// Out-of-class definitions of the static constexpr members. Required pre-C++17
// for ODR-use through &name::value; redundant but harmless in C++17 where
// such members are implicitly inline.
template <typename F, typename R, typename... Args>
constexpr copyable_ops<true, R, Args...> copyable_ops_for_t<F, true, R, Args...>::value;

template <typename F, typename R, typename... Args>
constexpr copyable_ops<false, R, Args...> copyable_ops_for_t<F, false, R, Args...>::value;

template <typename F, typename R, typename... Args>
constexpr moveonly_ops<true, R, Args...> moveonly_ops_for_t<F, true, R, Args...>::value;

template <typename F, typename R, typename... Args>
constexpr moveonly_ops<false, R, Args...> moveonly_ops_for_t<F, false, R, Args...>::value;

}  // namespace detail

// ============================================================================
// fixed_function
// ============================================================================

template <typename Signature, std::size_t Capacity = 32>
class fixed_function;  // primary template, undefined.

// ---- non-noexcept signature -------------------------------------------------

template <typename R, typename... Args, std::size_t Capacity>
class fixed_function<R(Args...), Capacity> : public detail::fixed_function_impl<false, Capacity, R, Args...> {
  using base = detail::fixed_function_impl<false, Capacity, R, Args...>;

 public:
  using base::operator bool;
  using base::has_value;
  using base::reset;
  using base::swap;

  constexpr fixed_function() noexcept = default;
  constexpr fixed_function(std::nullptr_t) noexcept : base(nullptr) {}

  fixed_function(R (*function)(Args...)) : base() { assign(function); }

  template <typename F,
            typename Decayed = typename std::decay<F>::type,
            typename = typename std::enable_if<!std::is_same<Decayed, fixed_function>::value &&
                                               std::is_copy_constructible<Decayed>::value &&
                                               std::is_invocable_r<R, Decayed&, Args...>::value>::type>
  fixed_function(F&& function) : base() {
    assign(std::forward<F>(function));
  }

  fixed_function(const fixed_function&) = default;
  fixed_function(fixed_function&&) noexcept = default;
  fixed_function& operator=(const fixed_function&) = default;
  fixed_function& operator=(fixed_function&&) noexcept = default;
  ~fixed_function() = default;

  fixed_function& operator=(std::nullptr_t) noexcept {
    this->reset();
    return *this;
  }

  fixed_function& operator=(R (*function)(Args...)) {
    assign(function);
    return *this;
  }

  template <typename F,
            typename Decayed = typename std::decay<F>::type,
            typename = typename std::enable_if<!std::is_same<Decayed, fixed_function>::value &&
                                               std::is_copy_constructible<Decayed>::value &&
                                               std::is_invocable_r<R, Decayed&, Args...>::value>::type>
  fixed_function& operator=(F&& function) {
    assign(std::forward<F>(function));
    return *this;
  }

  R operator()(Args... args) const {
    METL_ASSERT(this->ops_ != nullptr);
    return this->ops_->invoke(this->storage_ptr(), std::forward<Args>(args)...);
  }

  bool try_assign(R (*function)(Args...)) {
    METL_ASSERT(function != nullptr);
    return this->try_assign_callable(function);
  }

  template <typename F>
  bool try_assign(F&& function) {
    return this->try_assign_callable(std::forward<F>(function));
  }

  void assign(R (*function)(Args...)) {
    const bool assigned = try_assign(function);
    METL_ASSERT(assigned);
    (void)assigned;
  }

  template <typename F>
  void assign(F&& function) {
    const bool assigned = try_assign(std::forward<F>(function));
    METL_ASSERT(assigned);
    (void)assigned;
  }
};

// ---- noexcept signature -----------------------------------------------------

template <typename R, typename... Args, std::size_t Capacity>
class fixed_function<R(Args...) noexcept, Capacity>
    : public detail::fixed_function_impl<true, Capacity, R, Args...> {
  using base = detail::fixed_function_impl<true, Capacity, R, Args...>;

 public:
  using base::operator bool;
  using base::has_value;
  using base::reset;
  using base::swap;

  constexpr fixed_function() noexcept = default;
  constexpr fixed_function(std::nullptr_t) noexcept : base(nullptr) {}

  fixed_function(R (*function)(Args...) noexcept) : base() { assign(function); }

  template <typename F,
            typename Decayed = typename std::decay<F>::type,
            typename = typename std::enable_if<
                !std::is_same<Decayed, fixed_function>::value && std::is_copy_constructible<Decayed>::value &&
                std::is_nothrow_invocable_r<R, Decayed&, Args...>::value>::type>
  fixed_function(F&& function) : base() {
    assign(std::forward<F>(function));
  }

  fixed_function(const fixed_function&) = default;
  fixed_function(fixed_function&&) noexcept = default;
  fixed_function& operator=(const fixed_function&) = default;
  fixed_function& operator=(fixed_function&&) noexcept = default;
  ~fixed_function() = default;

  fixed_function& operator=(std::nullptr_t) noexcept {
    this->reset();
    return *this;
  }

  fixed_function& operator=(R (*function)(Args...) noexcept) {
    assign(function);
    return *this;
  }

  template <typename F,
            typename Decayed = typename std::decay<F>::type,
            typename = typename std::enable_if<
                !std::is_same<Decayed, fixed_function>::value && std::is_copy_constructible<Decayed>::value &&
                std::is_nothrow_invocable_r<R, Decayed&, Args...>::value>::type>
  fixed_function& operator=(F&& function) {
    assign(std::forward<F>(function));
    return *this;
  }

  R operator()(Args... args) const noexcept {
    METL_ASSERT(this->ops_ != nullptr);
    return this->ops_->invoke(this->storage_ptr(), std::forward<Args>(args)...);
  }

  bool try_assign(R (*function)(Args...) noexcept) {
    METL_ASSERT(function != nullptr);
    return this->try_assign_callable(function);
  }

  template <typename F>
  bool try_assign(F&& function) {
    return this->try_assign_callable(std::forward<F>(function));
  }

  void assign(R (*function)(Args...) noexcept) {
    const bool assigned = try_assign(function);
    METL_ASSERT(assigned);
    (void)assigned;
  }

  template <typename F>
  void assign(F&& function) {
    const bool assigned = try_assign(std::forward<F>(function));
    METL_ASSERT(assigned);
    (void)assigned;
  }
};

// ---- nullptr comparison / swap (fixed_function) -----------------------------

template <typename Sig, std::size_t Cap>
METL_NODISCARD inline bool operator==(const fixed_function<Sig, Cap>& f, std::nullptr_t) noexcept {
  return !f;
}
template <typename Sig, std::size_t Cap>
METL_NODISCARD inline bool operator==(std::nullptr_t, const fixed_function<Sig, Cap>& f) noexcept {
  return !f;
}
template <typename Sig, std::size_t Cap>
METL_NODISCARD inline bool operator!=(const fixed_function<Sig, Cap>& f, std::nullptr_t) noexcept {
  return static_cast<bool>(f);
}
template <typename Sig, std::size_t Cap>
METL_NODISCARD inline bool operator!=(std::nullptr_t, const fixed_function<Sig, Cap>& f) noexcept {
  return static_cast<bool>(f);
}

template <typename Sig, std::size_t Cap>
inline void swap(fixed_function<Sig, Cap>& lhs, fixed_function<Sig, Cap>& rhs) noexcept {
  lhs.swap(rhs);
}

// ============================================================================
// fixed_any_invocable
// ============================================================================

template <typename Signature, std::size_t Capacity = 32>
class fixed_any_invocable;  // primary template, undefined.

// ---- non-noexcept signature -------------------------------------------------

template <typename R, typename... Args, std::size_t Capacity>
class fixed_any_invocable<R(Args...), Capacity>
    : public detail::fixed_any_invocable_impl<false, Capacity, R, Args...> {
  using base = detail::fixed_any_invocable_impl<false, Capacity, R, Args...>;

 public:
  using base::operator bool;
  using base::has_value;
  using base::reset;
  using base::swap;

  constexpr fixed_any_invocable() noexcept = default;
  constexpr fixed_any_invocable(std::nullptr_t) noexcept : base(nullptr) {}

  fixed_any_invocable(R (*function)(Args...)) : base() { assign(function); }

  template <typename F,
            typename Decayed = typename std::decay<F>::type,
            typename = typename std::enable_if<!std::is_same<Decayed, fixed_any_invocable>::value &&
                                               std::is_move_constructible<Decayed>::value &&
                                               std::is_invocable_r<R, Decayed&, Args...>::value>::type>
  fixed_any_invocable(F&& function) : base() {
    assign(std::forward<F>(function));
  }

  fixed_any_invocable(const fixed_any_invocable&) = delete;
  fixed_any_invocable& operator=(const fixed_any_invocable&) = delete;
  fixed_any_invocable(fixed_any_invocable&&) noexcept = default;
  fixed_any_invocable& operator=(fixed_any_invocable&&) noexcept = default;
  ~fixed_any_invocable() = default;

  fixed_any_invocable& operator=(std::nullptr_t) noexcept {
    this->reset();
    return *this;
  }

  fixed_any_invocable& operator=(R (*function)(Args...)) {
    assign(function);
    return *this;
  }

  template <typename F,
            typename Decayed = typename std::decay<F>::type,
            typename = typename std::enable_if<!std::is_same<Decayed, fixed_any_invocable>::value &&
                                               std::is_move_constructible<Decayed>::value &&
                                               std::is_invocable_r<R, Decayed&, Args...>::value>::type>
  fixed_any_invocable& operator=(F&& function) {
    assign(std::forward<F>(function));
    return *this;
  }

  R operator()(Args... args) const {
    METL_ASSERT(this->ops_ != nullptr);
    return this->ops_->invoke(this->storage_ptr(), std::forward<Args>(args)...);
  }

  bool try_assign(R (*function)(Args...)) {
    METL_ASSERT(function != nullptr);
    return this->try_assign_callable(function);
  }

  template <typename F>
  bool try_assign(F&& function) {
    return this->try_assign_callable(std::forward<F>(function));
  }

  void assign(R (*function)(Args...)) {
    const bool assigned = try_assign(function);
    METL_ASSERT(assigned);
    (void)assigned;
  }

  template <typename F>
  void assign(F&& function) {
    const bool assigned = try_assign(std::forward<F>(function));
    METL_ASSERT(assigned);
    (void)assigned;
  }
};

// ---- noexcept signature -----------------------------------------------------

template <typename R, typename... Args, std::size_t Capacity>
class fixed_any_invocable<R(Args...) noexcept, Capacity>
    : public detail::fixed_any_invocable_impl<true, Capacity, R, Args...> {
  using base = detail::fixed_any_invocable_impl<true, Capacity, R, Args...>;

 public:
  using base::operator bool;
  using base::has_value;
  using base::reset;
  using base::swap;

  constexpr fixed_any_invocable() noexcept = default;
  constexpr fixed_any_invocable(std::nullptr_t) noexcept : base(nullptr) {}

  fixed_any_invocable(R (*function)(Args...) noexcept) : base() { assign(function); }

  template <
      typename F,
      typename Decayed = typename std::decay<F>::type,
      typename = typename std::enable_if<!std::is_same<Decayed, fixed_any_invocable>::value &&
                                         std::is_move_constructible<Decayed>::value &&
                                         std::is_nothrow_invocable_r<R, Decayed&, Args...>::value>::type>
  fixed_any_invocable(F&& function) : base() {
    assign(std::forward<F>(function));
  }

  fixed_any_invocable(const fixed_any_invocable&) = delete;
  fixed_any_invocable& operator=(const fixed_any_invocable&) = delete;
  fixed_any_invocable(fixed_any_invocable&&) noexcept = default;
  fixed_any_invocable& operator=(fixed_any_invocable&&) noexcept = default;
  ~fixed_any_invocable() = default;

  fixed_any_invocable& operator=(std::nullptr_t) noexcept {
    this->reset();
    return *this;
  }

  fixed_any_invocable& operator=(R (*function)(Args...) noexcept) {
    assign(function);
    return *this;
  }

  template <
      typename F,
      typename Decayed = typename std::decay<F>::type,
      typename = typename std::enable_if<!std::is_same<Decayed, fixed_any_invocable>::value &&
                                         std::is_move_constructible<Decayed>::value &&
                                         std::is_nothrow_invocable_r<R, Decayed&, Args...>::value>::type>
  fixed_any_invocable& operator=(F&& function) {
    assign(std::forward<F>(function));
    return *this;
  }

  R operator()(Args... args) const noexcept {
    METL_ASSERT(this->ops_ != nullptr);
    return this->ops_->invoke(this->storage_ptr(), std::forward<Args>(args)...);
  }

  bool try_assign(R (*function)(Args...) noexcept) {
    METL_ASSERT(function != nullptr);
    return this->try_assign_callable(function);
  }

  template <typename F>
  bool try_assign(F&& function) {
    return this->try_assign_callable(std::forward<F>(function));
  }

  void assign(R (*function)(Args...) noexcept) {
    const bool assigned = try_assign(function);
    METL_ASSERT(assigned);
    (void)assigned;
  }

  template <typename F>
  void assign(F&& function) {
    const bool assigned = try_assign(std::forward<F>(function));
    METL_ASSERT(assigned);
    (void)assigned;
  }
};

// ---- nullptr comparison / swap (fixed_any_invocable) ------------------------

template <typename Sig, std::size_t Cap>
METL_NODISCARD inline bool operator==(const fixed_any_invocable<Sig, Cap>& f, std::nullptr_t) noexcept {
  return !f;
}
template <typename Sig, std::size_t Cap>
METL_NODISCARD inline bool operator==(std::nullptr_t, const fixed_any_invocable<Sig, Cap>& f) noexcept {
  return !f;
}
template <typename Sig, std::size_t Cap>
METL_NODISCARD inline bool operator!=(const fixed_any_invocable<Sig, Cap>& f, std::nullptr_t) noexcept {
  return static_cast<bool>(f);
}
template <typename Sig, std::size_t Cap>
METL_NODISCARD inline bool operator!=(std::nullptr_t, const fixed_any_invocable<Sig, Cap>& f) noexcept {
  return static_cast<bool>(f);
}

template <typename Sig, std::size_t Cap>
inline void swap(fixed_any_invocable<Sig, Cap>& lhs, fixed_any_invocable<Sig, Cap>& rhs) noexcept {
  lhs.swap(rhs);
}

}  // namespace metl
