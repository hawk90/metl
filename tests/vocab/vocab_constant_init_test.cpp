// expected and variant are constant-initialized at namespace scope (#395).
//
// A dynamically initialized global is built at startup, in link order, so an
// interrupt enabled early or another translation unit's static initializer can
// see it before its constructor ran. The conformance suite checks one adapter
// per type (conformance_constinit_test.cpp); this test covers the constructors
// and specializations that suite does not reach: the default, value, error and
// in-place constructors, expected<void, E>, and an alternative with a
// non-trivial constructor and destructor.
//
// Two checks, so that every build checks something:
//
//   - At compile time, METL_CONST_INIT on each object. The compiler enforces it
//     only with `constinit` (C++20) or Clang's require_constant_initialization
//     in C++17. Elsewhere -- GCC or MSVC in C++17, which includes the
//     arm-none-eabi-g++ build under QEMU -- it expands to nothing.
//   - At run time, in every build: a dynamic initializer defined BEFORE the
//     objects copies their bytes. Within one translation unit, dynamic
//     initialization runs in definition order, and all constant initialization
//     comes first. A constant-initialized object therefore already holds its
//     value when the copy is taken; a dynamically initialized one still holds
//     the zeros of static storage. main() compares the copy with the object, so
//     each object it checks must have a value whose bytes are not all zero --
//     variant_default (index 0, int 0) is the one that cannot be checked so.

#include "metl_check.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include <metl/attributes.hpp>
#include <metl/expected.hpp>
#include <metl/variant.hpp>

#if (defined(__cpp_constinit) && __cpp_constinit >= 201907L) || \
    METL_HAS_CPP_ATTRIBUTE(clang::require_constant_initialization)
#define VOCAB_CONST_INIT_ENFORCED 1
#else
#define VOCAB_CONST_INIT_ENFORCED 0
#endif

namespace vocab_constant_init {

// A constexpr constructor and a destructor with an effect: neither trivially
// constructible nor trivially destructible.
inline int destroyed = 0;
struct counted {
  int value;
  constexpr counted() noexcept : value(7) {}
  constexpr explicit counted(int v) noexcept : value(v) {}
  counted(const counted&) = default;
  counted& operator=(const counted&) = default;
  ~counted() { ++destroyed; }
};

enum class error_code : unsigned char { none, timeout };

// Declared here and defined after `early`, which copies them.
extern metl::expected<int, int> expected_default;
extern metl::expected<int, int> expected_value;
extern metl::expected<int, int> expected_in_place;
extern metl::expected<int, error_code> expected_error;
extern metl::expected<int, error_code> expected_unexpected;
extern metl::expected<counted, error_code> expected_counted;
extern metl::expected<void, int> expected_void;
extern metl::expected<void, int> expected_void_error;
extern metl::variant<int, counted> variant_default;
extern metl::variant<counted, int> variant_counted;
extern metl::variant<int, counted> variant_converting;
extern metl::variant<int, counted> variant_in_place_type;
extern metl::variant<int, long> variant_in_place_index;

constexpr std::size_t max_bytes = 64;

struct bytes_of {
  unsigned char bytes[max_bytes];
  std::size_t size;
};

template <typename T>
bytes_of copy_bytes(const T& object) {
  static_assert(sizeof(T) <= max_bytes, "raise max_bytes");
  bytes_of copy{};
  std::memcpy(copy.bytes, static_cast<const void*>(&object), sizeof(T));
  copy.size = sizeof(T);
  return copy;
}

// The objects as the first dynamic initializer of this file finds them.
struct early_copies {
  bytes_of expected_default;
  bytes_of expected_value;
  bytes_of expected_in_place;
  bytes_of expected_error;
  bytes_of expected_unexpected;
  bytes_of expected_counted;
  bytes_of expected_void;
  bytes_of expected_void_error;
  bytes_of variant_counted;
  bytes_of variant_converting;
  bytes_of variant_in_place_type;
  bytes_of variant_in_place_index;
};

early_copies take_early_copies() {
  return {copy_bytes(vocab_constant_init::expected_default),
          copy_bytes(vocab_constant_init::expected_value),
          copy_bytes(vocab_constant_init::expected_in_place),
          copy_bytes(vocab_constant_init::expected_error),
          copy_bytes(vocab_constant_init::expected_unexpected),
          copy_bytes(vocab_constant_init::expected_counted),
          copy_bytes(vocab_constant_init::expected_void),
          copy_bytes(vocab_constant_init::expected_void_error),
          copy_bytes(vocab_constant_init::variant_counted),
          copy_bytes(vocab_constant_init::variant_converting),
          copy_bytes(vocab_constant_init::variant_in_place_type),
          copy_bytes(vocab_constant_init::variant_in_place_index)};
}

// Dynamically initialized, and defined before every object it copies.
const early_copies early = take_early_copies();

METL_CONST_INIT metl::expected<int, int> expected_default;
METL_CONST_INIT metl::expected<int, int> expected_value{5};
METL_CONST_INIT metl::expected<int, int> expected_in_place{metl::in_place, 6};
METL_CONST_INIT metl::expected<int, error_code> expected_error{metl::unexpect, error_code::timeout};
METL_CONST_INIT metl::expected<int, error_code> expected_unexpected{
    metl::unexpected<error_code>(error_code::timeout)};
METL_CONST_INIT metl::expected<counted, error_code> expected_counted;
METL_CONST_INIT metl::expected<void, int> expected_void;
METL_CONST_INIT metl::expected<void, int> expected_void_error{metl::unexpect, 3};

METL_CONST_INIT metl::variant<int, counted> variant_default;
METL_CONST_INIT metl::variant<counted, int> variant_counted;
METL_CONST_INIT metl::variant<int, counted> variant_converting{9};
METL_CONST_INIT metl::variant<int, counted> variant_in_place_type{metl::in_place_type<counted>, 4};
METL_CONST_INIT metl::variant<int, long> variant_in_place_index{metl::in_place_index<1>, 8L};

bool all_zero(const bytes_of& copy) {
  for (std::size_t i = 0; i < copy.size; ++i) {
    if (copy.bytes[i] != 0) {
      return false;
    }
  }
  return true;
}

// Whether the object held its bytes before any dynamic initializer of this file ran.
template <typename T>
bool held_before_dynamic_init(const char* name, const bytes_of& before, const T& object) {
  const bytes_of now = copy_bytes(object);
  if (all_zero(now)) {
    std::fprintf(stderr, "  %s: its bytes are all zero, so this check cannot tell\n", name);
    return false;
  }
  if (std::memcmp(before.bytes, now.bytes, now.size) != 0) {
    std::fprintf(stderr, "  %s: dynamically initialized; `early` ran before its constructor\n", name);
    return false;
  }
  return true;
}

}  // namespace vocab_constant_init

int main() {
  using namespace vocab_constant_init;

  // Before anything below changes an object.
  CHECK(held_before_dynamic_init("expected_default", early.expected_default, expected_default));
  CHECK(held_before_dynamic_init("expected_value", early.expected_value, expected_value));
  CHECK(held_before_dynamic_init("expected_in_place", early.expected_in_place, expected_in_place));
  CHECK(held_before_dynamic_init("expected_error", early.expected_error, expected_error));
  CHECK(held_before_dynamic_init("expected_unexpected", early.expected_unexpected, expected_unexpected));
  CHECK(held_before_dynamic_init("expected_counted", early.expected_counted, expected_counted));
  CHECK(held_before_dynamic_init("expected_void", early.expected_void, expected_void));
  CHECK(held_before_dynamic_init("expected_void_error", early.expected_void_error, expected_void_error));
  CHECK(held_before_dynamic_init("variant_counted", early.variant_counted, variant_counted));
  CHECK(held_before_dynamic_init("variant_converting", early.variant_converting, variant_converting));
  CHECK(
      held_before_dynamic_init("variant_in_place_type", early.variant_in_place_type, variant_in_place_type));
  CHECK(held_before_dynamic_init(
      "variant_in_place_index", early.variant_in_place_index, variant_in_place_index));

  CHECK(expected_default.has_value());
  CHECK_EQ(*expected_default, 0);
  CHECK_EQ(*expected_value, 5);
  CHECK_EQ(*expected_in_place, 6);
  CHECK(expected_error.error() == error_code::timeout);
  CHECK(expected_unexpected.error() == error_code::timeout);
  CHECK_EQ(expected_counted->value, 7);
  CHECK(expected_void.has_value());
  CHECK_EQ(expected_void_error.error(), 3);

  CHECK_EQ(metl::get<int>(variant_default), 0);
  CHECK_EQ(metl::get<counted>(variant_counted).value, 7);
  CHECK_EQ(metl::get<int>(variant_converting), 9);
  CHECK_EQ(metl::get<counted>(variant_in_place_type).value, 4);
  CHECK_EQ(metl::get<1>(variant_in_place_index), 8L);

  // The objects stay usable: a switch destroys the old member once.
  expected_counted = metl::unexpected<error_code>(error_code::none);
  CHECK_EQ(destroyed, 1);
  variant_counted.emplace<int>(1);
  CHECK_EQ(destroyed, 2);

  std::printf("vocab_constant_init_test: run-time check done; METL_CONST_INIT %s\n",
              VOCAB_CONST_INIT_ENFORCED ? "enforced by the compiler" : "not enforced by this compiler");
  return metl_test::exit_code();
}
