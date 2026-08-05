#include <csetjmp>

#include <metl/assert.hpp>
#include <metl/config.hpp>

namespace {

// The assert/panic failure path is [[noreturn]]: after the customization-point
// handler runs, the library unconditionally std::abort()s so a returning
// handler can never fall through into UB. To still observe that the handler is
// invoked (and receives the expected expression/file/line), the test handlers
// escape via longjmp *before* the guaranteed abort is reached.
std::jmp_buf g_assert_jmp;
std::jmp_buf g_panic_jmp;

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
  std::longjmp(g_assert_jmp, 1);
}

void test_panic_handler(const char* message, const char* file, int line) noexcept {
  panic_capture::called = true;
  panic_capture::message = message;
  panic_capture::file = file;
  panic_capture::line = line;
  std::longjmp(g_panic_jmp, 1);
}

}  // namespace

int main() {
  auto previous_assert = metl::set_assert_handler(&test_assert_handler);
  auto previous_panic = metl::set_panic_handler(&test_panic_handler);

  // setjmp returns 0 on the initial call; the handler longjmps back with 1.
  if (setjmp(g_assert_jmp) == 0) {
    METL_ASSERT(false);
    // Unreachable: the assert path must never fall through.
    return 3;
  }

  if (!assert_capture::called || assert_capture::expression == nullptr ||
      assert_capture::expression[0] != 'f' || assert_capture::line <= 0 || assert_capture::file == nullptr) {
    return 1;
  }

  if (setjmp(g_panic_jmp) == 0) {
    METL_PANIC("boom");
    // Unreachable: panic is [[noreturn]].
    return 4;
  }

  if (!panic_capture::called || panic_capture::message == nullptr || panic_capture::message[0] != 'b' ||
      panic_capture::line <= 0 || panic_capture::file == nullptr) {
    return 2;
  }

  metl::set_assert_handler(previous_assert);
  metl::set_panic_handler(previous_panic);
  return 0;
}
