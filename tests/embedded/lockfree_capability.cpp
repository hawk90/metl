// Capability probe for METL's Tier 1 lock-free types — the CI job that keeps
// their claims honest (docs/SCOPE.md §3: a tier and its CI job arrive together).
//
// Covers metl::atomic_handle and metl::mpmc_queue. Both ask the same underlying
// question — does this target have a lock-free compare-exchange — so they share
// one probe rather than two near-identical ones.
//
// A lock-free CAS is required. ARMv7-M and up have
// LDREX/STREX; ARMv6-M (Cortex-M0/M0+) has no CAS at all, and GCC lowers atomic
// read-modify-write there to libatomic calls. The claim being tested is
// therefore two-sided, and testing only the positive half would be worthless:
//
//   METL_EXPECT_LOCK_FREE_HANDLE=1  the trait must be TRUE and atomic_handle
//                                   must instantiate and generate code
//   METL_EXPECT_LOCK_FREE_HANDLE=0  the trait must be FALSE, which is what makes
//                                   the static_assert inside atomic_handle fire
//                                   instead of silently degrading to a lock
//
// The expected value comes from the CI matrix, so a toolchain change that
// quietly moves a target across the line fails this build rather than changing
// METL's progress guarantees behind everyone's back.
//
// Compiled freestanding, never linked or run.

#include <cstdint>

#include <metl/atomic_handle.hpp>
#include <metl/handle_pool.hpp>
#include <metl/mpmc_queue.hpp>
#include <metl/versioned_handle.hpp>

#if !defined(METL_EXPECT_LOCK_FREE_HANDLE)
#error "define METL_EXPECT_LOCK_FREE_HANDLE=0 or 1 (the CI matrix supplies it)"
#endif

namespace {

struct tag {};
using handle = metl::versioned_handle<tag>;
using pool_handle = metl::handle_pool<int, 8>::handle_type;

// Both the standalone handle and the one a pool issues are 32-bit words, so they
// must agree about the target's capability.
static_assert(metl::has_lock_free_handle_atomic_v<handle> == (METL_EXPECT_LOCK_FREE_HANDLE != 0),
              "metl::atomic_handle capability does not match what the CI matrix "
              "declares for this target — see docs/SCOPE.md §3");
static_assert(metl::has_lock_free_handle_atomic_v<pool_handle> == (METL_EXPECT_LOCK_FREE_HANDLE != 0),
              "a pool handle and a bare handle of the same width must agree");

#if METL_EXPECT_LOCK_FREE_HANDLE

// Instantiate for real where the capability exists: a trait that is true but an
// instantiation that fails to compile would still be a broken claim.
metl::atomic_handle<handle> g_cell;

// mpmc_queue gates on the same capability, through its own static_assert on
// std::atomic<std::size_t>. Instantiating it here proves the two agree — a
// target where the handle atomics were lock-free but size_t's were not would
// otherwise only show up at a user's build.
metl::mpmc_queue<std::uint32_t, 4> g_queue;

static_assert(sizeof(metl::atomic_handle<handle>) == sizeof(handle),
              "the atomic cell must stay a single word");
static_assert(metl::atomic_handle<handle>::is_always_lock_free, "instantiation implies lock-free");

// Force codegen for every operation, so this is not merely a template-parse
// check. Under -Os an unused inline function would vanish; taking its address
// keeps it.
void exercise() noexcept {
  (void)g_queue.try_push(1u);
  std::uint32_t popped = 0;
  (void)g_queue.try_pop(popped);

  g_cell.store(handle{1, 1});
  handle observed = g_cell.load();
  (void)g_cell.exchange(handle{2, 1});
  handle desired{3, 1};
  while (!g_cell.compare_exchange_weak(observed, desired)) {
  }
  (void)g_cell.compare_exchange_strong(observed, desired);
}

}  // namespace

extern "C" void metl_atomic_handle_capability_anchor(void) noexcept;
extern "C" void metl_atomic_handle_capability_anchor(void) noexcept {
  exercise();
}

#else

}  // namespace

// No CAS on this target: atomic_handle must NOT be instantiated here. The
// capability assertions above are the whole test.
extern "C" void metl_atomic_handle_capability_anchor(void) noexcept;
extern "C" void metl_atomic_handle_capability_anchor(void) noexcept {}

#endif
