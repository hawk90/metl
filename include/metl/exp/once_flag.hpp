#pragma once

/// @file
/// @brief `metl::exp::once_flag` and `metl::exp::try_call_once`: the names of
///        `metl::once_flag` and `metl::try_call_once` (<metl/once_flag.hpp>) in
///        `metl::exp`, for code that names them there.
///
/// experimental: may change or be removed in a minor release.
/// Tracking issue: #524. This header stays until 2.0.0. New code includes
/// <metl/once_flag.hpp> and names `metl::once_flag`.

#include "metl/once_flag.hpp"

namespace metl {
namespace exp {

using metl::once_flag;
using metl::try_call_once;

}  // namespace exp
}  // namespace metl
