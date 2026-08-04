#include <metl/object_pool.hpp>

namespace {

struct tracker {
  static int constructions;
  static int destructions;

  tracker() : value(0) { ++constructions; }
  explicit tracker(int input) : value(input) { ++constructions; }
  ~tracker() { ++destructions; }

  int value;
};

int tracker::constructions = 0;
int tracker::destructions = 0;

}  // namespace

int main() {
  tracker::constructions = 0;
  tracker::destructions = 0;

  metl::object_pool<tracker, 2> pool;
  if (!pool.empty() || pool.capacity() != 2 || pool.available() != 2) {
    return 1;
  }

  tracker* first = pool.try_emplace(10);
  tracker* second = pool.try_emplace(20);
  if (first == nullptr || second == nullptr || first == second) {
    return 2;
  }

  if (first->value != 10 || second->value != 20 || !pool.full() || pool.size() != 2) {
    return 3;
  }

  if (pool.try_emplace(30) != nullptr) {
    return 4;
  }

  if (!pool.contains(first) || !pool.contains(second) || pool.contains(nullptr)) {
    return 5;
  }

  if (!pool.destroy(first) || pool.contains(first) || pool.available() != 1) {
    return 6;
  }

  tracker* third = pool.try_emplace(30);
  if (third == nullptr || third->value != 30 || !pool.full()) {
    return 7;
  }

  int local = 0;
  if (pool.destroy(reinterpret_cast<tracker*>(&local))) {
    return 8;
  }

  pool.clear();
  if (!pool.empty() || pool.available() != 2) {
    return 9;
  }

  return tracker::constructions == tracker::destructions ? 0 : 10;
}
