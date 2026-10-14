#pragma once

// Checks that a recorded history of bounded-FIFO-queue operations is
// linearizable (#341): that some sequential order of the operations, one that
// respects real time, explains every result.
//
// Each operation carries the time it was invoked and the time it returned, as
// positions in one total order (the test takes them from a seq_cst counter).
// Operation a precedes b in real time when a returned before b was invoked;
// overlapping operations may be ordered either way.
//
// The search is Wing and Gong's: repeatedly pick an operation that no
// remaining one precedes, apply it to a sequential bounded FIFO, and backtrack
// when the result disagrees. Searched states (which operations are done, what
// the queue holds) are remembered, as Lowe and Porcupine do, so a history of a
// few dozen operations is checked exhaustively.
//
// What a failed try_ means differs by queue:
//   strict   a failed push means the queue was full at its linearization
//            point, a failed pop that it was empty (spsc_queue);
//   relaxed  a failure may happen at any time and says nothing, so only the
//            successful operations have to line up (mpmc_queue, whose header
//            documents spurious failures while another thread is mid-call).

#include <cstddef>
#include <cstdint>
#include <set>
#include <utility>
#include <vector>

namespace metl_test {

struct queue_op {
  bool is_push = false;
  int value = 0;  // pushed, or popped when ok
  bool ok = false;
  std::uint64_t call = 0;
  std::uint64_t ret = 0;
};

enum class failures { strict, relaxed };

class fifo_history_checker {
 public:
  fifo_history_checker(const std::vector<queue_op>& history, std::size_t capacity, failures mode)
      : capacity_(capacity), strict_(mode == failures::strict) {
    for (const queue_op& op : history) {
      if (op.ok || strict_) {
        ops_.push_back(op);
      }
    }
  }

  // Linearizable histories have at most 64 operations here, which is what keeps
  // the search exhaustive; a longer one is refused rather than half-checked.
  bool too_long() const { return ops_.size() > 64; }

  bool linearizable() {
    if (too_long()) {
      return false;
    }
    std::vector<int> queue;
    return search(0, queue);
  }

 private:
  bool search(std::uint64_t done, std::vector<int>& queue) {
    const std::uint64_t all = ops_.size() == 64 ? ~std::uint64_t{0} : (std::uint64_t{1} << ops_.size()) - 1;
    if (done == all) {
      return true;
    }
    if (!seen_.insert({done, queue}).second) {
      return false;  // already searched from here, and it failed
    }
    std::uint64_t earliest_return = ~std::uint64_t{0};
    for (std::size_t i = 0; i < ops_.size(); ++i) {
      if ((done >> i & 1) == 0 && ops_[i].ret < earliest_return) {
        earliest_return = ops_[i].ret;
      }
    }
    for (std::size_t i = 0; i < ops_.size(); ++i) {
      const queue_op& op = ops_[i];
      // A candidate: invoked before every remaining operation had returned.
      if ((done >> i & 1) != 0 || op.call > earliest_return) {
        continue;
      }
      std::vector<int> next = queue;
      if (!apply(op, next)) {
        continue;
      }
      if (search(done | (std::uint64_t{1} << i), next)) {
        return true;
      }
    }
    return false;
  }

  bool apply(const queue_op& op, std::vector<int>& queue) const {
    if (op.is_push) {
      if (!op.ok) {
        return queue.size() == capacity_;  // strict only: relaxed failures are dropped
      }
      if (queue.size() == capacity_) {
        return false;
      }
      queue.push_back(op.value);
      return true;
    }
    if (!op.ok) {
      return queue.empty();
    }
    if (queue.empty() || queue.front() != op.value) {
      return false;
    }
    queue.erase(queue.begin());
    return true;
  }

  std::vector<queue_op> ops_;
  std::size_t capacity_;
  bool strict_;
  std::set<std::pair<std::uint64_t, std::vector<int>>> seen_;
};

inline bool linearizable(const std::vector<queue_op>& history, std::size_t capacity, failures mode) {
  fifo_history_checker checker(history, capacity, mode);
  return checker.linearizable();
}

}  // namespace metl_test
