// Element types that sabotage the obvious spellings in library code.
//
//   * hijacked   -- operator& returns nullptr. Library code that writes `&obj`
//                   gets nullptr and builds on it, finds nothing, or crashes.
//   * no_address -- operator& is deleted, so `&obj` does not compile.
//   * no_heap    -- the class deletes its own operator new, the usual way to
//                   forbid heap allocation. Placement new written without `::`
//                   finds only that deleted form and does not compile, though
//                   nothing here touches the heap.
//
// Every container, vocabulary type and callable that stores or refers to a
// user's object is built with each of them here. Compiling this file is half
// the test; the checks are the other half.

#include "metl_check.hpp"

#include <cstddef>
#include <cstdint>
#include <new>

#include <metl/arena_allocator.hpp>
#include <metl/atomic_ref.hpp>
#include <metl/coro/protothread.hpp>
#include <metl/coro/scheduler.hpp>
#include <metl/delegate.hpp>
#include <metl/fixed_deque.hpp>
#include <metl/fixed_function.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/function_ref.hpp>
#include <metl/handle_pool.hpp>
#include <metl/hash.hpp>
#include <metl/lookup_table.hpp>
#include <metl/monotonic_buffer.hpp>
#include <metl/object_pool.hpp>
#include <metl/optional.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_allocator.hpp>
#include <metl/static_message_queue.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>
#include <metl/variant.hpp>

namespace {

struct hijacked {
  int value = 0;
  hijacked() = default;
  explicit hijacked(int v) : value(v) {}
  hijacked* operator&() noexcept { return nullptr; }
  const hijacked* operator&() const noexcept { return nullptr; }
  friend bool operator==(const hijacked& a, const hijacked& b) noexcept { return a.value == b.value; }
  friend bool operator<(const hijacked& a, const hijacked& b) noexcept { return a.value < b.value; }
};

struct hijacked_hash {
  std::size_t operator()(const hijacked& h) const noexcept { return static_cast<std::size_t>(h.value); }
};

struct no_address {
  std::uint32_t value = 0;
  no_address() = default;
  explicit no_address(std::uint32_t v) : value(v) {}
  void operator&() const = delete;
};

struct no_heap {
  int value = 0;
  no_heap() = default;
  explicit no_heap(int v) : value(v) {}
  static void* operator new(std::size_t) = delete;
  static void* operator new[](std::size_t) = delete;
  friend bool operator<(const no_heap& a, const no_heap& b) noexcept { return a.value < b.value; }
  friend bool operator==(const no_heap& a, const no_heap& b) noexcept { return a.value == b.value; }
};

struct hijacked_task : metl::coro::protothread {
  int runs = 0;
  bool run() noexcept {
    ++runs;
    return true;
  }
  hijacked_task* operator&() noexcept { return nullptr; }
};

struct hijacked_counter {
  int count = 0;
  int bump() noexcept { return ++count; }
  int read() const noexcept { return count; }
  hijacked_counter* operator&() noexcept { return nullptr; }
  const hijacked_counter* operator&() const noexcept { return nullptr; }
};

struct no_heap_callable {
  int operator()(int x) const noexcept { return x + 1; }
  static void* operator new(std::size_t) = delete;
};

void address_hijack() {
  {
    metl::optional<hijacked> o;
    o.emplace(7);
    CHECK(o.has_value());
    CHECK_EQ(o->value, 7);
    CHECK_EQ((*o).value, 7);
  }
  {
    metl::flat_map<int, hijacked, 4> m;
    m.emplace(2, hijacked{20});
    CHECK_DEREF_EQ(m.find(2), hijacked{20});
    const auto& cm = m;
    CHECK(cm.find(2) != nullptr);
  }
  {
    metl::flat_set<hijacked, 4> s;
    s.emplace(hijacked{3});
    CHECK(s.find(hijacked{3}) != nullptr);
  }
  {
    metl::static_unordered_map<int, hijacked, 4> m;
    m.emplace(5, hijacked{50});
    CHECK_DEREF_EQ(m.find(5), hijacked{50});
    CHECK_EQ(m.begin()->value.value, 50);
  }
  {
    metl::static_unordered_set<hijacked, 4, hijacked_hash> s;
    s.emplace(hijacked{9});
    CHECK_EQ(s.begin()->value, 9);
  }
  {
    const auto table = metl::make_lookup_table<int, hijacked, 1>(
        std::array<metl::lookup_entry<int, hijacked>, 1>{{{1, hijacked{11}}}});
    CHECK_DEREF_EQ(table.find(1), hijacked{11});
  }
  {
    hijacked_counter counter;
    auto bump = metl::delegate<int()>::bind<hijacked_counter, &hijacked_counter::bump>(counter);
    auto read = metl::delegate<int()>::bind<hijacked_counter, &hijacked_counter::read>(
        static_cast<const hijacked_counter&>(counter));
    CHECK_EQ(bump(), 1);
    CHECK_EQ(read(), 1);
  }
  {
    hijacked_task task;
    metl::coro::scheduler<2> sched;
    CHECK(sched.try_attach_protothread(task));
    sched.run_once();
    CHECK_EQ(task.runs, 1);
  }
  {
    no_address cell{1};
    metl::atomic_ref<no_address> ref(cell);
    ref.store(no_address{2});
    CHECK_EQ(cell.value, 2u);
  }
  {
    const no_address key{0x01020304u};
    const no_address same{0x01020304u};
    CHECK_EQ(metl::fnv1a_hash{}(key), metl::fnv1a_hash{}(same));
  }
}

void deleted_operator_new() {
  {
    metl::object_pool<no_heap, 2> pool;
    no_heap* p = pool.emplace(1);
    CHECK_EQ(p->value, 1);
    pool.destroy(p);
  }
  {
    metl::handle_pool<no_heap, 2> pool;
    const auto h = pool.emplace(2);
    CHECK_DEREF_EQ(pool.get(h), no_heap{2});
  }
  {
    metl::fixed_deque<no_heap, 2> d;
    d.emplace_back(3);
    d.emplace_front(4);
    CHECK_EQ(d.front().value, 4);
  }
  {
    metl::ring_buffer<no_heap, 2> r;
    r.emplace_back(5);
    CHECK_EQ(r.front().value, 5);
  }
  {
    metl::fixed_queue<no_heap, 2> q;
    q.emplace(6);
    CHECK_EQ(q.front().value, 6);
  }
  {
    metl::static_message_queue<no_heap, 2> q;
    q.emplace(7);
    CHECK_EQ(q.front().value, 7);
  }
  {
    metl::variant<int, no_heap> v;
    v.emplace<no_heap>(8);
    metl::variant<int, no_heap> copy = v;
    CHECK_DEREF_EQ(metl::get_if<no_heap>(&copy), no_heap{8});
  }
  {
    metl::flat_set<no_heap, 4> s;
    s.emplace(no_heap{2});
    s.emplace(no_heap{1});
    CHECK_EQ(s.nth(0).value, 1);
    CHECK(s.erase(no_heap{1}));
  }
  {
    metl::flat_map<int, no_heap, 4> m;
    m.emplace(2, no_heap{2});
    m.emplace(1, no_heap{1});
    CHECK(m.erase(1));
  }
  {
    metl::fixed_vector<no_heap, 2> v;
    v.emplace_back(9);
    CHECK_EQ(v[0].value, 9);
  }
  {
    metl::optional<no_heap> o;
    o.emplace(10);
    CHECK_EQ(o->value, 10);
  }
  {
    metl::fixed_function<int(int), 16> f{no_heap_callable{}};
    metl::fixed_function<int(int), 16> g = f;
    CHECK_EQ(g(1), 2);
    metl::fixed_any_invocable<int(int), 16> h{no_heap_callable{}};
    CHECK_EQ(h(2), 3);
  }
  {
    metl::arena_allocator<64> arena;
    CHECK_EQ(arena.emplace<no_heap>(11)->value, 11);
    metl::monotonic_buffer<64> mono;
    CHECK_EQ(mono.emplace<no_heap>(12)->value, 12);
    metl::static_allocator<no_heap, 64> alloc;
    CHECK_EQ(alloc.create(13)->value, 13);
  }
}

}  // namespace

int main() {
  address_hijack();
  deleted_operator_new();
  return metl_test::exit_code();
}
