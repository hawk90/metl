#pragma once

/// @file
/// @brief `metl::exp::bitset`: the name of `metl::bitset` (<metl/bitset.hpp>)
///        in `metl::exp`, for code that names it there.
///
/// experimental: may change or be removed in a minor release.
/// Tracking issue: #527. This header stays until 2.0.0. New code includes
/// <metl/bitset.hpp> and names `metl::bitset`.

#include "metl/bitset.hpp"

namespace metl {
namespace exp {

using metl::bitset;

}  // namespace exp
}  // namespace metl
