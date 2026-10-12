// AddressSanitizer container-annotation test for metl::fixed_vector.
//
// fixed_vector stores its elements in an inline buffer; the unused-capacity
// tail [size(), capacity()) is poisoned so an out-of-bounds access past size()
// is trapped by ASan even though the whole buffer is a single object.
//
// Without AddressSanitizer there is nothing to check, so the test reports a
// skip (it is compiled in every configuration, including gcc/msvc/TSAN).
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

// #321: an element whose destructor re-enters its vector. pop_back shrinks
// first and then destroys, so the destructor may clear() the vector; that
// nested clear used to poison the tail, including the slot whose destructor
// was still running, and the destructor's own later write was reported as a
// use-after-poison.
struct reentrant;
metl::fixed_vector<reentrant, 4>& reentrant_vec();
struct reentrant {
  std::int64_t state = 1;
  bool clears = false;
  explicit reentrant(bool c) : clears(c) {}
  reentrant(const reentrant&) = default;
  reentrant& operator=(const reentrant&) = default;
  ~reentrant() {
    if (clears) {
      reentrant_vec().clear();
    }
    *static_cast<volatile std::int64_t*>(&state) = 0;  // finish its own teardown
  }
};
metl::fixed_vector<reentrant, 4>& reentrant_vec() {
  static metl::fixed_vector<reentrant, 4> v;
  return v;
}

void destructor_reenters() {
  auto& v = reentrant_vec();
  v.emplace_back(false);
  v.emplace_back(false);
  v.emplace_back(true);
  v.pop_back();  // ~reentrant clears the rest, then writes to itself
  CHECK_EQ(v.size(), 0u);
  // Once every destructor has returned, the whole buffer is the tail again.
  const auto* base = v.data();
  CHECK(__asan_address_is_poisoned(base) != 0);
  CHECK(__asan_address_is_poisoned(base + 2) != 0);

  v.emplace_back(false);
  v.emplace_back(true);
  v.emplace_back(false);
  v.erase(v.begin() + 1);  // the shifted-out last element is destroyed: no clear
  CHECK_EQ(v.size(), 2u);
  v.clear();  // destroys index 1 (clears = false after the shift) and index 0
  CHECK_EQ(v.size(), 0u);
}

}  // namespace

int main() {
  destructor_reenters();

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
    // The index is volatile: at -O1 and above a constant offset the compiler
    // can prove lies inside the stack object is not instrumented, and the read
    // would go unchecked.
    volatile std::size_t index = 5;
    volatile std::int64_t sink = v.data()[index];  // OOB past size() -> ASan trap
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

#else  // Not built under AddressSanitizer.

int main() {
#ifdef METL_TEST_ASAN_BUILD
  // The build passed -fsanitize=address, yet fixed_vector did not see it: the
  // annotations are off where they were meant to be checked. Skipping here
  // would turn the ASan legs green without running this test.
  CHECK_EQ(METL_FIXED_VECTOR_ASAN, 1);
  return metl_test::exit_code();
#else
  return metl_test::skip("fixed_vector_asan_test", "not built under AddressSanitizer");
#endif
}

#endif
