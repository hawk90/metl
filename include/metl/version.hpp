#pragma once

#include "metl/config.hpp"

namespace metl {

/// Compile-time library version, exposed as `metl::version::{major,minor,patch}`.
///
/// The values mirror the `metl::version_*` constants from `<metl/config.hpp>`
/// and are usable in constant expressions (e.g. `static_assert`).
struct version {
  /// Major version component.
  static constexpr int major = version_major;
  /// Minor version component.
  static constexpr int minor = version_minor;
  /// Patch version component.
  static constexpr int patch = version_patch;
};

}  // namespace metl
