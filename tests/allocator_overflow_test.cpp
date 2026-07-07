// Regression test: allocator size math must not integer-overflow *before* the
// bounds check. A huge element count / byte request previously wrapped
// size_type into a small value that slipped past the capacity check, allowing
// an out-of-bounds allocation. The guards must reject such requests (nullptr).

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/arena_allocator.hpp>
#include <metl/static_allocator.hpp>

int main() {
  constexpr std::size_t max = static_cast<std::size_t>(-1);

  // ---- static_allocator: sizeof(T) * count overflow ----
  {
    metl::static_allocator<std::uint64_t, 64> alloc;  // 64 bytes
    // count so large that sizeof(uint64_t) * count wraps around.
    CHECK(alloc.try_allocate(max) == nullptr);
    CHECK(alloc.try_allocate(max / 4) == nullptr);
    // A sane request still works.
    CHECK(alloc.try_allocate(1) != nullptr);
  }

  // ---- arena_allocator: padding + bytes + sizeof(record) overflow ----
  {
    metl::arena_allocator<64> arena;
    CHECK(arena.allocate(max) == nullptr);
    CHECK(arena.allocate(max - 8) == nullptr);
    // Nothing was consumed by the rejected requests.
    CHECK(arena.empty());
    // A small request still succeeds.
    CHECK(arena.allocate(8) != nullptr);
  }

  return metl_test::exit_code();
}
