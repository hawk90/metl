// Trips every METL_ASSERT precondition of the allocators, pools and the
// remaining utilities once (#364): arena_allocator, monotonic_buffer,
// static_allocator, object_pool, handle_pool, lookup_table, format,
// spsc_byte_ring, tick_extender, coro::deadline_scheduler and coro::scheduler.
//
// Most are the asserting form of a try_ operation that does not fit: emplace
// into a full pool, a write larger than the ring, a number into too small a
// buffer. The rest are an index past the end, a null pointer, an arena rewound
// to a mark it never reached, and a counter reading wider than its bits. Each
// object lives in static storage, since the trip longjmps out.

#include "assert_trip.hpp"
#include "metl_check.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

#include <metl/arena_allocator.hpp>
#include <metl/coro/deadline_scheduler.hpp>
#include <metl/coro/scheduler.hpp>
#include <metl/coro/stepper.hpp>
#include <metl/format.hpp>
#include <metl/handle_pool.hpp>
#include <metl/lock.hpp>
#include <metl/lookup_table.hpp>
#include <metl/monotonic_buffer.hpp>
#include <metl/object_pool.hpp>
#include <metl/optional.hpp>
#include <metl/span.hpp>
#include <metl/spsc_byte_ring.hpp>
#include <metl/static_allocator.hpp>
#include <metl/tick_extender.hpp>

namespace {

using metl_test::trips;

void allocator_trips() {
  static metl::arena_allocator<64> arena;
  CHECK(trips([] { (void)arena.emplace<std::array<unsigned char, 128>>(); }));
  arena.reset();
  static metl::arena_allocator<64>::mark_type later = arena.mark();
  (void)arena.try_emplace<int>(1);
  later = arena.mark();
  arena.reset();
  CHECK(trips([] { arena.rewind(later); }));  // a mark past the current offset

  static metl::monotonic_buffer<16> buffer;
  CHECK(trips([] { (void)buffer.emplace<std::array<unsigned char, 32>>(); }));

  using allocator = metl::static_allocator<int, 1>;
  static allocator one;
  CHECK(trips([] { (void)one.allocate(2); }));
  (void)one.try_allocate(1);
  CHECK(trips([] { (void)one.create(1); }));
  CHECK(trips([] { one.construct(nullptr, 1); }));
  CHECK(trips([] { one.destroy(nullptr); }));
}

void pool_trips() {
  static metl::object_pool<int, 1> objects;
  (void)objects.try_emplace(1);
  CHECK(trips([] { (void)objects.emplace(2); }));

  using pool = metl::handle_pool<int, 1>;
  static pool handles;
  (void)handles.try_emplace(1);
  CHECK(trips([] { (void)handles.emplace(2); }));
  CHECK(trips([] { (void)handles.generation_of(1); }));
}

void lookup_table_trips() {
  static const auto table = metl::make_lookup_table<int, int, 1>({{{1, 10}}});
  CHECK(trips([] { (void)table[1]; }));
  CHECK(trips([] { (void)table.at(1); }));
}

void format_trips() {
  static char two[2];
  CHECK(trips([] { (void)metl::format_uint(metl::span<char>(two, 2), 123u); }));
  CHECK(trips([] { (void)metl::format_int(metl::span<char>(two, 2), -12); }));
  CHECK(trips([] { (void)metl::format_hex(metl::span<char>(two, 2), 0x123u); }));
}

void byte_ring_trips() {
  static metl::spsc_byte_ring<4> ring;
  static const std::byte five[5] = {};
  CHECK(trips([] { ring.write(metl::span<const std::byte>(five, 5)); }));
}

std::uint32_t too_wide() noexcept {
  return 0x100;  // nine bits, for an 8-bit counter
}

void tick_extender_trips() {
  static metl::tick_extender<8, metl::null_lock> clock;
  CHECK(trips([] { (void)clock.now(&too_wide); }));
}

metl::optional<std::uint32_t> never_due(void*, std::uint32_t) noexcept {
  return metl::nullopt;
}

void deadline_scheduler_trips() {
  static metl::coro::deadline_scheduler<1> scheduler;
  static int task_a = 0;
  static int task_b = 0;
  (void)scheduler.try_schedule(&task_a, &never_due, 10);
  CHECK(trips([] { scheduler.schedule(&task_b, &never_due, 20); }));
}

bool never_done(void*) noexcept {
  return false;
}

struct waiting_stepper : metl::coro::stepper {
  metl::coro::step_result step() noexcept override { return metl::coro::step_result::yield; }
};

struct waiting_protothread {
  bool run() noexcept { return false; }
};

// Each asserting attach on a scheduler already full.
void scheduler_trips() {
  static metl::coro::scheduler<1> scheduler;
  static int held = 0;
  static int task = 0;
  static waiting_stepper stepper;
  static waiting_protothread protothread;
  scheduler.attach(&held, &never_done);
  CHECK(trips([] { scheduler.attach(&task, &never_done); }));
  CHECK(trips([] { scheduler.attach_stepper(stepper); }));
  CHECK(trips([] { scheduler.attach_protothread(protothread); }));
}

}  // namespace

int main() {
  if (!metl_test::asserts_active) {
    return metl_test::skip("utility_assert_trips_test", "METL_ASSERT is stripped at this hardening level");
  }
  allocator_trips();
  pool_trips();
  lookup_table_trips();
  format_trips();
  byte_ring_trips();
  tick_extender_trips();
  deadline_scheduler_trips();
  scheduler_trips();
  return metl_test::exit_code();
}
