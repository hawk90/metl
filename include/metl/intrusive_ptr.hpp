#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace metl {

/// @brief Tag type selecting the adopt-reference intrusive_ptr constructor.
struct adopt_ref_t {
  explicit constexpr adopt_ref_t(int) noexcept {}
};

/// @brief Tag type selecting the retain-reference intrusive_ptr constructor.
struct retain_ref_t {
  explicit constexpr retain_ref_t(int) noexcept {}
};

/// @brief Tag: take ownership of an existing strong reference (no add-ref).
inline constexpr adopt_ref_t adopt_ref{0};
/// @brief Tag: acquire a new strong reference (add-ref on construction).
inline constexpr retain_ref_t retain_ref{0};

/// @brief Selects a non-atomic or atomic (thread-safe) reference counter.
enum class refcount_kind { non_atomic, atomic };

/// @brief CRTP base adding an embedded reference count and intrusive_ptr hooks.
///
/// Derive as `class T : public intrusive_ref_counter<T>` to make T usable with
/// intrusive_ptr. The friend hooks intrusive_ptr_add_ref / intrusive_ptr_release
/// maintain the count; when it reaches zero only the derived destructor runs —
/// memory deallocation is the user's responsibility (object pool, static
/// storage, etc.). No heap allocation is performed.
/// @tparam Derived The most-derived type (CRTP self-type).
/// @tparam Kind Non-atomic (default) or atomic for thread-safe counting.
/// @warning On release, the object is destroyed through `Derived`; a
///          static_assert requires Derived to be `final` or to have a virtual
///          destructor so the correct most-derived object is destroyed.
template <typename Derived, refcount_kind Kind = refcount_kind::non_atomic>
class intrusive_ref_counter {
 public:
  static constexpr refcount_kind kind = Kind;

  /// @brief Returns the current strong reference count.
  METL_NODISCARD std::size_t use_count() const noexcept {
    if constexpr (Kind == refcount_kind::atomic) {
      return refcount_.load(std::memory_order_acquire);
    } else {
      return refcount_;
    }
  }

 protected:
  constexpr intrusive_ref_counter() noexcept : refcount_(0) {}
  intrusive_ref_counter(const intrusive_ref_counter&) noexcept : refcount_(0) {}
  intrusive_ref_counter& operator=(const intrusive_ref_counter&) noexcept {
    // Reference count is intrinsic to the object identity, not its value.
    // Preserve current refcount on assignment.
    return *this;
  }
  ~intrusive_ref_counter() = default;

 private:
  using counter_t = std::conditional_t<Kind == refcount_kind::atomic, std::atomic<std::size_t>, std::size_t>;
  mutable counter_t refcount_;

  friend void intrusive_ptr_add_ref(const Derived* ptr) noexcept {
    METL_ASSERT(ptr != nullptr);
    const auto* base = static_cast<const intrusive_ref_counter*>(ptr);
    if constexpr (Kind == refcount_kind::atomic) {
      base->refcount_.fetch_add(1, std::memory_order_relaxed);
    } else {
      ++base->refcount_;
    }
  }

  friend void intrusive_ptr_release(const Derived* ptr) noexcept {
    METL_ASSERT(ptr != nullptr);
    const auto* base = static_cast<const intrusive_ref_counter*>(ptr);
    std::size_t prev;
    if constexpr (Kind == refcount_kind::atomic) {
      prev = base->refcount_.fetch_sub(1, std::memory_order_acq_rel);
    } else {
      prev = base->refcount_--;
    }
    METL_ASSERT(prev != 0);
    if (prev == 1) {
      // Destroy the object in place. Memory is owned by the caller (pool,
      // static storage, etc.) and must be released by the user.
      //
      // Contract: destruction goes through Derived, so Derived must be the
      // most-derived type. A non-final Derived with a non-virtual destructor
      // that is further subclassed would be sliced here (UB / leaked members).
      // Enforce the safe cases: Derived is a concrete leaf (final) or a
      // polymorphic base with a virtual destructor (virtual dispatch destroys
      // the real most-derived object correctly).
      static_assert(std::is_final<Derived>::value || std::has_virtual_destructor<Derived>::value,
                    "intrusive_ref_counter<Derived>: the reference-count release destroys the "
                    "object through Derived. To keep that well-defined, declare Derived 'final' "
                    "(concrete leaf types) or give it a virtual destructor (bases meant to be "
                    "subclassed).");
      static_cast<const Derived*>(ptr)->~Derived();
    }
  }
};

// METL_ATTRIBUTE_TRIVIAL_ABI: intrusive_ptr owns a single raw pointer and is
// trivially relocatable (its move leaves the source null; it holds no pointer
// to itself). The attribute lets it be passed/returned in a register and
// destroyed by the callee, matching a raw pointer's calling convention, without
// changing observable behavior. No-op on toolchains lacking the attribute.
/// @brief Owning smart pointer for types carrying their own reference count.
///
/// Manages the strong reference count through the ADL hooks
/// intrusive_ptr_add_ref / intrusive_ptr_release (supplied by
/// intrusive_ref_counter or user-defined). Copy shares ownership (add-ref); move
/// transfers it. No separate control block and no heap allocation of its own.
/// @tparam T Pointee type providing the intrusive ref-count hooks.
/// @warning The pointee must supply the intrusive_ptr_add_ref /
///          intrusive_ptr_release hooks, or use will fail to compile.
template <typename T>
class METL_ATTRIBUTE_TRIVIAL_ABI intrusive_ptr {
 public:
  using element_type = T;
  using pointer = T*;

  /// @brief Constructs an empty (null) pointer.
  constexpr intrusive_ptr() noexcept : ptr_(nullptr) {}
  /// @brief Constructs an empty (null) pointer.
  constexpr intrusive_ptr(std::nullptr_t) noexcept : ptr_(nullptr) {}

  /// @brief Adopts an existing strong reference without incrementing the count.
  /// @param ptr Raw pointer whose reference is transferred to this object.
  intrusive_ptr(pointer ptr, adopt_ref_t) noexcept : ptr_(ptr) {}

  /// @brief Retains a new strong reference, incrementing the count.
  /// @param ptr Raw pointer to share ownership of (may be null).
  intrusive_ptr(pointer ptr, retain_ref_t) : ptr_(ptr) {
    if (ptr_ != nullptr) {
      intrusive_ptr_add_ref(ptr_);
    }
  }

  /// @brief Copy constructor; shares ownership by incrementing the count.
  intrusive_ptr(const intrusive_ptr& other) : ptr_(other.ptr_) {
    if (ptr_ != nullptr) {
      intrusive_ptr_add_ref(ptr_);
    }
  }

  /// @brief Move constructor; transfers ownership and nulls the source.
  intrusive_ptr(intrusive_ptr&& other) noexcept : ptr_(other.ptr_) { other.ptr_ = nullptr; }

  // Converting copy constructor: intrusive_ptr<U> -> intrusive_ptr<T>
  // when U* is convertible to T*.
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*> && !std::is_same_v<U, T>>>
  intrusive_ptr(const intrusive_ptr<U>& other) : ptr_(other.get()) {
    if (ptr_ != nullptr) {
      intrusive_ptr_add_ref(ptr_);
    }
  }

  // Converting move constructor.
  template <typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*> && !std::is_same_v<U, T>>>
  intrusive_ptr(intrusive_ptr<U>&& other) noexcept : ptr_(other.detach()) {}

  /// @brief Destructor; releases the held reference if any.
  ~intrusive_ptr() { reset(); }

  /// @brief Copy assignment; releases the current reference and shares `other`.
  intrusive_ptr& operator=(const intrusive_ptr& other) {
    if (this == &other) {
      return *this;
    }

    reset();
    ptr_ = other.ptr_;
    if (ptr_ != nullptr) {
      intrusive_ptr_add_ref(ptr_);
    }
    return *this;
  }

  /// @brief Move assignment; releases the current reference and takes `other`.
  intrusive_ptr& operator=(intrusive_ptr&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    reset();
    ptr_ = other.ptr_;
    other.ptr_ = nullptr;
    return *this;
  }

  /// @brief Releases the held reference, becoming empty.
  intrusive_ptr& operator=(std::nullptr_t) noexcept {
    reset();
    return *this;
  }

  /// @brief Returns the raw pointer without affecting the reference count.
  METL_NODISCARD pointer get() const noexcept { return ptr_; }

  /// @brief Dereferences the managed object.
  /// @pre The pointer must be non-null.
  METL_NODISCARD T& operator*() const noexcept { return *ptr_; }
  /// @brief Member access on the managed object.
  /// @pre The pointer must be non-null.
  METL_NODISCARD pointer operator->() const noexcept { return ptr_; }

  /// @brief Tests whether a non-null object is held.
  METL_NODISCARD constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }
  /// @brief Tests whether a non-null object is held.
  METL_NODISCARD constexpr bool has_value() const noexcept { return ptr_ != nullptr; }

  /// @brief Releases the held reference (decrementing the count) and nulls this.
  void reset() noexcept {
    if (ptr_ != nullptr) {
      intrusive_ptr_release(ptr_);
      ptr_ = nullptr;
    }
  }

  /// @brief Releases ownership without decrementing the reference count.
  /// @return The raw pointer; the caller now owns the strong reference and must
  ///         eventually release it.
  METL_NODISCARD pointer detach() noexcept {
    pointer result = ptr_;
    ptr_ = nullptr;
    return result;
  }

  /// @brief Swaps the managed pointers of two intrusive_ptr objects.
  void swap(intrusive_ptr& other) noexcept {
    pointer temp = ptr_;
    ptr_ = other.ptr_;
    other.ptr_ = temp;
  }

 private:
  pointer ptr_;
};

// ---------------------------------------------------------------------------
// Comparison operators
// ---------------------------------------------------------------------------

template <typename T, typename U>
METL_NODISCARD inline bool operator==(const intrusive_ptr<T>& lhs, const intrusive_ptr<U>& rhs) noexcept {
  return lhs.get() == rhs.get();
}

template <typename T, typename U>
METL_NODISCARD inline bool operator!=(const intrusive_ptr<T>& lhs, const intrusive_ptr<U>& rhs) noexcept {
  return lhs.get() != rhs.get();
}

template <typename T, typename U>
METL_NODISCARD inline bool operator<(const intrusive_ptr<T>& lhs, const intrusive_ptr<U>& rhs) noexcept {
  return std::less<>{}(lhs.get(), rhs.get());
}

template <typename T, typename U>
METL_NODISCARD inline bool operator<=(const intrusive_ptr<T>& lhs, const intrusive_ptr<U>& rhs) noexcept {
  return !(rhs < lhs);
}

template <typename T, typename U>
METL_NODISCARD inline bool operator>(const intrusive_ptr<T>& lhs, const intrusive_ptr<U>& rhs) noexcept {
  return rhs < lhs;
}

template <typename T, typename U>
METL_NODISCARD inline bool operator>=(const intrusive_ptr<T>& lhs, const intrusive_ptr<U>& rhs) noexcept {
  return !(lhs < rhs);
}

// intrusive_ptr<T> vs T*
template <typename T>
METL_NODISCARD inline bool operator==(const intrusive_ptr<T>& lhs, T* rhs) noexcept {
  return lhs.get() == rhs;
}

template <typename T>
METL_NODISCARD inline bool operator==(T* lhs, const intrusive_ptr<T>& rhs) noexcept {
  return lhs == rhs.get();
}

template <typename T>
METL_NODISCARD inline bool operator!=(const intrusive_ptr<T>& lhs, T* rhs) noexcept {
  return lhs.get() != rhs;
}

template <typename T>
METL_NODISCARD inline bool operator!=(T* lhs, const intrusive_ptr<T>& rhs) noexcept {
  return lhs != rhs.get();
}

template <typename T>
METL_NODISCARD inline bool operator<(const intrusive_ptr<T>& lhs, T* rhs) noexcept {
  return std::less<>{}(lhs.get(), rhs);
}

template <typename T>
METL_NODISCARD inline bool operator<(T* lhs, const intrusive_ptr<T>& rhs) noexcept {
  return std::less<>{}(lhs, rhs.get());
}

template <typename T>
METL_NODISCARD inline bool operator<=(const intrusive_ptr<T>& lhs, T* rhs) noexcept {
  return !(rhs < lhs);
}

template <typename T>
METL_NODISCARD inline bool operator<=(T* lhs, const intrusive_ptr<T>& rhs) noexcept {
  return !(rhs < lhs);
}

template <typename T>
METL_NODISCARD inline bool operator>(const intrusive_ptr<T>& lhs, T* rhs) noexcept {
  return rhs < lhs;
}

template <typename T>
METL_NODISCARD inline bool operator>(T* lhs, const intrusive_ptr<T>& rhs) noexcept {
  return rhs < lhs;
}

template <typename T>
METL_NODISCARD inline bool operator>=(const intrusive_ptr<T>& lhs, T* rhs) noexcept {
  return !(lhs < rhs);
}

template <typename T>
METL_NODISCARD inline bool operator>=(T* lhs, const intrusive_ptr<T>& rhs) noexcept {
  return !(lhs < rhs);
}

// intrusive_ptr<T> vs nullptr_t
template <typename T>
METL_NODISCARD inline bool operator==(const intrusive_ptr<T>& lhs, std::nullptr_t) noexcept {
  return lhs.get() == nullptr;
}

template <typename T>
METL_NODISCARD inline bool operator==(std::nullptr_t, const intrusive_ptr<T>& rhs) noexcept {
  return rhs.get() == nullptr;
}

template <typename T>
METL_NODISCARD inline bool operator!=(const intrusive_ptr<T>& lhs, std::nullptr_t) noexcept {
  return lhs.get() != nullptr;
}

template <typename T>
METL_NODISCARD inline bool operator!=(std::nullptr_t, const intrusive_ptr<T>& rhs) noexcept {
  return rhs.get() != nullptr;
}

template <typename T>
METL_NODISCARD inline bool operator<(const intrusive_ptr<T>& lhs, std::nullptr_t) noexcept {
  return std::less<T*>{}(lhs.get(), nullptr);
}

template <typename T>
METL_NODISCARD inline bool operator<(std::nullptr_t, const intrusive_ptr<T>& rhs) noexcept {
  return std::less<T*>{}(nullptr, rhs.get());
}

template <typename T>
METL_NODISCARD inline bool operator<=(const intrusive_ptr<T>& lhs, std::nullptr_t) noexcept {
  return !(nullptr < lhs);
}

template <typename T>
METL_NODISCARD inline bool operator<=(std::nullptr_t, const intrusive_ptr<T>& rhs) noexcept {
  return !(rhs < nullptr);
}

template <typename T>
METL_NODISCARD inline bool operator>(const intrusive_ptr<T>& lhs, std::nullptr_t) noexcept {
  return nullptr < lhs;
}

template <typename T>
METL_NODISCARD inline bool operator>(std::nullptr_t, const intrusive_ptr<T>& rhs) noexcept {
  return rhs < nullptr;
}

template <typename T>
METL_NODISCARD inline bool operator>=(const intrusive_ptr<T>& lhs, std::nullptr_t) noexcept {
  return !(lhs < nullptr);
}

template <typename T>
METL_NODISCARD inline bool operator>=(std::nullptr_t, const intrusive_ptr<T>& rhs) noexcept {
  return !(nullptr < rhs);
}

// ---------------------------------------------------------------------------
// swap
// ---------------------------------------------------------------------------

/// @brief ADL swap for two intrusive_ptr objects.
template <typename T>
inline void swap(intrusive_ptr<T>& lhs, intrusive_ptr<T>& rhs) noexcept {
  lhs.swap(rhs);
}

}  // namespace metl

// ---------------------------------------------------------------------------
// std::hash specialization
// ---------------------------------------------------------------------------

namespace std {

template <typename T>
struct hash<::metl::intrusive_ptr<T>> {
  METL_NODISCARD std::size_t operator()(const ::metl::intrusive_ptr<T>& ptr) const noexcept {
    return std::hash<T*>{}(ptr.get());
  }
};

}  // namespace std
