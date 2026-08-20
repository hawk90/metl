#include "metl/fixed_priority_queue.hpp"

#include "metl_check.hpp"

#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

namespace {

struct tracker {
  static int constructions;
  static int destructions;
  int value;

  explicit tracker(int v = 0) noexcept : value(v) { ++constructions; }
  tracker(const tracker& other) noexcept : value(other.value) { ++constructions; }
  tracker(tracker&& other) noexcept : value(other.value) { ++constructions; }
  tracker& operator=(const tracker&) noexcept = default;
  tracker& operator=(tracker&&) noexcept = default;
  ~tracker() { ++destructions; }
};

int tracker::constructions = 0;
int tracker::destructions = 0;

struct by_value {
  bool operator()(const tracker& lhs, const tracker& rhs) const noexcept { return lhs.value < rhs.value; }
};

/// A comparator that carries state, to prove the explicit-comparator constructor
/// actually stores it rather than default-constructing a fresh one.
struct offset_less {
  int bias = 0;
  bool operator()(int lhs, int rhs) const noexcept { return (lhs % bias) < (rhs % bias); }
};

/// The whole point of the type: the array must satisfy the heap property after
/// every single operation, not just at the end. Checked directly against the
/// storage rather than inferred from pop order, so a broken sift is caught where
/// it happens.
template <typename Queue, typename Compare>
bool heap_ordered(const Queue& queue, Compare comp) {
  const auto slots = queue.as_span();
  for (std::size_t i = 1; i < slots.size(); ++i) {
    const std::size_t parent = (i - 1) / 2;
    // comp(a, b) means "a comes out after b", so a parent must never come after
    // its child.
    if (comp(slots[parent], slots[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  // ---------------------------------------------------------------------
  // Max-heap by default, and the heap property holds after every push.
  // ---------------------------------------------------------------------
  {
    metl::fixed_priority_queue<int, 8> queue;
    CHECK(queue.empty());
    CHECK(!queue.full());
    CHECK_EQ(queue.size(), 0u);
    CHECK_EQ(queue.capacity(), 8u);

    const int input[] = {5, 1, 8, 3, 8, 0, 7, 2};
    for (int value : input) {
      CHECK(queue.try_push(value));
      CHECK(heap_ordered(queue, std::less<int>{}));
    }
    CHECK(queue.full());
    CHECK_EQ(queue.size(), 8u);
    CHECK_EQ(queue.top(), 8);

    // Pop order is descending, and the invariant survives every pop.
    int previous = 9;
    std::size_t popped = 0;
    while (!queue.empty()) {
      const int current = queue.top();
      CHECK(current <= previous);
      previous = current;
      queue.pop();
      ++popped;
      CHECK(heap_ordered(queue, std::less<int>{}));
    }
    CHECK_EQ(popped, 8u);
    CHECK(queue.empty());
  }

  // ---------------------------------------------------------------------
  // A full queue refuses, and refusing leaves the contents alone.
  // ---------------------------------------------------------------------
  {
    metl::fixed_priority_queue<int, 3> queue;
    CHECK(queue.try_push(1));
    CHECK(queue.try_push(2));
    CHECK(queue.try_push(3));
    CHECK(queue.full());

    // Even a value that would become the new top must be refused.
    CHECK(!queue.try_push(99));
    CHECK_EQ(queue.size(), 3u);
    CHECK_EQ(queue.top(), 3);
    CHECK(!queue.try_emplace(99));
    CHECK_EQ(queue.size(), 3u);
    CHECK_EQ(queue.top(), 3);
  }

  // ---------------------------------------------------------------------
  // std::greater turns it into the min-heap a deadline queue needs.
  // ---------------------------------------------------------------------
  {
    metl::fixed_priority_queue<int, 8, std::greater<int>> queue;
    const int input[] = {5, 1, 8, 3};
    for (int value : input) {
      queue.push(value);
      CHECK(heap_ordered(queue, std::greater<int>{}));
    }
    CHECK_EQ(queue.top(), 1);
    queue.pop();
    CHECK_EQ(queue.top(), 3);
    queue.pop();
    CHECK_EQ(queue.top(), 5);
  }

  // ---------------------------------------------------------------------
  // A stateful comparator is stored, not default-constructed.
  // ---------------------------------------------------------------------
  {
    metl::fixed_priority_queue<int, 8, offset_less> queue{offset_less{10}};
    queue.push(23);  // 23 % 10 == 3
    queue.push(47);  // 47 % 10 == 7
    queue.push(11);  // 11 % 10 == 1
    // A default-constructed offset_less would have bias 0 and divide by zero, so
    // reaching here at all already proves the stored one is in use.
    CHECK_EQ(queue.top(), 47);
  }

  // ---------------------------------------------------------------------
  // erase_if removes the matching elements and restores the heap.
  // ---------------------------------------------------------------------
  {
    metl::fixed_priority_queue<int, 16> queue;
    for (int i = 0; i < 12; ++i) {
      queue.push(i);
    }
    CHECK_EQ(queue.size(), 12u);
    CHECK_EQ(queue.top(), 11);

    // Drop the odd values, including the current top.
    const std::size_t removed = queue.erase_if([](const int& v) { return (v % 2) != 0; });
    CHECK_EQ(removed, 6u);
    CHECK_EQ(queue.size(), 6u);
    CHECK(heap_ordered(queue, std::less<int>{}));
    CHECK_EQ(queue.top(), 10);

    int previous = 99;
    while (!queue.empty()) {
      CHECK(queue.top() <= previous);
      CHECK_EQ(queue.top() % 2, 0);
      previous = queue.top();
      queue.pop();
      CHECK(heap_ordered(queue, std::less<int>{}));
    }

    // Matching nothing, and matching everything.
    for (int i = 0; i < 5; ++i) {
      queue.push(i);
    }
    CHECK_EQ(queue.erase_if([](const int&) { return false; }), 0u);
    CHECK_EQ(queue.size(), 5u);
    CHECK_EQ(queue.erase_if([](const int&) { return true; }), 5u);
    CHECK(queue.empty());
    // Erasing from an empty queue is not a special case.
    CHECK_EQ(queue.erase_if([](const int&) { return true; }), 0u);
  }

  // ---------------------------------------------------------------------
  // Element lifetimes balance across pushes, pops, erase_if and clear.
  // ---------------------------------------------------------------------
  tracker::constructions = 0;
  tracker::destructions = 0;
  {
    metl::fixed_priority_queue<tracker, 8, by_value> queue;
    for (int i = 0; i < 8; ++i) {
      queue.emplace((i * 7) % 8);
      CHECK(heap_ordered(queue, by_value{}));
    }
    CHECK(!queue.try_emplace(99));
    CHECK_EQ(queue.top().value, 7);

    queue.pop();
    queue.pop();
    CHECK_EQ(queue.size(), 6u);
    CHECK_EQ(queue.erase_if([](const tracker& t) { return t.value < 3; }), 3u);
    CHECK(heap_ordered(queue, by_value{}));
    queue.clear();
    CHECK(queue.empty());
  }
  CHECK_EQ(tracker::constructions, tracker::destructions);

  // ---------------------------------------------------------------------
  // Contract details that are compile-time facts, not runtime ones.
  // ---------------------------------------------------------------------
  {
    using queue_type = metl::fixed_priority_queue<int, 4>;
    queue_type queue;
    // top() hands out a const reference even on a mutable queue: a caller must
    // not be able to change the key the heap is ordered by.
    static_assert(std::is_const_v<std::remove_reference_t<decltype(queue.top())>>,
                  "top() must be const-qualified so the ordering key cannot be mutated in place");
    static_assert(std::is_same_v<queue_type::value_type, int>, "value_type");
    static_assert(std::is_same_v<queue_type::value_compare, std::less<int>>, "default comparator");
    // Capacity is a compile-time constant, which is what makes the progress
    // guarantee a compile-time bound.
    CHECK_EQ(queue.capacity(), 4u);
    CHECK(queue.empty());
  }

  // ---------------------------------------------------------------------
  // A single element is the boundary case for both sift directions.
  // ---------------------------------------------------------------------
  {
    metl::fixed_priority_queue<int, 1> queue;
    CHECK(queue.try_push(42));
    CHECK(queue.full());
    CHECK(!queue.try_push(43));
    CHECK_EQ(queue.top(), 42);
    queue.pop();
    CHECK(queue.empty());
    CHECK(heap_ordered(queue, std::less<int>{}));
  }

  return metl_test::exit_code();
}
