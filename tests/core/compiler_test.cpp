#include <metl/compiler.hpp>

namespace {

METL_NODISCARD constexpr int nodiscard_value() noexcept {
  return 7;
}

METL_FORCE_INLINE int force_inline_identity(int value) noexcept {
  return value;
}

// Every C++17 compiler reports these two attributes; the detection macro must
// be usable in #if, where MSVC accepts __has_cpp_attribute.
#if METL_HAS_CPP_ATTRIBUTE(nodiscard) < 201603L || METL_HAS_CPP_ATTRIBUTE(fallthrough) < 201603L
#error "METL_HAS_CPP_ATTRIBUTE misses a C++17 attribute"
#endif

constexpr bool static_checks() {
  if (METL_CXX_STANDARD < 201703L) {
    return false;
  }

  if ((METL_COMPILER_CLANG + METL_COMPILER_GCC + METL_COMPILER_MSVC) > 1) {
    return false;
  }

  return nodiscard_value() == 7;
}

static_assert(static_checks(), "compiler constexpr checks failed");

}  // namespace

int main() {
  if (force_inline_identity(9) != 9) {
    return 1;
  }

  if ((METL_COMPILER_CLANG + METL_COMPILER_GCC + METL_COMPILER_MSVC) == 0) {
    return 2;
  }

  if (METL_COMPILER_MSVC) {
    if (METL_COMPILER_MSVC_VERSION <= 0) {
      return 3;
    }
  }

  return 0;
}
