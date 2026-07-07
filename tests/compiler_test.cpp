#include <metl/compiler.hpp>

namespace {

METL_NODISCARD constexpr int nodiscard_value() noexcept {
  return 7;
}

METL_FORCE_INLINE int force_inline_identity(int value) noexcept {
  return value;
}

constexpr bool static_checks() {
  if (metl::cxx_standard < 201703L) {
    return false;
  }

  if ((METL_COMPILER_CLANG + METL_COMPILER_GCC + METL_COMPILER_MSVC) > 1) {
    return false;
  }

  if (metl::is_clang != (METL_COMPILER_CLANG != 0)) {
    return false;
  }

  if (metl::is_gcc != (METL_COMPILER_GCC != 0)) {
    return false;
  }

  if (metl::is_msvc != (METL_COMPILER_MSVC != 0)) {
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

  if (metl::active_compiler == metl::compiler_id::unknown) {
    return 2;
  }

  if (METL_CXX_STANDARD != metl::cxx_standard) {
    return 3;
  }

  if (METL_COMPILER_CLANG) {
    if (METL_COMPILER_CLANG_VERSION_MAJOR <= 0) {
      return 4;
    }
  }

  if (METL_COMPILER_GCC) {
    if (METL_COMPILER_GCC_VERSION_MAJOR <= 0) {
      return 5;
    }
  }

  if (METL_COMPILER_MSVC) {
    if (METL_COMPILER_MSVC_VERSION <= 0) {
      return 6;
    }
  }

  int state = 0;
  switch (state) {
    case 0:
      METL_FALLTHROUGH;
    case 1:
      state = 1;
      break;
    default:
      return 7;
  }

  return state == 1 ? 0 : 8;
}
