// The type layouts METL commits to on ARM (#539): sizeof, alignof and the offsets of
// the members a user names. tools/check_layout.py compares these rows with the
// previous release and fails on a change in a minor or patch release, unless the
// change is listed there. The invariants job compiles this file for each Cortex-M,
// so a row that stops matching the headers stops the build.
//
// The rows are what ARM GCC with libstdc++ measures. Other standard libraries can
// differ: libc++ gives metl::fsm 40 bytes here, not 20.
//
// A row is the measurement, not a limit: both directions fail. The sizeof ratchet
// in tests/core/ram_footprint_test.cpp misses a change that keeps the size.

#include <cstddef>
#include <cstdint>

#include <metl/arena_allocator.hpp>
#include <metl/atomic_handle.hpp>
#include <metl/atomic_ref.hpp>
#include <metl/bitset.hpp>
#include <metl/coro/deadline_scheduler.hpp>
#include <metl/coro/protothread.hpp>
#include <metl/coro/scheduler.hpp>
#include <metl/coro/stepper.hpp>
#include <metl/crc16.hpp>
#include <metl/crc32.hpp>
#include <metl/crc8.hpp>
#include <metl/delegate.hpp>
#include <metl/event_dispatcher.hpp>
#include <metl/expected.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_function.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/fixed_string.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/fsm.hpp>
#include <metl/function_ref.hpp>
#include <metl/handle_pool.hpp>
#include <metl/intrusive_forward_list.hpp>
#include <metl/intrusive_list.hpp>
#include <metl/intrusive_ptr.hpp>
#include <metl/lock.hpp>
#include <metl/lookup_table.hpp>
#include <metl/mmio.hpp>
#include <metl/monotonic_buffer.hpp>
#include <metl/mpmc_queue.hpp>
#include <metl/object_pool.hpp>
#include <metl/once_flag.hpp>
#include <metl/optional.hpp>
#include <metl/parse.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/scope_exit.hpp>
#include <metl/span.hpp>
#include <metl/spsc_byte_ring.hpp>
#include <metl/spsc_queue.hpp>
#include <metl/static_allocator.hpp>
#include <metl/static_message_queue.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>
#include <metl/tick_extender.hpp>
#include <metl/variant.hpp>
#include <metl/versioned_handle.hpp>

namespace {

enum class layout_state : std::uint8_t { off, on };
enum class layout_event : std::uint8_t { toggle };
struct layout_tag;
struct layout_list_node : metl::list_hook<>, metl::forward_list_hook<> {};
struct layout_node final : metl::intrusive_ref_counter<layout_node> {};
using layout_scope_exit = decltype(metl::make_scope_exit(static_cast<void (*)() noexcept>(nullptr)));

// Aliases: offsetof takes one macro argument for the type, so its commas must go.
using layout_lookup_entry = metl::lookup_entry<std::uint32_t, std::uint32_t>;
using layout_parsed = metl::parsed<std::uint32_t>;
using layout_transition = metl::fsm_transition<layout_state, layout_event>;

#define LAYOUT(size, align, ...)                                      \
  static_assert(sizeof(__VA_ARGS__) == (size), #__VA_ARGS__ " size"); \
  static_assert(alignof(__VA_ARGS__) == (align), #__VA_ARGS__ " alignment")
#define OFFSET(offset, member, type) static_assert(offsetof(type, member) == (offset), #type " " #member)

// clang-format off
LAYOUT(72, 8, metl::arena_allocator<64>);
LAYOUT(4, 4, metl::atomic_ref<std::uint32_t>);
LAYOUT(4, 4, metl::bitset<16>);
LAYOUT(8, 4, metl::bitset<64>);
LAYOUT(128, 8, metl::coro::deadline_scheduler<4, std::uint32_t>);
LAYOUT(4, 4, metl::coro::protothread);
LAYOUT(60, 4, metl::coro::scheduler<4>);
LAYOUT(8, 4, metl::coro::stepper);
LAYOUT(2, 1, metl::crc8_params);
LAYOUT(4, 2, metl::crc16_params);
LAYOUT(8, 4, metl::crc32_params);
LAYOUT(8, 4, metl::delegate<std::uint32_t(std::uint32_t)>);
LAYOUT(72, 8, metl::event_dispatcher<void(std::uint32_t), 4>);
LAYOUT(8, 4, metl::expected<std::uint32_t, std::uint32_t>);
LAYOUT(8, 4, metl::expected<void, std::uint32_t>);
LAYOUT(24, 4, metl::fixed_deque<std::uint32_t, 4>);
LAYOUT(24, 8, metl::fixed_function<std::uint32_t(std::uint32_t), 16>);
LAYOUT(24, 4, metl::fixed_priority_queue<std::uint32_t, 4>);
LAYOUT(24, 4, metl::fixed_queue<std::uint32_t, 4>);
LAYOUT(20, 4, metl::fixed_stack<std::uint32_t, 4>);
LAYOUT(24, 4, metl::fixed_string<16>);
LAYOUT(20, 4, metl::fixed_vector<std::uint32_t, 4>);
LAYOUT(40, 4, metl::flat_map<std::uint32_t, std::uint32_t, 4>);
LAYOUT(24, 4, metl::flat_set<std::uint32_t, 4>);
LAYOUT(20, 4, metl::fsm<layout_state, layout_event, 1>);
LAYOUT(12, 4, metl::fsm_transition<layout_state, layout_event>);
LAYOUT(8, 4, metl::function_ref<std::uint32_t(std::uint32_t)>);
LAYOUT(4, 4, metl::guarded<std::uint32_t, metl::null_lock>);
LAYOUT(32, 4, metl::handle_pool<std::uint32_t, 4>);
LAYOUT(4, 4, metl::intrusive_ptr<layout_node>);
LAYOUT(16, 4, metl::lookup_table<std::uint32_t, std::uint32_t, 2>);
LAYOUT(8, 4, metl::lookup_entry<std::uint32_t, std::uint32_t>);
LAYOUT(4, 4, metl::mmio_ptr<std::uint32_t>);
LAYOUT(72, 8, metl::monotonic_buffer<64>);
LAYOUT(28, 4, metl::object_pool<std::uint32_t, 4>);
LAYOUT(4, 4, metl::once_flag<metl::null_lock>);
LAYOUT(8, 4, metl::optional<std::uint32_t>);
LAYOUT(12, 4, metl::parsed<std::uint32_t>);
LAYOUT(24, 4, metl::ring_buffer<std::uint32_t, 4>);
LAYOUT(8, 4, layout_scope_exit);
LAYOUT(8, 4, metl::span<std::uint32_t>);
LAYOUT(4, 4, metl::span<std::uint32_t, 4>);
LAYOUT(192, 64, metl::spsc_byte_ring<16>);
LAYOUT(192, 64, metl::spsc_queue<std::uint32_t, 4>);
LAYOUT(16, 8, metl::static_allocator<std::uint32_t, 4>);
LAYOUT(28, 4, metl::static_message_queue<std::uint32_t, 4>);
LAYOUT(148, 4, metl::static_unordered_map<std::uint32_t, std::uint32_t, 8>);
LAYOUT(84, 4, metl::static_unordered_set<std::uint32_t, 8>);
LAYOUT(8, 8, metl::tick_extender<16>);
LAYOUT(8, 4, metl::variant<std::uint32_t, std::uint8_t>);
LAYOUT(4, 4, metl::versioned_handle<layout_tag>);
LAYOUT(12, 4, metl::list_hook<>);
LAYOUT(8, 4, metl::forward_list_hook<>);
LAYOUT(16, 4, metl::intrusive_list<layout_list_node>);
LAYOUT(12, 4, metl::intrusive_forward_list<layout_list_node>);

// Cortex-M0 has no compare-and-swap, so these two do not exist there.
#if !defined(__ARM_ARCH_6M__)
LAYOUT(192, 64, metl::mpmc_queue<std::uint32_t, 4>);
LAYOUT(4, 4, metl::atomic_handle<metl::versioned_handle<layout_tag>>);
#endif

OFFSET(0, initial, metl::crc8_params);
OFFSET(1, final_xor, metl::crc8_params);
OFFSET(0, initial, metl::crc16_params);
OFFSET(2, final_xor, metl::crc16_params);
OFFSET(0, initial, metl::crc32_params);
OFFSET(4, final_xor, metl::crc32_params);
OFFSET(0, key, layout_lookup_entry);
OFFSET(4, value, layout_lookup_entry);
OFFSET(0, value, layout_parsed);
OFFSET(4, tail, layout_parsed);
OFFSET(0, from, layout_transition);
OFFSET(1, event, layout_transition);
OFFSET(2, to, layout_transition);
OFFSET(4, action, layout_transition);
// clang-format on

}  // namespace
