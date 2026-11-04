#pragma once

/// @file
/// @brief `metl::exp::intrusive_forward_list` and `metl::exp::forward_list_hook`:
///        the names of `metl::intrusive_forward_list` and
///        `metl::forward_list_hook` (<metl/intrusive_forward_list.hpp>) in
///        `metl::exp`, for code that names them there.
///
/// experimental: may change or be removed in a minor release.
/// Deprecated in 1.15.0 and removed in 2.0.0 (#645).
/// Tracking issue: #520. This header stays until 2.0.0. New code includes
/// <metl/intrusive_forward_list.hpp> and names `metl::intrusive_forward_list`.

#include "metl/intrusive_forward_list.hpp"

namespace metl {
namespace exp {

template <typename Tag = void>
using forward_list_hook METL_DEPRECATED(
    "metl::exp::forward_list_hook is removed in 2.0; use metl::forward_list_hook from "
    "<metl/intrusive_forward_list.hpp> (#520)") = metl::forward_list_hook<Tag>;
template <typename T, typename Tag = void>
using intrusive_forward_list METL_DEPRECATED(
    "metl::exp::intrusive_forward_list is removed in 2.0; use metl::intrusive_forward_list from "
    "<metl/intrusive_forward_list.hpp> (#520)") = metl::intrusive_forward_list<T, Tag>;

}  // namespace exp
}  // namespace metl
