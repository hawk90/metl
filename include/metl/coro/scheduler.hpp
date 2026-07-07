#pragma once

// Fixed-capacity round-robin scheduler for `protothread` and `stepper` tasks.
//
// Storage policy: non-owning. The scheduler holds raw pointers; the caller
// keeps tasks alive for as long as they are attached.
//
// Polling policy: each `run_once()` polls every attached task exactly once
// in attachment order. Tasks that report completion (their poll function
// returns `false`) are removed in place. `run_until_idle()` repeatedly calls
// `run_once()` until either nothing yields or `max_rounds` is reached.

#include "metl/compiler.hpp"
#include "metl/coro/stepper.hpp"
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
  /// @return true if attached, false if the scheduler is full.
  template <typename Protothread>
  METL_NODISCARD bool try_attach_protothread(Protothread& t) noexcept {
    return try_attach_impl(static_cast<void*>(&t), &protothread_poll<Protothread>);
  }

  /// @brief Attach a `stepper`-derived task.
  /// @param s The task; must outlive its attachment.
  /// @return true if attached, false if the scheduler is full.
  METL_NODISCARD bool try_attach_stepper(stepper& s) noexcept {
    return try_attach_impl(static_cast<void*>(&s), &stepper_poll);
  }

  /// @brief Generic attach for any `bool(*)(void*) noexcept` poll function.
  /// @param task Task pointer passed back to `fn`. @param fn The poll trampoline.
  /// @return true if attached, false if the scheduler is full.
  METL_NODISCARD bool try_attach(void* task, poll_fn fn) noexcept { return try_attach_impl(task, fn); }

  /// @brief Detach a task by pointer (linear search, O(N)).
  /// @param task The task to remove. @return true if it was attached and removed.
  /// @note Safe to call from within a task's own poll (see `run_once`).
  bool detach(void* task) noexcept {
    for (size_type i = 0; i < tasks_.size(); ++i) {
      if (tasks_[i].task == task) {
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
      if (tasks_[i].task == task) {
        return true;
      }
    }
    return false;
  }

  /// @brief Poll every attached task once in attachment order.
  /// @return The number of tasks that yielded (still want to run). Completed
  ///         tasks are removed in place. Task polls may attach/detach reentrantly.
  size_type run_once() noexcept;

  /// @brief Repeatedly `run_once()` until no task yields or `max_rounds` hit.
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

  bool try_attach_impl(void* task, poll_fn fn) noexcept {
    if (tasks_.full()) {
      return false;
    }
    return tasks_.try_push_back(task_slot{task, fn});
  }

  metl::fixed_vector<task_slot, Capacity> tasks_;
};

template <std::size_t Capacity>
typename scheduler<Capacity>::size_type scheduler<Capacity>::run_once() noexcept {
  // Reentrancy contract: a task's poll() may detach() or (try_)attach() tasks
  // — including itself — mid-round. We therefore snapshot the set of tasks
  // attached at entry and poll exactly that set, in attachment order. Iterating
  // the live vector by cached index/size is unsafe: a detach() shifts elements
  // and shrinks the vector, so a cached index would read a stale or
  // out-of-bounds slot.
  task_slot snapshot[Capacity == 0 ? 1 : Capacity];
  const size_type n = tasks_.size();
  for (size_type i = 0; i < n; ++i) {
    snapshot[i] = tasks_[i];
  }

  size_type still_running = 0;
  for (size_type i = 0; i < n; ++i) {
    const task_slot s = snapshot[i];
    // A previous poll in this round may already have detached this task; if so,
    // skip it (do not poll a task the caller has removed).
    if (!is_attached(s.task)) {
      continue;
    }
    const bool more = s.poll(s.task);
    if (more) {
      ++still_running;
    } else {
      // Completed: remove it. The task may already be gone if its own poll
      // detached itself; detach() tolerates a missing task.
      detach(s.task);
    }
  }
  return still_running;
}

template <std::size_t Capacity>
typename scheduler<Capacity>::size_type scheduler<Capacity>::run_until_idle(size_type max_rounds) noexcept {
  size_type rounds = 0;
  while (rounds < max_rounds && task_count() > 0) {
    const size_type running = run_once();
    ++rounds;
    if (running == 0) {
      break;
    }
  }
  return rounds;
}

}  // namespace coro
}  // namespace metl
