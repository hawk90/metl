#pragma once

/// @file
/// @brief Progress guarantees for `metl::coro::scheduler` (docs/SCOPE.md section 1).
///
///   | Operation | Guarantee |
///   |-----------|-----------|
///   | `try_attach`, `try_attach_protothread`, `try_attach_stepper` | wait-free, bounded by `Capacity` |
///   | `detach`, `is_attached` | wait-free, bounded by `Capacity` |
///   | `run_once` | bounded by the attached tasks and their own poll cost |
///   | `run_until_idle` | bounded by its `max_rounds` argument |
///
/// The task table is a flat array searched linearly, so attaching, detaching and
/// lookup are `Capacity`-bounded rather than constant -- attaching searches first,
/// because a task is attached at most once. The array is small and fixed, which is
/// the trade this type makes for having no free list to corrupt.
///
/// `run_once` visits each attached task once; its bound is therefore the sum of the
/// tasks' own poll functions, which the scheduler cannot bound for you. A task that
/// never returns is a task that never returns. `run_until_idle` takes `max_rounds`
/// for exactly this reason: a set of tasks that keep re-arming each other would
/// otherwise spin, and SCOPE.md section 2 rejects unbounded retry.
///
/// Single-threaded: this scheduler does not synchronise.

// Fixed-capacity round-robin scheduler for `protothread` and `stepper` tasks.
//
// Storage policy: non-owning. The scheduler holds raw pointers; the caller
// keeps tasks alive for as long as they are attached.
//
// Polling policy: each `run_once()` polls every attached task exactly once
// in attachment order. Tasks that report completion (their poll function
// returns `false`) are removed in place. `run_until_idle()` repeatedly calls
// `run_once()` until no task is attached or `max_rounds` is reached.

#include "metl/compiler.hpp"
#include "metl/config.hpp"
#include "metl/coro/stepper.hpp"
#include "metl/detail/addressof.hpp"
#include "metl/fixed_vector.hpp"

#include <cstddef>

namespace metl {
namespace coro {

/// @brief Type-erased poll function. @return true if the task still wants to run.
using poll_fn = bool (*)(void* task) noexcept;

/// @brief One attached task: a type-erased pointer and its poll trampoline.
struct task_slot {
  void* task;    ///< Non-owning pointer to the task object.
  poll_fn poll;  ///< Trampoline that drives one step of the task.
};

/// @brief Fixed-capacity, single-threaded cooperative round-robin scheduler.
///
/// Non-owning: holds raw task pointers; the caller keeps tasks alive while
/// attached. Each `run_once()` polls every attached task exactly once in
/// attachment order and removes completed tasks in place. This is a cooperative
/// run loop, so attached task polls must be non-blocking.
/// @tparam Capacity Maximum number of simultaneously attached tasks.
template <std::size_t Capacity>
class scheduler {
 public:
  using size_type = std::size_t;

  scheduler() noexcept = default;
  scheduler(const scheduler&) = delete;
  scheduler& operator=(const scheduler&) = delete;

  /// @brief Attach a protothread-derived task exposing `bool run() noexcept`.
  /// @param t The task; must outlive its attachment.
  /// @pre The task is not already attached; attaching it twice aborts.
  /// @return true if attached, false if the scheduler is full.
  template <typename Protothread>
  METL_NODISCARD bool try_attach_protothread(Protothread& t) noexcept {
    return try_attach_impl(static_cast<void*>(detail::addressof(t)), &protothread_poll<Protothread>);
  }

  /// @brief Attach a `stepper`-derived task.
  /// @param s The task; must outlive its attachment.
  /// @pre The task is not already attached; attaching it twice aborts.
  /// @return true if attached, false if the scheduler is full.
  METL_NODISCARD bool try_attach_stepper(stepper& s) noexcept {
    return try_attach_impl(static_cast<void*>(&s), &stepper_poll);
  }

  /// @brief Generic attach for any `bool(*)(void*) noexcept` poll function.
  /// @param task Task pointer passed back to `fn`. @param fn The poll trampoline.
  /// @pre The task is not already attached; attaching it twice aborts.
  /// @return true if attached, false if the scheduler is full.
  METL_NODISCARD bool try_attach(void* task, poll_fn fn) noexcept { return try_attach_impl(task, fn); }

  /// @brief Detach a task by pointer (linear search, O(N)).
  /// @param task The task to remove. @return true if it was attached and removed.
  /// @note Safe to call from within a task's own poll (see `run_once`).
  bool detach(void* task) noexcept {
    for (size_type i = 0; i < tasks_.size(); ++i) {
      if (tasks_[i].slot.task == task) {
        tasks_.erase(tasks_.begin() + static_cast<std::ptrdiff_t>(i));
        return true;
      }
    }
    return false;
  }

  /// @brief Whether a task is currently attached (O(N) linear search).
  /// @param task The task to look up. @return true if attached.
  METL_NODISCARD bool is_attached(void* task) const noexcept {
    for (size_type i = 0; i < tasks_.size(); ++i) {
      if (tasks_[i].slot.task == task) {
        return true;
      }
    }
    return false;
  }

  /// @brief Poll every attached task once in attachment order.
  /// @return The number of tasks that yielded (still want to run). Completed
  ///         tasks are removed in place. Task polls may attach/detach reentrantly.
  /// @note An entry is one attachment, not a task address or a (task, poll)
  ///       pair: a task attached during the round waits for the next one, even
  ///       when it is the same object, with the same poll function, as one
  ///       detached earlier in the round. A completed attachment is the one
  ///       removed, so a task that re-attaches itself from the poll in which it
  ///       completes stays attached.
  size_type run_once() noexcept;

  /// @brief Repeatedly `run_once()` until no task is attached or `max_rounds`
  ///        rounds have run.
  /// @note A task attached during a round, such as the next step a completing
  ///       task hands off to, is polled in a later round of the same call.
  /// @param max_rounds Upper bound on rounds, guaranteeing termination.
  /// @return The total number of rounds executed.
  size_type run_until_idle(size_type max_rounds = 1024) noexcept;

  /// @brief Number of currently attached tasks.
  METL_NODISCARD size_type task_count() const noexcept { return tasks_.size(); }
  /// @brief Whether no tasks are attached. @return true if empty.
  METL_NODISCARD bool empty() const noexcept { return tasks_.empty(); }
  /// @brief The maximum task capacity. @return `Capacity`.
  METL_NODISCARD static constexpr size_type capacity() noexcept { return Capacity; }

 private:
  template <typename Protothread>
  static bool protothread_poll(void* p) noexcept {
    auto* task = static_cast<Protothread*>(p);
    // Derived `run()` returns true when done; the scheduler wants
    // "true == still running", so invert.
    return !task->run();
  }

  static bool stepper_poll(void* p) noexcept { return static_cast<stepper*>(p)->poll(); }

  METL_NODISCARD bool try_attach_impl(void* task, poll_fn fn) noexcept {
    // A second entry for the same task outlives detach() and completion, which
    // each remove one, so the scheduler would poll an object the caller has
    // destroyed. A precondition, not "full": checked first, and never stripped.
    METL_HARDEN(!is_attached(task));
    if (tasks_.full()) {
      return false;
    }
    return tasks_.try_push_back(entry{task_slot{task, fn}, next_attachment_++});
  }

  // One attachment. The number tells attachments apart where the task address
  // and poll function cannot: a task detached and attached again, even with
  // the same poll function, is a new entry. It wraps after 2^N attaches; only
  // attachments alive in the same round are compared, and there are at most
  // Capacity of those.
  struct entry {
    task_slot slot;
    size_type attachment;
  };

  // Whether the attachment is still attached.
  bool holds(size_type attachment) const noexcept {
    for (size_type i = 0; i < tasks_.size(); ++i) {
      if (tasks_[i].attachment == attachment) {
        return true;
      }
    }
    return false;
  }

  // Removes the attachment, if still attached.
  void remove(size_type attachment) noexcept {
    for (size_type i = 0; i < tasks_.size(); ++i) {
      if (tasks_[i].attachment == attachment) {
        tasks_.erase(tasks_.begin() + static_cast<std::ptrdiff_t>(i));
        return;
      }
    }
  }

  metl::fixed_vector<entry, Capacity> tasks_;
  size_type next_attachment_ = 0;
};

template <std::size_t Capacity>
typename scheduler<Capacity>::size_type scheduler<Capacity>::run_once() noexcept {
  // Reentrancy contract: a task's poll() may detach() or (try_)attach() tasks
  // — including itself — mid-round. We therefore snapshot the set of tasks
  // attached at entry and poll exactly that set, in attachment order. Iterating
  // the live vector by cached index/size is unsafe: a detach() shifts elements
  // and shrinks the vector, so a cached index would read a stale or
  // out-of-bounds slot.
  entry snapshot[Capacity == 0 ? 1 : Capacity];
  const size_type n = tasks_.size();
  for (size_type i = 0; i < n; ++i) {
    snapshot[i] = tasks_[i];
  }

  size_type still_running = 0;
  for (size_type i = 0; i < n; ++i) {
    const entry e = snapshot[i];
    // A previous poll in this round may have detached this entry, and may have
    // attached something since -- at the same address, even with the same poll
    // function. Only the snapshot's own attachment is still this entry; anything
    // else is a mid-round attachment, which waits for the next round.
    if (!holds(e.attachment)) {
      continue;
    }
    const bool more = e.slot.poll(e.slot.task);
    if (more) {
      ++still_running;
    } else {
      // Completed: remove this attachment. Its poll may have detached it
      // already, or attached the task again; only the one that completed goes.
      remove(e.attachment);
    }
  }
  return still_running;
}

template <std::size_t Capacity>
typename scheduler<Capacity>::size_type scheduler<Capacity>::run_until_idle(size_type max_rounds) noexcept {
  // Not "until a round yields nothing": a task that completes after attaching
  // a successor makes a round that yields nothing and leaves the successor
  // attached, unpolled. Every task still attached after a round is one that
  // wants to run, so the loop ends when none is.
  size_type rounds = 0;
  while (rounds < max_rounds && task_count() > 0) {
    (void)run_once();
    ++rounds;
  }
  return rounds;
}

}  // namespace coro
}  // namespace metl
