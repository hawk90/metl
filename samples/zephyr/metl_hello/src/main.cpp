/*
 * metl-on-Zephyr sample.
 *
 * Exercises a couple of metl types (fixed_vector + expected + span) on a real
 * Zephyr target to prove the module wiring works: the app compiles as C++17,
 * finds <metl/...> via the metl Zephyr module, and links against Zephyr's C++
 * runtime with no exceptions / no RTTI / no heap.
 *
 * Deterministic by construction: fixed inputs, fixed-capacity storage, and a
 * single sentinel line ("metl on Zephyr: OK ...") that CI (twister console
 * harness) greps for. Returns 0 on success.
 */

#include <zephyr/kernel.h>

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

int main(void) {
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
    printk("metl on Zephyr: OK total=%d avg=%d\n", total, avg.value());
    return 0;
  }

  printk("metl on Zephyr: FAIL total=%d\n", total);
  return 1;
}
