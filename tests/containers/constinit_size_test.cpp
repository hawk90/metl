// A namespace-scope container is constant-initialized at any size (#613).
//
// The containers zero their storage during constant evaluation, so a
// namespace-scope one lands in .bss with no constructor left to run before
// main(). They used to zero it one byte (or one slot) per loop iteration, and
// the constant evaluator counts iterations: GCC stops at 262144, Clang near a
// million steps. Past that the object silently fell back to dynamic
// initialization, the case detail/constant_init.hpp exists to prevent. Each
// object below is just past a limit the old loop hit; `constinit` makes the
// fallback a compile error. C++17 cannot constant-initialize these containers
// at all (see constant_init.hpp), so the check is C++20's.

#include "metl_deprecated.hpp"  // still tests metl::variant / static_message_queue (#645)

#include <cstdint>

#include <metl/arena_allocator.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_function.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/monotonic_buffer.hpp>
#include <metl/object_pool.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_message_queue.hpp>
#include <metl/static_unordered_set.hpp>

#if __cplusplus >= 202002L
namespace {
// Past GCC's 262144 iterations as a byte loop.
constinit metl::fixed_vector<unsigned char, 262145> g_bytes;
constinit metl::fixed_deque<unsigned char, 262145> g_deque;
constinit metl::flat_map<std::uint16_t, std::uint16_t, 65537> g_map;
// Past both compilers' limits as a slot loop.
constinit metl::ring_buffer<std::uint32_t, 262145> g_ring;
constinit metl::static_message_queue<std::uint32_t, 262145> g_queue;
constinit metl::object_pool<std::uint32_t, 262145> g_pool;
constinit metl::static_unordered_set<std::uint32_t, 131073> g_set;
// A small raw buffer keeps the byte loop; it must still constant-initialize.
constinit metl::fixed_function<int(), 16> g_function;
// The byte allocators skip the run-time zeroing under C++20 and still clear
// their buffer in one assignment during constant evaluation (#628).
constinit metl::arena_allocator<262145> g_arena;
constinit metl::monotonic_buffer<262145> g_monotonic;
}  // namespace

int main() {
  // Constant-initialized means empty before any code ran.
  const bool empty = g_bytes.empty() && g_deque.empty() && g_map.empty() && g_ring.empty() &&
                     g_queue.empty() && g_pool.empty() && g_set.empty() && !g_function &&
                     g_arena.used() == 0 && g_monotonic.empty();
  return empty ? 0 : 1;
}
#else
int main() {
  return 0;
}
#endif
