#include "metl_check.hpp"

#include <cstdint>

#include <metl/register_access.hpp>

namespace {

// Host-side stand-in for a hardware register. volatile prevents the
// compiler from folding the read/write pair away even at host scope.
static volatile std::uint32_t fake_register_u32 = 0;
static volatile std::uint8_t fake_register_u8 = 0;
static volatile std::uint16_t fake_register_u16 = 0;

void test_read_write_u32() {
  fake_register_u32 = 0xdeadbeefu;
  const std::uint32_t value = metl::read_once<std::uint32_t>(&fake_register_u32);
  CHECK_EQ(value, 0xdeadbeefu);

  metl::write_once<std::uint32_t>(&fake_register_u32, 0xcafebabeu);
  CHECK_EQ(fake_register_u32, 0xcafebabeu);
}

void test_read_write_u8() {
  fake_register_u8 = 0u;
  metl::write_once<std::uint8_t>(&fake_register_u8, 0xa5u);
  const std::uint8_t value = metl::read_once<std::uint8_t>(&fake_register_u8);
  CHECK_EQ(value, 0xa5u);
}

void test_read_write_u16() {
  fake_register_u16 = 0u;
  metl::write_once<std::uint16_t>(&fake_register_u16, 0x1234u);
  CHECK_EQ(metl::read_once<std::uint16_t>(&fake_register_u16), 0x1234u);
}

void test_round_trip_does_not_fold() {
  // Two consecutive writes through volatile must both be issued; a
  // subsequent read must see the latest value.
  metl::write_once<std::uint32_t>(&fake_register_u32, 0x11111111u);
  metl::write_once<std::uint32_t>(&fake_register_u32, 0x22222222u);
  CHECK_EQ(metl::read_once<std::uint32_t>(&fake_register_u32), 0x22222222u);
}

void test_barriers_callable() {
  // Barriers cannot be observed from a single-threaded host test except
  // by confirming they compile, link, and execute without effect on
  // surrounding values.
  std::uint32_t before = 0x5a5a5a5au;
  metl::barrier_release();
  std::uint32_t mid = before;
  metl::barrier_full();
  std::uint32_t after = mid;
  metl::barrier_acquire();
  CHECK_EQ(before, after);
}

}  // namespace

int main() {
  test_read_write_u32();
  test_read_write_u8();
  test_read_write_u16();
  test_round_trip_does_not_fold();
  test_barriers_callable();
  return metl_test::exit_code();
}
