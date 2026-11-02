// Three places where a vocabulary type rejected or mis-detected what its std
// counterpart handles (#316).

#include "metl_check.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

#include <metl/expected.hpp>
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

// ---- swap is noexcept only when the swap it calls is (#600) ---------------

// Moves that cannot throw, and a swap that can: optional and expected swap the
// values themselves when both sides hold one, so the throwing swap is reached.
struct throwing_swap {
  int value = 0;
  throwing_swap() = default;
  explicit throwing_swap(int v) : value(v) {}
  throwing_swap(throwing_swap&&) noexcept = default;
  throwing_swap& operator=(throwing_swap&&) noexcept = default;
  friend void swap(throwing_swap& lhs, throwing_swap& rhs) noexcept(false) {
#if METL_NO_EXCEPTIONS
    std::swap(lhs.value, rhs.value);
#else
    (void)lhs;
    (void)rhs;
    throw 1;
#endif
  }
};

template <typename T>
constexpr bool nothrow_swap_v = noexcept(std::declval<T&>().swap(std::declval<T&>()));

static_assert(!nothrow_swap_v<metl::optional<throwing_swap>>);
static_assert(!nothrow_swap_v<metl::unexpected<throwing_swap>>);
static_assert(!nothrow_swap_v<metl::expected<throwing_swap, int>>);
static_assert(!nothrow_swap_v<metl::expected<int, throwing_swap>>);
static_assert(!nothrow_swap_v<metl::expected<void, throwing_swap>>);
static_assert(nothrow_swap_v<metl::optional<int>> && nothrow_swap_v<metl::expected<int, int>> &&
                  nothrow_swap_v<metl::expected<void, int>> && nothrow_swap_v<metl::unexpected<int>>,
              "and still noexcept when everything is");

void swap_throw_propagates() {
  metl::optional<throwing_swap> a(metl::in_place, 1);
  metl::optional<throwing_swap> b(metl::in_place, 2);
#if METL_NO_EXCEPTIONS
  a.swap(b);  // the element's own swap, reached
  CHECK_EQ(a->value, 2);
  CHECK_EQ(b->value, 1);
#else
  // Before #600 the swap was noexcept, and the throw terminated the program.
  bool caught = false;
  try {
    a.swap(b);
  } catch (int) {
    caught = true;
  }
  CHECK(caught);
#endif
}

}  // namespace

int main() {
  swap_throw_propagates();
  optional_hash_values();
  scope_exit_const_mutable_lambda();
  function_ref_converting_pointer();
  return metl_test::exit_code();
}
