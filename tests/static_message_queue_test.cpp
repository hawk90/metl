#include <metl/static_message_queue.hpp>

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
  metl::static_message_queue<int, 3> queue;
  if (!queue.empty() || queue.capacity() != 3) {
    return 1;
  }

  if (!queue.try_push(1) || !queue.try_emplace(2) || !queue.try_push(3)) {
    return 2;
  }

  if (!queue.full() || queue.front() != 1 || queue.try_push(4)) {
    return 3;
  }

  int out = 0;
  if (!queue.try_pop(out) || out != 1 || queue.size() != 2 || queue.front() != 2) {
    return 4;
  }

  if (!queue.try_push(4) || !queue.full()) {
    return 5;
  }

  if (!queue.try_pop(out) || out != 2) {
    return 6;
  }

  if (!queue.try_pop(out) || out != 3) {
    return 7;
  }

  if (!queue.try_pop(out) || out != 4 || !queue.empty()) {
    return 8;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::static_message_queue<tracker, 2> tracked;
    tracked.emplace(10);
    tracked.emplace(20);

    tracker item;
    if (!tracked.try_pop(item) || item.value != 10) {
      return 9;
    }

    tracked.clear();
  }

  if (tracker::constructions != tracker::destructions) {
    return 10;
  }

  return 0;
}
