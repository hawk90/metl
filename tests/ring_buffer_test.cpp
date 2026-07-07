#include <metl/ring_buffer.hpp>

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
  metl::ring_buffer<int, 3> buffer;
  if (!buffer.empty() || buffer.capacity() != 3) {
    return 1;
  }

  if (!buffer.try_push_back(1) || !buffer.try_emplace_back(2) || !buffer.try_push_back(3)) {
    return 2;
  }

  if (!buffer.full() || buffer.front() != 1 || buffer.back() != 3 || buffer[1] != 2 || buffer.at(1) != 2) {
    return 3;
  }

  if (buffer.try_push_back(4)) {
    return 4;
  }

  buffer.pop_front();
  if (buffer.size() != 2 || buffer.front() != 2) {
    return 5;
  }

  buffer.push_overwrite(4);
  if (buffer.size() != 3 || buffer.front() != 2 || buffer.back() != 4) {
    return 6;
  }

  buffer.push_overwrite(5);
  if (buffer.front() != 3 || buffer[1] != 4 || buffer.back() != 5) {
    return 7;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::ring_buffer<tracker, 2> tracked;
    tracked.emplace_back(10);
    tracked.push_overwrite(20);
    tracked.push_overwrite(30);

    if (tracked.size() != 2 || tracked.front().value != 20 || tracked.back().value != 30) {
      return 8;
    }

    tracked.clear();
  }

  return tracker::constructions == tracker::destructions ? 0 : 9;
}
