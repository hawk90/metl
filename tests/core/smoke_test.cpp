#include <metl/metl.hpp>

int main() {
  static_assert(metl::version::major >= 1, "metl::version is reachable through the umbrella header");
  return 0;
}
