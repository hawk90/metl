// Regression test: allocator size math must not integer-overflow *before* the
// bounds check. A huge element count / byte request previously wrapped
// size_type into a small value that slipped past the capacity check, allowing
// an out-of-bounds allocation. The guards must reject such requests (nullptr).

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/arena_allocator.hpp>
#include <metl/monotonic_buffer.hpp>

int main() {
  constexpr std::size_t max = static_cast<std::size_t>(-1);

  // ---- monotonic_buffer::try_allocate_n: sizeof(T) * count overflow ----
  {
    metl::monotonic_buffer<64> buffer;
    // count so large that sizeof(uint64_t) * count wraps around.
    CHECK(buffer.try_allocate_n<std::uint64_t>(max) == nullptr);
    CHECK(buffer.try_allocate_n<std::uint64_t>(max / 4) == nullptr);
    // The count whose product wraps to a request that fits: 8 * (2^61 + 1)
    // is 8 modulo 2^64 on a 64-bit size_t (and 8 * (2^29 + 1) on 32 bits).
    CHECK(buffer.try_allocate_n<std::uint64_t>(max / sizeof(std::uint64_t) + 2) == nullptr);
    CHECK(buffer.empty());
    // A sane request still works, aligned for the type; 0 returns null.
    CHECK(buffer.try_allocate_n<std::uint64_t>(0) == nullptr);
    std::uint64_t* one = buffer.try_allocate_n<std::uint64_t>(1);
    CHECK(one != nullptr);
    CHECK_EQ(reinterpret_cast<std::uintptr_t>(one) % alignof(std::uint64_t), std::uintptr_t{0});
  }

  // ---- arena_allocator: padding + bytes + sizeof(record) overflow ----
  {
    metl::arena_allocator<64> arena;
    CHECK(arena.try_allocate(max) == nullptr);
    CHECK(arena.try_allocate(max - 8) == nullptr);
    // Nothing was consumed by the rejected requests.
    CHECK(arena.empty());
    // A small request still succeeds.
    CHECK(arena.try_allocate(8) != nullptr);
  }

  return metl_test::exit_code();
}
