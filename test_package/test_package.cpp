// Exercises a container, a vocabulary type and the umbrella header, so a
// package that shipped only part of include/metl fails here.
#include <metl/fixed_vector.hpp>
#include <metl/metl.hpp>
#include <metl/optional.hpp>

int main() {
  metl::fixed_vector<int, 4> values;
  if (!values.try_push_back(7)) {
    return 1;
  }

  metl::optional<int> maybe;
  if (maybe.has_value()) {
    return 2;
  }
  maybe = values[0];

  return maybe.value_or(0) == 7 ? 0 : 3;
}
