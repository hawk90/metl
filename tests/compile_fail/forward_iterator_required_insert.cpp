// EXPECT-ERROR: try_insert requires a forward iterator
//
// The try_insert half of the forward-iterator contract. forward_iterator_required
// fires try_assign's assertion; the two messages share their tail, so that case
// alone could not show that this one still fires (#407).

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

  // A pointer is a forward iterator: accepted. The control must compile.
  const bool ok = v.try_insert(v.end(), &source[0], &source[3]) != v.end();

#ifdef METL_COMPILE_FAIL
  // Single-pass: must be rejected at compile time.
  const input_only_iterator first{&source[0]};
  const input_only_iterator last{&source[3]};
  return v.try_insert(v.end(), first, last) != v.end() ? 0 : 1;
#else
  return ok ? 0 : 1;
#endif
}
