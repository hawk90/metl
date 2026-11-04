#pragma once

/// @file
/// @brief `metl::exp::intrusive_list` and `metl::exp::list_hook`: the names of
///        `metl::intrusive_list` and `metl::list_hook` (<metl/intrusive_list.hpp>)
///        in `metl::exp`, for code that names them there.
///
/// experimental: may change or be removed in a minor release.
/// Deprecated in 1.15.0 and removed in 2.0.0 (#645).
/// Tracking issue: #520. This header stays until 2.0.0. New code includes
/// <metl/intrusive_list.hpp> and names `metl::intrusive_list`.

#include "metl/intrusive_list.hpp"

namespace metl {
namespace exp {

template <typename T, typename Tag = void>
using intrusive_list METL_DEPRECATED(
    "metl::exp::intrusive_list is removed in 2.0; use metl::intrusive_list from <metl/intrusive_list.hpp> "
    "(#520)") = metl::intrusive_list<T, Tag>;
template <typename Tag = void>
using list_hook METL_DEPRECATED(
    "metl::exp::list_hook is removed in 2.0; use metl::list_hook from <metl/intrusive_list.hpp> (#520)") =
    metl::list_hook<Tag>;

}  // namespace exp
}  // namespace metl
