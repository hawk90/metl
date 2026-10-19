// metl::exp::bitset (#336): every operation against a plain bool-array model,
// at sizes on both sides of each word edge, plus the cases the model cannot
// show: the tail bits past N stay zero, and an index past the end traps.

#include "assert_trip.hpp"
#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include <metl/exp/bitset.hpp>

namespace {

using metl_test::trips;

// ---- compile time -----------------------------------------------------------

static_assert(sizeof(metl::exp::bitset<1>) == 4, "one word");
static_assert(sizeof(metl::exp::bitset<32>) == 4, "one word, full");
static_assert(sizeof(metl::exp::bitset<33>) == 8, "a second word for bit 32");
static_assert(sizeof(metl::exp::bitset<256>) == 32, "a 256-slot pool's flags in 32 bytes");

constexpr bool constant_evaluated_operations() {
  metl::exp::bitset<40> bits;
  bits.set(0);
  bits.set(33);
  bits.set(39);
  bits.reset(0);
  bits <<= 1;  // 33 -> 34, 39 is shifted out
  return bits.count() == 1 && bits.test(34) && bits.find_first_set() == 34 && bits.find_first_clear() == 0 &&
         !bits.all();
}
static_assert(constant_evaluated_operations(), "every operation is usable in a constant expression");

// A namespace-scope bitset needs no constructor at run time.
constexpr metl::exp::bitset<64> g_constant_bits{};
static_assert(g_constant_bits.none(), "default-constructed is all clear");

// ---- a model to compare against ---------------------------------------------

template <std::size_t N>
struct model {
  bool bits[N] = {};

  std::size_t count() const {
    std::size_t total = 0;
    for (bool b : bits) {
      total += b ? 1 : 0;
    }
    return total;
  }
  std::size_t first(bool value) const {
    for (std::size_t i = 0; i < N; ++i) {
      if (bits[i] == value) {
        return i;
      }
    }
    return N;
  }
  void shift_up(std::size_t n) {
    for (std::size_t i = N; i-- > 0;) {
      bits[i] = i >= n && bits[i - n];
    }
  }
  void shift_down(std::size_t n) {
    for (std::size_t i = 0; i < N; ++i) {
      bits[i] = i + n < N && bits[i + n];
    }
  }
};

template <std::size_t N>
bool agrees(const metl::exp::bitset<N>& bits, const model<N>& expected) {
  for (std::size_t i = 0; i < N; ++i) {
    if (bits.test(i) != expected.bits[i]) {
      return false;
    }
  }
  return bits.count() == expected.count() && bits.find_first_set() == expected.first(true) &&
         bits.find_first_clear() == expected.first(false) && bits.any() == (expected.count() != 0) &&
         bits.none() == (expected.count() == 0) && bits.all() == (expected.count() == N);
}

// A fixed linear congruential sequence: the same run on every platform.
std::uint32_t next(std::uint32_t& state) {
  state = state * 1664525U + 1013904223U;
  return state >> 8;
}

template <std::size_t N>
void matches_the_model() {
  metl::exp::bitset<N> bits;
  model<N> expected;
  CHECK(agrees(bits, expected));
  std::uint32_t state = static_cast<std::uint32_t>(N);
  for (int step = 0; step < 2000; ++step) {
    const std::uint32_t pick = next(state);
    const std::size_t index = pick % N;
    switch (pick % 7) {
      case 0:
      case 1:
      case 2:
        bits.set(index);
        expected.bits[index] = true;
        break;
      case 3:
      case 4:
        bits.reset(index);
        expected.bits[index] = false;
        break;
      case 5: {
        const std::size_t n = next(state) % (N + 3);
        bits <<= n;
        expected.shift_up(n);
        break;
      }
      default: {
        const std::size_t n = next(state) % (N + 3);
        bits >>= n;
        expected.shift_down(n);
        break;
      }
    }
    const bool same = agrees(bits, expected);
    CHECK(same);
    if (!same) {
      return;  // one report, not one per remaining step
    }
  }
  bits.reset();
  CHECK(bits.none());
}

// ---- what the model cannot show -------------------------------------------

/// Every bit set, by index.
template <std::size_t N>
metl::exp::bitset<N> full() {
  metl::exp::bitset<N> bits;
  for (std::size_t i = 0; i < N; ++i) {
    bits.set(i);
  }
  return bits;
}

template <std::size_t N>
void boundary_bits() {
  // 31 and 32 straddle the first word edge; N - 1 is the last valid bit.
  for (std::size_t i : {std::size_t{0}, std::size_t{31}, std::size_t{32}, N / 2, N - 1}) {
    if (i >= N) {
      continue;
    }
    metl::exp::bitset<N> bits;
    bits.set(i);
    CHECK(bits.test(i));
    CHECK_EQ(bits.count(), std::size_t{1});
    CHECK_EQ(bits.find_first_set(), i);
    bits.reset(i);
    CHECK(bits.none());
  }
}

/// A shift left pushes the top bits into the tail of the last word. They must
/// be dropped there: otherwise count() and == see bits that do not exist.
template <std::size_t N>
void tail_bits_stay_zero() {
  metl::exp::bitset<N> shifted = full<N>();
  shifted <<= 1;
  metl::exp::bitset<N> expected = full<N>();
  expected.reset(0);
  CHECK_EQ(shifted.count(), N - 1);
  CHECK(shifted == expected);
  // And shifting back down must not bring a dropped bit back.
  shifted >>= 1;
  metl::exp::bitset<N> down = full<N>();
  down.reset(N - 1);
  CHECK(shifted == down);
  // A full set has no clear bit, though its last word has clear tail bits.
  CHECK_EQ(full<N>().find_first_clear(), N);
  CHECK(full<N>().all());
}

template <std::size_t N>
void shift_edges() {
  for (std::size_t n : {std::size_t{0}, std::size_t{31}, std::size_t{32}, std::size_t{33}, N - 1, N, N + 5}) {
    metl::exp::bitset<N> up = full<N>();
    up <<= n;
    CHECK_EQ(up.count(), n >= N ? 0 : N - n);
    CHECK_EQ(up.find_first_set(), n >= N ? N : n);
    metl::exp::bitset<N> down = full<N>();
    down >>= n;
    CHECK_EQ(down.count(), n >= N ? 0 : N - n);
    CHECK_EQ(down.find_first_clear(), n >= N ? 0 : (n == 0 ? N : N - n));
  }
}

template <std::size_t N>
void out_of_range_traps() {
  metl::exp::bitset<N> bits;
  CHECK(trips([&] { (void)bits.test(N); }));
  CHECK(trips([&] { bits.set(N); }));
  CHECK(trips([&] { bits.reset(N); }));
  CHECK(!trips([&] { bits.set(N - 1); }));
  CHECK(bits.test(N - 1));
}

template <std::size_t N>
void all_cases() {
  matches_the_model<N>();
  boundary_bits<N>();
  tail_bits_stay_zero<N>();
  shift_edges<N>();
  out_of_range_traps<N>();
}

}  // namespace

int main() {
  all_cases<1>();
  all_cases<31>();
  all_cases<32>();
  all_cases<33>();
  all_cases<64>();
  all_cases<100>();
  return metl_test::exit_code();
}
