// Verifies metl's security floor: METL_HARDEN is NOT stripped even at
// METL_HARDENING_NONE (AUDIT E.3). This TU pins the lowest hardening level, so
// METL_ASSERT / METL_DASSERT are compiled out. A full-table insert into a
// static_unordered_map reaches construct_at with an out-of-range index; only the
// always-on METL_HARDEN guard stands between that and a wild out-of-bounds
// write. A forked child performs the overflow and must be killed by the abort.
#define METL_HARDENING 0
#include "metl_check.hpp"

#include <metl/static_unordered_map.hpp>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#define METL_HARDEN_TEST_HAVE_FORK 1
#else
#define METL_HARDEN_TEST_HAVE_FORK 0
#endif

int main() {
#if METL_HARDEN_TEST_HAVE_FORK
  const pid_t pid = fork();
  if (pid == 0) {
    // Child. emplace() carries no size_>=Capacity refusal (that guard lives in
    // try_emplace), and at NONE its precondition METL_ASSERT is stripped, so it
    // keeps inserting distinct keys until every physical bucket is occupied. The
    // next insert makes locate_insert_index yield index == npos; with METL_ASSERT
    // gone, only METL_HARDEN(index < bucket_count) in construct_at stands between
    // that and a wild out-of-bounds write. Looping one past bucket_count
    // guarantees we cross the physical-full boundary and trip the guard.
    using Map = metl::static_unordered_map<int, int, 8>;
    Map m;
    for (int i = 0; i < static_cast<int>(Map::bucket_count) + 1; ++i) {
      m.emplace(i, i);
    }
    _exit(0);  // reached only if METL_HARDEN did NOT fire — a security regression
  }

  int status = 0;
  (void)waitpid(pid, &status, 0);
  // The child must have been killed by a signal (SIGABRT from the guard), not
  // exited cleanly.
  CHECK(WIFSIGNALED(status));
  CHECK(!(WIFEXITED(status) && WEXITSTATUS(status) == 0));
#else
  // No fork on this platform: nothing to exercise here (compile-only coverage).
#endif
  return metl_test::exit_code();
}
