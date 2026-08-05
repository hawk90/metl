#include <metl/spsc_queue.hpp>

namespace {

struct tracker {
  static int constructions;
  static int destructions;

  tracker() : value(0) { ++constructions; }
  explicit tracker(int input) : value(input) { ++constructions; }
  tracker(const tracker& other) : value(other.value) { ++constructions; }
  tracker(tracker&& other) noexcept : value(other.value) { ++constructions; }

  tracker& operator=(const tracker& other) {
    value = other.value;
    return *this;
  }

  tracker& operator=(tracker&& other) noexcept {
    value = other.value;
    return *this;
  }

  ~tracker() { ++destructions; }

  int value;
};

int tracker::constructions = 0;
int tracker::destructions = 0;

}  // namespace

int main() {
  // Basic push/pop on a small queue.
  metl::spsc_queue<int, 4> queue;
  if (!queue.empty() || queue.full() || queue.size_approx() != 0 || queue.capacity() != 4) {
    return 1;
  }

  if (!queue.try_push(1) || !queue.try_push(2) || !queue.try_push(3) || !queue.try_push(4)) {
    return 2;
  }

  if (!queue.full() || queue.empty() || queue.size_approx() != 4) {
    return 3;
  }

  // Pushing into a full queue must fail.
  if (queue.try_push(5)) {
    return 4;
  }

  // FIFO order on pop.
  int out = -1;
  if (!queue.try_pop(out) || out != 1) {
    return 5;
  }
  if (!queue.try_pop(out) || out != 2) {
    return 6;
  }
  if (!queue.try_pop(out) || out != 3) {
    return 7;
  }
  if (!queue.try_pop(out) || out != 4) {
    return 8;
  }

  // Now empty: pop fails.
  if (queue.try_pop(out)) {
    return 9;
  }
  if (!queue.empty()) {
    return 10;
  }

  // Wrap-around exercise: push/pop many times across the bitmask boundary.
  for (int i = 0; i < 100; ++i) {
    if (!queue.try_push(i)) {
      return 11;
    }
    int popped = -1;
    if (!queue.try_pop(popped) || popped != i) {
      return 12;
    }
  }

  // try_emplace constructs in place. Use an inner scope so all tracker
  // objects (queue + local) are destroyed before checking the balance.
  tracker::constructions = 0;
  tracker::destructions = 0;
  {
    metl::spsc_queue<tracker, 2> tq;
    if (!tq.try_emplace(7)) {
      return 13;
    }
    if (!tq.try_emplace(11)) {
      return 14;
    }
    if (tq.try_emplace(99)) {
      return 15;
    }

    tracker out_t;
    if (!tq.try_pop(out_t) || out_t.value != 7) {
      return 16;
    }
    if (!tq.try_pop(out_t) || out_t.value != 11) {
      return 17;
    }
  }

  // Destructor of remaining elements: push two, do not pop, let queue dtor clean up.
  {
    metl::spsc_queue<tracker, 2> dq;
    if (!dq.try_emplace(1)) {
      return 18;
    }
    if (!dq.try_emplace(2)) {
      return 19;
    }
    // dq goes out of scope here; both elements must be destructed.
  }

  if (tracker::constructions != tracker::destructions) {
    return 20;
  }

  // Move-only push path via try_push(T&&).
  metl::spsc_queue<int, 2> mq;
  int v = 42;
  if (!mq.try_push(static_cast<int&&>(v))) {
    return 21;
  }
  int popped_mq = 0;
  if (!mq.try_pop(popped_mq) || popped_mq != 42) {
    return 22;
  }

  return 0;
}
