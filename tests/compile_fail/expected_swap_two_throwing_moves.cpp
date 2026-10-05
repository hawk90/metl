// EXPECT-ERROR: metl::expected::swap requires T or E to be nothrow move constructible
//
// Swapping a value-state expected with an error-state one moves each member
// across. With one nothrow move a throw can always be rolled back; with two
// throwing moves the last step can fail after both originals are gone, leaving
// a destroyed member under a stale discriminant -- a double destroy
// (docs/AUDIT.md G.6). std::expected::swap carries the same requirement.

#include <metl/expected.hpp>

namespace {

struct throwing_move {
  int value = 0;
  throwing_move() = default;
  throwing_move(const throwing_move&) = default;
  throwing_move(throwing_move&& other) noexcept(false) : value(other.value) {}
  throwing_move& operator=(const throwing_move&) = default;
  throwing_move& operator=(throwing_move&&) noexcept(false) { return *this; }
};

}  // namespace

void control(metl::expected<throwing_move, int>& a, metl::expected<throwing_move, int>& b) {
  a.swap(b);  // E = int moves without throwing: allowed
}

#ifdef METL_COMPILE_FAIL
void offender(metl::expected<throwing_move, throwing_move>& a,
              metl::expected<throwing_move, throwing_move>& b) {
  a.swap(b);
}
#endif
