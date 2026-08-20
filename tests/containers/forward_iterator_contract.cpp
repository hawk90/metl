// Compile-only fixture for the forward-iterator half of the recoverable-API
// contract (SCOPE.md §9 R1: "contents unchanged on failure").
//
// `try_assign` / `try_insert` must know the length of a range before writing
// anything, so a single-pass input iterator cannot be accepted. The rejection
// is a `static_assert` rather than an `enable_if` — a named reason beats "no
// matching function" — which means it is a hard error, not a substitution
// failure, and therefore cannot be detected from inside a running test.
//
// So it is proved the way the handle-atomics capability gate is proved: this
// file is compiled twice by the `api-contract` job.
//
//   -DMETL_ITERATOR_KIND=0  (forward)  must SUCCEED
//   -DMETL_ITERATOR_KIND=1  (input)    must FAIL
//
// If the second one ever compiles, the constraint is gone and the gate is dead.

#include "metl/fixed_vector.hpp"

#include <cstddef>
#include <iterator>

#ifndef METL_ITERATOR_KIND
#define METL_ITERATOR_KIND 0
#endif

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

#if METL_ITERATOR_KIND == 0
  // A pointer is a forward iterator (contiguous, in fact) — accepted.
  const bool ok = v.try_assign(&source[0], &source[3]);
  return ok ? 0 : 1;
#else
  // Single-pass — must be rejected at compile time.
  const input_only_iterator first{&source[0]};
  const input_only_iterator last{&source[3]};
  const bool ok = v.try_assign(first, last);
  return ok ? 0 : 1;
#endif
}
