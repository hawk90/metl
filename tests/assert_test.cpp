#include <metl/assert.hpp>
#include <metl/config.hpp>

namespace {

struct assert_capture {
  static bool called;
  static const char* expression;
  static const char* file;
  static int line;
};

bool assert_capture::called = false;
const char* assert_capture::expression = nullptr;
const char* assert_capture::file = nullptr;
int assert_capture::line = 0;

struct panic_capture {
  static bool called;
  static const char* message;
  static const char* file;
  static int line;
};

bool panic_capture::called = false;
const char* panic_capture::message = nullptr;
const char* panic_capture::file = nullptr;
int panic_capture::line = 0;

void test_assert_handler(const char* expression, const char* file, int line) noexcept {
  assert_capture::called = true;
  assert_capture::expression = expression;
  assert_capture::file = file;
  assert_capture::line = line;
}

void test_panic_handler(const char* message, const char* file, int line) noexcept {
  panic_capture::called = true;
  panic_capture::message = message;
  panic_capture::file = file;
  panic_capture::line = line;
}

}  // namespace

int main() {
  auto previous_assert = metl::set_assert_handler(&test_assert_handler);
  auto previous_panic = metl::set_panic_handler(&test_panic_handler);

  METL_ASSERT(false);

  if (!assert_capture::called || assert_capture::expression == nullptr ||
      assert_capture::expression[0] != 'f' || assert_capture::line <= 0 || assert_capture::file == nullptr) {
    return 1;
  }

  METL_PANIC("boom");

  if (!panic_capture::called || panic_capture::message == nullptr || panic_capture::message[0] != 'b' ||
      panic_capture::line <= 0 || panic_capture::file == nullptr) {
    return 2;
  }

  metl::set_assert_handler(previous_assert);
  metl::set_panic_handler(previous_panic);
  return 0;
}
