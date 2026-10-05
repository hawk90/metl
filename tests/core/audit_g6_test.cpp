// Regression tests from the third review pass that need no exceptions, so they run
// on QEMU too. Each block failed against the unfixed headers.
//
// `poisoned` writes -1 into itself on destruction and -2 when moved from, so a
// read of a dead or moved-from object shows up as a wrong value without a
// sanitizer.

#include "metl_check.hpp"

#include <type_traits>

#include <metl/arena_allocator.hpp>
#include <metl/expected.hpp>
#include <metl/fixed_vector.hpp>

namespace {

struct poisoned {
  int value;
  explicit poisoned(int v = 0) noexcept : value(v) {}
  poisoned(const poisoned& o) noexcept : value(o.value) {}
  poisoned(poisoned&& o) noexcept : value(o.value) { o.value = -2; }
  poisoned& operator=(const poisoned& o) noexcept {
    value = o.value;
    return *this;
  }
  poisoned& operator=(poisoned&& o) noexcept {
    value = o.value;
    o.value = -2;
    return *this;
  }
  ~poisoned() { value = -1; }
};

struct error_with_fallback {
  poisoned fallback;
  int code;
};

struct value_with_code {
  int payload;
  poisoned code;
};

int g_destroyed = 0;
struct counted {
  ~counted() { ++g_destroyed; }
};

}  // namespace

int main() {
  // fixed_vector::assign(n, v[0]): clear() used to destroy the source first.
  {
    metl::fixed_vector<poisoned, 8> v;
    v.emplace_back(7);
    v.emplace_back(8);
    v.assign(3, v[0]);
    CHECK_EQ(v.size(), 3u);
    for (const auto& item : v) {
      CHECK_EQ(item.value, 7);
    }
  }

  // expected: switching state from a subobject of the member being replaced.
  {
    metl::expected<poisoned, error_with_fallback> e{metl::unexpect, error_with_fallback{poisoned(5), 1}};
    e = e.error().fallback;  // error -> value, reading from inside the error
    CHECK(e.has_value());
    CHECK_EQ(e->value, 5);
  }
  {
    metl::expected<value_with_code, poisoned> e{metl::in_place, value_with_code{1, poisoned(9)}};
    e.emplace_error(e.value().code);  // value -> error, reading from inside the value
    CHECK(!e.has_value());
    CHECK_EQ(e.error().value, 9);
  }

  // arena_allocator: destroys what is left when it goes out of scope, and
  // cannot be copied (a copy carried the destroy records and ran them twice).
  static_assert(!std::is_copy_constructible_v<metl::arena_allocator<64>>, "arena must not be copyable");
  static_assert(!std::is_move_constructible_v<metl::arena_allocator<64>>, "arena must not be movable");
  {
    g_destroyed = 0;
    {
      metl::arena_allocator<128> arena;
      CHECK(arena.try_emplace<counted>() != nullptr);
      CHECK(arena.try_emplace<counted>() != nullptr);
    }
    CHECK_EQ(g_destroyed, 2);
  }

  return metl_test::exit_code();
}
