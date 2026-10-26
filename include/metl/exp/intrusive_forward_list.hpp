#pragma once

/// @file
/// @brief `metl::exp::intrusive_forward_list` and `metl::exp::forward_list_hook`:
///        the names of `metl::intrusive_forward_list` and
///        `metl::forward_list_hook` (<metl/intrusive_forward_list.hpp>) in
///        `metl::exp`, for code that names them there.
///
/// experimental: may change or be removed in a minor release.
/// Tracking issue: #520. This header stays until 2.0.0. New code includes
/// <metl/intrusive_forward_list.hpp> and names `metl::intrusive_forward_list`.

#include "metl/intrusive_forward_list.hpp"

namespace metl {
namespace exp {

using metl::forward_list_hook;
using metl::intrusive_forward_list;

}  // namespace exp
}  // namespace metl
