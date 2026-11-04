#pragma once

/// @file
/// @brief `metl::exp::bitset`: the name of `metl::bitset` (<metl/bitset.hpp>)
///        in `metl::exp`, for code that names it there.
///
/// experimental: may change or be removed in a minor release.
/// Deprecated in 1.15.0 and removed in 2.0.0 (#645).
/// Tracking issue: #527. This header stays until 2.0.0. New code includes
/// <metl/bitset.hpp> and names `metl::bitset`.

#include "metl/bitset.hpp"

namespace metl {
namespace exp {

template <std::size_t N>
using bitset METL_DEPRECATED(
    "metl::exp::bitset is removed in 2.0; use metl::bitset from <metl/bitset.hpp> (#527)") = metl::bitset<N>;

}  // namespace exp
}  // namespace metl
