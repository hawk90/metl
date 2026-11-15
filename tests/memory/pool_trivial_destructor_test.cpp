// object_pool and handle_pool are trivially destructible when T is (#271).
// A namespace-scope pool of a trivially destructible T then registers no
// destructor through __cxa_atexit, and the image carries no ~object_pool().
// For any other T the destructor still destroys every live object.
#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <metl/handle_pool.hpp>
#include <metl/object_pool.hpp>

namespace {

struct plain {
  int a;
  int b;
};

int g_destroyed = 0;
struct counted {
  int value = 0;
  explicit counted(int v) noexcept : value(v) {}
  counted(const counted&) = delete;
  counted& operator=(const counted&) = delete;
  counted(counted&&) = delete;
  counted& operator=(counted&&) = delete;
  ~counted() { ++g_destroyed; }
};

static_assert(std::is_trivially_destructible_v<metl::object_pool<int, 4>>);
static_assert(std::is_trivially_destructible_v<metl::object_pool<plain, 4>>);
static_assert(std::is_trivially_destructible_v<metl::object_pool<std::uint8_t, 300>>);
static_assert(std::is_trivially_destructible_v<metl::handle_pool<int, 4>>);
static_assert(std::is_trivially_destructible_v<metl::handle_pool<plain, 4, std::uint32_t>>);
static_assert(!std::is_trivially_destructible_v<metl::object_pool<counted, 4>>);
static_assert(!std::is_trivially_destructible_v<metl::handle_pool<counted, 4>>);

// Still neither copyable nor movable, and still default-constructible
// without throwing.
template <typename Pool>
constexpr bool still_pinned() {
  static_assert(!std::is_copy_constructible_v<Pool> && !std::is_move_constructible_v<Pool>);
  static_assert(!std::is_copy_assignable_v<Pool> && !std::is_move_assignable_v<Pool>);
  static_assert(std::is_nothrow_default_constructible_v<Pool>);
  return true;
}
static_assert(still_pinned<metl::object_pool<int, 4>>());
static_assert(still_pinned<metl::object_pool<counted, 4>>());
static_assert(still_pinned<metl::handle_pool<int, 4>>());
static_assert(still_pinned<metl::handle_pool<counted, 4>>());

}  // namespace

int main() {
  g_destroyed = 0;
  {
    metl::object_pool<counted, 4> pool;
    CHECK(pool.try_emplace(1) != nullptr);
    CHECK(pool.try_emplace(2) != nullptr);
  }
  CHECK_EQ(g_destroyed, 2);

  g_destroyed = 0;
  {
    metl::handle_pool<counted, 4> pool;
    CHECK(pool.try_emplace(1).valid());
    CHECK(pool.try_emplace(2).valid());
    CHECK(pool.try_emplace(3).valid());
  }
  CHECK_EQ(g_destroyed, 3);

  // A trivially destructible pool works as before.
  metl::object_pool<int, 2> ints;
  int* first = ints.try_emplace(7);
  CHECK(first != nullptr && *first == 7);
  CHECK(ints.destroy(first));
  CHECK(ints.empty());
  return metl_test::exit_code();
}
