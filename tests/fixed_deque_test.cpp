#include <metl/fixed_deque.hpp>

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
  metl::fixed_deque<int, 4> deque;
  if (!deque.empty() || deque.capacity() != 4) {
    return 1;
  }

  if (!deque.try_push_back(2) || !deque.try_push_front(1) || !deque.try_push_back(3)) {
    return 2;
  }

  if (deque.size() != 3 || deque.front() != 1 || deque.back() != 3 || deque[1] != 2 || deque.at(1) != 2) {
    return 3;
  }

  deque.push_front(0);
  if (!deque.full() || deque.front() != 0 || deque.back() != 3) {
    return 4;
  }

  if (deque.try_push_back(4)) {
    return 5;
  }

  deque.pop_front();
  deque.pop_back();
  if (deque.size() != 2 || deque.front() != 1 || deque.back() != 2) {
    return 6;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::fixed_deque<tracker, 3> tracked;
    tracked.emplace_back(10);
    tracked.emplace_front(5);
    tracked.emplace_back(20);

    if (tracked.front().value != 5 || tracked.back().value != 20 || tracked[1].value != 10) {
      return 7;
    }

    tracked.clear();
  }

  return tracker::constructions == tracker::destructions ? 0 : 8;
}
