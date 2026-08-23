// EXPECT-ERROR: the range must be measurable before anything is written
//
// The forward-iterator half of the recoverable-API contract (SCOPE.md §9 R1:
// "contents unchanged on failure").
//
// `try_assign` / `try_insert` must know the length of a range before writing
// anything, so a single-pass input iterator cannot be accepted. The rejection
// is a `static_assert` rather than an `enable_if` — a named reason beats "no
// matching function" — which means it is a hard error, not a substitution
// failure, and therefore cannot be detected from inside a running test.
//
// So it is proved by compiling this file twice, which is what
// tools/check_compile_fail.py does for every case in this directory.
//
// It moved here from tests/containers/ and predates that tool: the job used to
// run the two compiles inline and check only that the second one FAILED, not
// why. A typo in this file would have satisfied that forever. The tool requires
// the diagnostic to match the EXPECT-ERROR line above, so a failure for the
// wrong reason is now a failure.

#include "metl/fixed_vector.hpp"

#include <cstddef>
#include <iterator>

namespace {

/// Minimal single-pass source: everything a range needs except the ability to
/// be measured without being consumed.
struct input_only_iterator {
  using iterator_category = std::input_iterator_tag;
  using value_type = int;
  using difference_type = std::ptrdiff_t;
  using pointer = const int*;
  using reference = const int&;

  const int* p;

  reference operator*() const noexcept { return *p; }
  input_only_iterator& operator++() noexcept {
    ++p;
    return *this;
  }
  bool operator==(const input_only_iterator& rhs) const noexcept { return p == rhs.p; }
  bool operator!=(const input_only_iterator& rhs) const noexcept { return p != rhs.p; }
};

const int source[3] = {1, 2, 3};

}  // namespace

int main() {
  metl::fixed_vector<int, 8> v;

  // A pointer is a forward iterator (contiguous, in fact) — accepted. This is
  // the control: it must compile on its own.
  const bool ok = v.try_assign(&source[0], &source[3]);

#ifdef METL_COMPILE_FAIL
  // Single-pass — must be rejected at compile time.
  const input_only_iterator first{&source[0]};
  const input_only_iterator last{&source[3]};
  return v.try_assign(first, last) ? 0 : 1;
#else
  return ok ? 0 : 1;
#endif
}
