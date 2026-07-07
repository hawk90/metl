// Semihosting link+run smoke test for a real embedded C library.
//
// Unlike tests/embedded_smoke.cpp (a compile-only archive), this translation
// unit has a main() and is LINKED against a bare-metal libc (picolibc for the
// QEMU run, or newlib-nano for the link-only fallback) and, under picolibc +
// QEMU semihosting, actually RUN on an emulated Cortex-M. It exercises a
// representative slice of METL types at runtime and reports success by printing
// a sentinel line the CI job greps for. It returns 0 on success so the libc's
// crt0 requests a clean semihosting exit.
//
// Kept deliberately small and dependency-light: only <cstdio>/<cstdint> from
// the target libc plus METL headers.

#include <cstdint>
#include <cstdio>

#include <metl/crc32.hpp>
#include <metl/endian.hpp>
#include <metl/expected.hpp>
#include <metl/fixed_string.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/optional.hpp>
#include <metl/span.hpp>

namespace {

metl::expected<int, int> checked_div(int a, int b) {
  if (b == 0) {
    return metl::unexpected<int>{-1};
  }
  return a / b;
}

}  // namespace

int main() {
  // optional + fixed_vector round-trip.
  metl::optional<int> answer{42};
  metl::fixed_vector<int, 8> v;
  v.push_back(answer.value());
  v.push_back(7);

  int sum = 0;
  for (int x : v) {
    sum += x;
  }

  // expected control flow.
  const auto ok = checked_div(84, 2);
  const auto bad = checked_div(1, 0);

  // endian round-trip (validates endian.hpp on the target's byte order).
  const std::uint32_t be = metl::to_big_endian<std::uint32_t>(0x01020304u);
  const std::uint32_t restored = metl::from_big_endian<std::uint32_t>(be);

  // crc32 over a known buffer.
  const std::uint8_t data[] = {0x00, 0x01, 0x02, 0x03};
  const std::uint32_t crc = metl::crc32(metl::span<const std::uint8_t>{data, sizeof(data)});

  // fixed_string.
  metl::fixed_string<16> name{"metl"};

  const bool pass = (sum == 49) && ok.has_value() && (ok.value() == 42) && (!bad.has_value()) &&
                    (restored == 0x01020304u) && (crc != 0u) && (name.size() == 4);

  if (pass) {
    std::printf("METL_SEMIHOST_PASS\n");
    return 0;
  }

  std::printf("METL_SEMIHOST_FAIL sum=%d crc=%lu\n", sum, static_cast<unsigned long>(crc));
  return 1;
}
