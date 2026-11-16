// Zero-capacity containers take no room for an element (#696).
//
// A zero-length array is ill-formed, so these types kept storage for one
// element, plus padding, at Capacity 0. Now:
//
// - fixed_vector and the ring family hold their storage as a base
//   (detail/capacity_storage.hpp). An empty base takes no room, and every
//   Capacity >= 1 layout stays what it was.
// - The pools, event_dispatcher, arena_allocator and monotonic_buffer have a
//   Capacity 0 specialisation that is an empty class.
//
// Sizes are pinned against size_t and against mirror structs, so the test holds
// on 32- and 64-bit hosts. Every member of each specialisation runs, and every
// precondition it asserts is tripped.

#include "assert_trip.hpp"
#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <metl/arena_allocator.hpp>
#include <metl/delegate.hpp>
#include <metl/event_dispatcher.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/handle_pool.hpp>
#include <metl/monotonic_buffer.hpp>
#include <metl/object_pool.hpp>
#include <metl/ring_buffer.hpp>

namespace {

// --- sizes -----------------------------------------------------------------

static_assert(sizeof(metl::fixed_vector<int, 0>) == sizeof(std::size_t), "only the size");
static_assert(sizeof(metl::fixed_stack<int, 0>) == sizeof(std::size_t), "only the size");
static_assert(sizeof(metl::ring_buffer<int, 0>) == 2 * sizeof(std::size_t), "only head and size");
static_assert(sizeof(metl::fixed_deque<int, 0>) == 2 * sizeof(std::size_t), "only head and size");
static_assert(sizeof(metl::fixed_queue<int, 0>) == 2 * sizeof(std::size_t), "only head and size");
// flat_set holds its storage as a base too. flat_map cannot: its value_type is
// nested in the class, so the empty storage is a member and costs a byte plus
// padding (measured and decided on #696). The empty comparator costs nothing
// in either (#318).
static_assert(sizeof(metl::flat_set<int, 0>) == sizeof(std::size_t), "only the size");
static_assert(sizeof(metl::flat_map<int, int, 0>) == 2 * sizeof(std::size_t),
              "the size, and the empty storage member's byte padded to a word");

static_assert(std::is_empty_v<metl::object_pool<int, 0>>, "a pool of no slots holds nothing");
static_assert(std::is_empty_v<metl::handle_pool<int, 0>>, "a pool of no slots holds nothing");
static_assert(std::is_empty_v<metl::event_dispatcher<void(int), 0>>, "no slots, nothing held");
static_assert(std::is_empty_v<metl::arena_allocator<0>>, "an arena of no bytes holds nothing");
static_assert(std::is_empty_v<metl::monotonic_buffer<0>>, "a buffer of no bytes holds nothing");

// Capacity >= 1 keeps the layout it had: the storage first, then the counters.
struct vector_of_4_ints {
  alignas(int) unsigned char bytes[4 * sizeof(int)];
  std::size_t size;
};
struct ring_of_4_ints {
  alignas(int) unsigned char bytes[4 * sizeof(int)];
  std::size_t head;
  std::size_t size;
};
static_assert(sizeof(metl::fixed_vector<int, 4>) == sizeof(vector_of_4_ints), "unchanged");
static_assert(sizeof(metl::ring_buffer<int, 4>) == sizeof(ring_of_4_ints), "unchanged");
static_assert(sizeof(metl::fixed_deque<int, 4>) == sizeof(ring_of_4_ints), "unchanged");

int heard = 0;
void hear(int value) {
  heard += value;
}

// Kept outside the tripping callables: a trip longjmps out without running
// destructors.
metl::object_pool<int, 0> object_pool0;
metl::handle_pool<int, 0> handle_pool0;
metl::arena_allocator<0> arena0;
metl::monotonic_buffer<0> buffer0;

}  // namespace

int main() {
  // fixed_vector and the ring family behave as empty and full at once.
  {
    metl::fixed_vector<int, 0> vec;
    CHECK(vec.empty());
    CHECK(vec.full());
    CHECK(!vec.try_push_back(1));
    CHECK(vec.begin() == vec.end());
    CHECK_EQ(vec.capacity(), std::size_t{0});

    metl::ring_buffer<int, 0> ring;
    CHECK(ring.empty());
    CHECK(!ring.try_push_back(1));
    metl::fixed_deque<int, 0> deque;
    CHECK(!deque.try_push_front(1));
    CHECK(!deque.try_push_back(1));
    metl::fixed_queue<int, 0> queue;
    CHECK(!queue.try_push(1));
    metl::fixed_stack<int, 0> stack;
    CHECK(!stack.try_push(1));

    // Copies and moves of an empty container are empty.
    metl::fixed_vector<int, 0> copy = vec;
    metl::ring_buffer<int, 0> moved = static_cast<metl::ring_buffer<int, 0>&&>(ring);
    CHECK(copy.empty());
    CHECK(moved.empty());
  }

  // object_pool: nothing to emplace, nothing to destroy.
  {
    int outsider = 0;
    CHECK(object_pool0.try_emplace(1) == nullptr);
    CHECK(!object_pool0.destroy(&outsider));
    CHECK(!object_pool0.contains(&outsider));
    object_pool0.clear();
    CHECK(object_pool0.empty());
    CHECK(object_pool0.full());
    CHECK_EQ(object_pool0.size(), std::size_t{0});
    CHECK_EQ(object_pool0.capacity(), std::size_t{0});
    CHECK_EQ(object_pool0.available(), std::size_t{0});
    if (metl_test::asserts_active) {
      CHECK(metl_test::trips([] { (void)object_pool0.emplace(1); }));
    }
  }

  // handle_pool: no handle is ever live.
  {
    using pool_t = metl::handle_pool<int, 0>;
    const pool_t::handle_type handle = handle_pool0.try_emplace(1);
    CHECK(!handle.valid());
    CHECK(handle_pool0.get(handle) == nullptr);
    const pool_t& view = handle_pool0;
    CHECK(view.get(handle) == nullptr);
    CHECK(!handle_pool0.contains(handle));
    CHECK(!handle_pool0.destroy(handle));
    handle_pool0.clear();
    CHECK(handle_pool0.empty());
    CHECK(handle_pool0.full());
    CHECK_EQ(handle_pool0.size(), std::size_t{0});
    CHECK_EQ(handle_pool0.capacity(), std::size_t{0});
    CHECK_EQ(handle_pool0.available(), std::size_t{0});
    if (metl_test::asserts_active) {
      CHECK(metl_test::trips([] { (void)handle_pool0.emplace(1); }));
      CHECK(metl_test::trips([] { (void)handle_pool0.generation_of(0); }));
    }
  }

  // event_dispatcher: every subscription fails, so nothing is heard.
  {
    metl::event_dispatcher<void(int), 0> dispatcher;
    const auto listener = metl::delegate<void(int)>::from_function<&hear>();
    CHECK(!dispatcher.subscribe(listener).has_value());
    CHECK(!dispatcher.try_subscribe(listener).has_value());
    CHECK(!dispatcher.unsubscribe({1}));
    dispatcher.dispatch(5);
    CHECK_EQ(heard, 0);
    dispatcher.clear();
    CHECK(dispatcher.empty());
    CHECK_EQ(dispatcher.size(), std::size_t{0});
    CHECK_EQ(dispatcher.capacity(), std::size_t{0});
  }

  // arena_allocator: every allocation fails; a bad alignment is still caught.
  {
    CHECK(arena0.try_allocate(1) == nullptr);
    CHECK(arena0.try_allocate(0, 8) == nullptr);
    CHECK(arena0.try_emplace<int>(1) == nullptr);
    const auto mark = arena0.mark();
    CHECK_EQ(mark.offset, std::size_t{0});
    arena0.rewind(mark);
    arena0.reset();
    CHECK(arena0.empty());
    CHECK_EQ(arena0.capacity(), std::size_t{0});
    CHECK_EQ(arena0.used(), std::size_t{0});
    CHECK_EQ(arena0.remaining(), std::size_t{0});
    CHECK(metl_test::trips([] { (void)arena0.try_allocate(1, 3); }));
    CHECK(metl_test::trips([] { (void)arena0.try_allocate(1, 0); }));
    if (metl_test::asserts_active) {
      CHECK(metl_test::trips([] { (void)arena0.allocate(1); }));
      CHECK(metl_test::trips([] { (void)arena0.emplace<int>(1); }));
      CHECK(metl_test::trips([] { arena0.rewind({1}); }));
    }
  }

  // monotonic_buffer: the same, and it stays copyable as the general one is.
  {
    CHECK(buffer0.try_allocate(1) == nullptr);
    CHECK(buffer0.try_allocate_n<int>(2) == nullptr);
    CHECK(buffer0.try_emplace<int>(1) == nullptr);
    buffer0.reset();
    CHECK(buffer0.empty());
    CHECK_EQ(buffer0.capacity(), std::size_t{0});
    CHECK_EQ(buffer0.used(), std::size_t{0});
    CHECK_EQ(buffer0.remaining(), std::size_t{0});
    const metl::monotonic_buffer<0> copy = buffer0;
    CHECK(copy.empty());
    CHECK(metl_test::trips([] { (void)buffer0.try_allocate(1, 3); }));
    CHECK(metl_test::trips([] { (void)buffer0.try_allocate(1, 0); }));
    if (metl_test::asserts_active) {
      CHECK(metl_test::trips([] { (void)buffer0.allocate(1); }));
      CHECK(metl_test::trips([] { (void)buffer0.emplace<int>(1); }));
    }
  }

  return metl_test::exit_code();
}
