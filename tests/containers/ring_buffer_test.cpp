#include <algorithm>
#include <type_traits>

#include <metl/ring_buffer.hpp>

namespace {

struct tracker {
  static int constructions;
  static int destructions;

  tracker() : value(0) { ++constructions; }
  explicit tracker(int input) : value(input) { ++constructions; }
  tracker(const tracker& other) : value(other.value) { ++constructions; }
  tracker(tracker&& other) noexcept : value(other.value) { ++constructions; }
  ~tracker() { ++destructions; }

  tracker& operator=(const tracker& other) {
    value = other.value;
    return *this;
  }

  tracker& operator=(tracker&& other) noexcept {
    value = other.value;
    return *this;
  }

  int value;
};

int tracker::constructions = 0;
int tracker::destructions = 0;

}  // namespace

int main() {
  metl::ring_buffer<int, 3> buffer;
  if (!buffer.empty() || buffer.capacity() != 3) {
    return 1;
  }

  if (!buffer.try_push_back(1) || !buffer.try_emplace_back(2) || !buffer.try_push_back(3)) {
    return 2;
  }

  if (!buffer.full() || buffer.front() != 1 || buffer.back() != 3 || buffer[1] != 2 || buffer.at(1) != 2) {
    return 3;
  }

  if (buffer.try_push_back(4)) {
    return 4;
  }

  buffer.pop_front();
  if (buffer.size() != 2 || buffer.front() != 2) {
    return 5;
  }

  buffer.push_overwrite(4);
  if (buffer.size() != 3 || buffer.front() != 2 || buffer.back() != 4) {
    return 6;
  }

  buffer.push_overwrite(5);
  if (buffer.front() != 3 || buffer[1] != 4 || buffer.back() != 5) {
    return 7;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::ring_buffer<tracker, 2> tracked;
    tracked.emplace_back(10);
    tracked.push_overwrite(20);
    tracked.push_overwrite(30);

    if (tracked.size() != 2 || tracked.front().value != 20 || tracked.back().value != 30) {
      return 8;
    }

    tracked.clear();
  }

  // ---- iteration ------------------------------------------------------------
  // Logical order, including across the wrap. A raw pointer walk over the
  // storage array would go wrong exactly here, which is why the iterator holds
  // a logical index and goes through the same physical_index mapping as at().
  {
    metl::ring_buffer<int, 4> r;
    for (int i = 1; i <= 4; ++i) {
      if (!r.try_push_back(i)) {
        return 10;
      }
    }
    r.pop_front();
    if (!r.try_push_back(5)) {  // head is now mid-array: logical order is 2,3,4,5
      return 11;
    }

    int walked = 0;
    for (int value : r) {
      walked = walked * 10 + value;
    }
    if (walked != 2345) {
      return 12;
    }

    // Reverse, const, and the cbegin/cend spellings.
    int reversed = 0;
    for (auto it = r.rbegin(); it != r.rend(); ++it) {
      reversed = reversed * 10 + *it;
    }
    if (reversed != 5432) {
      return 13;
    }

    const metl::ring_buffer<int, 4>& cr = r;
    int const_sum = 0;
    for (int value : cr) {
      const_sum += value;
    }
    if (const_sum != 14) {
      return 14;
    }
    if (static_cast<int>(r.cend() - r.cbegin()) != 4) {
      return 15;
    }

    // Random access: the category is claimed, so the operations must work.
    auto it = r.begin();
    if (*(it + 3) != 5 || it[1] != 3) {
      return 16;
    }
    it += 2;
    if (*it != 4 || (it - r.begin()) != 2) {
      return 17;
    }
    --it;
    if (*it != 3 || !(r.begin() < it) || !(it <= it)) {
      return 18;
    }

    // A mutable iterator converts to a const one; the reverse must not compile.
    metl::ring_buffer<int, 4>::const_iterator converted = r.begin();
    if (*converted != 2) {
      return 19;
    }
    static_assert(!std::is_convertible<metl::ring_buffer<int, 4>::const_iterator,
                                       metl::ring_buffer<int, 4>::iterator>::value,
                  "const_iterator must not convert back to iterator");

    // Writing through the iterator reaches the element.
    *r.begin() = 20;
    if (r.front() != 20) {
      return 20;
    }
  }

  // ---- empty range ----------------------------------------------------------
  {
    metl::ring_buffer<int, 4> empty;
    if (empty.begin() != empty.end() || empty.cbegin() != empty.cend()) {
      return 21;
    }
    int touched = 0;
    for (int value : empty) {
      touched += value;
    }
    if (touched != 0) {
      return 22;
    }
  }

  return tracker::constructions == tracker::destructions ? 0 : 9;
}
