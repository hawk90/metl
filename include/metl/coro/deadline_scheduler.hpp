#pragma once

// Fixed-capacity deadline-ordered scheduler, the earliest deadline first.
//
// The companion to `coro::scheduler`, which is round-robin: it polls every
// attached task on every pass, so a task that only wants to run in 500 ms is
// still visited on every loop iteration and has to check the clock itself.
// This one keeps tasks in a `fixed_priority_queue` ordered by deadline and polls
// only the ones that are due, which is what a timer/deadline workload actually
// wants.
//
// Storage policy: non-owning, same as `coro::scheduler`. The scheduler holds raw
// task pointers; the caller keeps tasks alive while they are scheduled.
//
// THE TICK MUST NOT WRAP. `Tick` is compared with plain `<`. Hardware timers are
// usually 16- or 32-bit counters that roll over, and the tempting fix is to
// compare signed differences (`(int32_t)(a - b) < 0`). That comparison is NOT a
// strict weak ordering over the full range -- it loses transitivity once the
// spread exceeds half a period -- and a heap requires one, so the queue would
// silently order wrongly rather than fail. Widen the counter before scheduling
// instead: accumulate into a 32- or 64-bit software tick in the overflow ISR and
// pass that. Deliberately not hidden inside this header, because the widening has
// to happen where the overflow is observed.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/fixed_priority_queue.hpp"
#include "metl/optional.hpp"

#include <cstddef>
#include <cstdint>

namespace metl {
namespace coro {

/// @brief Type-erased deadline poll function.
/// @param task The task pointer given to `schedule`.
/// @param now The tick `run_due` was called with.
/// @return The next deadline to run at, or `nullopt` when the task is finished
///         and should not be rescheduled.
template <typename Tick>
using deadline_poll_fn = optional<Tick> (*)(void* task, Tick now) noexcept;

/// @brief Fixed-capacity, single-threaded scheduler that runs tasks in deadline
///        order (earliest first).
///
/// **Progress guarantee (SCOPE.md I3): bounded.** `schedule` and `cancel` are
/// O(log Capacity) and O(Capacity) respectively, both compile-time bounded.
/// `run_due` is bounded by its `max_dispatches` argument, which exists precisely
/// so that a task re-arming at a deadline that is already due cannot spin
/// forever.
///
/// @tparam Capacity Maximum number of simultaneously scheduled tasks.
/// @tparam Tick Monotonic, non-wrapping time unit. See the header note.
template <std::size_t Capacity, typename Tick = std::uint32_t>
class deadline_scheduler {
 public:
  using size_type = std::size_t;
  using tick_type = Tick;
  using poll_type = deadline_poll_fn<Tick>;

  /// One scheduled task: when it wants to run, and how to run it.
  struct entry {
    Tick deadline;   ///< Tick at or after which the task should be polled.
    void* task;      ///< Non-owning pointer to the task object.
    poll_type poll;  ///< Trampoline that runs one step and reports the next deadline.
  };

  deadline_scheduler() noexcept = default;
  deadline_scheduler(const deadline_scheduler&) = delete;
  deadline_scheduler& operator=(const deadline_scheduler&) = delete;
  // Stated rather than left implicit: deleting the copy operations already
  // suppresses the moves, but saying so is what makes "this type does not
  // relocate" a decision a reader can see.
  deadline_scheduler(deadline_scheduler&&) = delete;
  deadline_scheduler& operator=(deadline_scheduler&&) = delete;

  /// @brief Schedule @p task to be polled at or after @p deadline.
  /// @param task The task; must outlive its scheduling. @param fn Its trampoline.
  /// @param deadline Tick to run at.
  /// @return true if scheduled, false if the scheduler is full.
  /// @note While a poll is running, one slot is held back for that task's own
  ///       re-arm, so this can report full one slot early. See `run_due`.
  METL_NODISCARD bool try_schedule(void* task, poll_type fn, Tick deadline) {
    if (queue_.size() + reserved() >= Capacity) {
      return false;
    }
    return queue_.try_push(entry{deadline, task, fn});
  }

  /// @brief Schedule @p task to be polled at or after @p deadline.
  /// @pre The scheduler is not full; a full scheduler asserts and aborts. Use
  ///      `try_schedule` where "full" is a recoverable outcome.
  void schedule(void* task, poll_type fn, Tick deadline) {
    const bool scheduled = try_schedule(task, fn, deadline);
    METL_ASSERT(scheduled);
    (void)scheduled;
  }

  /// @brief Cancel every scheduling of @p task.
  /// @return true if at least one was removed; false if it was not scheduled.
  /// @note Plain name and discardable: the boolean answers "was it there", it does
  ///       not report a failure (SCOPE.md section 9, R4).
  bool cancel(void* task) noexcept {
    return queue_.erase_if([task](const entry& e) noexcept { return e.task == task; }) != 0;
  }

  /// @brief Whether @p task has at least one pending deadline. O(Capacity).
  METL_NODISCARD bool is_scheduled(void* task) const noexcept {
    const span<const entry> pending = queue_.as_span();
    for (size_type i = 0; i < pending.size(); ++i) {
      if (pending[i].task == task) {
        return true;
      }
    }
    return false;
  }

  /// @brief The earliest pending deadline, or `nullopt` if nothing is scheduled.
  /// @note This is the number an idle loop wants: sleep until it, then `run_due`.
  METL_NODISCARD optional<Tick> next_deadline() const noexcept {
    if (queue_.empty()) {
      return nullopt;
    }
    return queue_.top().deadline;
  }

  /// @brief Poll every task whose deadline is at or before @p now, earliest first.
  /// @param now The current tick.
  /// @param max_dispatches Upper bound on polls in this call, guaranteeing
  ///        termination. A task that re-arms at a deadline <= `now` is asking to
  ///        run again within this same call, which is legitimate for a catch-up
  ///        timer but must not be unbounded.
  /// @return The number of polls performed.
  ///
  /// **Reentrancy contract.** A task's poll may `try_schedule` and `cancel`,
  /// including itself. Its own entry has already been popped before the poll runs,
  /// so cancelling itself from inside its poll affects only entries it added; to
  /// stop after this run, return `nullopt` instead. One slot is reserved for the
  /// running task's re-arm while its poll is on the stack, so a poll that fills the
  /// scheduler cannot make the re-arm fail — `try_schedule` reports full one slot
  /// early instead, which is the recoverable half of the pair.
  size_type run_due(Tick now, size_type max_dispatches = 1024) {
    size_type dispatched = 0;
    while (dispatched < max_dispatches && !queue_.empty() && !(now < queue_.top().deadline)) {
      const entry due = queue_.top();
      queue_.pop();
      ++dispatched;

      in_poll_ = true;
      const optional<Tick> next = due.poll(due.task, now);
      in_poll_ = false;

      if (next.has_value()) {
        // Guaranteed to fit: we popped one slot above and held it reserved for
        // exactly this push while the poll was running.
        queue_.push(entry{*next, due.task, due.poll});
      }
    }
    return dispatched;
  }

  /// @brief Number of pending scheduled entries.
  METL_NODISCARD size_type task_count() const noexcept { return queue_.size(); }
  /// @brief Whether nothing is scheduled.
  METL_NODISCARD bool empty() const noexcept { return queue_.empty(); }
  /// @brief The maximum number of simultaneously scheduled tasks.
  METL_NODISCARD static constexpr size_type capacity() noexcept { return Capacity; }
  /// @brief Cancel everything.
  void clear() noexcept { queue_.clear(); }

 private:
  /// Orders the heap so that the EARLIEST deadline is on top. `fixed_priority_queue`
  /// is a max-heap: `comp(a, b) == true` means "a comes out after b", so comparing
  /// with `>` puts the smallest deadline first.
  struct later_deadline {
    bool operator()(const entry& lhs, const entry& rhs) const noexcept { return lhs.deadline > rhs.deadline; }
  };

  /// One slot held back while a poll is on the stack, for that task's re-arm.
  size_type reserved() const noexcept { return in_poll_ ? size_type{1} : size_type{0}; }

  fixed_priority_queue<entry, Capacity, later_deadline> queue_;
  bool in_poll_ = false;
};

}  // namespace coro
}  // namespace metl
