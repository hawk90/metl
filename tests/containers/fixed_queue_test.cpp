#include <metl/fixed_queue.hpp>

namespace {

struct tracker {
  static int constructions;
  static int destructions;

  tracker() : value(0) { ++constructions; }
  explicit tracker(int input) : value(input) { ++constructions; }
  tracker(const tracker& other) : value(other.value) { ++constructions; }
  tracker(tracker&& other) noexcept : value(other.value) { ++constructions; }
  ~tracker() { ++destructions; }

  tracker& operator=(const tracker& other) {
    value = other.value;
    return *this;
  }

  tracker& operator=(tracker&& other) noexcept {
    value = other.value;
    return *this;
  }

  int value;
};

int tracker::constructions = 0;
int tracker::destructions = 0;

}  // namespace

int main() {
  metl::fixed_queue<int, 3> queue;
  if (!queue.empty() || queue.capacity() != 3) {
    return 1;
  }

  if (!queue.try_push(1) || !queue.try_emplace(2) || !queue.try_push(3)) {
    return 2;
  }

  if (!queue.full() || queue.front() != 1 || queue.back() != 3) {
    return 3;
  }

  if (queue.try_push(4)) {
    return 4;
  }

  queue.pop();
  if (queue.size() != 2 || queue.front() != 2) {
    return 5;
  }

  queue.push(4);
  if (queue.back() != 4 || queue.front() != 2) {
    return 6;
  }

  queue.clear();
  if (!queue.empty()) {
    return 7;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::fixed_queue<tracker, 2> tracked;
    tracked.emplace(10);
    tracked.push(tracker(20));

    if (tracked.front().value != 10 || tracked.back().value != 20) {
      return 8;
    }

    tracked.pop();
    if (tracked.front().value != 20) {
      return 9;
    }
  }

  return tracker::constructions == tracker::destructions ? 0 : 10;
}
