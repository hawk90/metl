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
// METL_CONST_INIT is checked only where the compiler enforces it: `constinit`
// (C++20), or Clang's require_constant_initialization in C++17. Elsewhere the
// test says it checked nothing, and is skipped.

#include "metl_check.hpp"

#include <metl/attributes.hpp>
#include <metl/expected.hpp>
#include <metl/variant.hpp>

#if (defined(__cpp_constinit) && __cpp_constinit >= 201907L) || \
    METL_HAS_CPP_ATTRIBUTE(clang::require_constant_initialization)
#define VOCAB_CONST_INIT_ENFORCED 1
#else
#define VOCAB_CONST_INIT_ENFORCED 0
#endif

namespace {

// A constexpr constructor and a destructor with an effect: neither trivially
// constructible nor trivially destructible.
int destroyed = 0;
struct counted {
  int value;
  constexpr counted() noexcept : value(7) {}
  constexpr explicit counted(int v) noexcept : value(v) {}
  counted(const counted&) = default;
  counted& operator=(const counted&) = default;
  ~counted() { ++destroyed; }
};

enum class error_code : unsigned char { none, timeout };

#if VOCAB_CONST_INIT_ENFORCED
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
#endif

}  // namespace

int main() {
#if VOCAB_CONST_INIT_ENFORCED
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
  return metl_test::exit_code();
#else
  return metl_test::skip("vocab_constant_init_test",
                         "this compiler does not enforce METL_CONST_INIT in C++17");
#endif
}
