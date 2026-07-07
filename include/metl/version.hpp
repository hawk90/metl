#pragma once

#include "metl/config.hpp"

namespace metl {

struct version {
  static constexpr int major = version_major;
  static constexpr int minor = version_minor;
  static constexpr int patch = version_patch;
};

}  // namespace metl
