#pragma once

/// @file
/// @brief Inline storage for up to `N` objects of `T` that are reached as ONE array,
///        shared by `fixed_vector`, `flat_map` and `flat_set`.
///
/// Those three containers hand out `data()` and let callers index it, iterate it
/// and wrap it in a `span`. Until 2026-10-05 they stored `storage_for<T>[N]` --
/// N separate byte buffers -- and placement-new'd one complete `T` into each, so
/// `data() + i` for `i > 0` was pointer arithmetic from one object into another:
/// undefined behaviour by the letter of the standard, although the layout is
/// byte-for-byte that of `T[N]` and no compiler miscompiles it.
///
/// Here the storage is a single `unsigned char` buffer and `data()` is a pointer
/// to element 0 of the `T[N]` array object that buffer provides:
///
/// - An `unsigned char` array implicitly creates implicit-lifetime objects in its
///   storage (P0593R6, adopted as a defect report against earlier standards).
///   Arrays are implicit-lifetime for ANY element type, so a `T[N]` exists there
///   as soon as the buffer does -- with none of its elements alive yet.
/// - `std::launder` on a `T(*)[N]` is therefore valid even when no element has
///   been constructed: the object it needs is the array.
/// - An object created exactly at `bytes + i * sizeof(T)` with type `T` becomes
///   element `i` of that array ([intro.object]/2, already in C++17), so
///   `data() + i` is arithmetic within one array object. Destroying an element
///   and constructing a new one in its place keeps this true.
///
/// This is the model C++20 gives `std::allocator<T>::allocate`, which "starts the
/// lifetime of the array object but not that of any of its elements".
///
/// A `union { T elems[N]; }` was considered and rejected: in C++17 placement-new
/// into a member of an inactive union does not start that member array's
/// lifetime (that arrives with C++26's trivial unions), so it would only move
/// the problem.
///
/// Residual, unchanged by this file: for a `T` with `const` or reference
/// members, C++17 [basic.life]/8 still asks for a per-element `std::launder`
/// after an element is destroyed and re-created. C++20 removed that rule
/// (P1971) and no compiler optimises on it; laundering every `operator[]` would
/// cost nothing at run time but cannot be done for `end()`, so it is recorded
/// rather than patched.

#include <cstddef>
#include <new>
#include <type_traits>

namespace metl {
namespace detail {

/// @brief Storage for up to `N` `T`s, reached as one implicitly created `T[N]`.
/// @tparam T Element type: a complete object type, not an array.
/// @tparam N Number of elements; at least 1 (callers pass `Capacity == 0 ? 1 : Capacity`,
///           because `T[0]` is ill-formed). Bounds and ASan logic keep using `Capacity`.
/// @note No constructor, on purpose: the buffer stays uninitialised, so a
///       container's default constructor does no `N * sizeof(T)` stores.
template <typename T, std::size_t N>
struct array_storage {
  static_assert(std::is_object_v<T> && !std::is_array_v<T>,
                "array_storage holds objects, and not arrays of them");
  static_assert(N >= 1, "array_storage needs at least one slot; pass Capacity == 0 ? 1 : Capacity");

  alignas(T) unsigned char bytes[sizeof(T) * N];

  /// @brief Pointer to element 0 of the array; valid with any number of elements alive.
  T* data() noexcept { return *std::launder(reinterpret_cast<T(*)[N]>(&bytes)); }
  /// @brief Pointer to element 0 of the array; valid with any number of elements alive.
  const T* data() const noexcept { return *std::launder(reinterpret_cast<const T(*)[N]>(&bytes)); }

  /// @brief Address of slot `index`, for placement new. Computed from the bytes, never from
  ///        `data()`, so it does not depend on any element being alive.
  /// @pre `index < N`.
  void* slot(std::size_t index) noexcept { return &bytes[index * sizeof(T)]; }
};

}  // namespace detail
}  // namespace metl
