// The memory-safety floor at METL_HARDENING_NONE.
//
// This TU strips METL_ASSERT. Each case below is a precondition violation that,
// with only METL_ASSERT guarding it, wrote out of bounds:
//
//   - fixed_vector::emplace(pos, ...) on a full vector shifted one element past
//     the storage (and over size_);
//   - pop_back() on an empty vector destroyed data()[-1] and wrapped size_ to
//     SIZE_MAX, so the next push wrote far out of bounds (fixed_stack::pop too);
//   - monotonic_buffer::allocate with a non-power-of-two alignment returned a
//     misaligned pointer.
//
// The cases after those are the rest of the rule in config.hpp: the library
// corrupting its own state, looping without bound, handing back a hazard, or
// calling through an empty callable.
//
// Each now sits behind METL_HARDEN, which survives NONE. The handler longjmps
// out before the abort -- as tests/core/hardening_common.h does -- so the check
// runs without fork() and therefore on QEMU too.
#undef METL_HARDENING  // this test pins its own level, whatever the build passes
#define METL_HARDENING 0

#include "metl_check.hpp"

#include <atomic>
#include <csetjmp>
#include <cstddef>
#include <cstdint>
#include <new>

#include <metl/arena_allocator.hpp>
#include <metl/assert.hpp>
#include <metl/atomic_ref.hpp>
#include <metl/delegate.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_function.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/fixed_string.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/function_ref.hpp>
#include <metl/intrusive_ptr.hpp>
#include <metl/mmio.hpp>
#include <metl/monotonic_buffer.hpp>
#include <metl/parse.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/span.hpp>
#include <metl/spsc_byte_ring.hpp>
#include <metl/static_message_queue.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

std::jmp_buf g_jump;
bool g_fired = false;

void capture(const char* /*expr*/, const char* /*file*/, int /*line*/) noexcept {
  g_fired = true;
  std::longjmp(g_jump, 1);
}

// Runs `misuse` and reports whether a guard stopped it. The longjmp skips the
// destructors of whatever `misuse` had live (a fixed_function it was building,
// say): those objects are abandoned, not destroyed, which is harmless here.
// CMakeLists.txt builds this file without /EH on MSVC so that holds there too.
template <typename F>
bool guarded_against(F misuse) {
  g_fired = false;
  if (setjmp(g_jump) == 0) {
    misuse();
  }
  return g_fired;
}

int widen(long x) {
  return static_cast<int>(x);
}

struct counted final : metl::intrusive_ref_counter<counted> {};

// Counts move assignments, so a test can see one that should never have run.
struct counts_assignments {
  static int assignments;
  int value = 0;

  counts_assignments() = default;
  counts_assignments(int v) : value(v) {}
  counts_assignments(const counts_assignments&) = default;
  counts_assignments(counts_assignments&&) noexcept = default;
  counts_assignments& operator=(const counts_assignments&) = default;
  counts_assignments& operator=(counts_assignments&& other) noexcept {
    ++assignments;
    value = other.value;
    return *this;
  }
  bool operator<(const counts_assignments& other) const noexcept { return value < other.value; }
};
int counts_assignments::assignments = 0;

// Aligned for itself, but std::atomic of it wants 8 on the hosts this runs on.
struct two_words {
  std::uint32_t low;
  std::uint32_t high;
};

}  // namespace

int main() {
  metl::set_assert_handler(&capture);

  // Positional emplace into a full vector.
  {
    metl::fixed_vector<int, 4> v;
    for (int i = 0; i < 4; ++i) {
      v.push_back(i);
    }
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      v.emplace(v.begin(), 42);
    }
    CHECK(g_fired);
    CHECK_EQ(v.size(), 4u);
    CHECK_EQ(v[0], 0);
  }

  // pop_back on an empty vector, and pop on an empty fixed_stack.
  {
    metl::fixed_vector<int, 4> v;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      v.pop_back();
    }
    CHECK(g_fired);
    CHECK_EQ(v.size(), 0u);

    metl::fixed_stack<int, 4> s;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      s.pop();
    }
    CHECK(g_fired);
    CHECK_EQ(s.size(), 0u);
  }

  // A non-power-of-two alignment.
  {
    metl::monotonic_buffer<64> buffer;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      (void)buffer.allocate(4, 3);
    }
    CHECK(g_fired);
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      (void)buffer.allocate(4, 0);
    }
    CHECK(g_fired);
    g_fired = false;
    void* ok = buffer.allocate(4, 4);
    CHECK(!g_fired);
    CHECK(ok != nullptr);
  }

  // Empty pops on the ring containers: destroyed a dead slot and wrapped
  // size_ to SIZE_MAX; a fixed_queue destructor then looped ~2^64 times.
  {
    metl::ring_buffer<int, 4> ring;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      ring.pop_front();
    }
    CHECK(g_fired);
    CHECK_EQ(ring.size(), 0u);

    metl::fixed_deque<int, 4> deque;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      deque.pop_back();
    }
    CHECK(g_fired);
    CHECK_EQ(deque.size(), 0u);

    metl::fixed_queue<int, 4> queue;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      queue.pop();
    }
    CHECK(g_fired);
    CHECK_EQ(queue.size(), 0u);

    metl::static_message_queue<int, 4> messages;
    g_fired = false;
    if (setjmp(g_jump) == 0) {
      messages.pop();
    }
    CHECK(g_fired);
    CHECK_EQ(messages.size(), 0u);
  }

  // fixed_vector: positions outside [begin, end], empty and reversed erase, a
  // resize that can never finish, an own-range assign, and emplace_back on a
  // Capacity-0 vector (back() would be data()[SIZE_MAX]).
  {
    metl::fixed_vector<int, 4> v;
    v.push_back(1);
    CHECK(guarded_against([&] { v.emplace(v.begin() + 2, 9); }));
    CHECK(guarded_against([&] { v.insert(v.begin() + 2, 9); }));
    CHECK(guarded_against([&] { (void)v.try_insert(v.begin() + 2, 9); }));
    CHECK_EQ(v.size(), 1u);

    metl::fixed_vector<int, 4> empty;
    CHECK(guarded_against([&] { empty.erase(empty.begin()); }));
    CHECK(guarded_against([&] { v.erase(v.begin() + 1, v.begin()); }));
    CHECK(guarded_against([&] { v.resize(5); }));
    CHECK(guarded_against([&] { v.resize(5, 0); }));
    CHECK(guarded_against([&] { v.assign(v.begin(), v.end()); }));
    CHECK_EQ(v.size(), 1u);
    CHECK_EQ(v[0], 1);

    metl::fixed_vector<int, 0> none;
    CHECK(guarded_against([&] { (void)none.emplace_back(1); }));

    // Count and range inserts outside [begin, end] are refused too: by their
    // own check, or by the emplace each delegates to before anything moves.
    const int source[2] = {7, 8};
    CHECK(guarded_against([&] { v.insert(v.begin() + 2, 1u, 9); }));
    CHECK(guarded_against([&] { v.insert(v.begin() + 2, source, source + 2); }));
    CHECK_EQ(v.size(), 1u);

    // Neither delegates to emplace for nothing to insert, so only their own
    // check sees the position.
    CHECK(guarded_against([&] { (void)v.insert(v.begin() + 2, 0u, 9); }));
    CHECK(guarded_against([&] { (void)v.insert(v.begin() + 2, source, source); }));

    // On a full vector the try_ forms return end() before reaching emplace, so
    // only their own check sees the position.
    metl::fixed_vector<int, 2> full;
    full.push_back(1);
    full.push_back(2);
    CHECK(guarded_against([&] { (void)full.try_emplace(full.begin() + 3, 9); }));
    CHECK(guarded_against([&] { (void)full.try_insert(full.begin() + 3, 1u, 9); }));
    CHECK(guarded_against([&] { (void)full.try_insert(full.begin() + 3, source, source + 2); }));
    CHECK_EQ(full.size(), 2u);
  }

  // fixed_priority_queue: pop on an empty queue underflows the last index.
  // (Without its own guard, the vector's pop_back would still fire -- but only
  // after the out-of-bounds move the underflow causes.)
  {
    metl::fixed_priority_queue<int, 4> queue;
    CHECK(guarded_against([&] { queue.pop(); }));
    CHECK(queue.empty());

    // The vector's check firing late is not enough: by then the move from
    // index SIZE_MAX into slot 0 has run. Count it.
    metl::fixed_priority_queue<counts_assignments, 4> counted_queue;
    counts_assignments::assignments = 0;
    CHECK(guarded_against([&] { counted_queue.pop(); }));
    CHECK_EQ(counts_assignments::assignments, 0);
  }

  // static_unordered_map: insert_or_assign of a new key into a full map would
  // hand back a reference to slot npos.
  {
    metl::static_unordered_map<int, int, 2> map;
    map.emplace(1, 10);
    map.emplace(2, 20);
    CHECK(guarded_against([&] { (void)map.insert_or_assign(3, 30); }));
    CHECK_EQ(map.size(), 2u);
  }

  // static_unordered_map / static_unordered_set: at this level nothing stops
  // an insert past Capacity, which is safe while an empty bucket remains. Once
  // every bucket is taken, the next new key has nowhere to go: npos.
  {
    using map_type = metl::static_unordered_map<int, int, 2>;
    map_type map;
    for (int key = 0; key < static_cast<int>(map_type::bucket_count); ++key) {
      CHECK(!guarded_against([&] { (void)map.emplace(key, key); }));
    }
    CHECK(guarded_against([&] { (void)map.emplace(-1, 0); }));
    CHECK_EQ(map.size(), map_type::bucket_count);

    using set_type = metl::static_unordered_set<int, 2>;
    set_type set;
    for (int key = 0; key < static_cast<int>(set_type::bucket_count); ++key) {
      CHECK(!guarded_against([&] { (void)set.emplace(key); }));
    }
    CHECK(guarded_against([&] { (void)set.emplace(-1); }));
    CHECK_EQ(set.size(), set_type::bucket_count);
  }

  // flat_map / flat_set: a duplicate key would be stored twice.
  {
    metl::flat_map<int, int, 4> map;
    map.emplace(1, 10);
    CHECK(guarded_against([&] { map.emplace(1, 20); }));
    CHECK_EQ(map.size(), 1u);

    metl::flat_set<int, 4> set;
    set.emplace(1);
    CHECK(guarded_against([&] { set.emplace(1); }));
    CHECK_EQ(set.size(), 1u);
  }

  // flat_map / flat_set: emplace or insert_or_assign of a new key into a full
  // container. The insert is refused, which leaves its position at size() ==
  // Capacity, and the reference handed back would be one past the end.
  {
    metl::flat_map<int, int, 2> map;
    map.emplace(1, 10);
    map.emplace(2, 20);
    CHECK(guarded_against([&] { (void)map.emplace(3, 30); }));
    CHECK(guarded_against([&] { (void)map.insert_or_assign(3, 30); }));
    CHECK_EQ(map.size(), 2u);
    CHECK(!guarded_against([&] { (void)map.insert_or_assign(2, 21); }));
    CHECK_DEREF_EQ(map.find(2), 21);

    metl::flat_set<int, 2> set;
    set.emplace(1);
    set.emplace(2);
    CHECK(guarded_against([&] { (void)set.emplace(3); }));
    CHECK_EQ(set.size(), 2u);
  }

  // arena_allocator: a non-power-of-two alignment corrupts the bump offset,
  // as monotonic_buffer's does.
  {
    metl::arena_allocator<64> arena;
    CHECK(guarded_against([&] { (void)arena.allocate(4, 3); }));
    CHECK(guarded_against([&] { (void)arena.allocate(4, 0); }));
    CHECK(arena.empty());
    CHECK(!guarded_against([&] { (void)arena.allocate(4, 4); }));
    // The alignment is checked whatever the size, as monotonic_buffer checks it:
    // an empty request with a bad alignment is the same caller bug, and
    // returning null for it hid that.
    CHECK(guarded_against([&] { (void)arena.allocate(0, 3); }));
    CHECK(!guarded_against([&] { CHECK(arena.allocate(0, 4) == nullptr); }));
  }

  // monotonic_buffer: the same zero-byte cases, which it already got right.
  {
    metl::monotonic_buffer<64> buffer;
    CHECK(guarded_against([&] { (void)buffer.allocate(0, 3); }));
    CHECK(!guarded_against([&] { CHECK(buffer.allocate(0, 4) == nullptr); }));
  }

  // spsc_byte_ring: consuming more than is readable moves the read index past
  // the write index, and readable_size() wraps to nearly SIZE_MAX.
  {
    metl::spsc_byte_ring<8> ring;
    const std::byte bytes[2] = {std::byte{1}, std::byte{2}};
    CHECK(ring.try_write(metl::span<const std::byte>(bytes, 2)));
    CHECK(guarded_against([&] { ring.consume(3); }));
    CHECK_EQ(ring.readable_size(), 2u);
    CHECK(!guarded_against([&] { ring.consume(2); }));
    CHECK_EQ(ring.readable_size(), 0u);
  }

  // span: a fixed extent over the wrong count, and views past the end, would
  // report a size() larger than the storage behind them.
  {
    int data[4] = {1, 2, 3, 4};
    metl::span<int> all(data);
    CHECK(guarded_against([&] { (void)metl::span<int, 4>(data, 2); }));
    CHECK(guarded_against([&] { (void)all.subspan(9); }));
    CHECK(guarded_against([&] { (void)all.subspan(1, 9); }));
    CHECK(guarded_against([&] { (void)all.first(9); }));
    CHECK(guarded_against([&] { (void)all.last(9); }));
    CHECK(guarded_against([&] { (void)all.first<9>(); }));
    CHECK(guarded_against([&] { (void)all.last<9>(); }));
    CHECK(guarded_against([&] { (void)all.subspan<9>(); }));
    CHECK(guarded_against([&] { (void)all.subspan<1, 9>(); }));

    // Pointer pairs: reversed, and a fixed extent over the wrong length.
    CHECK(guarded_against([&] { (void)metl::span<int>(data + 2, data); }));
    CHECK(guarded_against([&] { (void)metl::span<int, 4>(data, data + 2); }));
    CHECK(!guarded_against([&] { (void)metl::span<int, 4>(data, data + 4); }));

    // A fixed extent over a container, or a span, of the wrong size.
    metl::fixed_vector<int, 4> two;
    two.push_back(1);
    two.push_back(2);
    CHECK(guarded_against([&] { (void)metl::span<int, 4>(two); }));
    CHECK(guarded_against([&] { (void)metl::span<int, 4>(metl::span<int>(data, 2)); }));
    CHECK(!guarded_against([&] { (void)metl::span<int, 4>(all); }));
  }

  // fixed_string: strlen of a null pointer.
  {
    metl::fixed_string<8> text;
    CHECK(guarded_against([&] { (void)text.try_assign(nullptr); }));
    CHECK(guarded_against([&] { (void)text.try_append(nullptr); }));
    const char* null_text = nullptr;
    CHECK(guarded_against([&] { (void)(text == null_text); }));
  }

  // Callables: a null function stored as engaged, and a call through an empty
  // wrapper -- both a jump to address 0.
  {
    int (*null_fn)(long) = nullptr;
    CHECK(guarded_against([&] { metl::fixed_function<int(int)> f(null_fn); }));
    CHECK(guarded_against([&] { metl::fixed_any_invocable<int(int)> f(null_fn); }));
    int (*null_exact)(int) = nullptr;
    CHECK(guarded_against([&] { metl::function_ref<int(int)> f(null_exact); }));

    metl::fixed_function<int(long)> empty_function;
    CHECK(guarded_against([&] { (void)empty_function(1); }));
    metl::fixed_any_invocable<int(long)> empty_invocable;
    CHECK(guarded_against([&] { (void)empty_invocable(1); }));
    metl::delegate<int(long)> empty_delegate;
    CHECK(guarded_against([&] { (void)empty_delegate(1); }));
    metl::fixed_function<int(long) noexcept> empty_noexcept_function;
    CHECK(guarded_against([&] { (void)empty_noexcept_function(1); }));
    metl::fixed_any_invocable<int(long) noexcept> empty_noexcept_invocable;
    CHECK(guarded_against([&] { (void)empty_noexcept_invocable(1); }));
    metl::function_ref<int(long)> empty_ref;
    CHECK(guarded_against([&] { (void)empty_ref(1); }));

    // try_assign of a null pointer of the exact signature.
    metl::fixed_function<int(int)> assigned;
    CHECK(guarded_against([&] { (void)assigned.try_assign(null_exact); }));
    CHECK(!assigned);
    metl::fixed_any_invocable<int(int)> assigned_invocable;
    CHECK(guarded_against([&] { (void)assigned_invocable.try_assign(null_exact); }));
    CHECK(!assigned_invocable);

    metl::fixed_function<int(long)> bound(&widen);
    CHECK(!guarded_against([&] { (void)bound(1); }));
  }

  // intrusive_ptr: releasing a count that is already zero wraps it.
  {
    counted object;
    CHECK(guarded_against([&] { intrusive_ptr_release(&object); }));
  }

  // parse: the asserting form would return a result built from the error.
  {
    const char text[] = "x";
    CHECK(guarded_against([&] { (void)metl::parse_uint<unsigned>(metl::span<const char>(text, 1)); }));
    CHECK(guarded_against([&] { (void)metl::parse_int<int>(metl::span<const char>(text, 1)); }));
    CHECK(guarded_against([&] { (void)metl::parse_hex<unsigned>(metl::span<const char>(text, 1)); }));
  }

  // atomic_ref: an object aligned for T but not for std::atomic<T> would be
  // accessed by a misaligned atomic instruction -- a fault, or a torn access.
  {
    if constexpr (alignof(std::atomic<two_words>) > alignof(two_words)) {
      alignas(std::atomic<two_words>) unsigned char buffer[2 * sizeof(two_words)];
      two_words* misaligned = ::new (static_cast<void*>(buffer + alignof(two_words))) two_words{};
      CHECK(guarded_against([&] { metl::atomic_ref<two_words> ref(*misaligned); }));
      two_words* aligned = ::new (static_cast<void*>(buffer)) two_words{};
      CHECK(!guarded_against([&] { metl::atomic_ref<two_words> ref(*aligned); }));
    }
  }

  // mmio: a misaligned register address hard-faults on Cortex-M0.
  {
    CHECK(guarded_against([&] { metl::mmio_ptr<std::uint32_t> reg(std::uintptr_t{0x1002}); }));
  }

  return metl_test::exit_code();
}
