/*
 * metl-on-ESP-IDF sample.
 *
 * Exercises a couple of metl types (fixed_vector + expected + span) on an
 * ESP32-class target to prove the IDF component wiring works: the app compiles
 * as C++ (ESP-IDF builds .cpp as C++17+ by default), finds <metl/...> via the
 * in-tree `metl` component (REQUIRES metl), and links into the firmware with no
 * exceptions / no RTTI / no heap use from metl.
 *
 * Deterministic by construction: fixed inputs, fixed-capacity storage, and a
 * single sentinel line ("metl on ESP-IDF: OK ...") printed on success. Returns
 * from app_main() normally on success.
 *
 * ESP-IDF calls app_main() as the application entry point; it must have C
 * linkage (extern "C") in a C++ translation unit.
 */

#include <cstdio>

#include <metl/expected.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/span.hpp>

namespace {

// Exception-free error handling with metl::expected: divide, or report an
// error value instead of throwing.
metl::expected<int, int> checked_div(int numerator, int denominator) {
  if (denominator == 0) {
    return metl::unexpected<int>{-1};
  }
  return numerator / denominator;
}

// Sum a view without owning it (metl::span is a non-owning contiguous view).
int sum(metl::span<const int> xs) {
  int total = 0;
  for (int x : xs) {
    total += x;
  }
  return total;
}

}  // namespace

extern "C" void app_main(void) {
  // Fixed-capacity, no-heap container.
  metl::fixed_vector<int, 8> v;
  v.push_back(2);
  v.push_back(3);
  v.push_back(4);

  const int total = sum(metl::span<const int>{v.data(), v.size()});  // 9
  const auto avg = checked_div(total, static_cast<int>(v.size()));   // 9 / 3 = 3
  const auto bad = checked_div(total, 0);                            // error path

  const bool ok = (total == 9) && avg.has_value() && (avg.value() == 3) && !bad.has_value();

  if (ok) {
    std::printf("metl on ESP-IDF: OK total=%d avg=%d\n", total, avg.value());
  } else {
    std::printf("metl on ESP-IDF: FAIL total=%d\n", total);
  }
}
