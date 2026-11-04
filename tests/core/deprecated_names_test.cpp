// Every name deprecated for removal in 2.0 still works in 1.x, and does what
// its replacement does (#645). The rest of the suite uses the replacements;
// this file keeps the old names covered until 2.0 removes them, and their
// assertions tripped (tools/check_assert_trips.py) wherever METL_ASSERT is
// compiled in.
#include "assert_trip.hpp"
#include "metl_check.hpp"
#include "metl_deprecated.hpp"

#include <cstddef>

#include <metl/arena_allocator.hpp>
#include <metl/monotonic_buffer.hpp>
#include <metl/static_allocator.hpp>

namespace {

using metl_test::trips;

void allocator_allocate() {
  // allocate is try_allocate under its 1.x name: null when it does not fit.
  metl::arena_allocator<64> arena;
  CHECK(arena.allocate(8) != nullptr);
  CHECK(arena.allocate(4096) == nullptr);
  CHECK(arena.allocate(0) == nullptr);
  metl::monotonic_buffer<64> buffer;
  CHECK(buffer.allocate(8, 8) != nullptr);
  CHECK(buffer.allocate(4096) == nullptr);
  CHECK_EQ(buffer.used(), std::size_t{8});

  // static_allocator's asserting forms still assert.
  using allocator = metl::static_allocator<int, 1>;
  static allocator one;
  CHECK(!metl_test::asserts_active || trips([] { (void)one.allocate(2); }));
  (void)one.try_allocate(1);
  CHECK(!metl_test::asserts_active || trips([] { (void)one.create(1); }));
  CHECK(!metl_test::asserts_active || trips([] { one.construct(nullptr, 1); }));
  CHECK(!metl_test::asserts_active || trips([] { one.destroy(nullptr); }));
}

}  // namespace

int main() {
  allocator_allocate();
  return metl_test::exit_code();
}
