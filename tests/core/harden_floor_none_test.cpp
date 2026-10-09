// Verifies metl's security floor: METL_HARDEN is NOT stripped even at
// METL_HARDENING_NONE. This TU pins the lowest hardening level, so
// METL_ASSERT / METL_DASSERT are compiled out. A full-table insert into a
// static_unordered_map reaches construct_at with an out-of-range index; only the
// always-on METL_HARDEN guard stands between that and a wild out-of-bounds
// write.
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
  // emplace() carries no size_>=Capacity refusal (that guard lives in
  // try_emplace), and at NONE its precondition METL_ASSERT is stripped, so it
  // keeps inserting distinct keys until every physical bucket is occupied. The
  // next insert makes locate_insert_index yield index == npos; with METL_ASSERT
  // gone, only METL_HARDEN(index < bucket_count) in construct_at stands between
  // that and a wild out-of-bounds write. Looping one past bucket_count
  // guarantees we cross the physical-full boundary and trip the guard.
  if (setjmp(g_jump) == 0) {
    for (; g_inserted < static_cast<int>(Map::bucket_count) + 1; ++g_inserted) {
      g_map.emplace(g_inserted, g_inserted);
    }
  }
  CHECK(g_fired);  // reached without it only if METL_HARDEN did NOT fire
  CHECK_EQ(g_inserted, static_cast<int>(Map::bucket_count));
  return metl_test::exit_code();
}
