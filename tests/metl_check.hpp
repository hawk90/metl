#pragma once

// Tiny, dependency-free test-assertion helper for METL's exit-code-based tests.
//
// Historically a failing test only signalled via a numeric return code, so a
// red test never said *which* line or value was wrong. CHECK / CHECK_EQ print
//
//   <file>:<line>: CHECK failed: <expr>
//   <file>:<line>: CHECK_EQ failed: <a> == <b> (left = <va>, right = <vb>)
//
// to stderr, record the failure, and let the test keep running. Return
// metl_test::exit_code() (0 on success, 1 if any check failed) from main().
//
// Usage:
//   #include "metl_check.hpp"
//   int main() {
//     CHECK(v.empty());
//     CHECK_EQ(v.size(), 0u);
//     return metl_test::exit_code();
//   }

#include <cstdio>
#include <type_traits>

namespace metl_test {

inline int& failure_count() {
  static int count = 0;
  return count;
}

template <typename T>
inline void print_value(std::FILE* out, const T& value) {
  if constexpr (std::is_same<T, bool>::value) {
    std::fprintf(out, "%s", value ? "true" : "false");
  } else if constexpr (std::is_same<T, char>::value) {
    std::fprintf(out, "'%c'", value);
  } else if constexpr (std::is_integral<T>::value && std::is_signed<T>::value) {
    std::fprintf(out, "%lld", static_cast<long long>(value));
  } else if constexpr (std::is_integral<T>::value) {
    std::fprintf(out, "%llu", static_cast<unsigned long long>(value));
  } else if constexpr (std::is_floating_point<T>::value) {
    std::fprintf(out, "%g", static_cast<double>(value));
  } else if constexpr (std::is_same<typename std::decay<T>::type, const char*>::value ||
                       std::is_same<typename std::decay<T>::type, char*>::value) {
    std::fprintf(out, "\"%s\"", value);
  } else if constexpr (std::is_pointer<T>::value) {
    std::fprintf(out, "%p", static_cast<const void*>(value));
  } else {
    std::fprintf(out, "<unprintable>");
  }
}

inline void check(bool condition, const char* expr, const char* file, int line) {
  if (!condition) {
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expr);
    ++failure_count();
  }
}

template <typename A, typename B>
inline void check_eq(
    const A& lhs, const B& rhs, const char* lhs_expr, const char* rhs_expr, const char* file, int line) {
  if (!(lhs == rhs)) {
    std::fprintf(stderr, "%s:%d: CHECK_EQ failed: %s == %s (left = ", file, line, lhs_expr, rhs_expr);
    print_value(stderr, lhs);
    std::fprintf(stderr, ", right = ");
    print_value(stderr, rhs);
    std::fprintf(stderr, ")\n");
    ++failure_count();
  }
}

inline int exit_code() {
  const int failures = failure_count();
  if (failures > 0) {
    std::fprintf(stderr, "FAILED: %d check(s) failed\n", failures);
    return 1;
  }
  return 0;
}

}  // namespace metl_test

// static_cast<bool> so types with an explicit operator bool (e.g. optional,
// expected, function_ref) can be checked directly.
#define CHECK(expr) ::metl_test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

#define CHECK_EQ(lhs, rhs) ::metl_test::check_eq((lhs), (rhs), #lhs, #rhs, __FILE__, __LINE__)
