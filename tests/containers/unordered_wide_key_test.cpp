// 64-bit keys on a 32-bit size_t (#643).
//
// libstdc++'s std::hash<unsigned long long> is a cast to size_t, which keeps
// only the low word on a 32-bit target. Keys that differ only in the high
// word, such as `i << 32`, then all hash alike and every lookup scans the
// table. The containers fold the high word in for std::hash; this test counts
// key comparisons to show it. On a 64-bit host the cast loses nothing, so the
// bound holds either way; QEMU's 32-bit Cortex-M runs are where it bites.

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <type_traits>

#include <metl/hash.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

std::size_t g_compares = 0;

struct counting_equal {
  bool operator()(std::uint64_t a, std::uint64_t b) const noexcept {
    ++g_compares;
    return a == b;
  }
};

constexpr std::size_t keys = 64;

#if defined(__SIZEOF_INT128__)
// The same loss on a 64-bit host: libstdc++'s std::hash<unsigned __int128> is
// a cast too, in the GNU dialects where __int128 counts as integral.
__extension__ typedef unsigned __int128 u128;  // GNU type; keeps -Wpedantic quiet
struct counting_equal_128 {
  bool operator()(u128 a, u128 b) const noexcept {
    ++g_compares;
    return a == b;
  }
};

// The fold is constexpr and keeps the high word apart, at compile time. With
// std::hash called instead (not constexpr in libstdc++ or libc++), this does
// not compile, which is what the mutant hash_key_drops_high_word meets.
// Integral and hashable: libstdc++ turns both off for __int128 in the strict
// dialects; the checks below run only where the type is a real key.
template <typename Wide>
constexpr bool hashes_as_integer =
    std::is_integral_v<Wide> && std::is_default_constructible_v<std::hash<Wide>>;

template <typename Wide>
constexpr bool folds_high_word() {
  if constexpr (hashes_as_integer<Wide>) {
    constexpr std::hash<Wide> hasher{};
    return metl::detail::hash_key(hasher, static_cast<Wide>(1) << 64) !=
               metl::detail::hash_key(hasher, static_cast<Wide>(2) << 64) &&
           metl::detail::hash_key(hasher, (static_cast<Wide>(1) << 64) | 1U) !=
               metl::detail::hash_key(hasher, (static_cast<Wide>(2) << 64) | 2U);
  } else {
    return true;
  }
}
static_assert(folds_high_word<u128>(), "a 128-bit key must keep its high word");

// A template, so the strict dialects (where libstdc++ disables
// std::hash<__int128>) discard the branch instead of compiling it.
template <typename Wide>
void wide_on_a_64_bit_host() {
  if constexpr (hashes_as_integer<Wide>) {
    metl::static_unordered_map<Wide, int, keys, std::hash<Wide>, counting_equal_128> map;
    for (std::size_t i = 0; i < keys; ++i) {
      CHECK(map.try_emplace(static_cast<Wide>(i) << 64, static_cast<int>(i)));
    }
    g_compares = 0;
    for (std::size_t i = 0; i < keys; ++i) {
      CHECK(map.find(static_cast<Wide>(i) << 64) != nullptr);
    }
    CHECK(g_compares <= keys * 4);
  }
}
#else
using u128 = void;  // no 128-bit type: the check below compiles to nothing
template <typename>
void wide_on_a_64_bit_host() {}
#endif

}  // namespace

int main() {
  // The fold keeps keys that differ only in the high word apart, and is not
  // fooled by a key whose two words are equal (a plain XOR maps those to 0).
  static_assert(sizeof(std::size_t) >= 8 ||
                    metl::detail::hash_key(std::hash<std::uint64_t>{}, std::uint64_t{1} << 32) !=
                        metl::detail::hash_key(std::hash<std::uint64_t>{}, std::uint64_t{2} << 32),
                "high words must reach the hash");
  static_assert(sizeof(std::size_t) >= 8 ||
                    metl::detail::hash_key(std::hash<std::uint64_t>{}, (std::uint64_t{1} << 32) | 1U) !=
                        metl::detail::hash_key(std::hash<std::uint64_t>{}, (std::uint64_t{2} << 32) | 2U),
                "a key whose words are equal must not fold to a constant");
  // Another hasher, or a key no wider than size_t, is passed through.
  CHECK_EQ(metl::detail::hash_key(metl::identity_hash{}, std::uint32_t{7}), std::size_t{7});
  CHECK_EQ(metl::detail::hash_key(std::hash<std::uint32_t>{}, std::uint32_t{7}),
           std::hash<std::uint32_t>{}(std::uint32_t{7}));

  {
    metl::static_unordered_map<std::uint64_t, int, keys, std::hash<std::uint64_t>, counting_equal> map;
    for (std::size_t i = 0; i < keys; ++i) {
      CHECK(map.try_emplace(static_cast<std::uint64_t>(i) << 32, static_cast<int>(i)));
    }
    g_compares = 0;
    for (std::size_t i = 0; i < keys; ++i) {
      const auto* found = map.find(static_cast<std::uint64_t>(i) << 32);
      CHECK(found != nullptr && *found == static_cast<int>(i));
    }
    // About 1.4 compares a hit with the high word in, 32.5 without it.
    CHECK(g_compares <= keys * 4);
  }

  {
    metl::static_unordered_set<std::uint64_t, keys, std::hash<std::uint64_t>, counting_equal> set;
    for (std::size_t i = 0; i < keys; ++i) {
      CHECK(set.try_emplace(static_cast<std::uint64_t>(i) << 32));
    }
    g_compares = 0;
    for (std::size_t i = 0; i < keys; ++i) {
      CHECK(set.contains(static_cast<std::uint64_t>(i) << 32));
    }
    CHECK(g_compares <= keys * 4);
  }

  wide_on_a_64_bit_host<u128>();

  return metl_test::exit_code();
}
