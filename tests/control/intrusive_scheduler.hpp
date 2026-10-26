#pragma once

// An intrusive variant of metl::coro::scheduler, built beside it to be measured
// against it (#337, roadmap #477). Not a library type: switching the real
// scheduler would drop its Capacity parameter, which is a 2.0.0 decision.
//
// The contract is scheduler's, and the tests that pin it run against both
// (tests/control/scheduler_contract.hpp). What differs is where an attachment
// lives:
//
//   * scheduler<Capacity> stores one entry per attachment in a Capacity-sized
//     array, numbers attachments to tell them apart, and copies the array into
//     a snapshot on the stack for every round. Attaching searches for a free
//     number, O(Capacity) times O(Capacity); detaching searches, O(Capacity).
//
//   * intrusive_scheduler stores nothing. The caller owns a task_node, usually
//     as a base of the task, and attaching links it: O(1). Detaching unlinks
//     it: O(1). There is no capacity, and no attachment number: the node is
//     the attachment.
//
// A round needs no snapshot. run_once links two markers into the list, a
// cursor before the first task and an end after the last. Each step moves the
// cursor past the next node and polls it, so
//   * a task attached during the round is linked after the end marker and
//     waits for the next round;
//   * a task detached during the round is unlinked, and the cursor never
//     reaches it;
//   * a round nested inside a poll links its own markers and skips the outer
//     round's.
// After a poll that reports completion, the attachment that completed is still
// attached exactly when its node is still right before the cursor: a node
// detached in the poll is gone from there, and attached again it is after the
// end marker. That comparison reads only the cursor, so a task that detached
// itself and was then destroyed is never read again.
//
// The hook adds the check the array cannot make: a task destroyed while it is
// attached stops the program (METL_HARDEN in list_hook's destructor), where
// scheduler<Capacity> would later poll the dead object.

#include <cstddef>
#include <type_traits>

#include <metl/config.hpp>
#include <metl/coro/scheduler.hpp>
#include <metl/coro/stepper.hpp>
#include <metl/intrusive_list.hpp>

namespace metl_test {

class intrusive_scheduler;

/// The storage of one attachment: a base of the task, owned by the caller.
class task_node : public metl::list_hook<> {
 public:
  task_node() noexcept = default;
  // A copy is a different task, not attached: like the hook, the slot is not
  // copied, and assignment keeps the target's own.
  task_node(const task_node&) noexcept : metl::list_hook<>() {}
  // NOLINTNEXTLINE(cert-oop54-cpp)
  task_node& operator=(const task_node&) noexcept { return *this; }
  ~task_node() = default;

 private:
  friend class intrusive_scheduler;
  metl::coro::task_slot slot_{nullptr, nullptr};  // poll == nullptr: a round's marker
};

class intrusive_scheduler {
 public:
  using size_type = std::size_t;

  intrusive_scheduler() noexcept = default;
  intrusive_scheduler(const intrusive_scheduler&) = delete;
  intrusive_scheduler& operator=(const intrusive_scheduler&) = delete;

  /// Attaches a protothread-derived task that is also a task_node.
  template <typename Protothread>
  void attach_protothread(Protothread& t) noexcept {
    static_assert(std::is_base_of_v<task_node, Protothread>, "the task must derive from task_node");
    attach(t, static_cast<void*>(&t), &protothread_poll<Protothread>);
  }

  /// Attaches a stepper-derived task that is also a task_node.
  template <typename Stepper>
  void attach_stepper(Stepper& s) noexcept {
    static_assert(std::is_base_of_v<task_node, Stepper>, "the task must derive from task_node");
    static_assert(std::is_base_of_v<metl::coro::stepper, Stepper>, "the task must derive from stepper");
    attach(s, static_cast<void*>(static_cast<metl::coro::stepper*>(&s)), &stepper_poll);
  }

  /// Attaches `node`, polling `fn(task)`. O(1); never full.
  /// @pre `node` is not attached: attaching it twice stops the program.
  void attach(task_node& node, void* task, metl::coro::poll_fn fn) noexcept {
    // Before the slot is written: it belongs to the attachment still live.
    METL_HARDEN(!node.is_linked());
    METL_HARDEN(fn != nullptr);  // a null poll would read as a marker
    node.slot_ = metl::coro::task_slot{task, fn};
    tasks_.push_back(node);
  }

  /// Detaches `node`. O(1). @return true if it was attached.
  /// @pre `node` is attached to this scheduler, if to any.
  bool detach(task_node& node) noexcept {
    if (!node.is_linked()) {
      return false;
    }
    tasks_.erase(node);
    return true;
  }

  bool is_attached(const task_node& node) const noexcept { return node.is_linked(); }

  size_type task_count() const noexcept { return tasks_.size() - (2 * rounds_); }
  bool empty() const noexcept { return task_count() == 0; }

  /// Polls every task attached when the round starts once, in attachment
  /// order; removes the attachments that complete. @return the number that
  /// still want to run.
  size_type run_once() noexcept;

  /// As scheduler::run_until_idle.
  size_type run_until_idle(size_type max_rounds = 1024) noexcept {
    size_type rounds = 0;
    while (rounds < max_rounds && task_count() > 0) {
      (void)run_once();
      ++rounds;
    }
    return rounds;
  }

 private:
  using list_type = metl::intrusive_list<task_node, void>;

  template <typename Protothread>
  static bool protothread_poll(void* p) noexcept {
    return !static_cast<Protothread*>(p)->run();
  }

  static bool stepper_poll(void* p) noexcept { return static_cast<metl::coro::stepper*>(p)->poll(); }

  list_type tasks_;
  size_type rounds_ = 0;  // rounds running, each with two markers linked
};

inline intrusive_scheduler::size_type intrusive_scheduler::run_once() noexcept {
  task_node cursor;
  task_node end;
  auto at = tasks_.insert(tasks_.begin(), cursor);
  tasks_.push_back(end);
  ++rounds_;

  size_type still_running = 0;
  for (;;) {
    auto next = at;
    ++next;
    task_node& node = *next;
    if (&node == &end) {
      break;
    }
    // Move the cursor past the node before polling it. Nothing but this
    // round moves or unlinks the cursor, so `at` stays valid whatever the
    // poll does.
    auto after = next;
    ++after;
    tasks_.splice(after, tasks_, at);
    const metl::coro::task_slot slot = node.slot_;
    if (slot.poll == nullptr) {
      continue;  // an outer round's marker
    }
    const task_node* const polled = &node;
    if (slot.poll(slot.task)) {
      ++still_running;
      continue;
    }
    // Completed. `polled` is compared, never dereferenced: the poll may have
    // destroyed the task. What is read is the node before the cursor, which is
    // linked, so alive.
    auto before = at;
    --before;
    if (&*before == polled) {
      tasks_.erase(before);
    }
  }

  tasks_.erase(cursor);
  tasks_.erase(end);
  --rounds_;
  return still_running;
}

}  // namespace metl_test
