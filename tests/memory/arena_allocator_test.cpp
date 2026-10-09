#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/arena_allocator.hpp>

namespace {

struct tracker {
  static int constructions;
  static int destructions;

  tracker(int a, int b) : lhs(a), rhs(b) { ++constructions; }
  tracker(const tracker& other) : lhs(other.lhs), rhs(other.rhs) { ++constructions; }
  tracker& operator=(const tracker& other) {
    lhs = other.lhs;
    rhs = other.rhs;
    return *this;  // no object created or destroyed: counters untouched
  }
  ~tracker() { ++destructions; }

  int lhs;
  int rhs;
};

struct alignas(8) aligned_value {
  aligned_value(std::uint32_t a, std::uint32_t b) : first(a), second(b) {}

  std::uint32_t first;
  std::uint32_t second;
};

int tracker::constructions = 0;
int tracker::destructions = 0;

// The per-allocation bookkeeping record: the previous offset, the payload
// offset and a destroy hook (arena_allocator.hpp, allocation_record). Mirrored
// here, not measured through used(), so a miscounted offset cannot calibrate
// the expectation it is checked against.
struct record_mirror {
  std::size_t previous_offset;
  std::size_t payload_offset;
  void (*destroy)(void*) noexcept;
};

constexpr std::size_t record_size = sizeof(record_mirror);

}  // namespace

int main() {
  metl::arena_allocator<256> arena;
  if (!arena.empty() || arena.capacity() != 256 || arena.used() != 0) {
    return 1;
  }

  void* raw = arena.allocate(3, 1);
  if (raw == nullptr || arena.used() == 0) {
    return 2;
  }

  const auto first_mark = arena.mark();
  aligned_value* aligned = arena.try_emplace<aligned_value>(1u, 2u);
  if (aligned == nullptr || (reinterpret_cast<std::uintptr_t>(aligned) % alignof(aligned_value)) != 0u) {
    return 3;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  const auto object_mark = arena.mark();
  tracker* value = arena.try_emplace<tracker>(4, 5);
  if (value == nullptr || value->lhs != 4 || value->rhs != 5 || tracker::constructions != 1) {
    return 4;
  }

  arena.rewind(object_mark);
  if (tracker::destructions != 1) {
    return 5;
  }

  tracker* reused = arena.try_emplace<tracker>(7, 8);
  if (reused == nullptr || reused->lhs != 7 || reused->rhs != 8) {
    return 6;
  }

  const std::size_t used_before_fail = arena.used();
  if (arena.allocate(1024, 1) != nullptr || arena.used() != used_before_fail) {
    return 7;
  }

  arena.rewind(first_mark);
  if (arena.used() > first_mark.offset || tracker::destructions != 2) {
    return 8;
  }

  void* reused_raw = arena.allocate(8, alignof(std::uint32_t));
  if (reused_raw == nullptr) {
    return 9;
  }

  arena.reset();
  if (!arena.empty() || arena.used() != 0 || arena.remaining() != 256) {
    return 10;
  }

  if (tracker::constructions != tracker::destructions) {
    return 11;
  }

  // Raw allocate + rewind scenario: interleave raw allocations with typed
  // emplacements, ensure rewind walks back through both kinds safely without
  // calling destroy on raw records (which carry destroy=nullptr).
  tracker::constructions = 0;
  tracker::destructions = 0;

  const auto stage0 = arena.mark();
  void* raw_a = arena.allocate(4, 1);
  if (raw_a == nullptr) {
    return 12;
  }

  tracker* typed_a = arena.try_emplace<tracker>(10, 11);
  if (typed_a == nullptr || tracker::constructions != 1) {
    return 13;
  }

  void* raw_b = arena.allocate(7, 1);
  if (raw_b == nullptr) {
    return 14;
  }

  tracker* typed_b = arena.try_emplace<tracker>(20, 21);
  if (typed_b == nullptr || tracker::constructions != 2) {
    return 15;
  }

  void* raw_c = arena.allocate(2, alignof(std::uint16_t));
  if (raw_c == nullptr) {
    return 16;
  }

  // Rewind across mixed raw + typed records. Both typed objects must be
  // destroyed exactly once. Raw allocations must not invoke any destroy hook.
  arena.rewind(stage0);
  if (tracker::destructions != 2) {
    return 17;
  }
  if (arena.used() != stage0.offset) {
    return 18;
  }

  // Allocate raw memory after rewind to confirm the offset state is clean.
  void* fresh_raw = arena.allocate(8, alignof(std::uint64_t));
  if (fresh_raw == nullptr) {
    return 19;
  }
  if ((reinterpret_cast<std::uintptr_t>(fresh_raw) % alignof(std::uint64_t)) != 0u) {
    return 20;
  }

  arena.reset();
  if (!arena.empty() || arena.used() != 0) {
    return 21;
  }

  // Final balance check: no typed object was leaked across the mixed scenario.
  if (tracker::constructions != tracker::destructions) {
    return 22;
  }

  // Zero-byte raw allocate returns nullptr without consuming any space.
  const std::size_t used_before_zero = arena.used();
  if (arena.allocate(0, 1) != nullptr || arena.used() != used_before_zero) {
    return 23;
  }

  // used() is exactly payload + padding + one record per allocation, and each
  // payload starts where the previous record ended (rounded up to alignment).
  {
    metl::arena_allocator<256> exact;
    const auto* base = static_cast<unsigned char*>(exact.allocate(1, 1));
    CHECK(base != nullptr);
    CHECK_EQ(exact.used(), 1 + record_size);
    CHECK_EQ(exact.remaining(), 256 - (1 + record_size));
    const auto* second = static_cast<unsigned char*>(exact.allocate(5, 1));
    CHECK(second != nullptr);
    CHECK_EQ(second - base, static_cast<std::ptrdiff_t>(1 + record_size));
    CHECK_EQ(exact.used(), 1 + record_size + 5 + record_size);
    const auto mark = exact.mark();
    CHECK_EQ(mark.offset, exact.used());
  }

  // An allocation that fits exactly -- payload plus record fill the arena to
  // the last byte -- succeeds; one byte more does not.
  {
    constexpr std::size_t capacity = 64;
    metl::arena_allocator<capacity> full;
    CHECK(full.allocate(capacity - record_size + 1, 1) == nullptr);
    CHECK(full.empty());
    void* fits = full.allocate(capacity - record_size, 1);
    CHECK(fits != nullptr);
    CHECK_EQ(full.used(), capacity);
    CHECK_EQ(full.remaining(), std::size_t{0});
    CHECK(full.allocate(1, 1) == nullptr);
  }

  return metl_test::exit_code();
}
