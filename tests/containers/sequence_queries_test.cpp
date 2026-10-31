// empty(), full() and size() on the sequence containers run during constant
// evaluation (#537).
//
// Each container has a user-provided destructor, so it is not a literal type: no
// `constexpr` object of it exists and a `static_assert` cannot name one. Its
// constructor can still run in a constant-initialized object, and that object's
// constructor is where the queries are called. In C++20 `constinit` fails to
// compile if any query is not constexpr. In C++17 these containers are not
// constant-initialized (detail/constant_init.hpp), so the run-time checks remain.

#include "metl_check.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>

#include <metl/fixed_deque.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/ring_buffer.hpp>

namespace {

template <typename Container>
struct queried {
  constexpr queried() noexcept
      : container(), empty(container.empty()), full(container.full()), size(container.size()) {}

  Container container;
  bool empty;
  bool full;
  std::size_t size;
};

#if defined(__cpp_constinit) && __cpp_constinit >= 201907L
#define SEQUENCE_QUERIES_CONSTANT constinit
#else
#define SEQUENCE_QUERIES_CONSTANT
#endif

SEQUENCE_QUERIES_CONSTANT queried<metl::fixed_vector<int, 4>> g_vector;
SEQUENCE_QUERIES_CONSTANT queried<metl::ring_buffer<int, 4>> g_ring;
SEQUENCE_QUERIES_CONSTANT queried<metl::fixed_deque<int, 4>> g_deque;
SEQUENCE_QUERIES_CONSTANT queried<metl::fixed_queue<int, 4>> g_queue;
SEQUENCE_QUERIES_CONSTANT queried<metl::fixed_stack<int, 4>> g_stack;
SEQUENCE_QUERIES_CONSTANT queried<metl::fixed_priority_queue<int, 4>> g_heap;

template <typename Container>
void check_answers(const queried<Container>& object) {
  CHECK(object.empty);
  CHECK(!object.full);
  CHECK_EQ(object.size, std::size_t{0});
  CHECK(object.container.empty());
}

// `fixed_priority_queue() = default` takes both properties from Compare.
struct throwing_compare {
  throwing_compare() noexcept(false) {}
  bool operator()(int left, int right) const { return left < right; }
};
static_assert(std::is_nothrow_default_constructible_v<metl::fixed_priority_queue<int, 4>>,
              "a noexcept Compare constructor makes the queue's noexcept");
static_assert(!std::is_nothrow_default_constructible_v<metl::fixed_priority_queue<int, 4, throwing_compare>>,
              "a throwing Compare constructor must not be called from a noexcept one");

}  // namespace

int main() {
  check_answers(g_vector);
  check_answers(g_ring);
  check_answers(g_deque);
  check_answers(g_queue);
  check_answers(g_stack);
  check_answers(g_heap);

  // Constant initialization leaves each one usable at run time.
  CHECK(g_queue.container.try_push(1));
  CHECK_EQ(g_queue.container.size(), std::size_t{1});
  CHECK(g_stack.container.try_push(1));
  CHECK_EQ(g_stack.container.size(), std::size_t{1});
  CHECK(g_heap.container.try_push(1));
  CHECK_EQ(g_heap.container.size(), std::size_t{1});
  g_deque.container.push_front(1);
  CHECK_EQ(g_deque.container.size(), std::size_t{1});
  return metl_test::exit_code();
}
