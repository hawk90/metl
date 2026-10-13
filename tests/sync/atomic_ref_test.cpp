#include "metl_check.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>
#include <type_traits>

#include <metl/atomic_ref.hpp>

namespace {

void test_load_store_u32() {
  alignas(std::atomic<std::uint32_t>) std::uint32_t storage = 0u;
  metl::atomic_ref<std::uint32_t> ref(storage);

  ref.store(0xdeadbeefu);
  CHECK_EQ(ref.load(), 0xdeadbeefu);
  CHECK_EQ(storage, 0xdeadbeefu);
}

void test_exchange() {
  alignas(std::atomic<std::uint32_t>) std::uint32_t storage = 0x11111111u;
  metl::atomic_ref<std::uint32_t> ref(storage);

  const std::uint32_t prev = ref.exchange(0x22222222u);
  CHECK_EQ(prev, 0x11111111u);
  CHECK_EQ(ref.load(), 0x22222222u);
}

void test_compare_exchange_strong_success() {
  alignas(std::atomic<std::uint32_t>) std::uint32_t storage = 100u;
  metl::atomic_ref<std::uint32_t> ref(storage);

  std::uint32_t expected = 100u;
  const bool ok = ref.compare_exchange_strong(expected, 200u);
  CHECK(ok);
  CHECK_EQ(ref.load(), 200u);
}

void test_compare_exchange_strong_failure() {
  alignas(std::atomic<std::uint32_t>) std::uint32_t storage = 100u;
  metl::atomic_ref<std::uint32_t> ref(storage);

  std::uint32_t expected = 999u;
  const bool ok = ref.compare_exchange_strong(expected, 200u);
  CHECK(!ok);
  CHECK_EQ(expected, 100u);  // updated to the actual value
  CHECK_EQ(ref.load(), 100u);
}

void test_fetch_add_sub() {
  alignas(std::atomic<std::uint32_t>) std::uint32_t storage = 10u;
  metl::atomic_ref<std::uint32_t> ref(storage);

  const std::uint32_t prev = ref.fetch_add(5u);
  CHECK_EQ(prev, 10u);
  CHECK_EQ(ref.load(), 15u);

  const std::uint32_t prev2 = ref.fetch_sub(3u);
  CHECK_EQ(prev2, 15u);
  CHECK_EQ(ref.load(), 12u);
}

void test_fetch_bitwise() {
  alignas(std::atomic<std::uint32_t>) std::uint32_t storage = 0xff00ff00u;
  metl::atomic_ref<std::uint32_t> ref(storage);

  ref.fetch_and(0x0fff0fffu);
  CHECK_EQ(ref.load(), 0x0f000f00u);

  ref.fetch_or(0x000000ffu);
  CHECK_EQ(ref.load(), 0x0f000fffu);

  ref.fetch_xor(0xffffffffu);
  CHECK_EQ(ref.load(), ~0x0f000fffu);
}

void test_u8_u16_u64() {
  {
    alignas(std::atomic<std::uint8_t>) std::uint8_t s = 0u;
    metl::atomic_ref<std::uint8_t> r(s);
    r.store(0x7fu);
    CHECK_EQ(r.load(), 0x7fu);
    CHECK_EQ(r.fetch_add(static_cast<std::uint8_t>(1)), 0x7fu);
    CHECK_EQ(r.load(), 0x80u);
  }
  {
    alignas(std::atomic<std::uint16_t>) std::uint16_t s = 0u;
    metl::atomic_ref<std::uint16_t> r(s);
    r.store(0xabcdu);
    CHECK_EQ(r.load(), 0xabcdu);
  }
  {
    alignas(std::atomic<std::uint64_t>) std::uint64_t s = 0ull;
    metl::atomic_ref<std::uint64_t> r(s);
    r.store(0x1122334455667788ull);
    CHECK_EQ(r.load(), 0x1122334455667788ull);
  }
}

void test_traits() {
  static_assert(metl::atomic_ref<std::uint32_t>::required_alignment == alignof(std::atomic<std::uint32_t>),
                "required_alignment must match std::atomic alignment");
  static_assert(
      metl::atomic_ref<std::uint32_t>::is_always_lock_free == std::atomic<std::uint32_t>::is_always_lock_free,
      "is_always_lock_free must match std::atomic");
  static_assert(!std::is_copy_assignable_v<metl::atomic_ref<std::uint32_t>>,
                "atomic_ref must not be copy-assignable");
  static_assert(std::is_copy_constructible_v<metl::atomic_ref<std::uint32_t>>,
                "atomic_ref must be copy-constructible");
}

// A T with a padding byte. compare_exchange_strong compared it byte-wise, so it
// failed although every member was equal; std::atomic_ref compares value bits.
struct padded {
  char a;
  short b;
};

void test_strong_ignores_padding() {
  static_assert(!std::has_unique_object_representations_v<padded>, "padded has a padding byte");
  alignas(metl::atomic_ref<padded>::required_alignment) unsigned char raw[sizeof(padded)];
  std::memset(raw, 0xAB, sizeof raw);
  padded* object = new (raw) padded;  // default-initialised: the padding keeps 0xAB
  object->a = 1;
  object->b = 2;
  metl::atomic_ref<padded> ref(*object);

  padded expected;
  std::memset(&expected, 0, sizeof expected);
  expected.a = 1;
  expected.b = 2;
  const padded desired{9, 9};
  const bool exchanged = ref.compare_exchange_strong(expected, desired);
  if (metl::detail::atomic_ref_ignores_padding_v<padded>) {
    CHECK(exchanged);
    CHECK_EQ(ref.load().a, 9);
    CHECK_EQ(ref.load().b, 9);
  } else {
    // No __builtin_clear_padding: documented to fail as weak may.
    CHECK_EQ(expected.a, 1);
    CHECK_EQ(expected.b, 2);
  }

  // A real mismatch still fails and reports the held value.
  padded wrong{1, 3};
  CHECK(!ref.compare_exchange_strong(wrong, padded{7, 7}));
  CHECK_EQ(wrong.a, ref.load().a);
  CHECK_EQ(wrong.b, ref.load().b);
}

// Aligned below its size, with no padding and no default constructor. Every
// operation goes through a word of its size, so none needs libatomic.
struct bytes4 {
  explicit bytes4(unsigned char first) : b{first, 2, 3, 4} {}
  unsigned char b[4];
};

void test_under_aligned() {
  static_assert(alignof(bytes4) < sizeof(bytes4), "bytes4 is aligned below its size");
  static_assert(!std::is_default_constructible_v<bytes4>, "bytes4 has no default constructor");
  alignas(metl::atomic_ref<bytes4>::required_alignment) bytes4 object(1);
  metl::atomic_ref<bytes4> ref(object);

  CHECK_EQ(ref.load().b[0], 1);
  ref.store(bytes4(5));
  CHECK_EQ(ref.load().b[0], 5);
  CHECK_EQ(ref.exchange(bytes4(6)).b[0], 5);
  CHECK_EQ(ref.load().b[0], 6);

  bytes4 expected(6);
  CHECK(ref.compare_exchange_strong(expected, bytes4(7)));
  CHECK_EQ(ref.load().b[0], 7);
  bytes4 stale(6);
  CHECK(!ref.compare_exchange_strong(stale, bytes4(8)));
  CHECK_EQ(stale.b[0], 7);
}

}  // namespace

int main() {
  test_traits();
  test_load_store_u32();
  test_exchange();
  test_compare_exchange_strong_success();
  test_compare_exchange_strong_failure();
  test_fetch_add_sub();
  test_fetch_bitwise();
  test_u8_u16_u64();
  test_strong_ignores_padding();
  test_under_aligned();
  return metl_test::exit_code();
}
