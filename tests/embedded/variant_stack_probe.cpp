// Switching a variant's alternative costs no stack for a copy of it.
//
// emplace and assignment from another variant used to build the new value in a
// local first -- a temporary or a whole backup variant -- in case an argument
// lived inside the alternative about to be destroyed. With a 1 KiB alternative
// that was a 1 KiB frame on every switch, even under -fno-exceptions where the
// backup buys nothing (#315). They now construct in place when nothing can
// alias and the construction cannot throw, and keep the temporary in an
// out-of-line function for the cases that need it.
//
// cmake/CheckStackFrames.cmake reads this TU's .su file. Every frame here must
// stay far below the alternative's size; the out-of-line temporary paths are
// excluded by name, since holding the temporary is their job.

#include <metl/variant.hpp>

// Named, not anonymous: a function taking a type from an unnamed namespace has
// internal linkage, and an unused one is never emitted, so it has no frame.
namespace metl_variant_probe {

struct big {
  unsigned char bytes[1024];
  big() noexcept : bytes{} {}
  big(const big& other) noexcept : big() {
    for (unsigned i = 0; i < sizeof(bytes); ++i) {
      bytes[i] = other.bytes[i];
    }
  }
  big(big&& other) noexcept : big(static_cast<const big&>(other)) {}
  big& operator=(const big&) noexcept = default;
  big& operator=(big&&) noexcept = default;
  ~big() = default;
};

using probe_variant = metl::variant<int, big>;

}  // namespace metl_variant_probe

using metl_variant_probe::big;
using metl_variant_probe::probe_variant;

void metl_probe_variant_copy_assign(probe_variant& to, const probe_variant& from) {
  to = from;
}
void metl_probe_variant_move_assign(probe_variant& to, probe_variant&& from) {
  to = static_cast<probe_variant&&>(from);
}
void metl_probe_variant_emplace_default(probe_variant& to) {
  (void)to.emplace<big>();
}
void metl_probe_variant_assign_value(probe_variant& to, const big& value) {
  to = value;
}
