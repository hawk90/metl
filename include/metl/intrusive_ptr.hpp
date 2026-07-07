#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"

#include <atomic>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace metl {

struct adopt_ref_t {
  explicit constexpr adopt_ref_t(int) noexcept {}
};

struct retain_ref_t {
  explicit constexpr retain_ref_t(int) noexcept {}
};

inline constexpr adopt_ref_t adopt_ref{0};
inline constexpr retain_ref_t retain_ref{0};

enum class refcount_kind { non_atomic, atomic };

// CRTP base providing reference counting and ADL hooks for intrusive_ptr.
//
// Embedded note: heap allocation is OFF. When the reference count reaches
// zero, this base only invokes the derived destructor. Memory deallocation
// is the user's responsibility (typically through metl::object_pool or
// static storage). Users may also override the ADL hooks
// intrusive_ptr_add_ref / intrusive_ptr_release for their own types and
// bypass this CRTP base entirely.
template <typename Derived, refcount_kind Kind = refcount_kind::non_atomic>
class intrusive_ref_counter {
 public:
  static constexpr refcount_kind kind = Kind;

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
template <typename T>
class METL_ATTRIBUTE_TRIVIAL_ABI intrusive_ptr {
 public:
  using element_type = T;
  using pointer = T*;

  constexpr intrusive_ptr() noexcept : ptr_(nullptr) {}
  constexpr intrusive_ptr(std::nullptr_t) noexcept : ptr_(nullptr) {}

  intrusive_ptr(pointer ptr, adopt_ref_t) noexcept : ptr_(ptr) {}

  intrusive_ptr(pointer ptr, retain_ref_t) : ptr_(ptr) {
    if (ptr_ != nullptr) {
      intrusive_ptr_add_ref(ptr_);
    }
  }

  intrusive_ptr(const intrusive_ptr& other) : ptr_(other.ptr_) {
    if (ptr_ != nullptr) {
      intrusive_ptr_add_ref(ptr_);
    }
  }

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

  ~intrusive_ptr() { reset(); }

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

  intrusive_ptr& operator=(intrusive_ptr&& other) noexcept {
    if (this == &other) {
      return *this;
    }

    reset();
    ptr_ = other.ptr_;
    other.ptr_ = nullptr;
    return *this;
  }

  intrusive_ptr& operator=(std::nullptr_t) noexcept {
    reset();
    return *this;
  }

  METL_NODISCARD pointer get() const noexcept { return ptr_; }

  METL_NODISCARD T& operator*() const noexcept { return *ptr_; }
  METL_NODISCARD pointer operator->() const noexcept { return ptr_; }

  METL_NODISCARD constexpr explicit operator bool() const noexcept { return ptr_ != nullptr; }
  METL_NODISCARD constexpr bool has_value() const noexcept { return ptr_ != nullptr; }

  void reset() noexcept {
    if (ptr_ != nullptr) {
      intrusive_ptr_release(ptr_);
      ptr_ = nullptr;
    }
  }

  // Release ownership without decrementing the reference count. Returns the
  // raw pointer; the caller now owns the strong reference.
  METL_NODISCARD pointer detach() noexcept {
    pointer result = ptr_;
    ptr_ = nullptr;
    return result;
  }

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
