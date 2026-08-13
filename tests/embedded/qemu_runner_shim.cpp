// Exit reporter for the Cortex-M conformance runner.
//
// Linked into every test with -Wl,--wrap=main, so crt0's call to main lands here
// first. That matters because the tests are not uniform: only 30 of the 75 use
// tests/metl_check.hpp and its exit_code(); the other 45 return raw status codes
// from main. A sentinel emitted by the shared helper would therefore have
// covered 40% of the suite. Wrapping main covers all of it and touches no test.
//
// The line this prints is what the runner grades on, rather than QEMU's exit
// status: semihosting SYS_EXIT does not propagate cleanly on every machine +
// libc combination, which is why the existing picolibc smoke job tolerates a
// timeout. A conformance gate cannot tolerate one — a hang and a pass must be
// distinguishable — so the contract is explicit:
//
//   METL_QEMU_EXIT 0     the test's main returned 0
//   METL_QEMU_EXIT <n>   it returned n
//   (no line at all)     it hung, crashed, or never reached the end
//
// The absence of the line is a failure, so an early abort cannot be mistaken for
// a pass no matter what it printed on the way out.

#include <cstdio>

extern "C" int __real_main(void);

extern "C" int __wrap_main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  std::printf("METL_QEMU_BEGIN\n");
  const int status = __real_main();
  std::printf("METL_QEMU_EXIT %d\n", status);
  std::fflush(stdout);
  return status;
}
