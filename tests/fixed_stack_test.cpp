#include <metl/fixed_stack.hpp>

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
  metl::fixed_stack<int, 3> stack;
  if (!stack.empty() || stack.capacity() != 3) {
    return 1;
  }

  if (!stack.try_push(1) || !stack.try_emplace(2) || !stack.try_push(3)) {
    return 2;
  }

  if (!stack.full() || stack.top() != 3) {
    return 3;
  }

  if (stack.try_push(4)) {
    return 4;
  }

  stack.pop();
  if (stack.size() != 2 || stack.top() != 2) {
    return 5;
  }

  stack.push(4);
  if (stack.top() != 4) {
    return 6;
  }

  stack.clear();
  if (!stack.empty()) {
    return 7;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::fixed_stack<tracker, 2> tracked;
    tracked.emplace(10);
    tracked.push(tracker(20));

    if (tracked.top().value != 20) {
      return 8;
    }

    tracked.pop();
    if (tracked.top().value != 10) {
      return 9;
    }
  }

  return tracker::constructions == tracker::destructions ? 0 : 10;
}
