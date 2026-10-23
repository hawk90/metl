#include <cstdint>

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

// Records the value it held when destroyed: a pool that wrote its free-list
// link into the slot before ~T() ran would make it report the link instead.
struct last_words {
  static std::uint32_t heard;

  explicit last_words(std::uint32_t input) : value(input) {}
  ~last_words() { heard = value; }

  std::uint32_t value;
};

std::uint32_t last_words::heard = 0;

// The free-list order: the slot freed last is reused first, and only an
// empty free list takes a slot never used.
int reuse_order() {
  metl::object_pool<int, 3> pool;
  int* a = pool.try_emplace(1);
  if (a == nullptr || !pool.destroy(a) || pool.try_emplace(2) != a) {
    return 11;
  }
  int* b = pool.try_emplace(3);
  int* c = pool.try_emplace(4);
  if (b == nullptr || c == nullptr || b == a || c == a || c == b || !pool.full()) {
    return 12;
  }
  if (!pool.destroy(a) || !pool.destroy(c) || pool.try_emplace(5) != c || pool.try_emplace(6) != a) {
    return 13;
  }
  return pool.try_emplace(7) == nullptr ? 0 : 14;
}

int link_after_destructor() {
  metl::object_pool<last_words, 4> pool;
  last_words* first = pool.try_emplace(0xA5A5A5A5u);
  last_words* second = pool.try_emplace(0x5A5A5A5Au);
  if (!pool.destroy(first) || last_words::heard != 0xA5A5A5A5u) {
    return 15;
  }
  if (!pool.destroy(second) || last_words::heard != 0x5A5A5A5Au) {
    return 16;
  }
  return 0;
}

// A one-byte T with more slots than a byte can count: each slot widens to
// hold a 16-bit link, and every slot is still reachable and distinct.
int byte_sized_wide_links() {
  static metl::object_pool<std::uint8_t, 300> pool;
  std::uint8_t* slots[300] = {};
  for (int round = 0; round < 2; ++round) {
    for (int i = 0; i < 300; ++i) {
      slots[i] = pool.try_emplace(static_cast<std::uint8_t>(i));
      if (slots[i] == nullptr) {
        return 17;
      }
    }
    if (!pool.full() || pool.try_emplace(std::uint8_t{0}) != nullptr) {
      return 18;
    }
    for (int i = 0; i < 300; ++i) {
      if (*slots[i] != static_cast<std::uint8_t>(i)) {
        return 19;
      }
    }
    for (int i = 0; i < 300; i += 2) {  // free in two interleaved passes
      if (!pool.destroy(slots[i])) {
        return 20;
      }
    }
    for (int i = 1; i < 300; i += 2) {
      if (!pool.destroy(slots[i])) {
        return 20;
      }
    }
  }
  return pool.empty() ? 0 : 21;
}

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

  if (tracker::constructions != tracker::destructions) {
    return 10;
  }

  if (const int failed = reuse_order()) {
    return failed;
  }
  if (const int failed = link_after_destructor()) {
    return failed;
  }
  return byte_sized_wide_links();
}
