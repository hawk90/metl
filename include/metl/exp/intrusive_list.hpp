#pragma once

/// @file
/// @brief `metl::exp::intrusive_list` and `metl::exp::list_hook`: the names of
///        `metl::intrusive_list` and `metl::list_hook` (<metl/intrusive_list.hpp>)
///        in `metl::exp`, for code that names them there.
///
/// experimental: may change or be removed in a minor release.
/// Tracking issue: #520. This header stays until 2.0.0. New code includes
/// <metl/intrusive_list.hpp> and names `metl::intrusive_list`.

#include "metl/intrusive_list.hpp"

namespace metl {
namespace exp {

using metl::intrusive_list;
using metl::list_hook;

}  // namespace exp
}  // namespace metl
