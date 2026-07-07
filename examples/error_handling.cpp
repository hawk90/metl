// error_handling.cpp
//
// Exception-free error handling with METL's vocabulary types:
//
//   - metl::expected<T, E> — a value OR an error, returned by value.
//   - metl::optional<T>    — a value OR nothing.
//   - metl::variant<...>   — a type-safe tagged union.
//
// These replace exceptions and out-parameters on freestanding targets where
// throwing is banned or unavailable.
//
// !!! Contract note (differs from the standard library): the "unchecked"
// !!! accessors ASSERT rather than throw. metl::expected::value() /
// !!! metl::optional::value() / metl::optional::operator* / metl::get<>()
// !!! all call METL_ASSERT on the empty/wrong-alternative case, which aborts
// !!! by default (there is no std::bad_optional_access / bad_expected_access).
// !!! So you must branch on has_value() / operator bool / holds_alternative()
// !!! FIRST, exactly as this example does. value_or() is the safe default-path.

#include <cstdint>
#include <cstdio>

#include <metl/expected.hpp>
#include <metl/optional.hpp>
#include <metl/variant.hpp>

namespace {

enum class parse_error : std::uint8_t {
  empty,
  not_a_digit,
  overflow,
};

// Parse an unsigned decimal integer. Returns the value or a typed error —
// no exceptions, no errno, no sentinel value ambiguity.
metl::expected<std::uint32_t, parse_error> parse_u32(const char* s) noexcept {
  if (s == nullptr || *s == '\0') {
    return metl::unexpected<parse_error>(parse_error::empty);
  }
  std::uint32_t acc = 0;
  for (const char* p = s; *p != '\0'; ++p) {
    if (*p < '0' || *p > '9') {
      return metl::unexpected<parse_error>(parse_error::not_a_digit);
    }
    const std::uint32_t digit = static_cast<std::uint32_t>(*p - '0');
    if (acc > (0xFFFFFFFFu - digit) / 10u) {
      return metl::unexpected<parse_error>(parse_error::overflow);
    }
    acc = acc * 10u + digit;
  }
  return acc;
}

// optional models "maybe there is a value". Here: look up a config key.
metl::optional<std::int32_t> config_lookup(const char* key) noexcept {
  if (key[0] == 'g') {
    return metl::optional<std::int32_t>(50);  // "gain"
  }
  return metl::nullopt;  // unknown key
}

// A variant carries one of several result shapes.
struct ok_result {
  std::uint32_t value;
};
struct busy_result {
  std::uint32_t retry_ms;
};
using command_result = metl::variant<ok_result, busy_result>;

command_result run_command(bool device_busy) noexcept {
  if (device_busy) {
    return busy_result{25};
  }
  return ok_result{7};
}

}  // namespace

int main() {
  // ---- expected: branch on success, read value() only on the happy path ----
  if (auto r = parse_u32("4093")) {
    if (r.value() != 4093) {
      return 1;
    }
  } else {
    return 2;  // should have parsed
  }

  {
    auto bad = parse_u32("12x");
    if (bad || bad.error() != parse_error::not_a_digit) {
      return 3;
    }
  }
  {
    auto ovf = parse_u32("99999999999");
    if (ovf || ovf.error() != parse_error::overflow) {
      return 4;
    }
  }

  // ---- optional: value_or() gives a safe default without a branch ----------
  const std::int32_t gain = config_lookup("gain").value_or(1);
  const std::int32_t missing = config_lookup("nope").value_or(-1);
  if (gain != 50 || missing != -1) {
    return 5;
  }

  // Explicit presence check before dereferencing (operator* asserts if empty).
  if (auto v = config_lookup("gain")) {
    if (*v != 50) {
      return 6;
    }
  }

  // ---- variant: dispatch on the active alternative -------------------------
  {
    command_result res = run_command(/*device_busy=*/false);
    if (!metl::holds_alternative<ok_result>(res)) {
      return 7;
    }
    if (metl::get<ok_result>(res).value != 7) {
      return 8;
    }
  }
  {
    command_result res = run_command(/*device_busy=*/true);
    // visit handles every alternative in one place.
    const std::uint32_t code = metl::visit(
        [](auto&& alt) -> std::uint32_t {
          using A = std::decay_t<decltype(alt)>;
          if constexpr (std::is_same_v<A, busy_result>) {
            return 1000u + alt.retry_ms;
          } else {
            return alt.value;
          }
        },
        res);
    if (code != 1025u) {
      return 9;
    }
  }

  std::printf("error_handling: expected + optional + variant OK (gain=%d)\n", gain);
  return 0;
}
