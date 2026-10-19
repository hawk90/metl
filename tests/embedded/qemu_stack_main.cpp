// The runtime stack high-water mark of tests/embedded/stack_probe.cpp's
// operations, measured under QEMU (#404).
//
// The `invariants` job's stack gate reads `-fstack-usage`, which is static and
// has three blind spots: it exists only without LTO (GCC writes no .su under
// IPO), it reports single frames rather than a call chain, and nothing ever
// runs. This file links stack_probe.cpp into a program, runs every operation it
// drives, and measures how deep the stack really went -- once without LTO and
// once with -flto, which is the build the static gate cannot see.
//
// HOW. `measure` reads SP, paints the region below it with a pattern, calls the
// probe's driver, and scans upward from the bottom of the painted region for
// the first word the pattern no longer holds. Everything below SP at that point
// belongs to the driver's call chain, so the distance is its high-water mark.
//
//   * `measure` is noinline and calls the driver through a volatile pointer, so
//     not even LTO can fold the driver into the frame SP was read in, which
//     would hide its stack use above the painted region.
//   * The paint and the scan are loops inside `measure`'s own frame; they make
//     no calls, so they do not touch the painted region themselves.
//   * A driver that reaches the bottom of the painted region has not been
//     measured, only bounded below. That is reported as "saturated" and fails;
//     it is never a number.
//   * A pushed word that happens to equal the pattern makes the mark read up to
//     one word short. The pattern is not a value any of this code computes.
//
// The output is one line, `METL_STACK_HWM <bytes>` or
// `METL_STACK_HWM saturated`, which tools/check_qemu_probes.py grades. The
// runner shim's METL_QEMU_EXIT line follows it.

#include <cstddef>
#include <cstdint>
#include <cstdio>

extern "C" void metl_stack_probe_drive(void);

namespace {

constexpr std::uint32_t kPattern = 0xC5A5A55Cu;

// Below `measure`'s SP. The linker script gives the stack 4 KiB, and crt0 plus
// main use a few hundred bytes above this point, so 3 KiB stays inside it.
constexpr std::size_t kPaintWords = 3072 / sizeof(std::uint32_t);

constexpr std::size_t kSaturated = ~static_cast<std::size_t>(0);

void (*volatile g_drive)(void) = &metl_stack_probe_drive;

/// Bytes of stack the driver used, or kSaturated if it reached the bottom of
/// the paint.
__attribute__((noinline)) std::size_t measure() {
  std::uintptr_t sp = 0;
#if defined(__arm__)
  __asm__ __volatile__("mov %0, sp" : "=r"(sp));
#else
  // Host builds only check that this file compiles; the number is meaningless
  // there and nothing reads it.
  sp = reinterpret_cast<std::uintptr_t>(__builtin_frame_address(0));
#endif
  volatile std::uint32_t* const top =
      reinterpret_cast<volatile std::uint32_t*>(sp & ~static_cast<std::uintptr_t>(3));
  volatile std::uint32_t* const bottom = top - kPaintWords;

  for (volatile std::uint32_t* word = bottom; word < top; ++word) {
    *word = kPattern;
  }

  g_drive();

  volatile std::uint32_t* deepest = bottom;
  while (deepest < top && *deepest == kPattern) {
    ++deepest;
  }
  if (deepest == bottom) {
    return kSaturated;
  }
  return static_cast<std::size_t>(top - deepest) * sizeof(std::uint32_t);
}

}  // namespace

int main() {
  const std::size_t used = measure();
  if (used == kSaturated) {
    std::printf("METL_STACK_HWM saturated\n");
    return 1;
  }
  std::printf("METL_STACK_HWM %u\n", static_cast<unsigned>(used));
  return 0;
}
