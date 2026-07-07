#include <metl/fixed_vector.hpp>

namespace {

struct tracker {
  static int constructions;
  static int destructions;

  tracker() : value(0) { ++constructions; }
  explicit tracker(int input) : value(input) { ++constructions; }
  tracker(const tracker& other) : value(other.value) { ++constructions; }
  tracker(tracker&& other) noexcept : value(other.value) { ++constructions; }

  tracker& operator=(const tracker& other) {
    value = other.value;
    return *this;
  }

  tracker& operator=(tracker&& other) noexcept {
    value = other.value;
    return *this;
  }

  ~tracker() { ++destructions; }

  int value;
};

int tracker::constructions = 0;
int tracker::destructions = 0;

}  // namespace

int main() {
  metl::fixed_vector<int, 3> numbers;
  if (!numbers.empty() || numbers.capacity() != 3) {
    return 1;
  }

  if (!numbers.try_push_back(1) || !numbers.try_emplace_back(2) || !numbers.try_push_back(3)) {
    return 2;
  }

  if (!numbers.full() || numbers.try_push_back(4)) {
    return 3;
  }

  if (numbers.front() != 1 || numbers.back() != 3 || numbers.size() != 3) {
    return 4;
  }

  auto view = numbers.as_span();
  if (view.size() != 3 || view[1] != 2) {
    return 5;
  }

  numbers.pop_back();
  if (numbers.size() != 2 || numbers.back() != 2) {
    return 6;
  }

  metl::fixed_vector<int, 3> copied(numbers);
  if (copied.size() != 2 || copied[0] != 1 || copied[1] != 2) {
    return 7;
  }

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::fixed_vector<tracker, 2> tracked;
    tracked.emplace_back(10);
    tracked.emplace_back(20);

    if (tracked.size() != 2 || tracked[0].value != 10 || tracked[1].value != 20) {
      return 8;
    }

    tracked.clear();
    if (!tracked.empty()) {
      return 9;
    }
  }

  if (tracker::constructions != tracker::destructions) {
    return 10;
  }

  // initializer_list ctor.
  metl::fixed_vector<int, 5> il_vec{1, 2, 3, 4};
  if (il_vec.size() != 4 || il_vec[0] != 1 || il_vec[3] != 4) {
    return 11;
  }

  // at().
  if (il_vec.at(2) != 3) {
    return 12;
  }

  // insert(pos, value) at beginning.
  {
    metl::fixed_vector<int, 8> v{2, 3, 4};
    auto it = v.insert(v.begin(), 1);
    if (*it != 1 || v.size() != 4 || v[0] != 1 || v[1] != 2 || v[3] != 4) {
      return 13;
    }
  }

  // insert(pos, value) at middle.
  {
    metl::fixed_vector<int, 8> v{1, 2, 4, 5};
    auto it = v.insert(v.begin() + 2, 3);
    if (*it != 3 || v.size() != 5 || v[2] != 3 || v[3] != 4 || v[4] != 5) {
      return 14;
    }
  }

  // insert(pos, value) at end.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3};
    auto it = v.insert(v.end(), 4);
    if (*it != 4 || v.size() != 4 || v.back() != 4) {
      return 15;
    }
  }

  // emplace(pos, args...).
  {
    metl::fixed_vector<int, 8> v{1, 2, 4};
    auto it = v.emplace(v.begin() + 2, 3);
    if (*it != 3 || v.size() != 4 || v[2] != 3 || v[3] != 4) {
      return 16;
    }
  }

  // insert(pos, n, value).
  {
    metl::fixed_vector<int, 8> v{1, 5};
    auto it = v.insert(v.begin() + 1, 3, 9);
    if (*it != 9 || v.size() != 5 || v[0] != 1 || v[1] != 9 || v[2] != 9 || v[3] != 9 || v[4] != 5) {
      return 17;
    }
  }

  // insert(pos, n, value) with n > tail.
  {
    metl::fixed_vector<int, 8> v{1, 2};
    v.insert(v.end(), 3, 7);
    if (v.size() != 5 || v[0] != 1 || v[1] != 2 || v[2] != 7 || v[3] != 7 || v[4] != 7) {
      return 18;
    }
  }

  // insert(pos, n, value) with n=0.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3};
    auto it = v.insert(v.begin() + 1, 0, 99);
    if (it != v.begin() + 1 || v.size() != 3) {
      return 19;
    }
  }

  // insert(pos, first, last).
  {
    metl::fixed_vector<int, 8> v{1, 5};
    int src[] = {2, 3, 4};
    v.insert(v.begin() + 1, src, src + 3);
    if (v.size() != 5 || v[0] != 1 || v[1] != 2 || v[2] != 3 || v[3] != 4 || v[4] != 5) {
      return 20;
    }
  }

  // erase(pos).
  {
    metl::fixed_vector<int, 8> v{1, 2, 3, 4, 5};
    auto it = v.erase(v.begin() + 2);
    if (*it != 4 || v.size() != 4 || v[0] != 1 || v[1] != 2 || v[2] != 4 || v[3] != 5) {
      return 21;
    }
  }

  // erase(first, last).
  {
    metl::fixed_vector<int, 8> v{1, 2, 3, 4, 5};
    auto it = v.erase(v.begin() + 1, v.begin() + 4);
    if (*it != 5 || v.size() != 2 || v[0] != 1 || v[1] != 5) {
      return 22;
    }
  }

  // erase entire range.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3};
    v.erase(v.begin(), v.end());
    if (!v.empty()) {
      return 23;
    }
  }

  // resize(n) shrink and grow.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3, 4, 5};
    v.resize(2);
    if (v.size() != 2 || v[0] != 1 || v[1] != 2) {
      return 24;
    }
    v.resize(4);
    if (v.size() != 4 || v[0] != 1 || v[1] != 2 || v[2] != 0 || v[3] != 0) {
      return 25;
    }
  }

  // resize(n, value).
  {
    metl::fixed_vector<int, 8> v{1, 2};
    v.resize(5, 7);
    if (v.size() != 5 || v[2] != 7 || v[3] != 7 || v[4] != 7) {
      return 26;
    }
  }

  // assign(n, value).
  {
    metl::fixed_vector<int, 8> v{1, 2, 3};
    v.assign(4, 9);
    if (v.size() != 4 || v[0] != 9 || v[3] != 9) {
      return 27;
    }
  }

  // assign(first, last).
  {
    metl::fixed_vector<int, 8> v{1, 2, 3};
    int src[] = {10, 20, 30, 40};
    v.assign(src, src + 4);
    if (v.size() != 4 || v[0] != 10 || v[3] != 40) {
      return 28;
    }
  }

  // swap (same size).
  {
    metl::fixed_vector<int, 8> a{1, 2, 3};
    metl::fixed_vector<int, 8> b{7, 8, 9};
    a.swap(b);
    if (a.size() != 3 || a[0] != 7 || b[0] != 1) {
      return 29;
    }
  }

  // swap (different sizes).
  {
    metl::fixed_vector<int, 8> a{1, 2};
    metl::fixed_vector<int, 8> b{7, 8, 9, 10};
    a.swap(b);
    if (a.size() != 4 || a[0] != 7 || a[3] != 10) {
      return 30;
    }
    if (b.size() != 2 || b[0] != 1 || b[1] != 2) {
      return 31;
    }
  }

  // free swap.
  {
    metl::fixed_vector<int, 8> a{1, 2};
    metl::fixed_vector<int, 8> b{9};
    metl::swap(a, b);
    if (a.size() != 1 || a[0] != 9 || b.size() != 2) {
      return 32;
    }
  }

  // reverse iterators.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3, 4};
    int sum = 0;
    int expected = 4;
    for (auto it = v.rbegin(); it != v.rend(); ++it) {
      if (*it != expected) {
        return 33;
      }
      --expected;
      sum += *it;
    }
    if (sum != 10) {
      return 34;
    }
  }

  // const reverse iterators.
  {
    const metl::fixed_vector<int, 4> v{10, 20, 30};
    int expected = 30;
    for (auto it = v.crbegin(); it != v.crend(); ++it) {
      if (*it != expected) {
        return 35;
      }
      expected -= 10;
    }
  }

  // comparison operators.
  {
    metl::fixed_vector<int, 4> a{1, 2, 3};
    metl::fixed_vector<int, 4> b{1, 2, 3};
    metl::fixed_vector<int, 4> c{1, 2, 4};
    metl::fixed_vector<int, 4> d{1, 2};
    if (!(a == b)) {
      return 36;
    }
    if (a != b) {
      return 37;
    }
    if (!(a < c)) {
      return 38;
    }
    if (!(d < a)) {
      return 39;
    }
    if (!(a <= b) || !(a >= b)) {
      return 40;
    }
    if (!(c > a)) {
      return 41;
    }
    if (a > c) {
      return 42;
    }
  }

  // Different capacity comparison.
  {
    metl::fixed_vector<int, 4> a{1, 2, 3};
    metl::fixed_vector<int, 8> b{1, 2, 3};
    if (!(a == b)) {
      return 43;
    }
  }

  // erase_if.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3, 4, 5, 6};
    auto removed = metl::erase_if(v, [](int x) { return x % 2 == 0; });
    if (removed != 3 || v.size() != 3 || v[0] != 1 || v[1] != 3 || v[2] != 5) {
      return 44;
    }
  }

  // erase_if removes nothing.
  {
    metl::fixed_vector<int, 8> v{1, 3, 5};
    auto removed = metl::erase_if(v, [](int x) { return x > 100; });
    if (removed != 0 || v.size() != 3) {
      return 45;
    }
  }

  // erase_if removes all.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3};
    auto removed = metl::erase_if(v, [](int) { return true; });
    if (removed != 3 || !v.empty()) {
      return 46;
    }
  }

  // tracker lifetime balance after a battery of mutators.
  tracker::constructions = 0;
  tracker::destructions = 0;
  {
    metl::fixed_vector<tracker, 8> v;
    v.emplace_back(1);
    v.emplace_back(2);
    v.emplace_back(3);
    v.insert(v.begin() + 1, tracker(99));
    if (v.size() != 4 || v[1].value != 99) {
      return 47;
    }
    v.erase(v.begin());
    v.resize(2);
    v.assign(3, tracker(5));
    if (v.size() != 3 || v[0].value != 5) {
      return 48;
    }
    v.clear();
  }
  if (tracker::constructions != tracker::destructions) {
    return 49;
  }

  return 0;
}
