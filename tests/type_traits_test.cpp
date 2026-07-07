#include <cstddef>

#include <metl/type_traits.hpp>

namespace {

struct base {};
struct derived : base {};
struct aggregate {
  int value;
};

struct non_trivial {
  non_trivial() : value(0) {}
  ~non_trivial() {}

  int value;
};

struct with_value_type {
  using value_type = int;
};

struct without_value_type {};

template <typename T>
using detect_value_type_t = typename T::value_type;

template <typename T, metl::enable_if_t<metl::is_integral_v<T>, int> = 0>
constexpr T only_integral(T value) {
  return value;
}

constexpr bool static_checks() {
  if (!metl::is_same_v<metl::remove_cvref_t<const int&>, int>) {
    return false;
  }

  if (!metl::is_same_v<metl::decay_t<int[4]>, int*>) {
    return false;
  }

  if (!metl::conjunction_v<metl::bool_constant<true>, metl::bool_constant<true>>) {
    return false;
  }

  if (metl::conjunction_v<metl::bool_constant<true>, metl::bool_constant<false>>) {
    return false;
  }

  if (!metl::disjunction_v<metl::bool_constant<false>, metl::bool_constant<true>>) {
    return false;
  }

  if (!metl::negation_v<metl::false_type>) {
    return false;
  }

  if (!metl::is_detected_v<detect_value_type_t, with_value_type>) {
    return false;
  }

  if (metl::is_detected_v<detect_value_type_t, without_value_type>) {
    return false;
  }

  return only_integral(7) == 7;
}

static_assert(static_checks(), "type_traits constexpr checks failed");

}  // namespace

int main() {
  if (!metl::is_base_of_v<base, derived>) {
    return 1;
  }

  if (!metl::is_trivially_copyable_v<aggregate> || metl::is_trivially_destructible_v<non_trivial>) {
    return 2;
  }

  if (!metl::is_default_constructible_v<aggregate> || !metl::is_move_constructible_v<aggregate>) {
    return 3;
  }

  if (!metl::is_constructible_from_v<aggregate, const aggregate&>) {
    return 4;
  }

  metl::aligned_storage_t<aggregate> storage{};
  void* address = &storage;
  if (reinterpret_cast<std::size_t>(address) % alignof(aggregate) != 0) {
    return 5;
  }

  return 0;
}
