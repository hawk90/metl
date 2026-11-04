#pragma once

/// @file
/// @brief `metl::exp::once_flag` and `metl::exp::try_call_once`: the names of
///        `metl::once_flag` and `metl::try_call_once` (<metl/once_flag.hpp>) in
///        `metl::exp`, for code that names them there.
///
/// experimental: may change or be removed in a minor release.
/// Deprecated in 1.15.0 and removed in 2.0.0 (#645).
/// Tracking issue: #524. This header stays until 2.0.0. New code includes
/// <metl/once_flag.hpp> and names `metl::once_flag`.

#include "metl/once_flag.hpp"

namespace metl {
namespace exp {

template <typename Lock = irq_lock>
using once_flag METL_DEPRECATED(
    "metl::exp::once_flag is removed in 2.0; use metl::once_flag from <metl/once_flag.hpp> (#524)") =
    metl::once_flag<Lock>;
// Not deprecated on its own: a deprecated wrapper would be a second function
// that ADL finds beside metl::try_call_once, so an unqualified call under
// `using namespace metl::exp` would become ambiguous. The flag it takes
// warns instead.
using metl::try_call_once;

}  // namespace exp
}  // namespace metl
