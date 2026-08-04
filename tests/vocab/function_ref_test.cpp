#include <type_traits>

#include <metl/function_ref.hpp>

namespace {

int add_values(int lhs, int rhs) {
  return lhs + rhs;
}

struct accumulator {
  int base;

  int operator()(int value) { return base + value; }
};

struct multiplier {
  int factor;

  int operator()(int value) const { return factor * value; }
};

// P0792: binding a function_ref to an rvalue callable would dangle (it stores a
// pointer to a temporary destroyed at the end of the full-expression). The
// rvalue constructor is deleted, so such construction must not be well-formed.
// Lvalue callables and function pointers must remain constructible.
static_assert(std::is_constructible<metl::function_ref<int(int)>, accumulator&>::value,
              "function_ref must bind an lvalue callable");
static_assert(std::is_constructible<metl::function_ref<int(int)>, const multiplier&>::value,
              "function_ref must bind a const lvalue callable");
static_assert(std::is_constructible<metl::function_ref<int(int, int)>, decltype(&add_values)>::value,
              "function_ref must bind a function pointer");
static_assert(!std::is_constructible<metl::function_ref<int(int)>, accumulator>::value,
              "function_ref must reject rvalue callables (would dangle)");
static_assert(!std::is_constructible<metl::function_ref<int(int)>, multiplier>::value,
              "function_ref must reject rvalue callables (would dangle)");

}  // namespace

int main() {
  metl::function_ref<int(int, int)> free_function(add_values);
  if (!free_function || free_function(2, 3) != 5) {
    return 1;
  }

  accumulator stateful{7};
  metl::function_ref<int(int)> bound(stateful);
  if (!bound || bound(5) != 12) {
    return 2;
  }

  stateful.base = 10;
  if (bound(1) != 11) {
    return 3;
  }

  const multiplier readonly{4};
  metl::function_ref<int(int)> const_bound(readonly);
  if (!const_bound || const_bound(6) != 24) {
    return 4;
  }

  metl::function_ref<int(int)> empty;
  if (empty.has_value()) {
    return 5;
  }

  return 0;
}
