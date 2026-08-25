#include <metl/metl.hpp>

int main() {
  static_assert(metl::version::major == 0, "unexpected major version");
  return 0;
}

static_assert(false, "deliberate break to measure ci-gate; this branch is thrown away");
