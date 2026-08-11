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

}  // namespace

int main() {
  // --- the host is documented as having no interrupt masking -------------------
  // Pinned so the fallback path stays honest: if this ever became true on a
  // hosted build, irq_lock would be emitting instructions nobody vetted.
  CHECK(!metl::has_irq_masking);
  CHECK_EQ(METL_HAS_IRQ_MASKING, 0);

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

  return metl_test::exit_code();
}
