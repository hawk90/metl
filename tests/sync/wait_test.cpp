#include "metl_check.hpp"

#include <cstdint>

#include <metl/register_access.hpp>
#include <metl/wait.hpp>

namespace {

// A spin loop written the way the header documents it: poll a volatile flag,
// hint with cpu_relax() on each iteration. Single-threaded here, so the flag is
// already set -- what is being checked is that the loop is well-formed and
// terminates, not that it synchronises anything.
volatile std::uint32_t g_flag = 1;

std::uint32_t spin_until_flag() noexcept {
  std::uint32_t iterations = 0;
  while (g_flag == 0) {
    metl::cpu_relax();
    ++iterations;
  }
  return iterations;
}

}  // namespace

int main() {
  // --- the primitives are callable and return ---------------------------------
  metl::cpu_relax();
  metl::compiler_barrier();
  metl::send_event();

  // --- the event register is sticky -------------------------------------------
  // This is the property the header's usage pattern depends on: an event that
  // arrives BEFORE the wait is not lost, it makes the wait return immediately.
  // Signalling first therefore guarantees this returns promptly rather than
  // parking the test process. (On targets with no event mechanism
  // wait_for_event() degrades to cpu_relax(), which also returns immediately.)
  metl::send_event();
  metl::wait_for_event();

  // --- a documented spin loop terminates --------------------------------------
  CHECK_EQ(spin_until_flag(), std::uint32_t{0});

  // Now with an iteration that actually spins: clear the flag, run a bounded
  // loop that relaxes, and set it from within.
  g_flag = 0;
  std::uint32_t spins = 0;
  while (g_flag == 0) {
    metl::cpu_relax();
    if (++spins == 100) {
      g_flag = 1;
    }
  }
  CHECK_EQ(spins, std::uint32_t{100});

  // --- barriers compose with the hints ----------------------------------------
  metl::barrier_release();
  metl::compiler_barrier();
  metl::barrier_acquire();

  return metl_test::exit_code();
}
