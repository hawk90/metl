// Verifies metl's security floor: METL_HARDEN is NOT stripped even at
// METL_HARDENING_NONE. This TU pins the lowest hardening level, so
// METL_ASSERT / METL_DASSERT are compiled out. An insert into a full
// static_unordered_map must still be refused: METL_HARDEN(size_ < Capacity)
// stops it at Capacity (#579). Before that check was METL_HARDEN, the inserts
// went on until every bucket was taken, and the next reached construct_at with
// an out-of-range index, where a second METL_HARDEN stopped it.
//
// The handler longjmps out before the abort, as harden_floor_memory_test does,
// so this runs everywhere -- it used to fork, and on a host without fork() it
// checked nothing and still passed.
#undef METL_HARDENING  // this test pins its own level, whatever the build passes
#define METL_HARDENING 0
#include "metl_check.hpp"

#include <csetjmp>

#include <metl/assert.hpp>
#include <metl/static_unordered_map.hpp>

namespace {

std::jmp_buf g_jump;
bool g_fired = false;

void capture(const char* /*expr*/, const char* /*file*/, int /*line*/) noexcept {
  g_fired = true;
  std::longjmp(g_jump, 1);
}

using Map = metl::static_unordered_map<int, int, 8>;
Map g_map;  // outside main: setjmp leaves a modified local indeterminate
int g_inserted = 0;

}  // namespace

int main() {
  metl::set_assert_handler(&capture);
  // Insert distinct keys up to one past every bucket: the guard must stop the
  // first insert past Capacity, long before the buckets run out.
  if (setjmp(g_jump) == 0) {
    for (; g_inserted < static_cast<int>(Map::bucket_count) + 1; ++g_inserted) {
      g_map.emplace(g_inserted, g_inserted);
    }
  }
  CHECK(g_fired);  // reached without it only if METL_HARDEN did NOT fire
  CHECK_EQ(g_inserted, static_cast<int>(g_map.capacity()));
  CHECK_EQ(g_map.size(), g_map.capacity());
  return metl_test::exit_code();
}
