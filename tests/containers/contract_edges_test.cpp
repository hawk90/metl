// Contract claims from the headers that no other test would notice breaking
// (#196):
//
//   * spsc_byte_ring::readable_span() is empty only when the ring is empty,
//     including when the readable region straddles the physical end of the
//     buffer.
//   * static_unordered_map / static_unordered_set: a hasher that throws during
//     the in-place rebuild that clears tombstones empties the table and
//     propagates the exception.
//   * try_format_hex pads to exactly `digits` even past 16 digits and past
//     sizeof(T) * 2, bounded by the span.
//   * fixed_string::try_append(s.as_span()) and try_assign(s.c_str() + k) read
//     their own storage correctly.

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/fixed_string.hpp>
#include <metl/format.hpp>
#include <metl/span.hpp>
#include <metl/spsc_byte_ring.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

// --- spsc_byte_ring: readable_span across the seam ---------------------------

// Moves both indices to `offset` (mod 2 * Capacity, so the monotonic indices
// also cross one wrap), queues `fill` bytes, then drains them through
// readable_span() / consume(). At every step the span must be non-empty
// exactly when readable_size() is, must be the contiguous run up to the seam,
// and must hold the bytes in order.
template <std::size_t Capacity>
void check_readable_span_at(std::size_t offset, std::size_t fill) {
  metl::spsc_byte_ring<Capacity> ring;
  std::byte scratch[Capacity] = {};
  // Advance in chunks no larger than the ring.
  std::size_t moved = 0;
  while (moved < offset) {
    const std::size_t step = (offset - moved) < Capacity ? (offset - moved) : Capacity;
    CHECK(ring.try_write(metl::span<const std::byte>(scratch, step)));
    CHECK_EQ(ring.read(metl::span<std::byte>(scratch, step)), step);
    moved += step;
  }
  CHECK(ring.readable_span().empty());
  CHECK_EQ(ring.readable_size(), std::size_t{0});

  std::byte payload[Capacity] = {};
  for (std::size_t i = 0; i < fill; ++i) {
    payload[i] = static_cast<std::byte>(i + 1);
  }
  CHECK(ring.try_write(metl::span<const std::byte>(payload, fill)));

  std::size_t next = 0;
  int rounds = 0;
  while (ring.readable_size() > 0 && rounds < 4) {
    ++rounds;
    const metl::span<const std::byte> run = ring.readable_span();
    const std::size_t head = (offset + next) % Capacity;
    const std::size_t to_end = Capacity - head;
    const std::size_t remaining = fill - next;
    const std::size_t expected = remaining < to_end ? remaining : to_end;
    if (!CHECK(!run.empty())) {
      return;  // an empty span with bytes readable: the claim is broken
    }
    CHECK_EQ(run.size(), expected);
    for (std::size_t i = 0; i < run.size(); ++i) {
      CHECK_EQ(static_cast<unsigned>(run[i]), static_cast<unsigned>(next + i + 1));
    }
    next += run.size();
    ring.consume(run.size());
  }
  CHECK_EQ(next, fill);
  CHECK(ring.readable_span().empty());
  CHECK_EQ(ring.readable_size(), std::size_t{0});
}

template <std::size_t Capacity>
void test_readable_span_wrap() {
  for (std::size_t offset = 0; offset < 2 * Capacity; ++offset) {
    for (std::size_t fill = 0; fill <= Capacity; ++fill) {
      check_readable_span_at<Capacity>(offset, fill);
    }
  }
}

// --- static_unordered_map / _set: throwing hasher during the rebuild ----------

#if !METL_NO_EXCEPTIONS

struct hash_error {};

// Calls left before the hasher throws; negative means never.
int g_hash_calls_left = -1;

struct arming_hash {
  std::size_t operator()(int key) const {
    if (g_hash_calls_left == 0) {
      throw hash_error{};
    }
    if (g_hash_calls_left > 0) {
      --g_hash_calls_left;
    }
    return static_cast<std::size_t>(key);
  }
};

int g_live = 0;

// Counts live instances, so the test can see that emptying the table destroyed
// every element exactly once. Nothrow-movable, so the rebuild runs.
struct tracked {
  int value;
  explicit tracked(int v) noexcept : value(v) { ++g_live; }
  tracked(const tracked& other) noexcept : value(other.value) { ++g_live; }
  tracked(tracked&& other) noexcept : value(other.value) { ++g_live; }
  tracked& operator=(const tracked&) noexcept = default;
  tracked& operator=(tracked&&) noexcept = default;
  ~tracked() { --g_live; }
};

void test_map_rebuild_throw() {
  using map_type = metl::static_unordered_map<int, tracked, 8, arming_hash>;
  static_assert(map_type::bucket_count / 8 < 4, "the erases below must pass the rebuild threshold");
  g_hash_calls_left = -1;
  g_live = 0;
  {
    map_type map;
    for (int k = 0; k < 8; ++k) {
      CHECK(map.try_emplace(k, tracked(k)));
    }
    // Four tombstones: more than bucket_count / 8, so the next new key rebuilds.
    for (int k = 0; k < 4; ++k) {
      CHECK(map.erase(k));
    }
    CHECK_EQ(map.size(), std::size_t{4});
    CHECK_EQ(g_live, 4);

    // try_emplace hashes once to locate the slot; the next call is the
    // rebuild's, and that one throws.
    g_hash_calls_left = 1;
    bool threw = false;
    try {
      (void)map.try_emplace(100, tracked(100));
    } catch (const hash_error&) {
      threw = true;
    }
    g_hash_calls_left = -1;
    CHECK(threw);
    CHECK_EQ(map.size(), std::size_t{0});
    CHECK(map.empty());
    CHECK(map.begin() == map.end());
    CHECK_EQ(g_live, 0);
    // Still a working, empty table.
    CHECK(map.find(4) == nullptr);
    CHECK(map.try_emplace(7, tracked(70)));
    CHECK_EQ(map.size(), std::size_t{1});
    const tracked* found = map.find(7);
    if (CHECK(found != nullptr)) {
      CHECK_EQ(found->value, 70);
    }
  }
  CHECK_EQ(g_live, 0);
}

struct tracked_key {
  int key;
  explicit tracked_key(int k) noexcept : key(k) { ++g_live; }
  tracked_key(const tracked_key& other) noexcept : key(other.key) { ++g_live; }
  tracked_key(tracked_key&& other) noexcept : key(other.key) { ++g_live; }
  tracked_key& operator=(const tracked_key&) noexcept = default;
  tracked_key& operator=(tracked_key&&) noexcept = default;
  ~tracked_key() { --g_live; }
  friend bool operator==(const tracked_key& a, const tracked_key& b) noexcept { return a.key == b.key; }
};

struct arming_key_hash {
  std::size_t operator()(const tracked_key& k) const { return arming_hash{}(k.key); }
};

void test_set_rebuild_throw() {
  using set_type = metl::static_unordered_set<tracked_key, 8, arming_key_hash>;
  static_assert(set_type::bucket_count / 8 < 4, "the erases below must pass the rebuild threshold");
  g_hash_calls_left = -1;
  g_live = 0;
  {
    set_type set;
    for (int k = 0; k < 8; ++k) {
      CHECK(set.try_emplace(tracked_key(k)));
    }
    for (int k = 0; k < 4; ++k) {
      CHECK(set.erase(tracked_key(k)));
    }
    CHECK_EQ(set.size(), std::size_t{4});
    CHECK_EQ(g_live, 4);

    g_hash_calls_left = 1;
    bool threw = false;
    try {
      (void)set.try_emplace(tracked_key(100));
    } catch (const hash_error&) {
      threw = true;
    }
    g_hash_calls_left = -1;
    CHECK(threw);
    CHECK_EQ(set.size(), std::size_t{0});
    CHECK(set.empty());
    CHECK(set.begin() == set.end());
    CHECK_EQ(g_live, 0);
    CHECK(!set.contains(tracked_key(4)));
    CHECK(set.try_emplace(tracked_key(7)));
    CHECK_EQ(set.size(), std::size_t{1});
    CHECK(set.contains(tracked_key(7)));
  }
  CHECK_EQ(g_live, 0);
}

#endif  // !METL_NO_EXCEPTIONS

// --- try_format_hex: digits past 16 and past sizeof(T) * 2 -------------------

bool all_zero(metl::span<const char> text, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    if (text[i] != '0') {
      return false;
    }
  }
  return true;
}

void test_format_hex_wide_digits() {
  char buffer[40] = {};
  constexpr std::size_t guard = 32;

  // uint8_t, 20 digits: 18 zeros then "ab" -- far past sizeof(T) * 2 and 16.
  {
    for (char& c : buffer) {
      c = '#';
    }
    const metl::span<char> text =
        metl::try_format_hex(metl::span<char>(buffer, guard), std::uint8_t{0xAB}, 20);
    CHECK_EQ(text.size(), std::size_t{20});
    CHECK(text.data() == buffer);
    CHECK(all_zero(text, 18));
    CHECK_EQ(buffer[18], 'a');
    CHECK_EQ(buffer[19], 'b');
    CHECK_EQ(buffer[20], '#');  // nothing written past `digits`
  }

  // uint64_t, 17 digits: one leading zero then the full 16-digit value.
  {
    const metl::span<char> text = metl::try_format_hex(
        metl::span<char>(buffer, guard), std::uint64_t{0xFEDCBA9876543210ULL}, 17, metl::hex_case::upper);
    CHECK_EQ(text.size(), std::size_t{17});
    const char expected[] = "0FEDCBA9876543210";
    for (std::size_t i = 0; i < 17; ++i) {
      CHECK_EQ(text[i], expected[i]);
    }
  }

  // Zero, every width from 17 to the span size: exactly that many zeros.
  for (std::size_t digits = 17; digits <= guard; ++digits) {
    for (char& c : buffer) {
      c = '#';
    }
    const metl::span<char> text = metl::try_format_hex(metl::span<char>(buffer, guard), 0U, digits);
    CHECK_EQ(text.size(), digits);
    CHECK(all_zero(text, text.size()));
    CHECK_EQ(buffer[digits], '#');
  }

  // A span exactly `digits` long fits; one short returns empty and writes nothing.
  {
    const metl::span<char> fits = metl::try_format_hex(metl::span<char>(buffer, 24), std::uint16_t{1}, 24);
    CHECK_EQ(fits.size(), std::size_t{24});
    CHECK(all_zero(fits, 23));
    CHECK_EQ(buffer[23], '1');

    for (char& c : buffer) {
      c = '#';
    }
    const metl::span<char> short_span =
        metl::try_format_hex(metl::span<char>(buffer, 23), std::uint16_t{1}, 24);
    CHECK(short_span.empty());
    CHECK_EQ(buffer[0], '#');
  }

  // A huge `digits` is bounded by the span, not trusted.
  {
    const metl::span<char> text =
        metl::try_format_hex(metl::span<char>(buffer, guard), std::uint32_t{0x5}, std::size_t{1} << 20);
    CHECK(text.empty());
  }
}

// --- fixed_string: reading its own storage -----------------------------------

void test_fixed_string_self_append() {
  {
    metl::fixed_string<16> s("abc");
    CHECK(s.try_append(metl::span<const char>(s.as_span())));
    CHECK_EQ(s.size(), std::size_t{6});
    CHECK(s == "abcabc");
    CHECK_EQ(s.c_str()[6], '\0');
    CHECK(s.try_append(metl::span<const char>(s.as_span())));
    CHECK(s == "abcabcabcabc");
  }
  {
    // A prefix and a suffix of itself.
    metl::fixed_string<16> s("hello");
    CHECK(s.try_append(metl::span<const char>(s.as_span()).first(2)));
    CHECK(s == "hellohe");
    CHECK(s.try_append(metl::span<const char>(s.as_span()).last(3)));
    CHECK(s == "helloheohe");
  }
  {
    // Exactly to capacity, then one past it: refused, unchanged.
    metl::fixed_string<8> s("wxyz");
    CHECK(s.try_append(metl::span<const char>(s.as_span())));
    CHECK(s == "wxyzwxyz");
    CHECK(!s.try_append(metl::span<const char>(s.as_span())));
    CHECK(s == "wxyzwxyz");
    CHECK_EQ(s.size(), std::size_t{8});
  }
  {
    // Empty self-append is a no-op.
    metl::fixed_string<4> s;
    CHECK(s.try_append(metl::span<const char>(s.as_span())));
    CHECK(s.empty());
    CHECK_EQ(s.c_str()[0], '\0');
  }
}

void test_fixed_string_self_assign_suffix() {
  const char source[] = "0123456789abcdef";
  for (std::size_t k = 0; k <= 16; ++k) {
    metl::fixed_string<16> s(source);
    CHECK(s.try_assign(s.c_str() + k));
    CHECK_EQ(s.size(), 16 - k);
    for (std::size_t i = 0; i < s.size(); ++i) {
      CHECK_EQ(s[i], source[k + i]);
    }
    CHECK_EQ(s.c_str()[s.size()], '\0');
  }
  {
    // Assigning itself whole is a no-op.
    metl::fixed_string<8> s("same");
    CHECK(s.try_assign(s.c_str()));
    CHECK(s == "same");
  }
}

}  // namespace

int main() {
  test_readable_span_wrap<2>();
  test_readable_span_wrap<8>();
  test_readable_span_wrap<16>();
#if !METL_NO_EXCEPTIONS
  test_map_rebuild_throw();
  test_set_rebuild_throw();
#endif
  test_format_hex_wide_digits();
  test_fixed_string_self_append();
  test_fixed_string_self_assign_suffix();
  return metl_test::exit_code();
}
