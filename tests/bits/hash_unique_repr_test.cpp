// Regression test: fnv1a_hash's raw-object-representation overload is now
// constrained (static_assert) to types with a unique object representation, so
// it can only be used where the hash/equality invariant holds. This test
// exercises the sound cases and documents the constraint on unsound ones.

#include "metl_check.hpp"

#include <cstdint>
#include <type_traits>

#include <metl/hash.hpp>

namespace {

// No padding: unique object representation -> allowed.
struct packed_pair {
  std::uint32_t a;
  std::uint32_t b;
};

// Padding between members: NOT a unique object representation. Using this with
// fnv1a_hash's default overload would (correctly) fail to compile.
struct padded {
  std::uint8_t a;
  std::uint32_t b;
};

static_assert(std::has_unique_object_representations<packed_pair>::value,
              "packed_pair should have a unique object representation");
static_assert(!std::has_unique_object_representations<padded>::value,
              "padded has padding bytes and must be rejected by fnv1a_hash");
// float has no unique object representation (+0.0 vs -0.0): also rejected.
static_assert(!std::has_unique_object_representations<float>::value, "float excluded");

}  // namespace

int main() {
  metl::fnv1a_hash h;

  // Integer types are sound and deterministic.
  const int x = 12345;
  const int y = 12345;
  const int z = 99999;
  CHECK_EQ(h(x), h(y));
  CHECK(h(x) != h(z));

  // A no-padding aggregate is allowed and deterministic.
  packed_pair p1{1, 2};
  packed_pair p2{1, 2};
  packed_pair p3{1, 3};
  CHECK_EQ(h(p1), h(p2));
  CHECK(h(p1) != h(p3));

  // The C-string overload is unaffected.
  CHECK(h("alpha") != h("beta"));
  CHECK_EQ(h("same"), h("same"));

  return metl_test::exit_code();
}
