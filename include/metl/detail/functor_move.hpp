#pragma once
/// @file
/// @brief What a container's move does with its comparator, hasher or key
///        equality: it copies them, so the source keeps working ones.
///
/// A moved-from container is empty and usable: it takes new elements. Taking
/// one calls the functors, so a move that moved them out would leave a stateful
/// one (a `fixed_function`, say) empty, and the next insertion would call
/// nothing (#691). libstdc++'s `std::map` copies its comparator on move for the
/// same reason. A move-only functor cannot be copied, so it is moved, and the
/// source is then as usable as that functor leaves it.
///
/// The move's `noexcept` and whether it exists follow the operation actually
/// used: the functor's copy when it has one, its move otherwise.
///
/// Progress guarantee: wait-free, bounded (no code at run time beyond the
/// functor's own copy or move).
#include "metl/detail/abi.hpp"
#include "metl/detail/enable_copy_move.hpp"

#include <type_traits>

namespace metl {
METL_DETAIL_ABI_BEGIN
namespace detail {

/// @brief What a move constructor builds its functor from: `const F&` when
///        `F` is copy constructible, `F&&` otherwise.
template <typename F>
using functor_move_source_t = std::conditional_t<std::is_copy_constructible_v<F>, const F&, F&&>;

/// @brief What a move assignment assigns its functor from: `const F&` when
///        `F` is copy assignable, `F&&` otherwise.
template <typename F>
using functor_move_assign_source_t = std::conditional_t<std::is_copy_assignable_v<F>, const F&, F&&>;

template <typename F>
inline constexpr bool functor_move_constructible_v = std::is_constructible_v<F, functor_move_source_t<F>>;
template <typename F>
inline constexpr bool functor_nothrow_move_constructible_v =
    std::is_nothrow_constructible_v<F, functor_move_source_t<F>>;
template <typename F>
inline constexpr bool functor_move_assignable_v = std::is_assignable_v<F&, functor_move_assign_source_t<F>>;
template <typename F>
inline constexpr bool functor_nothrow_move_assignable_v =
    std::is_nothrow_assignable_v<F&, functor_move_assign_source_t<F>>;

/// @brief The source's functor, as a move constructor should take it.
template <typename F>
constexpr functor_move_source_t<F> functor_move_source(F& functor) noexcept {
  return static_cast<functor_move_source_t<F>>(functor);
}

/// @brief The source's functor, as a move assignment should take it.
template <typename F>
constexpr functor_move_assign_source_t<F> functor_move_assign_source(F& functor) noexcept {
  return static_cast<functor_move_assign_source_t<F>>(functor);
}

/// @brief The functor itself, value-initialized: a default-built holder never
///        leaves a stateful functor indeterminate (#574).
template <typename F>
struct functor_slot_storage {
  F value{};

  functor_slot_storage() = default;
  explicit functor_slot_storage(const F& functor) noexcept(std::is_nothrow_copy_constructible_v<F>)
      : value(functor) {}

 protected:
  // The move constructor's way in: a copy, or a move for a move-only F.
  struct from_move_source {};
  functor_slot_storage(from_move_source /*unused*/,
                       functor_move_source_t<F> functor) noexcept(functor_nothrow_move_constructible_v<F>)
      : value(static_cast<functor_move_source_t<F>>(functor)) {}
};

/// @brief Holds a functor whose move copies it, for a class that keeps its
///        copy and move operations implicit (`fixed_priority_queue`).
///
/// A trivially copyable functor (an empty `std::less`, say) is copied and moved
/// alike, so its holder's operations are all implicit and stay trivial.
template <typename F, bool = std::is_trivially_copyable_v<F>>
struct functor_slot_core : functor_slot_storage<F> {
  using functor_slot_storage<F>::functor_slot_storage;
};

template <typename F>
struct functor_slot_core<F, false> : functor_slot_storage<F> {
  using functor_slot_storage<F>::functor_slot_storage;
  functor_slot_core() = default;
  functor_slot_core(const functor_slot_core&) = default;
  functor_slot_core(functor_slot_core&& other) noexcept(functor_nothrow_move_constructible_v<F>)
      : functor_slot_storage<F>(typename functor_slot_storage<F>::from_move_source{},
                                functor_move_source(other.value)) {}
  functor_slot_core& operator=(const functor_slot_core&) = default;
  functor_slot_core& operator=(functor_slot_core&& other) noexcept(functor_nothrow_move_assignable_v<F>) {
    this->value = functor_move_assign_source(other.value);
    return *this;
  }
  ~functor_slot_core() = default;
};

/// @brief `functor_slot_core` with the operations `F` does not allow deleted,
///        so the holding class's implicit ones report `F`'s answer.
template <typename F>
using functor_slot = enable_copy_move<functor_slot_core<F>,
                                      std::is_copy_constructible_v<F>,
                                      std::is_copy_assignable_v<F>,
                                      functor_move_constructible_v<F>,
                                      functor_move_assignable_v<F>>;

}  // namespace detail
METL_DETAIL_ABI_END
}  // namespace metl
