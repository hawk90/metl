// METL_ASSERT_STRINGS=0: a failed check hands its handler empty strings, never
// null, and the line number (#540).

#define METL_ASSERT_STRINGS 0

#include "metl_check.hpp"

#include <csetjmp>

#include <metl/assert.hpp>
#include <metl/config.hpp>

namespace {

std::jmp_buf g_jump;
const char* g_first = nullptr;
const char* g_file = nullptr;
int g_line = 0;

[[noreturn]] void capture(const char* first, const char* file, int line) noexcept {
  g_first = first;
  g_file = file;
  g_line = line;
  std::longjmp(g_jump, 1);
}

void check_empty_file_and_line(int expected_line) {
  CHECK(g_file != nullptr);
  CHECK_EQ(g_file[0], '\0');
  CHECK_EQ(g_line, expected_line);
}

}  // namespace

int main() {
  (void)metl::set_assert_handler(&capture);
  (void)metl::set_panic_handler(&capture);

  // Volatile: `passes` so the compiler cannot prove a check fails and warn about
  // it, and `line` because it changes after setjmp and is read after longjmp.
  volatile bool passes = false;
  volatile int line = 0;

  if (setjmp(g_jump) == 0) {
    line = __LINE__ + 1;
    METL_HARDEN(passes);
    CHECK(false);
  } else {
    CHECK(g_first != nullptr);
    CHECK_EQ(g_first[0], '\0');
    check_empty_file_and_line(line);
  }

#if METL_HARDENING >= METL_HARDENING_FAST
  if (setjmp(g_jump) == 0) {
    line = __LINE__ + 1;
    METL_ASSERT(passes);
    CHECK(false);
  } else {
    CHECK(g_first != nullptr);
    CHECK_EQ(g_first[0], '\0');
    check_empty_file_and_line(line);
  }
#endif

  // The message is the caller's own text and stays.
  if (setjmp(g_jump) == 0) {
    line = __LINE__ + 1;
    METL_PANIC("kept");
    CHECK(false);
  } else {
    CHECK(g_first != nullptr);
    CHECK_EQ(g_first[0], 'k');
    check_empty_file_and_line(line);
  }

  return metl_test::exit_code();
}
