// handle_pool's constant-initializing constructor (#408).
//
// The default constructor links the free list at run time, so a namespace-scope
// pool is initialized in link order, and an interrupt enabled by an earlier
// constructor can reach it first. Since C++20, `handle_pool(constant_init)`
// builds the same empty pool during constant evaluation. `constinit` below makes
// a pool that is not constant-initialized a compile error, and the run-time half
// checks that the pool it builds behaves exactly like a default-constructed one:
// the same handles (index and generation), sizes and slot reuse.

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>

#include <metl/handle_pool.hpp>

#if defined(__cpp_lib_bit_cast) && defined(__cpp_constexpr) && __cpp_constexpr >= 201907L
namespace {

using small_pool = metl::handle_pool<std::uint32_t, 4>;
// The largest pool the default 16-bit index addresses: the constructor's loop
// must stay inside both compilers' constant-evaluation limits.
using largest_pool = metl::handle_pool<std::uint32_t, metl::handle_pool<std::uint32_t, 1>::max_capacity>;
// A 32-bit index, so the link is four bytes in this target's order.
using wide_pool = metl::handle_pool<std::uint32_t, 8, std::uint32_t>;

constinit small_pool g_small{small_pool::constant_init};
constinit largest_pool g_largest{largest_pool::constant_init};
constinit wide_pool g_wide{wide_pool::constant_init};

template <typename Pool>
void same_as_default(Pool& constant, std::size_t capacity) {
  Pool reference;
  typename Pool::handle_type held_a[16];
  typename Pool::handle_type held_b[16];
  for (int round = 0; round < 3; ++round) {
    for (std::size_t i = 0; i <= capacity; ++i) {  // one past capacity: both refuse
      const auto a = constant.try_emplace(static_cast<std::uint32_t>(i));
      const auto b = reference.try_emplace(static_cast<std::uint32_t>(i));
      CHECK_EQ(static_cast<bool>(a), static_cast<bool>(b));
      if (a && b) {
        CHECK_EQ(a.index(), b.index());
        CHECK_EQ(a.generation(), b.generation());
        const std::uint32_t* held = constant.get(a);
        CHECK(held != nullptr && *held == static_cast<std::uint32_t>(i));
        held_a[i] = a;
        held_b[i] = b;
      }
    }
    CHECK_EQ(constant.size(), reference.size());
    // Free two slots out of order: both pools must hand them back in the same order.
    constant.destroy(held_a[1]);
    reference.destroy(held_b[1]);
    constant.destroy(held_a[capacity - 1]);
    reference.destroy(held_b[capacity - 1]);
    for (int again = 0; again < 2; ++again) {
      const auto a = constant.try_emplace(9u);
      const auto b = reference.try_emplace(9u);
      CHECK_EQ(a.index(), b.index());
      CHECK_EQ(a.generation(), b.generation());
    }
    constant.clear();
    reference.clear();
  }
  CHECK(constant.empty());
}

}  // namespace

int main() {
  // Constant-initialized means empty and usable before any code ran.
  CHECK(g_small.empty() && g_largest.empty() && g_wide.empty());
  same_as_default(g_small, 4);
  same_as_default(g_wide, 8);
  // The largest pool's links are checked by filling it once: every slot must
  // come out in order, and the last one must end the list.
  for (std::size_t i = 0; i < largest_pool::max_capacity; ++i) {
    const auto h = g_largest.try_emplace(1u);
    if (!h || h.index() != i) {
      CHECK(false);
      break;
    }
  }
  CHECK(!g_largest.try_emplace(1u));
  return metl_test::exit_code();
}
#else
int main() {
  return 0;  // C++17: the constructor does not exist; the default one is tested elsewhere
}
#endif
