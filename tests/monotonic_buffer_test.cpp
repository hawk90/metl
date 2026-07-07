#include <cstddef>
#include <cstdint>

#include <metl/monotonic_buffer.hpp>

namespace {

struct alignas(8) aligned_value {
  aligned_value(std::uint32_t a, std::uint32_t b) : first(a), second(b) {}

  std::uint32_t first;
  std::uint32_t second;
};

struct pair_value {
  pair_value(int a, int b) : lhs(a), rhs(b) {}

  int lhs;
  int rhs;
};

}  // namespace

int main() {
  metl::monotonic_buffer<32> buffer;
  if (!buffer.empty() || buffer.capacity() != 32 || buffer.used() != 0) {
    return 1;
  }

  void* raw = buffer.allocate(3, 1);
  if (raw == nullptr || buffer.used() != 3 || buffer.remaining() != 29) {
    return 2;
  }

  aligned_value* aligned = buffer.try_emplace<aligned_value>(1u, 2u);
  if (aligned == nullptr || (reinterpret_cast<std::uintptr_t>(aligned) % alignof(aligned_value)) != 0u) {
    return 3;
  }

  pair_value* pair = buffer.try_emplace<pair_value>(4, 5);
  if (pair == nullptr || pair->lhs != 4 || pair->rhs != 5) {
    return 4;
  }

  const std::size_t used_before_fail = buffer.used();
  if (buffer.allocate(128, 1) != nullptr || buffer.used() != used_before_fail) {
    return 5;
  }

  buffer.reset();
  if (!buffer.empty() || buffer.used() != 0 || buffer.remaining() != 32) {
    return 6;
  }

  pair_value* reused = buffer.try_emplace<pair_value>(7, 8);
  if (reused == nullptr || reused->lhs != 7 || reused->rhs != 8) {
    return 7;
  }

  return 0;
}
