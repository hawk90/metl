#pragma once
/// @file
/// @brief An empty base that deletes the copy and move operations a class's
///        element does not allow, so the class can declare all of them `= default`.
///
/// A class that wraps a `T` declares its five special members `= default` and
/// keeps the work in internal bases, one per operation, each trivial or
/// user-provided depending on `T` (the shape libstdc++ and MSVC use for
/// `std::optional`). A defaulted operation is then trivial exactly when every
/// base's is, so `optional<int>` is trivially copyable while
/// `optional<fixed_function<...>>` keeps its user-provided copy.
///
/// What the defaulted operations cannot do alone is disappear for an element
/// that is not copyable: the internal base's user-provided copy constructor
/// exists for every `T` and fails only when instantiated. This base supplies
/// the deletion. Its operations are deleted where the flag is false and
/// defaulted where it is true, and a defaulted operation in the derived class
/// is deleted when a base's is, so `std::is_copy_constructible_v` reports the
/// element's answer instead of a hard error later (#601).
///
/// Unlike `gated_param` (detail/copy_move_gate.hpp), every operation keeps its
/// usual signature, so clang-tidy sees a copy constructor and a move
/// constructor and needs no suppression (#621).
///
/// The constructor half and the assignment half are two empty classes, the
/// second derived from the first, rather than sixteen specializations of one.
/// A chain keeps a single empty base subobject, which MSVC lays out at
/// offset 0 like the others (it gives every empty base after the first its own
/// byte).
///
/// Progress guarantee: wait-free, bounded (no code at run time).
#include "metl/detail/abi.hpp"

namespace metl {
METL_DETAIL_ABI_BEGIN
namespace detail {

/// @brief Copy and move constructors: deleted where the flag is false.
/// @tparam Owner The deriving class. One type per class, so a wrapper of a
///         wrapper holds two different empty bases at offset 0 and the
///         empty-base optimization still applies.
template <bool Copy, bool Move, typename Owner>
struct enable_copy_move_ctor {};

template <typename Owner>
struct enable_copy_move_ctor<false, true, Owner> {
  enable_copy_move_ctor() = default;
  enable_copy_move_ctor(const enable_copy_move_ctor&) = delete;
  enable_copy_move_ctor(enable_copy_move_ctor&&) = default;
  enable_copy_move_ctor& operator=(const enable_copy_move_ctor&) = default;
  enable_copy_move_ctor& operator=(enable_copy_move_ctor&&) = default;
  ~enable_copy_move_ctor() = default;
};

template <typename Owner>
struct enable_copy_move_ctor<true, false, Owner> {
  enable_copy_move_ctor() = default;
  enable_copy_move_ctor(const enable_copy_move_ctor&) = default;
  enable_copy_move_ctor(enable_copy_move_ctor&&) = delete;
  enable_copy_move_ctor& operator=(const enable_copy_move_ctor&) = default;
  enable_copy_move_ctor& operator=(enable_copy_move_ctor&&) = default;
  ~enable_copy_move_ctor() = default;
};

template <typename Owner>
struct enable_copy_move_ctor<false, false, Owner> {
  enable_copy_move_ctor() = default;
  enable_copy_move_ctor(const enable_copy_move_ctor&) = delete;
  enable_copy_move_ctor(enable_copy_move_ctor&&) = delete;
  enable_copy_move_ctor& operator=(const enable_copy_move_ctor&) = default;
  enable_copy_move_ctor& operator=(enable_copy_move_ctor&&) = default;
  ~enable_copy_move_ctor() = default;
};

/// @brief Adds the copy and move assignments on top of the constructor half.
template <bool Copy, bool CopyAssign, bool Move, bool MoveAssign, typename Owner>
struct enable_copy_move : enable_copy_move_ctor<Copy, Move, Owner> {};

template <bool Copy, bool Move, typename Owner>
struct enable_copy_move<Copy, false, Move, true, Owner> : enable_copy_move_ctor<Copy, Move, Owner> {
  enable_copy_move() = default;
  enable_copy_move(const enable_copy_move&) = default;
  enable_copy_move(enable_copy_move&&) = default;
  enable_copy_move& operator=(const enable_copy_move&) = delete;
  enable_copy_move& operator=(enable_copy_move&&) = default;
  ~enable_copy_move() = default;
};

template <bool Copy, bool Move, typename Owner>
struct enable_copy_move<Copy, true, Move, false, Owner> : enable_copy_move_ctor<Copy, Move, Owner> {
  enable_copy_move() = default;
  enable_copy_move(const enable_copy_move&) = default;
  enable_copy_move(enable_copy_move&&) = default;
  enable_copy_move& operator=(const enable_copy_move&) = default;
  enable_copy_move& operator=(enable_copy_move&&) = delete;
  ~enable_copy_move() = default;
};

template <bool Copy, bool Move, typename Owner>
struct enable_copy_move<Copy, false, Move, false, Owner> : enable_copy_move_ctor<Copy, Move, Owner> {
  enable_copy_move() = default;
  enable_copy_move(const enable_copy_move&) = default;
  enable_copy_move(enable_copy_move&&) = default;
  enable_copy_move& operator=(const enable_copy_move&) = delete;
  enable_copy_move& operator=(enable_copy_move&&) = delete;
  ~enable_copy_move() = default;
};

}  // namespace detail
METL_DETAIL_ABI_END
}  // namespace metl
