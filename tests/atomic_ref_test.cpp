#include "metl_check.hpp"

#include <atomic>
#include <cstdint>

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
  return metl_test::exit_code();
}
