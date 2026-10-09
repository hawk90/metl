#include "intrusive_ptr_abi.hpp"

#include <cstdio>

int main() {
  static abi_node node;
  node.value = 40;
  int taken = 0;
  {
    const metl::intrusive_ptr<abi_node> p = make(&node);
    taken = take(p);  // 40, plus the caller's reference and the parameter's
  }
  std::printf("take=%d use_count=%zu\n", taken, node.use_count());
  return (taken == 42 && node.use_count() == 0) ? 0 : 1;
}
