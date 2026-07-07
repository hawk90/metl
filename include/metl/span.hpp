#pragma once

#include "metl/compiler.hpp"
#include "metl/config.hpp"

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace metl {

/// Sentinel extent value denoting a span whose length is known only at runtime.
inline constexpr std::size_t dynamic_extent = static_cast<std::size_t>(-1);

template <typename T, std::size_t Extent = dynamic_extent>
class span;

namespace detail {

template <typename T>
struct is_span_helper : std::false_type {};

template <typename T, std::size_t N>
struct is_span_helper<span<T, N>> : std::true_type {};

template <typename T>
struct is_std_array_helper : std::false_type {};

template <typename T>
struct remove_cvref {
  using type = typename std::remove_cv<typename std::remove_reference<T>::type>::type;
};

template <typename T>
using remove_cvref_t = typename remove_cvref<T>::type;

// Detect data() and size() member functions on container-like types.
template <typename C, typename = void>
struct has_data_size : std::false_type {};

template <typename C>
struct has_data_size<C,
                     typename std::enable_if<!std::is_array<typename remove_cvref<C>::type>::value &&
                                                 !is_span_helper<typename remove_cvref<C>::type>::value,
                                             decltype(static_cast<void>(std::declval<C&>().data()),
                                                      static_cast<void>(std::declval<C&>().size()))>::type>
    : std::true_type {};

template <typename T, typename U>
struct is_qualification_convertible
    : std::integral_constant<bool, std::is_convertible<U (*)[], T (*)[]>::value> {};

// Storage helper: stores only data when extent is fixed; data + size otherwise.
template <typename T, std::size_t Extent>
struct span_storage {
  T* data;
  static constexpr std::size_t size = Extent;

  constexpr span_storage() noexcept : data(nullptr) {}
  constexpr span_storage(T* ptr, std::size_t /*count*/) noexcept : data(ptr) {}
};

template <typename T>
struct span_storage<T, dynamic_extent> {
  T* data;
  std::size_t size;

  constexpr span_storage() noexcept : data(nullptr), size(0) {}
  constexpr span_storage(T* ptr, std::size_t count) noexcept : data(ptr), size(count) {}
};

}  // namespace detail

/// Non-owning view over a contiguous sequence of `T`, like std::span (C++17).
///
/// Stores only a pointer (and, for dynamic extent, a length); owns NO storage
/// and performs NO allocation. The viewed range must outlive the span. Element
/// access asserts (aborts) on out-of-range indices rather than throwing.
///
/// @tparam T Element type (may be const-qualified for a read-only view).
/// @tparam Extent Fixed length, or dynamic_extent for a runtime length.
template <typename T, std::size_t Extent>
class span {
 public:
  using element_type = T;
  using value_type = typename std::remove_cv<T>::type;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using pointer = T*;
  using const_pointer = const T*;
  using reference = T&;
  using const_reference = const T&;
  using iterator = pointer;
  using const_iterator = const_pointer;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  static constexpr size_type extent = Extent;

  /// Constructs an empty span. Enabled only when Extent == 0 or dynamic_extent.
  template <std::size_t E = Extent, typename = typename std::enable_if<E == 0 || E == dynamic_extent>::type>
  constexpr span() noexcept : storage_(nullptr, 0) {}

  /// Constructs a span over `count` elements starting at `ptr`.
  /// @pre For a fixed extent, `count == Extent`; asserts otherwise.
  constexpr span(pointer ptr, size_type count) noexcept : storage_(ptr, count) {
    METL_ASSERT(Extent == dynamic_extent || count == Extent);
  }

  /// Constructs a span over the range [first, last).
  /// @pre `last >= first`, and for a fixed extent the length equals Extent.
  constexpr span(pointer first, pointer last) noexcept
      : storage_(first, static_cast<size_type>(last - first)) {
    METL_ASSERT(last >= first);
    METL_ASSERT(Extent == dynamic_extent || static_cast<size_type>(last - first) == Extent);
  }

  /// Constructs a span viewing all `N` elements of a C array.
  /// METL_LIFETIME_BOUND: the span refers into `array`; the array must outlive
  /// the span. clang flags obvious dangling (e.g. binding to a temporary array).
  template <std::size_t N, typename = typename std::enable_if<Extent == dynamic_extent || Extent == N>::type>
  constexpr span(element_type (&array METL_LIFETIME_BOUND)[N]) noexcept : storage_(array, N) {}

  /// Constructs a span viewing a container that exposes compatible data()/size().
  // Container constructor: enabled for types exposing data()/size() with compatible element type.
  template <typename C,
            typename = typename std::enable_if<
                detail::has_data_size<C&>::value && !std::is_same<detail::remove_cvref_t<C>, span>::value &&
                detail::is_qualification_convertible<
                    T,
                    typename std::remove_pointer<decltype(std::declval<C&>().data())>::type>::value>::type>
  // METL_LIFETIME_BOUND: the span views `container`'s storage; the container
  // must outlive the span. clang flags obvious dangling (e.g. a span bound to a
  // temporary container).
  constexpr span(C& container METL_LIFETIME_BOUND) noexcept : storage_(container.data(), container.size()) {
    METL_ASSERT(Extent == dynamic_extent || container.size() == Extent);
  }

  /// Converting constructor between compatible spans (extent/qualification).
  template <
      typename U,
      std::size_t N,
      typename = typename std::enable_if<(Extent == dynamic_extent || N == dynamic_extent || Extent == N) &&
                                         detail::is_qualification_convertible<T, U>::value>::type>
  constexpr span(const span<U, N>& other) noexcept : storage_(other.data(), other.size()) {
    METL_ASSERT(Extent == dynamic_extent || other.size() == Extent);
  }

  constexpr span(const span&) noexcept = default;
  span& operator=(const span&) noexcept = default;

  /// Returns an iterator to the first element.
  constexpr iterator begin() const noexcept { return storage_.data; }
  /// Returns an iterator one past the last element.
  constexpr iterator end() const noexcept { return storage_.data + size(); }
  constexpr const_iterator cbegin() const noexcept { return storage_.data; }
  constexpr const_iterator cend() const noexcept { return storage_.data + size(); }
  constexpr reverse_iterator rbegin() const noexcept { return reverse_iterator(end()); }
  constexpr reverse_iterator rend() const noexcept { return reverse_iterator(begin()); }
  constexpr const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
  constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

  /// Returns a reference to the first element.
  /// @pre Span is non-empty; asserts and aborts otherwise.
  METL_NODISCARD constexpr reference front() const noexcept {
    METL_ASSERT(size() > 0);
    return storage_.data[0];
  }

  /// Returns a reference to the last element.
  /// @pre Span is non-empty; asserts and aborts otherwise.
  METL_NODISCARD constexpr reference back() const noexcept {
    METL_ASSERT(size() > 0);
    return storage_.data[size() - 1];
  }

  /// Accesses the element at `index`.
  /// @pre `index < size()`; out-of-range asserts and aborts (does not throw).
  constexpr reference operator[](size_type index) const noexcept {
    METL_ASSERT(index < size());
    return storage_.data[index];
  }

  /// Returns a pointer to the first element.
  METL_NODISCARD constexpr pointer data() const noexcept { return storage_.data; }
  /// Returns the number of elements in the view.
  METL_NODISCARD constexpr size_type size() const noexcept { return storage_.size; }
  /// Returns the size of the view in bytes.
  METL_NODISCARD constexpr size_type size_bytes() const noexcept {
    return storage_.size * sizeof(element_type);
  }
  /// Returns true if the view is empty.
  METL_NODISCARD constexpr bool empty() const noexcept { return storage_.size == 0; }

  /// Returns a fixed-extent subview of the first `Count` elements.
  /// @pre `Count <= size()`; asserts otherwise.
  template <std::size_t Count>
  METL_NODISCARD constexpr span<element_type, Count> first() const noexcept {
    static_assert(Extent == dynamic_extent || Count <= Extent,
                  "first<Count>(): Count must not exceed Extent");
    METL_ASSERT(Count <= size());
    return span<element_type, Count>(storage_.data, Count);
  }

  /// Returns a fixed-extent subview of the last `Count` elements.
  /// @pre `Count <= size()`; asserts otherwise.
  template <std::size_t Count>
  METL_NODISCARD constexpr span<element_type, Count> last() const noexcept {
    static_assert(Extent == dynamic_extent || Count <= Extent, "last<Count>(): Count must not exceed Extent");
    METL_ASSERT(Count <= size());
    return span<element_type, Count>(storage_.data + (size() - Count), Count);
  }

  /// Returns a subview of `Count` elements starting at `Offset`.
  /// @pre `Offset <= size()` and the requested count fits; asserts otherwise.
  template <std::size_t Offset, std::size_t Count = dynamic_extent>
  METL_NODISCARD constexpr auto subspan() const noexcept
      -> span<element_type,
              (Count != dynamic_extent ? Count
                                       : (Extent != dynamic_extent ? Extent - Offset : dynamic_extent))> {
    static_assert(Extent == dynamic_extent || Offset <= Extent,
                  "subspan<Offset, Count>(): Offset must not exceed Extent");
    static_assert(Count == dynamic_extent || Extent == dynamic_extent || Count <= Extent - Offset,
                  "subspan<Offset, Count>(): Count out of range");
    METL_ASSERT(Offset <= size());
    constexpr std::size_t kResultExtent =
        (Count != dynamic_extent ? Count : (Extent != dynamic_extent ? Extent - Offset : dynamic_extent));
    const size_type actual_count = (Count == dynamic_extent) ? (size() - Offset) : Count;
    METL_ASSERT(actual_count <= (size() - Offset));
    return span<element_type, kResultExtent>(storage_.data + Offset, actual_count);
  }

  /// Returns a dynamic-extent subview of the first `count` elements.
  /// @pre `count <= size()`; asserts otherwise.
  METL_NODISCARD constexpr span<element_type, dynamic_extent> first(size_type count) const noexcept {
    METL_ASSERT(count <= size());
    return span<element_type, dynamic_extent>(storage_.data, count);
  }

  /// Returns a dynamic-extent subview of the last `count` elements.
  /// @pre `count <= size()`; asserts otherwise.
  METL_NODISCARD constexpr span<element_type, dynamic_extent> last(size_type count) const noexcept {
    METL_ASSERT(count <= size());
    return span<element_type, dynamic_extent>(storage_.data + (size() - count), count);
  }

  /// Returns a dynamic-extent subview of `count` elements starting at `offset`.
  /// @pre `offset <= size()` and `count` fits within the remainder; asserts otherwise.
  METL_NODISCARD constexpr span<element_type, dynamic_extent> subspan(
      size_type offset, size_type count = dynamic_extent) const noexcept {
    METL_ASSERT(offset <= size());
    const size_type actual_count = (count == dynamic_extent) ? (size() - offset) : count;
    METL_ASSERT(actual_count <= (size() - offset));
    return span<element_type, dynamic_extent>(storage_.data + offset, actual_count);
  }

 private:
  detail::span_storage<T, Extent> storage_;
};

// Deduction guides (CTAD).
template <typename T, std::size_t N>
span(T (&)[N]) -> span<T, N>;

template <typename C>
span(C&) -> span<typename std::remove_reference<decltype(*std::declval<C&>().data())>::type>;

template <typename C>
span(const C&)
    -> span<const typename std::remove_reference<decltype(*std::declval<const C&>().data())>::type>;

template <typename T>
span(T*, std::size_t) -> span<T>;

template <typename T>
span(T*, T*) -> span<T>;

// as_bytes / as_writable_bytes.
namespace detail {
template <std::size_t Extent, std::size_t ElemSize>
struct bytes_extent {
  static constexpr std::size_t value = (Extent == dynamic_extent) ? dynamic_extent : (Extent * ElemSize);
};
}  // namespace detail

/// Reinterprets a span as a read-only view of its underlying bytes.
template <typename T, std::size_t N>
METL_NODISCARD inline span<const std::byte, detail::bytes_extent<N, sizeof(T)>::value> as_bytes(
    span<T, N> s) noexcept {
  return span<const std::byte, detail::bytes_extent<N, sizeof(T)>::value>(
      reinterpret_cast<const std::byte*>(s.data()), s.size_bytes());
}

/// Reinterprets a non-const span as a writable view of its underlying bytes.
template <typename T, std::size_t N, typename = typename std::enable_if<!std::is_const<T>::value>::type>
METL_NODISCARD inline span<std::byte, detail::bytes_extent<N, sizeof(T)>::value> as_writable_bytes(
    span<T, N> s) noexcept {
  return span<std::byte, detail::bytes_extent<N, sizeof(T)>::value>(reinterpret_cast<std::byte*>(s.data()),
                                                                    s.size_bytes());
}

}  // namespace metl
