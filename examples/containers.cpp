// containers.cpp
//
// Typical usage of the three most common METL containers, all backed by
// inline fixed-capacity storage (no heap, no exceptions):
//
//   - metl::fixed_vector  — a dynamic array with a compile-time cap.
//   - metl::flat_map      — a sorted key/value store (binary search lookup).
//   - metl::ring_buffer   — a circular buffer / bounded FIFO.
//
// IMPORTANT flat_map contract (differs from std::map!):
//   flat_map::operator[] and flat_map::at() take an *integer position*, NOT a
//   key. They are positional accessors over the sorted storage, like a vector.
//   To look a value up *by key* use find() / contains() / try_emplace().
//   Using operator[](key) the way you would with std::map is a compile error
//   for non-integral keys and a silent position lookup for integral keys, so
//   always reach for find()/try_emplace() when you mean "by key".
//
// Everything here is main()-returning-0 with self-checks; a non-zero return
// means an invariant broke.

#include <cstdint>
#include <cstdio>

#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/ring_buffer.hpp>

namespace {

// ---- fixed_vector: a bounded dynamic array ---------------------------------
int demo_fixed_vector() {
  metl::fixed_vector<int, 8> v;  // capacity 8, currently empty

  // push_back asserts on overflow; try_push_back returns false instead.
  v.push_back(10);
  v.push_back(20);
  if (!v.try_push_back(30)) {
    return 1;
  }

  // Random access and iteration behave like std::vector.
  int sum = 0;
  for (int x : v) {
    sum += x;
  }
  if (sum != 60 || v.size() != 3 || v[1] != 20) {
    return 2;
  }

  v.pop_back();  // drop the 30
  if (v.size() != 2 || v.back() != 20) {
    return 3;
  }
  return 0;
}

// ---- flat_map: sorted associative store, lookup BY KEY via find ------------
int demo_flat_map() {
  // Map sensor-channel id -> last reading. Kept sorted by key internally.
  metl::flat_map<std::uint8_t, std::int32_t, 16> readings;

  // try_emplace inserts and returns false on a duplicate key or when full.
  readings.try_emplace(std::uint8_t{3}, 300);
  readings.try_emplace(std::uint8_t{1}, 100);
  readings.try_emplace(std::uint8_t{2}, 200);
  if (readings.try_emplace(std::uint8_t{1}, 999)) {
    return 10;  // duplicate key must be rejected
  }

  // Key lookup: use find() (returns mapped_type* or nullptr) or contains().
  const std::int32_t* r2 = readings.find(std::uint8_t{2});
  if (r2 == nullptr || *r2 != 200) {
    return 11;
  }
  if (readings.contains(std::uint8_t{5})) {
    return 12;  // never inserted
  }

  // insert_or_assign updates an existing key in place.
  readings.insert_or_assign(std::uint8_t{2}, 250);
  if (*readings.find(std::uint8_t{2}) != 250) {
    return 13;
  }

  // Iteration walks entries in ascending key order.
  std::uint8_t prev_key = 0;
  for (const auto& kv : readings) {
    if (kv.key < prev_key) {
      return 14;  // storage is not sorted -> contract broken
    }
    prev_key = kv.key;
  }

  // POSITIONAL access: operator[](i) / at(i) index the sorted storage. The
  // first (lowest-key) entry is key 1. This is NOT a key lookup!
  if (readings[0].key != 1 || readings.at(0).value != 100) {
    return 15;
  }

  readings.erase(std::uint8_t{1});  // erase BY KEY
  if (readings.contains(std::uint8_t{1}) || readings.size() != 2) {
    return 16;
  }
  return 0;
}

// ---- ring_buffer: bounded FIFO with overwrite-on-full ----------------------
int demo_ring_buffer() {
  metl::ring_buffer<int, 4> rb;  // holds up to 4 elements

  // try_push_back refuses to overwrite when full...
  for (int i = 0; i < 4; ++i) {
    if (!rb.try_push_back(i)) {
      return 20;
    }
  }
  if (rb.try_push_back(99)) {
    return 21;  // full -> must fail
  }

  // ...whereas push_overwrite drops the oldest element to make room. This is
  // the "keep the newest N samples" pattern common in signal windows.
  rb.push_overwrite(4);  // evicts 0; buffer now holds {1,2,3,4}
  if (rb.size() != 4 || rb.front() != 1 || rb.back() != 4) {
    return 22;
  }

  // Consume from the front like a queue.
  rb.pop_front();  // removes 1
  if (rb.front() != 2 || rb[0] != 2) {
    return 23;
  }
  return 0;
}

}  // namespace

int main() {
  if (int rc = demo_fixed_vector()) {
    std::fprintf(stderr, "fixed_vector demo failed: %d\n", rc);
    return rc;
  }
  if (int rc = demo_flat_map()) {
    std::fprintf(stderr, "flat_map demo failed: %d\n", rc);
    return rc;
  }
  if (int rc = demo_ring_buffer()) {
    std::fprintf(stderr, "ring_buffer demo failed: %d\n", rc);
    return rc;
  }

  std::printf("containers: fixed_vector + flat_map + ring_buffer OK\n");
  return 0;
}
