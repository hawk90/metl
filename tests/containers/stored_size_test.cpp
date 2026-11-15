// The stored size (and a ring's head) is the narrowest type that makes the
// container smaller (#318 section 3), detail::stored_size_t. A container of
// small elements shrinks; one of int is unchanged on a 32-bit target, and its
// counter is size_t itself there. The interface still speaks size_type.
// fixed_string keeps a size_t until #697 is understood: narrowing it makes
// GCC 16 -O3 report -Wmaybe-uninitialized in operator==.
#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <metl/fixed_deque.hpp>
#include <metl/fixed_string.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/ring_buffer.hpp>

namespace {

using u8 = std::uint8_t;
using metl::detail::stored_size_t;

// How many bytes a count needs, at each edge.
using metl::detail::count_bytes;
static_assert(count_bytes(0) == 1 && count_bytes(0xFF) == 1);
static_assert(count_bytes(0x100) == 2 && count_bytes(0xFFFF) == 2);
static_assert(count_bytes(0x10000) == 4);
static_assert(count_bytes(static_cast<std::size_t>(-1)) == sizeof(std::size_t));
// Never narrower than the alignment floor, capped at size_t.
static_assert(metl::detail::stored_size_bytes(1, 4) == (4 < sizeof(std::size_t) ? 4 : sizeof(std::size_t)));
static_assert(metl::detail::stored_size_bytes(2, 1) == 2);
static_assert(metl::detail::stored_size_bytes(1, 64) == sizeof(std::size_t));

// What the alias picks.
static_assert(std::is_same_v<stored_size_t<8, u8>, u8>);
static_assert(std::is_same_v<stored_size_t<255, u8>, u8>);
static_assert(std::is_same_v<stored_size_t<256, u8>, std::uint16_t>);
static_assert(std::is_same_v<stored_size_t<8, std::uint16_t>, std::uint16_t>);
static_assert(std::is_same_v<stored_size_t<0, char>, u8>);
// Never narrower than T's alignment, and size_t itself at size_t's width.
static_assert(sizeof(stored_size_t<8, int>) ==
              (alignof(int) < sizeof(std::size_t) ? alignof(int) : sizeof(std::size_t)));
static_assert(std::is_same_v<stored_size_t<8, std::max_align_t>, std::size_t>);
static_assert(std::is_unsigned_v<stored_size_t<8, int>>);

// The same sizes on a 32-bit Cortex-M and a 64-bit host.
static_assert(sizeof(metl::fixed_vector<u8, 8>) == 9);
static_assert(sizeof(metl::fixed_vector<u8, 300>) == 302);
static_assert(sizeof(metl::fixed_vector<int, 8>) == 8 * sizeof(int) + 4);
// Not narrowed (#697): the size is still a size_t.
static_assert(sizeof(metl::fixed_string<8>) ==
              (9 + sizeof(std::size_t) - 1) / sizeof(std::size_t) * sizeof(std::size_t) +
                  sizeof(std::size_t));
static_assert(sizeof(metl::ring_buffer<u8, 8>) == 10);
static_assert(sizeof(metl::fixed_deque<u8, 8>) == 10);
static_assert(sizeof(metl::flat_set<u8, 8>) == 10);
static_assert(sizeof(metl::flat_map<u8, u8, 8>) == 18);
// size() still returns size_type.
static_assert(std::is_same_v<decltype(metl::fixed_vector<u8, 8>{}.size()), std::size_t>);

// A full container at the edge of each width.
template <std::size_t N>
void fill_vector() {
  metl::fixed_vector<u8, N> v;
  for (std::size_t i = 0; i < N; ++i) {
    CHECK(v.try_push_back(static_cast<u8>(i)));
  }
  CHECK(!v.try_push_back(0));
  CHECK_EQ(v.size(), N);
  v.erase(v.begin(), v.begin() + 1);
  CHECK_EQ(v.size(), N - 1);
}

// A ring at the edge of each width, driven around so head_ wraps.
template <std::size_t N>
void cycle_ring() {
  metl::ring_buffer<u8, N> r;
  for (std::size_t i = 0; i < N; ++i) {
    CHECK(r.try_push_back(static_cast<u8>(i)));
  }
  CHECK_EQ(r.size(), N);
  for (std::size_t lap = 0; lap < 3 * N; ++lap) {
    const u8 expected = r.front();
    r.pop_front();
    CHECK(r.try_push_back(expected));
  }
  CHECK_EQ(r.size(), N);
  CHECK_EQ(static_cast<std::size_t>(r.front()), std::size_t{0});
}

}  // namespace

int main() {
  fill_vector<255>();
  fill_vector<256>();
  cycle_ring<255>();
  cycle_ring<256>();
  return metl_test::exit_code();
}
