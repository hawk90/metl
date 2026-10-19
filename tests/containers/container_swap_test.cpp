// Member and ADL swap for every container that holds elements inline (#348).
//
// Without a swap of its own, `using std::swap; swap(a, b)` on a container
// falls back to std::swap: three moves of the whole container, each moving
// every element and clearing the source. Each container below swaps element
// by element instead, so swapping a container of n and one of m elements
// move-constructs at most max(n, m) elements (one per position, for the
// temporary of the element swap or for the tail of the longer one) rather
// than 3 * (n + m). The hash containers move one element per slot that is
// occupied on one side only, at most n + m.
//
// Each case checks the contents after a member swap and after an ADL swap,
// sizes that differ in both directions (including empty), a self-swap, and
// that the swap is noexcept exactly when the element's move and swap are.
// What a throwing element move leaves is in throwing_element_test.cpp.

#include "metl_check.hpp"

#include <cstddef>
#include <type_traits>
#include <utility>
#include <vector>

#include <metl/fixed_deque.hpp>
#include <metl/fixed_priority_queue.hpp>
#include <metl/fixed_queue.hpp>
#include <metl/fixed_stack.hpp>
#include <metl/fixed_string.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

// Counts move constructions; ordered and hashed by value.
struct counted {
  int value = 0;
  static int& moves() {
    static int count = 0;
    return count;
  }

  counted() = default;
  explicit counted(int v) : value(v) {}
  counted(const counted&) = default;
  counted(counted&& other) noexcept : value(other.value) { ++moves(); }
  counted& operator=(const counted&) = default;
  counted& operator=(counted&& other) noexcept {
    value = other.value;
    return *this;
  }
  ~counted() = default;

  friend bool operator<(const counted& lhs, const counted& rhs) noexcept { return lhs.value < rhs.value; }
  friend bool operator==(const counted& lhs, const counted& rhs) noexcept { return lhs.value == rhs.value; }
};

struct counted_hash {
  std::size_t operator()(const counted& c) const noexcept { return static_cast<std::size_t>(c.value); }
};

// Move construction may throw.
struct throwing_move {
  int value = 0;
  throwing_move() = default;
  explicit throwing_move(int v) : value(v) {}
  throwing_move(const throwing_move&) = default;
  throwing_move(throwing_move&& other) noexcept(false) : value(other.value) {}
  throwing_move& operator=(const throwing_move&) = default;
  throwing_move& operator=(throwing_move&& other) noexcept(false) {
    value = other.value;
    return *this;
  }
  ~throwing_move() = default;
  bool operator<(const throwing_move& other) const noexcept { return value < other.value; }
  bool operator==(const throwing_move& other) const noexcept { return value == other.value; }
};

struct throwing_move_hash {
  std::size_t operator()(const throwing_move& t) const noexcept { return static_cast<std::size_t>(t.value); }
};

// Movable only by construction, and swappable only through its own ADL swap,
// which counts calls: a container must reach it rather than std::swap.
struct own_swap {
  int value = 0;
  static int& swaps() {
    static int count = 0;
    return count;
  }
  own_swap() = default;
  explicit own_swap(int v) : value(v) {}
  own_swap(own_swap&&) noexcept = default;
  own_swap& operator=(own_swap&&) = delete;
  ~own_swap() = default;
  friend void swap(own_swap& lhs, own_swap& rhs) noexcept {
    ++swaps();
    const int held = lhs.value;
    lhs.value = rhs.value;
    rhs.value = held;
  }
};

// A container's values in iteration order.
template <typename C>
std::vector<int> values_of(const C& c) {
  std::vector<int> out;
  for (const auto& element : c) {
    out.push_back(element.value);
  }
  return out;
}

// Both forms of swap, `a.swap(b)` and the ADL `swap(a, b)`, for one pair.
template <typename C, typename Fill, typename Read>
void check_swaps(Fill fill, Read read, const std::vector<int>& left, const std::vector<int>& right) {
  for (int form = 0; form < 2; ++form) {
    C a;
    C b;
    fill(a, left);
    fill(b, right);
    counted::moves() = 0;
    if (form == 0) {
      a.swap(b);
    } else {
      using std::swap;
      swap(a, b);
    }
    const std::size_t longer = left.size() > right.size() ? left.size() : right.size();
    CHECK(counted::moves() <= static_cast<int>(longer));
    CHECK(read(a) == right);
    CHECK(read(b) == left);

    // Self-swap changes nothing.
    C& alias = a;
    a.swap(alias);
    CHECK(read(a) == right);
  }
}

const std::vector<std::vector<int>> kContents = {{}, {1}, {1, 2, 3}, {4, 5, 6, 7}};

template <typename C, typename Fill, typename Read>
void check_all_pairs(Fill fill, Read read) {
  for (const auto& left : kContents) {
    for (const auto& right : kContents) {
      check_swaps<C>(fill, read, left, right);
    }
  }
}

// ---- sequences ---------------------------------------------------------------

// Rotates the ring's head before filling, so that the elements wrap past the
// end of the storage array.
template <typename C>
void fill_ring_wrapped(C& c, const std::vector<int>& values) {
  for (int i = 0; i < 3; ++i) {
    c.emplace_back(-1);
    c.pop_front();
  }
  for (const int v : values) {
    c.emplace_back(v);
  }
}

void sequences() {
  const auto read = [](const auto& c) { return values_of(c); };
  check_all_pairs<metl::ring_buffer<counted, 4>>(
      [](auto& c, const std::vector<int>& values) { fill_ring_wrapped(c, values); }, read);
  check_all_pairs<metl::fixed_deque<counted, 4>>(
      [](auto& c, const std::vector<int>& values) {
        fill_ring_wrapped(c, values);
        if (!c.empty()) {
          // And one more at the front, so the head moves backwards too.
          const int front = c.front().value;
          c.pop_front();
          c.emplace_front(front);
        }
      },
      read);

  const auto fill_queue = [](auto& c, const std::vector<int>& values) {
    for (int i = 0; i < 3; ++i) {
      c.push(counted(-1));
      c.pop();
    }
    for (const int v : values) {
      c.emplace(v);
    }
  };
  const auto read_queue = [](auto c) {
    std::vector<int> out;
    while (!c.empty()) {
      out.push_back(c.front().value);
      c.pop();
    }
    return out;
  };
  check_all_pairs<metl::fixed_queue<counted, 4>>(fill_queue, read_queue);

  const auto fill_stack = [](auto& c, const std::vector<int>& values) {
    for (const int v : values) {
      c.emplace(v);
    }
  };
  const auto read_stack = [](auto c) {
    std::vector<int> out;
    while (!c.empty()) {
      out.insert(out.begin(), c.top().value);
      c.pop();
    }
    return out;
  };
  check_all_pairs<metl::fixed_stack<counted, 4>>(fill_stack, read_stack);

  // A heap reads back in priority order.
  const auto read_heap = [](auto c) {
    std::vector<int> out;
    while (!c.empty()) {
      out.insert(out.begin(), c.top().value);
      c.pop();
    }
    return out;
  };
  check_all_pairs<metl::fixed_priority_queue<counted, 4>>(fill_stack, read_heap);
}

// ---- associative ---------------------------------------------------------------

void associative() {
  check_all_pairs<metl::flat_map<int, counted, 4>>(
      [](auto& c, const std::vector<int>& values) {
        for (const int v : values) {
          c.emplace(v, counted(v));
        }
      },
      [](const auto& c) {
        std::vector<int> out;
        for (const auto& entry : c) {
          CHECK_EQ(entry.key, entry.value.value);
          out.push_back(entry.value.value);
        }
        return out;
      });
  check_all_pairs<metl::flat_set<counted, 4>>(
      [](auto& c, const std::vector<int>& values) {
        for (const int v : values) {
          c.emplace(counted(v));
        }
      },
      [](const auto& c) { return values_of(c); });

  // The hash containers iterate in slot order, which the swap must carry over
  // with the elements; compare sorted.
  const auto sorted = [](std::vector<int> v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
      for (std::size_t j = i; j > 0 && v[j] < v[j - 1]; --j) {
        std::swap(v[j], v[j - 1]);
      }
    }
    return v;
  };
  for (const auto& left : kContents) {
    for (const auto& right : kContents) {
      for (int form = 0; form < 2; ++form) {
        metl::static_unordered_map<int, counted, 4> a;
        metl::static_unordered_map<int, counted, 4> b;
        for (const int v : left) {
          a.emplace(v, counted(v));
        }
        // An erased key leaves a tombstone, which must travel with its table.
        if (left.size() < 4) {
          a.emplace(99, counted(99));
          CHECK(a.erase(99));
        }
        for (const int v : right) {
          b.emplace(v, counted(v));
        }
        counted::moves() = 0;
        if (form == 0) {
          a.swap(b);
        } else {
          using std::swap;
          swap(a, b);
        }
        CHECK(counted::moves() <= static_cast<int>(left.size() + right.size()));
        std::vector<int> in_a;
        for (const auto& entry : a) {
          in_a.push_back(entry.value.value);
        }
        std::vector<int> in_b;
        for (const auto& entry : b) {
          in_b.push_back(entry.value.value);
        }
        CHECK(sorted(in_a) == right);
        CHECK(sorted(in_b) == left);
        CHECK_EQ(a.size(), right.size());
        CHECK_EQ(b.size(), left.size());
        for (const int v : left) {
          const counted* in_b_now = b.find(v);
          CHECK(in_b_now != nullptr);
          if (in_b_now != nullptr) {
            CHECK_EQ(in_b_now->value, v);
          }
          const counted* in_a_now = a.find(v);
          if (in_a_now != nullptr) {
            CHECK_EQ(in_a_now->value, v);
          }
        }
        CHECK(!b.contains(99));
        // Both still insert and find after the swap.
        CHECK_EQ(a.try_emplace(42, counted(42)), right.size() < 4);
        CHECK_EQ(a.contains(42), right.size() < 4);
        CHECK_EQ(b.try_emplace(43, counted(43)), left.size() < 4);
        CHECK_EQ(b.contains(43), left.size() < 4);
      }
    }
  }
  for (const auto& left : kContents) {
    for (const auto& right : kContents) {
      metl::static_unordered_set<counted, 4, counted_hash> a;
      metl::static_unordered_set<counted, 4, counted_hash> b;
      for (const int v : left) {
        a.emplace(counted(v));
      }
      for (const int v : right) {
        b.emplace(counted(v));
      }
      counted::moves() = 0;
      using std::swap;
      swap(a, b);
      CHECK(counted::moves() <= static_cast<int>(left.size() + right.size()));
      CHECK(sorted(values_of(a)) == right);
      CHECK(sorted(values_of(b)) == left);
      for (const int v : left) {
        CHECK(b.contains(counted(v)));
      }
      a.swap(a);
      CHECK(sorted(values_of(a)) == right);
    }
  }
}

// ---- fixed_string --------------------------------------------------------------

void strings() {
  const char* const texts[] = {"", "a", "abc", "abcdefgh"};
  for (const char* left : texts) {
    for (const char* right : texts) {
      metl::fixed_string<8> a(left);
      metl::fixed_string<8> b(right);
      a.swap(b);
      CHECK(a == right);
      CHECK(b == left);
      CHECK_EQ(a.c_str()[a.size()], '\0');
      CHECK_EQ(b.c_str()[b.size()], '\0');
      using std::swap;
      swap(a, b);
      CHECK(a == left);
      CHECK(b == right);
      a.swap(a);
      CHECK(a == left);
    }
  }
}

// ---- element swaps are found by ADL --------------------------------------------

void adl_element_swap() {
  metl::fixed_deque<own_swap, 4> a;
  metl::fixed_deque<own_swap, 4> b;
  a.emplace_back(1);
  a.emplace_back(2);
  b.emplace_back(3);
  own_swap::swaps() = 0;
  a.swap(b);
  CHECK_EQ(own_swap::swaps(), 1);
  CHECK_EQ(a.size(), 1u);
  CHECK_EQ(a.front().value, 3);
  CHECK_EQ(b.size(), 2u);
  CHECK_EQ(b.front().value, 1);
  CHECK_EQ(b.back().value, 2);
}

// ---- noexcept ----------------------------------------------------------------

template <template <typename> class C>
constexpr bool nothrow_exactly_when_element() {
  return std::is_nothrow_swappable_v<C<counted>> && !std::is_nothrow_swappable_v<C<throwing_move>>;
}

template <typename T>
using ring_of = metl::ring_buffer<T, 4>;
template <typename T>
using deque_of = metl::fixed_deque<T, 4>;
template <typename T>
using queue_of = metl::fixed_queue<T, 4>;
template <typename T>
using stack_of = metl::fixed_stack<T, 4>;
template <typename T>
using heap_of = metl::fixed_priority_queue<T, 4>;
template <typename T>
using flat_map_of = metl::flat_map<int, T, 4>;
template <typename T>
using flat_set_of = metl::flat_set<T, 4>;
template <typename T>
using unordered_map_of = metl::static_unordered_map<int, T, 4>;

static_assert(nothrow_exactly_when_element<ring_of>());
static_assert(nothrow_exactly_when_element<deque_of>());
static_assert(nothrow_exactly_when_element<queue_of>());
static_assert(nothrow_exactly_when_element<stack_of>());
static_assert(nothrow_exactly_when_element<heap_of>());
static_assert(nothrow_exactly_when_element<flat_map_of>());
static_assert(nothrow_exactly_when_element<flat_set_of>());
static_assert(nothrow_exactly_when_element<unordered_map_of>());
static_assert(std::is_nothrow_swappable_v<metl::static_unordered_set<counted, 4, counted_hash>>);
static_assert(!std::is_nothrow_swappable_v<metl::static_unordered_set<throwing_move, 4, throwing_move_hash>>);
static_assert(std::is_nothrow_swappable_v<metl::fixed_string<8>>);

// A comparator that cannot be swapped does not make the heap's swap ill-formed
// to ask about.
struct unswappable_less {
  unswappable_less() = default;
  unswappable_less(const unswappable_less&) = default;
  unswappable_less& operator=(const unswappable_less&) = delete;
  bool operator()(int lhs, int rhs) const noexcept { return lhs < rhs; }
};
static_assert(std::is_move_constructible_v<metl::fixed_priority_queue<int, 4, unswappable_less>>);

}  // namespace

int main() {
  sequences();
  associative();
  strings();
  adl_element_swap();
  return metl_test::exit_code();
}
