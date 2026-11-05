#include <metl/metl.hpp>

int main() {
  static_assert(metl::version_major >= 1, "metl::version_major is reachable through the umbrella header");
  return 0;
}
