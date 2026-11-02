// irq_lock from unprivileged code (#571).
//
// Unprivileged Thread mode executes CPSID as a no-op without a fault, so
// irq_lock masks nothing there. irq_lock states privileged execution as its
// precondition, and at METL_HARDENING_DEBUG lock() reads PRIMASK back and
// asserts. This test drops to unprivileged mode, checks that the assert fires,
// and returns to privileged mode through an SVC handler.
//
// Target-only, like irq_masking_test: on a host there is nothing to mask. It
// also skips on ARMv6-M, which has no VTOR to install the SVC handler and, on
// Cortex-M0, no unprivileged mode.
//
// Nothing is printed while unprivileged: the runner's QEMU rejects semihosting
// calls from unprivileged code, so the handler below only records the trip.

#undef METL_HARDENING  // this test pins its own level, whatever the build passes
#define METL_HARDENING 2

#include "metl_check.hpp"

#include <csetjmp>
#include <cstdint>

#include <metl/assert.hpp>
#include <metl/lock.hpp>

#if defined(__ARM_ARCH_6M__)
#define METL_IRQ_TEST_HAS_VTOR 0
#else
#define METL_IRQ_TEST_HAS_VTOR 1
#endif

#if METL_HAS_IRQ_MASKING && METL_IRQ_TEST_HAS_VTOR

namespace {

constexpr std::uintptr_t kSCB_VTOR = 0xE000ED08u;
constexpr std::size_t kSVCallVector = 11;
constexpr std::size_t kVectorCount = 16;

alignas(128) std::uint32_t g_vectors[kVectorCount];

std::jmp_buf g_jump;
volatile bool g_fired = false;

void record_trip(const char*, const char*, int) noexcept {
  g_fired = true;
  std::longjmp(g_jump, 1);
}

volatile std::uint32_t& reg(std::uintptr_t address) noexcept {
  return *reinterpret_cast<volatile std::uint32_t*>(address);
}

// Handler mode is privileged, so it may clear CONTROL.nPRIV for the thread.
void svc_handler() {
  __asm__ __volatile__("msr control, %0\n isb" ::"r"(0u) : "memory");
}

void install_vector_table() noexcept {
  const auto* existing = reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(reg(kSCB_VTOR)));
  for (std::size_t i = 0; i < kVectorCount; ++i) {
    g_vectors[i] = existing[i];
  }
  g_vectors[kSVCallVector] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&svc_handler)) | 1u;
  reg(kSCB_VTOR) = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&g_vectors[0]));
  __asm__ __volatile__("dsb\n isb" ::: "memory");
}

void drop_privilege() noexcept {
  __asm__ __volatile__("msr control, %0\n isb" ::"r"(1u) : "memory");
}

void regain_privilege() noexcept {
  __asm__ __volatile__("svc 0" ::: "memory");
}

std::uint32_t read_control() noexcept {
  std::uint32_t value = 0;
  __asm__ __volatile__("mrs %0, control" : "=r"(value)::"memory");
  return value;
}

// Whether lock() asserted. On a trip the longjmp leaves PRIMASK as it was:
// lock() never ran unlock(), and in unprivileged mode CPSID changed nothing.
bool lock_trips() noexcept {
  g_fired = false;
  if (setjmp(g_jump) == 0) {
    const auto state = metl::irq_lock::lock();
    metl::irq_lock::unlock(state);
  }
  return g_fired;
}

}  // namespace

#endif

int main() {
#if !METL_HAS_IRQ_MASKING
  return metl_test::skip("irq_lock_unprivileged_test", "METL_HAS_IRQ_MASKING == 0");
#elif !METL_IRQ_TEST_HAS_VTOR
  return metl_test::skip("irq_lock_unprivileged_test", "ARMv6-M has no VTOR; see the note above");
#else
  install_vector_table();
  const metl::assert_handler_t previous = metl::set_assert_handler(&record_trip);

  // Control: privileged, the lock masks and nothing fires.
  CHECK(!lock_trips());

  drop_privilege();
  const std::uint32_t unprivileged_control = read_control();
  const bool unprivileged_trip = lock_trips();
  regain_privilege();

  CHECK_EQ(unprivileged_control & 1u, std::uint32_t{1});
  CHECK(unprivileged_trip);
  CHECK_EQ(read_control() & 1u, std::uint32_t{0});
  CHECK(!lock_trips());  // and privileged again, it masks as before

  (void)metl::set_assert_handler(previous);
  return metl_test::exit_code();
#endif
}
