// The application side of config_api_lib.cpp.

#include <metl/fixed_vector.hpp>

int api(metl::fixed_vector<int, 4>& values) noexcept;

int main() {
  metl::fixed_vector<int, 4> values;
  values.push_back(42);
  return api(values) == 42 ? 0 : 1;
}
