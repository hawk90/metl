#include <metl/metl.hpp>

int main() {
  static_assert(metl::version::major == 0, "unexpected major version");
  return 0;
}
