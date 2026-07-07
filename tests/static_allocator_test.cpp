#include <metl/static_allocator.hpp>

namespace {

struct pair_value {
  pair_value(int a, int b) : lhs(a), rhs(b) {}

  int lhs;
  int rhs;
};

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
  metl::static_allocator<pair_value, 64> allocator;
  if (!allocator.empty() || allocator.capacity_bytes() != 64) {
    return 1;
  }

  pair_value* first = allocator.try_new(1, 2);
  if (first == nullptr || first->lhs != 1 || first->rhs != 2) {
    return 2;
  }

  pair_value* block = allocator.try_allocate(2);
  if (block == nullptr) {
    return 3;
  }

  allocator.construct(block, 3, 4);
  allocator.construct(block + 1, 5, 6);
  if (block[0].lhs != 3 || block[1].rhs != 6) {
    return 4;
  }

  allocator.destroy(block);
  allocator.destroy(block + 1);

  const std::size_t used_before_reset = allocator.used_bytes();
  if (used_before_reset == 0 || allocator.remaining_bytes() >= 64) {
    return 5;
  }

  allocator.reset();
  if (!allocator.empty() || allocator.used_bytes() != 0 || allocator.remaining_bytes() != 64) {
    return 6;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  metl::static_allocator<tracker, 32> tracked_allocator;
  tracker* tracked = tracked_allocator.create(9);
  if (tracked == nullptr || tracked->value != 9) {
    return 7;
  }

  tracked_allocator.destroy(tracked);
  if (tracker::constructions != tracker::destructions) {
    return 8;
  }

  if (tracked_allocator.try_allocate(64) != nullptr) {
    return 9;
  }

  return 0;
}
