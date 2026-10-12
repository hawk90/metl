// Three places where a vocabulary type rejected or mis-detected what its std
// counterpart handles (#316).

#include "metl_check.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>

#include <metl/function_ref.hpp>
#include <metl/optional.hpp>
#include <metl/scope_exit.hpp>

namespace {

// ---- std::hash<metl::optional<T>> is enabled only when std::hash<T> is ------

struct no_hash {};

static_assert(std::is_default_constructible_v<std::hash<metl::optional<int>>>,
              "an optional of a hashable type is hashable");
static_assert(std::is_invocable_v<std::hash<metl::optional<int>>, const metl::optional<int>&>,
              "and the hash can be called");
static_assert(!std::is_default_constructible_v<std::hash<metl::optional<no_hash>>>,
              "an optional of an unhashable type has a disabled hash, as std::optional does");
static_assert(!std::is_invocable_v<std::hash<metl::optional<no_hash>>, const metl::optional<no_hash>&>,
              "so SFINAE detection does not pick it");
static_assert(std::is_default_constructible_v<std::hash<metl::optional<const int>>>,
              "const T is hashed as T");

struct throwing_hash_key {
  int value;
};

}  // namespace

template <>
struct std::hash<throwing_hash_key> {
  std::size_t operator()(const throwing_hash_key& key) const { return static_cast<std::size_t>(key.value); }
};

namespace {

static_assert(!std::is_nothrow_invocable_v<std::hash<metl::optional<throwing_hash_key>>,
                                           const metl::optional<throwing_hash_key>&>,
              "noexcept follows the element's hash");

void optional_hash_values() {
  const metl::optional<int> empty;
  const metl::optional<int> five(5);
  CHECK_EQ(std::hash<metl::optional<int>>{}(empty), std::size_t{0});
  CHECK_EQ(std::hash<metl::optional<int>>{}(five), std::hash<int>{}(5));
}

// ---- scope_exit takes a const callable whose operator() is not const --------

void scope_exit_const_mutable_lambda() {
  int runs = 0;
  {
    const auto bump = [&runs]() mutable noexcept { ++runs; };
    metl::scope_exit guard(bump);  // stores a copy, which can be called
  }
  CHECK_EQ(runs, 1);
  {
    const auto bump = [&runs]() mutable noexcept { ++runs; };
    auto guard = metl::make_scope_exit(bump);
  }
  CHECK_EQ(runs, 2);
}

// ---- function_ref binds a function pointer of a convertible signature -------

int take_long(long value) {
  return static_cast<int>(value * 2);
}
int take_int(int value) {
  return value + 1;
}
long return_long(int value) {
  return value;
}
void consume(int) {}

int call_with(metl::function_ref<int(int)> f, int value) {
  return f(value);
}
void call_void(metl::function_ref<void(int)> f, int value) {
  f(value);
}

void function_ref_converting_pointer() {
  CHECK_EQ(call_with(take_long, 21), 42);    // parameter converts
  CHECK_EQ(call_with(take_int, 41), 42);     // exact: the existing constructor
  CHECK_EQ(call_with(return_long, 42), 42);  // result converts
  call_void(take_int, 1);                    // result discarded for void
  call_void(consume, 1);

  static_assert(!std::is_constructible_v<metl::function_ref<int(int)>, void (*)(const char*)>,
                "a pointer that cannot be called with the arguments still does not bind");
}

}  // namespace

int main() {
  optional_hash_values();
  scope_exit_const_mutable_lambda();
  function_ref_converting_pointer();
  return metl_test::exit_code();
}
