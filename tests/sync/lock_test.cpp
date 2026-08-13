#include "metl_check.hpp"

#include <cstdint>

#include <metl/fixed_vector.hpp>
#include <metl/lock.hpp>

namespace {

// Instrumented policy: proves guarded/scoped_lock acquire exactly once and always
// release, including when the body returns a value.
struct counting_lock {
  using state_type = int;

  static int locks;
  static int unlocks;
  static int depth;
  static int max_depth;

  static state_type lock() noexcept {
    ++locks;
    ++depth;
    if (depth > max_depth) {
      max_depth = depth;
    }
    return depth;
  }

  static void unlock(state_type state) noexcept {
    ++unlocks;
    // The state handed back must be the one this acquisition produced, which is
    // what makes nesting correct for a policy that saves and restores state.
    if (state == depth) {
      --depth;
    }
  }

  static void reset() noexcept {
    locks = 0;
    unlocks = 0;
    depth = 0;
    max_depth = 0;
  }
};

int counting_lock::locks = 0;
int counting_lock::unlocks = 0;
int counting_lock::depth = 0;
int counting_lock::max_depth = 0;

#if METL_HAS_IRQ_MASKING
// Reads PRIMASK directly, so the check observes the hardware rather than
// re-reading whatever irq_lock happens to return.
std::uint32_t read_primask() noexcept {
  std::uint32_t value = 0;
  __asm__ __volatile__("mrs %0, primask" : "=r"(value)::"memory");
  return value;
}
#endif

}  // namespace

int main() {
  // --- the capability trait matches the target ---------------------------------
  // Both directions are pinned. On a host there are no interrupts to mask, so a
  // true here would mean irq_lock was emitting instructions nobody vetted; on
  // Cortex-M a false would mean the primitive docs/SCOPE.md calls the correct
  // ISR-to-main-loop lock had silently degraded to a compiler barrier.
  //
  // This assertion used to read `CHECK(!metl::has_irq_masking)` unconditionally,
  // which passed on the host and was simply wrong about the target. The QEMU
  // conformance job caught it on its first green run.
#if METL_HAS_IRQ_MASKING
  CHECK(metl::has_irq_masking);
  CHECK_EQ(METL_HAS_IRQ_MASKING, 1);
#else
  CHECK(!metl::has_irq_masking);
  CHECK_EQ(METL_HAS_IRQ_MASKING, 0);
#endif

  // --- lock policies are stateless --------------------------------------------
  // The saved state travels with the guard, so a guarded object costs exactly
  // its value. This is what makes irq_lock free to nest.
  CHECK_EQ(sizeof(metl::guarded<std::uint32_t, metl::null_lock>), sizeof(std::uint32_t));
  CHECK_EQ(sizeof(metl::guarded<std::uint32_t, metl::irq_lock>), sizeof(std::uint32_t));

  // --- null_lock and irq_lock round-trip ---------------------------------------
  {
    const auto null_state = metl::null_lock::lock();
    metl::null_lock::unlock(null_state);

    const auto irq_state = metl::irq_lock::lock();
    metl::irq_lock::unlock(irq_state);
  }

  // --- scoped_lock acquires once and always releases ---------------------------
  {
    counting_lock::reset();
    {
      metl::scoped_lock<counting_lock> guard;
      CHECK_EQ(counting_lock::locks, 1);
      CHECK_EQ(counting_lock::unlocks, 0);
    }
    CHECK_EQ(counting_lock::locks, 1);
    CHECK_EQ(counting_lock::unlocks, 1);
    CHECK_EQ(counting_lock::depth, 0);
  }

  // --- critical sections nest --------------------------------------------------
  // The reason irq_lock saves PRIMASK instead of blanket-enabling: an inner
  // section must not release the outer one.
  {
    counting_lock::reset();
    {
      metl::scoped_lock<counting_lock> outer;
      {
        metl::scoped_lock<counting_lock> inner;
        CHECK_EQ(counting_lock::depth, 2);
      }
      // Inner released, outer still held.
      CHECK_EQ(counting_lock::depth, 1);
      CHECK_EQ(counting_lock::unlocks, 1);
    }
    CHECK_EQ(counting_lock::depth, 0);
    CHECK_EQ(counting_lock::max_depth, 2);
    CHECK_EQ(counting_lock::locks, 2);
    CHECK_EQ(counting_lock::unlocks, 2);
  }

  // --- guarded::with locks exactly once, around the whole body ------------------
  {
    counting_lock::reset();
    metl::guarded<metl::fixed_vector<int, 8>, counting_lock> shared;

    // The compound operation a per-operation lock could never make atomic: the
    // capacity check and the push happen inside one critical section.
    shared.with([](auto& v) {
      if (v.size() < v.capacity()) {
        v.push_back(1);
      }
      if (v.size() < v.capacity()) {
        v.push_back(2);
      }
    });
    CHECK_EQ(counting_lock::locks, 1);
    CHECK_EQ(counting_lock::unlocks, 1);

    // with() forwards the body's return value.
    const std::size_t size = shared.with([](auto& v) { return v.size(); });
    CHECK_EQ(size, std::size_t{2});
    CHECK_EQ(counting_lock::locks, 2);
    CHECK_EQ(counting_lock::unlocks, 2);
    CHECK_EQ(counting_lock::depth, 0);
  }

  // --- const guarded uses the const body ---------------------------------------
  {
    counting_lock::reset();
    const metl::guarded<std::uint32_t, counting_lock> value{7u};
    const std::uint32_t observed = value.with([](const std::uint32_t& v) { return v; });
    CHECK_EQ(observed, std::uint32_t{7});
    CHECK_EQ(counting_lock::locks, 1);
    CHECK_EQ(counting_lock::unlocks, 1);
  }

  // --- the default policy is irq_lock ------------------------------------------
  // The correct default for ISR-to-main-loop sharing on a single-core MCU, which
  // is what METL's examples actually do.
  static_assert(std::is_same<metl::guarded<int>::lock_type, metl::irq_lock>::value,
                "guarded must default to irq_lock");

  // --- a real guarded value works end to end -----------------------------------
  {
    metl::guarded<metl::fixed_vector<int, 4>, metl::irq_lock> shared;
    for (int i = 0; i < 6; ++i) {
      shared.with([i](auto& v) {
        if (v.size() < v.capacity()) {
          v.push_back(i);
        }
      });
    }
    const std::size_t size = shared.with([](const auto& v) { return v.size(); });
    CHECK_EQ(size, std::size_t{4});
  }

  // --- on a Cortex-M, check what irq_lock actually does to PRIMASK -------------
  // Runs only on the real (emulated) target, via the qemu-conformance job.
  //
  // Scope of this check, stated plainly: it proves the PRIMASK *register* moves
  // as intended, including that unlock restores rather than blanket-enables. It
  // does NOT yet prove that an interrupt is actually blocked — for that a real
  // source (SysTick) has to fire and be observed not running, which is the next
  // step. Register state is necessary, not sufficient.
#if METL_HAS_IRQ_MASKING
  {
    // Start from a known state: interrupts enabled.
    __asm__ __volatile__("cpsie i" ::: "memory");
    CHECK_EQ(read_primask(), std::uint32_t{0});

    const auto state = metl::irq_lock::lock();
    CHECK_EQ(read_primask(), std::uint32_t{1});  // masked while held
    metl::irq_lock::unlock(state);
    CHECK_EQ(read_primask(), std::uint32_t{0});  // and released

    // Nesting: the inner release must not lift the outer mask.
    {
      const auto outer = metl::irq_lock::lock();
      CHECK_EQ(read_primask(), std::uint32_t{1});
      {
        const auto inner = metl::irq_lock::lock();
        CHECK_EQ(read_primask(), std::uint32_t{1});
        metl::irq_lock::unlock(inner);
      }
      CHECK_EQ(read_primask(), std::uint32_t{1});  // still held by the outer
      metl::irq_lock::unlock(outer);
      CHECK_EQ(read_primask(), std::uint32_t{0});
    }

    // The case the save/restore design exists for: entering with interrupts
    // ALREADY disabled by the caller must leave them disabled on exit. A
    // blanket `cpsie i` in unlock would silently re-enable them here.
    {
      __asm__ __volatile__("cpsid i" ::: "memory");
      CHECK_EQ(read_primask(), std::uint32_t{1});
      const auto state_when_disabled = metl::irq_lock::lock();
      CHECK_EQ(read_primask(), std::uint32_t{1});
      metl::irq_lock::unlock(state_when_disabled);
      CHECK_EQ(read_primask(), std::uint32_t{1});  // preserved, not clobbered
      __asm__ __volatile__("cpsie i" ::: "memory");
    }

    // guarded<> composes with it: the mask is held for the whole body.
    {
      metl::guarded<std::uint32_t, metl::irq_lock> value{0u};
      const std::uint32_t inside = value.with([](std::uint32_t&) { return read_primask(); });
      CHECK_EQ(inside, std::uint32_t{1});
      CHECK_EQ(read_primask(), std::uint32_t{0});
    }
  }
#endif

  return metl_test::exit_code();
}
