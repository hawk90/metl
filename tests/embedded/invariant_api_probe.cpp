// The second image the I1/I2 symbol audit reads: every public type that
// invariant_probe.cpp does not link (#538).
//
// invariant_probe.cpp is also the code-size ratchet's fixed workload, and growing
// it would change what those budgets mean. This image has no size budget.
// The constraints stated at the top of that file apply here for the same reasons:
// no stdio, its own entry point, no --gc-sections, every METL object local.
//
// tools/check_probe_coverage.py fails when a public header is neither named here
// or there nor listed with a reason.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

#include <metl/arena_allocator.hpp>
#include <metl/atomic_handle.hpp>
#include <metl/atomic_ref.hpp>
#include <metl/bit.hpp>
#include <metl/bitfield.hpp>
#include <metl/bitset.hpp>
#include <metl/coro/deadline_scheduler.hpp>
#include <metl/coro/protothread.hpp>
#include <metl/coro/scheduler.hpp>
#include <metl/coro/stepper.hpp>
#include <metl/crc16.hpp>
#include <metl/crc8.hpp>
#include <metl/delegate.hpp>
#include <metl/endian.hpp>
#include <metl/event_dispatcher.hpp>
#include <metl/expected.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_function.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/fixed_string.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/fsm.hpp>
#include <metl/function_ref.hpp>
#include <metl/hash.hpp>
#include <metl/intrusive_ptr.hpp>
#include <metl/lock.hpp>
#include <metl/lookup_table.hpp>
#include <metl/mmio.hpp>
#include <metl/monotonic_buffer.hpp>
#include <metl/mpmc_queue.hpp>
#include <metl/once_flag.hpp>
#include <metl/optional.hpp>
#include <metl/register_access.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/scope_exit.hpp>
#include <metl/static_allocator.hpp>
#include <metl/static_unordered_set.hpp>
#include <metl/tick_extender.hpp>
#include <metl/variant.hpp>
#include <metl/versioned_handle.hpp>
#include <metl/wait.hpp>

namespace {

volatile std::uint32_t g_sink = 0;

void sink(std::uint32_t value) noexcept {
  g_sink = g_sink + value;
}

// ---- Allocators ------------------------------------------------------------

struct counted {
  explicit counted(std::uint32_t input) noexcept : value(input) {}
  ~counted() { sink(value); }
  std::uint32_t value;
};

void exercise_allocators() noexcept {
  metl::arena_allocator<128> arena;
  const auto mark = arena.mark();
  sink(arena.allocate(8) != nullptr ? 1u : 0u);
  sink(arena.try_emplace<counted>(1u) != nullptr ? 1u : 0u);
  arena.rewind(mark);
  arena.reset();

  metl::monotonic_buffer<64> buffer;
  sink(buffer.allocate(8) != nullptr ? 1u : 0u);
  sink(buffer.try_emplace<std::uint32_t>(2u) != nullptr ? 1u : 0u);
  buffer.reset();

  metl::static_allocator<counted, 2> slots;
  counted* const made = slots.try_new(3u);
  sink(made != nullptr ? 1u : 0u);
  if (made != nullptr) {
    slots.destroy(made);
  }
  sink(slots.try_allocate() != nullptr ? 1u : 0u);
  slots.reset();
}

// ---- Callables -------------------------------------------------------------

std::uint32_t twice(std::uint32_t value) noexcept {
  return value * 2u;
}

void exercise_callables() noexcept {
  const std::uint32_t offset = g_sink;

  const auto capture = [offset](std::uint32_t value) noexcept { return value + offset; };
  const metl::function_ref<std::uint32_t(std::uint32_t)> ref{capture};
  sink(ref(1u));

  metl::fixed_function<std::uint32_t(std::uint32_t), 16> owned{capture};
  sink(owned ? owned(2u) : 0u);
  metl::fixed_function<std::uint32_t(std::uint32_t), 16> moved{std::move(owned)};
  sink(moved ? moved(3u) : 0u);

  const auto bound = metl::delegate<std::uint32_t(std::uint32_t)>::from_function<&twice>();
  sink(bound(4u));

  metl::event_dispatcher<void(std::uint32_t), 2> dispatcher;
  const auto id = dispatcher.try_subscribe(metl::delegate<void(std::uint32_t)>::from_function<&sink>());
  dispatcher.dispatch(5u);
  if (id) {
    sink(dispatcher.unsubscribe(*id) ? 1u : 0u);
  }

  auto on_exit = metl::make_scope_exit([]() noexcept { sink(6u); });
  (void)on_exit;
}

// ---- Containers ------------------------------------------------------------

void exercise_sequences() noexcept {
  metl::ring_buffer<std::uint32_t, 4> ring;
  sink(ring.try_push_back(1u) ? 1u : 0u);
  metl::fixed_deque<std::uint32_t, 4> deque;
  sink(deque.try_push_front(2u) ? 1u : 0u);
  metl::fixed_queue<std::uint32_t, 4> queue;
  sink(queue.try_push(3u) ? 1u : 0u);
  metl::fixed_stack<std::uint32_t, 4> stack;
  sink(stack.try_push(4u) ? 1u : 0u);
  metl::fixed_string<8> text;
  sink(text.try_append("ok") ? 1u : 0u);
  sink(static_cast<std::uint32_t>(ring.size() + deque.size() + queue.size() + stack.size() + text.size()));
}

void exercise_associative() noexcept {
  metl::flat_map<std::uint32_t, std::uint32_t, 4> map;
  sink(map.try_emplace(1u, 2u) ? 1u : 0u);
  sink(map.contains(1u) ? 1u : 0u);
  metl::flat_set<std::uint32_t, 4> set;
  sink(set.try_emplace(3u) ? 1u : 0u);
  metl::static_unordered_set<std::uint32_t, 8> hashed;
  sink(hashed.try_emplace(4u) ? 1u : 0u);

  static constexpr metl::lookup_table<std::uint32_t, std::uint32_t, 1> table{{{{5u, 6u}}}};
  sink(table.find(5u) != nullptr ? 1u : 0u);
}

// ---- Vocabulary ------------------------------------------------------------

metl::expected<std::uint32_t, std::uint32_t> checked(std::uint32_t value) noexcept {
  if (value == 0u) {
    return metl::unexpected<std::uint32_t>(1u);
  }
  return value;
}

void exercise_vocabulary() noexcept {
  const auto result = checked(g_sink);
  sink(result.has_value() ? *result : result.error());

  metl::optional<std::uint32_t> maybe;
  sink(maybe.has_value() ? 1u : 0u);
  maybe.emplace(1u);
  sink(*maybe);

  metl::variant<std::uint32_t, std::uint8_t> either{std::uint32_t{2u}};
  either = std::uint8_t{3u};
  sink(metl::holds_alternative<std::uint8_t>(either) ? 1u : 0u);

  struct node final : metl::intrusive_ref_counter<node> {};
  // Never allocated: the counter's own add-ref and release paths are what link.
  const metl::intrusive_ptr<node> empty;
  sink(empty ? 1u : 0u);

  using handle = metl::versioned_handle<struct probe_handle_tag>;
  sink(handle{}.valid() ? 1u : 0u);
}

// ---- Concurrency -----------------------------------------------------------

// Tier 1 types need a compare-and-swap, which Cortex-M0 lacks. Both types depend on
// a template parameter, so the discarded branch is not instantiated there.
template <typename Handle, typename Value>
void exercise_tier1() noexcept {
  if constexpr (metl::has_lock_free_handle_atomic_v<Handle>) {
    metl::atomic_handle<Handle> cell;
    cell.store(Handle{});
    sink(cell.load().valid() ? 1u : 0u);
  }
  if constexpr (std::atomic<std::size_t>::is_always_lock_free) {
    metl::mpmc_queue<Value, 4> queue;
    sink(queue.try_push(1u) ? 1u : 0u);
    Value out = 0;
    sink(queue.try_pop(out) ? out : 0u);
  }
}

void exercise_concurrency() noexcept {
  exercise_tier1<metl::versioned_handle<struct probe_atomic_tag>, std::uint32_t>();

  alignas(std::atomic<std::uint32_t>) std::uint32_t word = 0;
  const metl::atomic_ref<std::uint32_t> ref(word);
  ref.store(1u);
  sink(ref.load());

  metl::guarded<std::uint32_t, metl::irq_lock> value;
  sink(value.with([](std::uint32_t& held) noexcept { return ++held; }));

  metl::once_flag<metl::null_lock> once;
  sink(metl::try_call_once(once, []() noexcept {}) ? 1u : 0u);

  metl::tick_extender<16> ticks;
  sink(static_cast<std::uint32_t>(
      ticks.now([]() noexcept { return static_cast<std::uint32_t>(g_sink & 0xFFFFu); })));

  metl::cpu_relax();
}

// ---- Control ---------------------------------------------------------------

enum class probe_state : std::uint8_t { off, on };
enum class probe_event : std::uint8_t { toggle };

class count_to_two final : public metl::coro::stepper {
 public:
  metl::coro::step_result step() noexcept override {
    return ++count_ >= 2u ? metl::coro::step_result::done : metl::coro::step_result::yield;
  }

 private:
  std::uint32_t count_ = 0;
};

class yield_once final : public metl::coro::protothread {
 public:
  bool run() noexcept {
    METL_PT_BEGIN();
    METL_PT_YIELD();
    METL_PT_END();
  }
};

metl::optional<std::uint32_t> poll_never(void*, std::uint32_t) noexcept {
  return metl::nullopt;
}

void exercise_control() noexcept {
  static constexpr std::array<metl::fsm_transition<probe_state, probe_event>, 1> transitions{
      {{probe_state::off, probe_event::toggle, probe_state::on, {}}}};
  metl::fsm<probe_state, probe_event, 1> machine{probe_state::off, transitions};
  sink(machine.dispatch(probe_event::toggle) ? 1u : 0u);

  count_to_two stepper;
  yield_once protothread;
  metl::coro::scheduler<4> tasks;
  sink(tasks.try_attach_stepper(stepper) ? 1u : 0u);
  sink(tasks.try_attach_protothread(protothread) ? 1u : 0u);
  tasks.run_once();
  sink(tasks.detach(&stepper) ? 1u : 0u);

  metl::coro::deadline_scheduler<2, std::uint32_t> deadlines;
  sink(deadlines.try_schedule(&stepper, &poll_never, 1u) ? 1u : 0u);
  sink(static_cast<std::uint32_t>(deadlines.run_due(g_sink)));
}

// ---- Bits and registers ----------------------------------------------------

volatile std::uint32_t g_register = 0;

void exercise_bits() noexcept {
  const std::uint32_t word = g_sink;
  sink(static_cast<std::uint32_t>(metl::popcount(word)));
  sink(metl::bitfield<0, 4>::extract(word));
  sink(metl::byteswap(word));

  metl::bitset<16> flags;
  flags.set(3);
  sink(static_cast<std::uint32_t>(flags.count()));

  const std::uint8_t bytes[] = {1u, 2u};
  sink(metl::crc8(bytes, sizeof(bytes)));
  sink(metl::crc16(bytes, sizeof(bytes)));
  sink(metl::fnv1a(bytes, sizeof(bytes)));

  metl::write_once<std::uint32_t>(&g_register, 1u);
  sink(metl::read_once<std::uint32_t>(&g_register));
  const metl::mmio_ptr<std::uint32_t> reg{&g_register};
  reg.write(2u);
  sink(reg.read());
}

int probe_main() noexcept {
  exercise_allocators();
  exercise_callables();
  exercise_sequences();
  exercise_associative();
  exercise_vocabulary();
  exercise_concurrency();
  exercise_control();
  exercise_bits();
  return static_cast<int>(g_sink);
}

}  // namespace

// Why these two are here: the comments on them in invariant_probe.cpp.
extern "C" void abort(void) {
  for (;;) {
  }
}

extern "C" [[noreturn]] void _start(void) {
  (void)probe_main();
  for (;;) {
  }
}
