// The QEMU runner's negative control. One CHECK fails, so this must report
// METL_QEMU_EXIT 1. tools/run_qemu_tests.py runs it before the suite and stops
// if it reads anything else: a runner that cannot see this failure reports
// every real test as passing too.
#include "metl_check.hpp"

int main() {
  volatile int observed = 1;  // volatile: the failure must happen at run time
  CHECK_EQ(observed, 2);
  return metl_test::exit_code();
}
