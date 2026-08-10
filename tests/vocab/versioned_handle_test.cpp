#include "metl_check.hpp"

#include <cstdint>
#include <type_traits>

#include <metl/versioned_handle.hpp>

namespace {

struct tag_a {};
struct tag_b {};

using handle = metl::versioned_handle<tag_a>;
using narrow_handle = metl::versioned_handle<tag_a, std::uint8_t, std::uint8_t>;
using wide_handle = metl::versioned_handle<tag_a, std::uint32_t, std::uint32_t>;

// --- Layout ------------------------------------------------------------------
// The whole point of the handle is that it is a machine word, so the atomic form
// can be a single-word CAS on a 32-bit MCU. Pin it.
static_assert(sizeof(handle) == 4, "default handle must be one 32-bit word");
static_assert(sizeof(narrow_handle) == 2, "8+8 handle must be 16 bits");
static_assert(sizeof(wide_handle) == 8, "32+32 handle must be 64 bits");

static_assert(std::is_trivially_copyable<handle>::value,
              "a handle must be memcpy-able so it can travel in a POD message");

// Tags are what stop a handle from one pool being used with another.
static_assert(!std::is_same<handle, metl::versioned_handle<tag_b>>::value,
              "handles with different tags must be different types");

// --- Constexpr ---------------------------------------------------------------
// Everything is pure arithmetic, so all of it works at compile time.
static_assert(!handle{}.valid(), "default-constructed handle is null");
static_assert(handle{7, 3}.index() == 7, "index round-trips");
static_assert(handle{7, 3}.generation() == 3, "generation round-trips");
static_assert(handle{7, 3}.valid(), "non-zero generation is valid");
static_assert(handle::from_packed(handle{7, 3}.packed()) == handle{7, 3}, "packed round-trips");

}  // namespace

int main() {
  // Null handle.
  const handle null;
  CHECK(!null.valid());
  CHECK(!static_cast<bool>(null));
  CHECK_EQ(null.packed(), std::uint32_t{0});

  // Generation 0 means null regardless of the index: the reserved value wins,
  // which is what keeps handle_pool's wraparound skip honest.
  const handle zero_generation{5, 0};
  CHECK(!zero_generation.valid());

  // Field independence at the extremes.
  const handle extremes{handle::max_index, handle::max_generation};
  CHECK_EQ(extremes.index(), handle::max_index);
  CHECK_EQ(extremes.generation(), handle::max_generation);
  CHECK(extremes.valid());

  // Index occupies the low bits, generation the high bits.
  const handle packed{0x1234u, 0xABCDu};
  CHECK_EQ(packed.packed(), std::uint32_t{0xABCD1234u});

  // Round-trip through the packed representation, which is what an atomic form
  // would load and store.
  CHECK_EQ(handle::from_packed(packed.packed()), packed);

  // Equality and ordering.
  CHECK(handle(1, 1) == handle(1, 1));
  CHECK(handle(1, 1) != handle(1, 2));
  CHECK(handle(1, 1) != handle(2, 1));
  CHECK(handle(1, 1) < handle(2, 1));
  CHECK(handle(1, 1) < handle(1, 2));

  // A narrow handle behaves the same way in 16 bits.
  const narrow_handle narrow{0x12u, 0x34u};
  CHECK_EQ(narrow.index(), std::uint8_t{0x12u});
  CHECK_EQ(narrow.generation(), std::uint8_t{0x34u});
  CHECK_EQ(narrow.packed(), std::uint16_t{0x3412u});

  return metl_test::exit_code();
}
