#pragma once
/// @file
/// @brief A layer that deletes the copy and move operations a class's element
///        does not allow, so the class can declare all of them `= default`.
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
/// exists for every `T` and fails only when instantiated. This layer supplies
/// the deletion. It derives from the top internal base, and its operations are
/// deleted where the flag is false and defaulted where it is true. A defaulted
/// operation in the derived class is deleted when its base's is, so
/// `std::is_copy_constructible_v` reports the element's answer instead of a
/// hard error later (#601).
///
/// Unlike `gated_param` (detail/copy_move_gate.hpp), every operation keeps its
/// usual signature, so clang-tidy sees a copy constructor and a move
/// constructor and needs no suppression (#621).
///
/// It wraps the base instead of sitting beside it as an empty base: a class
/// with a non-empty base and an empty one is laid out by MSVC with room for
/// the empty one, while a single chain of bases adds nothing on any compiler.
/// The constructor half and the assignment half are two layers, the second
/// derived from the first, rather than sixteen specializations of one. Both
/// inherit the base's constructors, so the class builds its base as before.
///
/// Progress guarantee: wait-free, bounded (no code at run time).
#include "metl/detail/abi.hpp"

namespace metl {
METL_DETAIL_ABI_BEGIN
namespace detail {

/// @brief Copy and move constructors of `Base`: deleted where the flag is false.
template <typename Base, bool Copy, bool Move>
struct enable_copy_move_ctor : Base {
  using Base::Base;
};

template <typename Base>
struct enable_copy_move_ctor<Base, false, true> : Base {
  using Base::Base;
  enable_copy_move_ctor() = default;
  enable_copy_move_ctor(const enable_copy_move_ctor&) = delete;
  enable_copy_move_ctor(enable_copy_move_ctor&&) = default;
  enable_copy_move_ctor& operator=(const enable_copy_move_ctor&) = default;
  enable_copy_move_ctor& operator=(enable_copy_move_ctor&&) = default;
  ~enable_copy_move_ctor() = default;
};

template <typename Base>
struct enable_copy_move_ctor<Base, true, false> : Base {
  using Base::Base;
  enable_copy_move_ctor() = default;
  enable_copy_move_ctor(const enable_copy_move_ctor&) = default;
  enable_copy_move_ctor(enable_copy_move_ctor&&) = delete;
  enable_copy_move_ctor& operator=(const enable_copy_move_ctor&) = default;
  enable_copy_move_ctor& operator=(enable_copy_move_ctor&&) = default;
  ~enable_copy_move_ctor() = default;
};

template <typename Base>
struct enable_copy_move_ctor<Base, false, false> : Base {
  using Base::Base;
  enable_copy_move_ctor() = default;
  enable_copy_move_ctor(const enable_copy_move_ctor&) = delete;
  enable_copy_move_ctor(enable_copy_move_ctor&&) = delete;
  enable_copy_move_ctor& operator=(const enable_copy_move_ctor&) = default;
  enable_copy_move_ctor& operator=(enable_copy_move_ctor&&) = default;
  ~enable_copy_move_ctor() = default;
};

/// @brief `Base` with its copy and move constructors and assignments deleted
///        where the flags are false, and left as they are where true.
template <typename Base, bool Copy, bool CopyAssign, bool Move, bool MoveAssign>
struct enable_copy_move : enable_copy_move_ctor<Base, Copy, Move> {
  using enable_copy_move_ctor<Base, Copy, Move>::enable_copy_move_ctor;
};

template <typename Base, bool Copy, bool Move>
struct enable_copy_move<Base, Copy, false, Move, true> : enable_copy_move_ctor<Base, Copy, Move> {
  using enable_copy_move_ctor<Base, Copy, Move>::enable_copy_move_ctor;
  enable_copy_move() = default;
  enable_copy_move(const enable_copy_move&) = default;
  enable_copy_move(enable_copy_move&&) = default;
  enable_copy_move& operator=(const enable_copy_move&) = delete;
  enable_copy_move& operator=(enable_copy_move&&) = default;
  ~enable_copy_move() = default;
};

template <typename Base, bool Copy, bool Move>
struct enable_copy_move<Base, Copy, true, Move, false> : enable_copy_move_ctor<Base, Copy, Move> {
  using enable_copy_move_ctor<Base, Copy, Move>::enable_copy_move_ctor;
  enable_copy_move() = default;
  enable_copy_move(const enable_copy_move&) = default;
  enable_copy_move(enable_copy_move&&) = default;
  enable_copy_move& operator=(const enable_copy_move&) = default;
  enable_copy_move& operator=(enable_copy_move&&) = delete;
  ~enable_copy_move() = default;
};

template <typename Base, bool Copy, bool Move>
struct enable_copy_move<Base, Copy, false, Move, false> : enable_copy_move_ctor<Base, Copy, Move> {
  using enable_copy_move_ctor<Base, Copy, Move>::enable_copy_move_ctor;
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
