// EXPECT-ERROR: METL_NODISCARD must not be predefined
//
// Predefined to nothing, it took [[nodiscard]] off every try_ result, so an
// ignored failure no longer warned.

#ifdef METL_COMPILE_FAIL
#define METL_NODISCARD
#endif

#include <metl/attributes.hpp>

int main() {
  return 0;
}
