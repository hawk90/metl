// fixed_vector::insert(pos, first, last) and assign(first, last) with a range
// of the vector's own elements (docs/AUDIT.md G.9). std::vector makes this a
// precondition; METL checks it: the range is shifted or cleared before it is
// read, so it used to copy moved-from or destroyed elements silently (insert
// of v's own range gave `1 1 1 1 2 3`). The assert is observed through a
// handler that longjmps out before the abort, so this also runs on QEMU.

#include "metl_check.hpp"

#include <csetjmp>

#include <metl/assert.hpp>
#include <metl/fixed_vector.hpp>

namespace {

std::jmp_buf g_jump;
bool g_asserted = false;

void capture(const char* /*expr*/, const char* /*file*/, int /*line*/) noexcept {
  g_asserted = true;
  std::longjmp(g_jump, 1);
}

using vec = metl::fixed_vector<int, 16>;

vec make() {
  vec v;
  v.push_back(1);
  v.push_back(2);
  v.push_back(3);
  return v;
}

}  // namespace

int main() {
  metl::set_assert_handler(&capture);

  // Own ranges assert, through every entry point that takes a range.
  {
    vec v = make();
    g_asserted = false;
    if (setjmp(g_jump) == 0) {
      v.insert(v.end(), v.begin(), v.end());
    }
    CHECK(g_asserted);
  }
  {
    vec v = make();
    g_asserted = false;
    if (setjmp(g_jump) == 0) {
      v.assign(v.begin() + 1, v.end());
    }
    CHECK(g_asserted);
  }
  {
    vec v = make();
    g_asserted = false;
    if (setjmp(g_jump) == 0) {
      v.assign(v.rbegin(), v.rend());
    }
    CHECK(g_asserted);
  }
  {
    vec v = make();
    g_asserted = false;
    if (setjmp(g_jump) == 0) {
      (void)v.try_insert(v.begin(), v.cbegin(), v.cend());
    }
    CHECK(g_asserted);
  }

  // Ranges of another vector, of a plain array, and empty own ranges are fine.
  {
    vec v = make();
    const vec other = make();
    g_asserted = false;
    v.insert(v.end(), other.begin(), other.end());
    CHECK(!g_asserted);
    CHECK_EQ(v.size(), 6u);

    const int plain[] = {7, 8};
    v.assign(plain, plain + 2);
    CHECK(!g_asserted);
    CHECK_EQ(v.size(), 2u);
    CHECK_EQ(v[1], 8);

    v.insert(v.begin(), v.begin(), v.begin());  // empty: nothing is read
    CHECK(!g_asserted);
    CHECK_EQ(v.size(), 2u);
  }

  return metl_test::exit_code();
}
