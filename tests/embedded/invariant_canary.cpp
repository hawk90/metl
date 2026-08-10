// METL invariant canary -- the NEGATIVE control for the symbol audit.
//
// This image deliberately violates the invariants: it calls operator new and
// malloc. tools/check_invariants.py MUST report violations for it. The CI job
// asserts a non-zero exit here, so a broken or silently no-op audit fails the
// build instead of quietly passing everything.
//
// A gate that cannot fail is not a gate. If this file ever passes the audit,
// the audit logic is dead -- fix the script, do not relax the check.
//
// Built with the same flags as invariant_probe.cpp (see the CI `invariants`
// job); the only difference is what the code does.

#include <cstddef>
#include <cstdint>

extern "C" void* malloc(std::size_t);
extern "C" void free(void*);

namespace {

volatile std::uint32_t g_sink = 0;

struct payload {
  std::uint32_t value;
};

}  // namespace

extern "C" int canary_main(void) {
  // Violation 1: operator new / operator delete.
  payload* boxed = new payload{7u};
  g_sink = g_sink + boxed->value;
  delete boxed;

  // Violation 2: the C heap directly.
  void* block = malloc(32);
  g_sink = g_sink + (block != nullptr ? 1u : 0u);
  free(block);

  return static_cast<int>(g_sink);
}

extern "C" [[noreturn]] void _start(void) {
  (void)canary_main();
  for (;;) {
  }
}
