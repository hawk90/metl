// The in_place tags and monostate in <metl/in_place.hpp>.
//
// monostate has every comparison, as std::monostate does: all of them say the
// two values are equal. The operands are read through volatile so that each
// operator is called at run time and not only folded by the compiler.

#include "metl_check.hpp"

#include <type_traits>

#include <metl/in_place.hpp>

namespace {

static_assert(std::is_empty_v<metl::monostate>, "monostate holds nothing");
static_assert(std::is_trivially_copyable_v<metl::monostate>, "monostate copies as bytes");
static_assert(metl::monostate{} == metl::monostate{}, "comparisons are constexpr");

bool runtime_choice() {
  volatile bool choice = true;
  return choice;
}

void monostate_comparisons() {
  const metl::monostate a = runtime_choice() ? metl::monostate{} : metl::monostate{};
  const metl::monostate b{};
  CHECK(a == b);
  CHECK(!(a != b));
  CHECK(!(a < b));
  CHECK(!(a > b));
  CHECK(a <= b);
  CHECK(a >= b);
}

}  // namespace

int main() {
  monostate_comparisons();
  return metl_test::exit_code();
}
