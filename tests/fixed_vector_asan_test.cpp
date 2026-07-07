// AddressSanitizer container-annotation test for metl::fixed_vector.
//
// fixed_vector stores its elements in an inline buffer; the unused-capacity
// tail [size(), capacity()) is poisoned so an out-of-bounds access past size()
// is trapped by ASan even though the whole buffer is a single object.
//
// This test is a no-op unless built under AddressSanitizer (it is compiled in
// every configuration, including gcc/msvc/TSAN, where it must trivially pass).
// Under ASan it verifies two things:
//   1. the poison boundaries are exact — live elements [0, size()) are
//      addressable, the tail [size(), capacity()) is poisoned, and the boundary
//      tracks push_back / pop_back;
//   2. an actual read into the poisoned tail is caught (a forked child reads
//      past size() and is expected to be killed by ASan).

#include "metl_check.hpp"

#include <metl/fixed_vector.hpp>

#if METL_FIXED_VECTOR_ASAN

#include <cstdint>

#include <sanitizer/asan_interface.h>

#if defined(__unix__) || defined(__APPLE__)
#define METL_ASAN_TEST_HAVE_FORK 1
#include <unistd.h>

#include <sys/wait.h>
#else
#define METL_ASAN_TEST_HAVE_FORK 0
#endif

namespace {

// A 8-byte, 8-aligned element makes every slot boundary land on an ASan shadow
// granule boundary, so the tail is poisoned exactly at element granularity.
using vec = metl::fixed_vector<std::int64_t, 8>;

bool poisoned(const vec& v, std::size_t slot) {
  const auto* base = v.data();
  return __asan_address_is_poisoned(base + slot) != 0;
}

}  // namespace

int main() {
  vec v;
  v.push_back(10);
  v.push_back(20);
  v.push_back(30);  // size() == 3

  // Live elements are addressable; the tail is poisoned.
  CHECK(!poisoned(v, 0));
  CHECK(!poisoned(v, 2));
  CHECK(poisoned(v, 3));
  CHECK(poisoned(v, 7));

  // The boundary follows the size down and back up.
  v.pop_back();  // size() == 2
  CHECK(!poisoned(v, 1));
  CHECK(poisoned(v, 2));
  v.push_back(40);  // size() == 3 again
  CHECK(!poisoned(v, 2));
  CHECK(poisoned(v, 3));

#if METL_ASAN_TEST_HAVE_FORK
  // A real read into the poisoned tail must be trapped. Do it in a child so the
  // ASan abort does not take down the test process.
  const pid_t pid = fork();
  if (pid == 0) {
    volatile std::int64_t sink = v.data()[5];  // OOB past size() -> ASan trap
    (void)sink;
    _exit(0);  // reached only if ASan did NOT catch it
  }
  CHECK(pid > 0);
  int status = 0;
  (void)waitpid(pid, &status, 0);
  // Expect abnormal termination (signal) or a non-zero ASan exit, never 0.
  const bool trapped = WIFSIGNALED(status) || (WIFEXITED(status) && WEXITSTATUS(status) != 0);
  CHECK(trapped);
#endif

  return metl_test::exit_code();
}

#else  // Not built under AddressSanitizer: trivially pass.

int main() {
  return metl_test::exit_code();
}

#endif
