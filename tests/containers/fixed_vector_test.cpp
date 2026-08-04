#include "metl_check.hpp"

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
  CHECK(numbers.empty());
  CHECK_EQ(numbers.capacity(), 3u);

  CHECK(numbers.try_push_back(1));
  CHECK(numbers.try_emplace_back(2));
  CHECK(numbers.try_push_back(3));

  CHECK(numbers.full());
  CHECK(!numbers.try_push_back(4));

  CHECK_EQ(numbers.front(), 1);
  CHECK_EQ(numbers.back(), 3);
  CHECK_EQ(numbers.size(), 3u);

  auto view = numbers.as_span();
  CHECK_EQ(view.size(), 3u);
  CHECK_EQ(view[1], 2);

  numbers.pop_back();
  CHECK_EQ(numbers.size(), 2u);
  CHECK_EQ(numbers.back(), 2);

  metl::fixed_vector<int, 3> copied(numbers);
  CHECK_EQ(copied.size(), 2u);
  CHECK_EQ(copied[0], 1);
  CHECK_EQ(copied[1], 2);

  tracker::constructions = 0;
  tracker::destructions = 0;

  {
    metl::fixed_vector<tracker, 2> tracked;
    tracked.emplace_back(10);
    tracked.emplace_back(20);

    CHECK_EQ(tracked.size(), 2u);
    CHECK_EQ(tracked[0].value, 10);
    CHECK_EQ(tracked[1].value, 20);

    tracked.clear();
    CHECK(tracked.empty());
  }

  CHECK_EQ(tracker::constructions, tracker::destructions);

  // initializer_list ctor.
  metl::fixed_vector<int, 5> il_vec{1, 2, 3, 4};
  CHECK_EQ(il_vec.size(), 4u);
  CHECK_EQ(il_vec[0], 1);
  CHECK_EQ(il_vec[3], 4);

  // at().
  CHECK_EQ(il_vec.at(2), 3);

  // insert(pos, value) at beginning.
  {
    metl::fixed_vector<int, 8> v{2, 3, 4};
    auto it = v.insert(v.begin(), 1);
    CHECK_EQ(*it, 1);
    CHECK_EQ(v.size(), 4u);
    CHECK_EQ(v[0], 1);
    CHECK_EQ(v[1], 2);
    CHECK_EQ(v[3], 4);
  }

  // insert(pos, value) at middle.
  {
    metl::fixed_vector<int, 8> v{1, 2, 4, 5};
    auto it = v.insert(v.begin() + 2, 3);
    CHECK_EQ(*it, 3);
    CHECK_EQ(v.size(), 5u);
    CHECK_EQ(v[2], 3);
    CHECK_EQ(v[3], 4);
    CHECK_EQ(v[4], 5);
  }

  // insert(pos, value) at end.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3};
    auto it = v.insert(v.end(), 4);
    CHECK_EQ(*it, 4);
    CHECK_EQ(v.size(), 4u);
    CHECK_EQ(v.back(), 4);
  }

  // emplace(pos, args...).
  {
    metl::fixed_vector<int, 8> v{1, 2, 4};
    auto it = v.emplace(v.begin() + 2, 3);
    CHECK_EQ(*it, 3);
    CHECK_EQ(v.size(), 4u);
    CHECK_EQ(v[2], 3);
    CHECK_EQ(v[3], 4);
  }

  // insert(pos, n, value).
  {
    metl::fixed_vector<int, 8> v{1, 5};
    auto it = v.insert(v.begin() + 1, 3, 9);
    CHECK_EQ(*it, 9);
    CHECK_EQ(v.size(), 5u);
    CHECK_EQ(v[0], 1);
    CHECK_EQ(v[1], 9);
    CHECK_EQ(v[2], 9);
    CHECK_EQ(v[3], 9);
    CHECK_EQ(v[4], 5);
  }

  // insert(pos, n, value) with n > tail.
  {
    metl::fixed_vector<int, 8> v{1, 2};
    v.insert(v.end(), 3, 7);
    CHECK_EQ(v.size(), 5u);
    CHECK_EQ(v[0], 1);
    CHECK_EQ(v[1], 2);
    CHECK_EQ(v[2], 7);
    CHECK_EQ(v[3], 7);
    CHECK_EQ(v[4], 7);
  }

  // insert(pos, n, value) with n=0.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3};
    auto it = v.insert(v.begin() + 1, 0, 99);
    CHECK(it == v.begin() + 1);
    CHECK_EQ(v.size(), 3u);
  }

  // insert(pos, first, last).
  {
    metl::fixed_vector<int, 8> v{1, 5};
    int src[] = {2, 3, 4};
    v.insert(v.begin() + 1, src, src + 3);
    CHECK_EQ(v.size(), 5u);
    CHECK_EQ(v[0], 1);
    CHECK_EQ(v[1], 2);
    CHECK_EQ(v[2], 3);
    CHECK_EQ(v[3], 4);
    CHECK_EQ(v[4], 5);
  }

  // erase(pos).
  {
    metl::fixed_vector<int, 8> v{1, 2, 3, 4, 5};
    auto it = v.erase(v.begin() + 2);
    CHECK_EQ(*it, 4);
    CHECK_EQ(v.size(), 4u);
    CHECK_EQ(v[0], 1);
    CHECK_EQ(v[1], 2);
    CHECK_EQ(v[2], 4);
    CHECK_EQ(v[3], 5);
  }

  // erase(first, last).
  {
    metl::fixed_vector<int, 8> v{1, 2, 3, 4, 5};
    auto it = v.erase(v.begin() + 1, v.begin() + 4);
    CHECK_EQ(*it, 5);
    CHECK_EQ(v.size(), 2u);
    CHECK_EQ(v[0], 1);
    CHECK_EQ(v[1], 5);
  }

  // erase entire range.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3};
    v.erase(v.begin(), v.end());
    CHECK(v.empty());
  }

  // resize(n) shrink and grow.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3, 4, 5};
    v.resize(2);
    CHECK_EQ(v.size(), 2u);
    CHECK_EQ(v[0], 1);
    CHECK_EQ(v[1], 2);
    v.resize(4);
    CHECK_EQ(v.size(), 4u);
    CHECK_EQ(v[0], 1);
    CHECK_EQ(v[1], 2);
    CHECK_EQ(v[2], 0);
    CHECK_EQ(v[3], 0);
  }

  // resize(n, value).
  {
    metl::fixed_vector<int, 8> v{1, 2};
    v.resize(5, 7);
    CHECK_EQ(v.size(), 5u);
    CHECK_EQ(v[2], 7);
    CHECK_EQ(v[3], 7);
    CHECK_EQ(v[4], 7);
  }

  // assign(n, value).
  {
    metl::fixed_vector<int, 8> v{1, 2, 3};
    v.assign(4, 9);
    CHECK_EQ(v.size(), 4u);
    CHECK_EQ(v[0], 9);
    CHECK_EQ(v[3], 9);
  }

  // assign(first, last).
  {
    metl::fixed_vector<int, 8> v{1, 2, 3};
    int src[] = {10, 20, 30, 40};
    v.assign(src, src + 4);
    CHECK_EQ(v.size(), 4u);
    CHECK_EQ(v[0], 10);
    CHECK_EQ(v[3], 40);
  }

  // swap (same size).
  {
    metl::fixed_vector<int, 8> a{1, 2, 3};
    metl::fixed_vector<int, 8> b{7, 8, 9};
    a.swap(b);
    CHECK_EQ(a.size(), 3u);
    CHECK_EQ(a[0], 7);
    CHECK_EQ(b[0], 1);
  }

  // swap (different sizes).
  {
    metl::fixed_vector<int, 8> a{1, 2};
    metl::fixed_vector<int, 8> b{7, 8, 9, 10};
    a.swap(b);
    CHECK_EQ(a.size(), 4u);
    CHECK_EQ(a[0], 7);
    CHECK_EQ(a[3], 10);
    CHECK_EQ(b.size(), 2u);
    CHECK_EQ(b[0], 1);
    CHECK_EQ(b[1], 2);
  }

  // free swap.
  {
    metl::fixed_vector<int, 8> a{1, 2};
    metl::fixed_vector<int, 8> b{9};
    metl::swap(a, b);
    CHECK_EQ(a.size(), 1u);
    CHECK_EQ(a[0], 9);
    CHECK_EQ(b.size(), 2u);
  }

  // reverse iterators.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3, 4};
    int sum = 0;
    int expected = 4;
    for (auto it = v.rbegin(); it != v.rend(); ++it) {
      CHECK_EQ(*it, expected);
      --expected;
      sum += *it;
    }
    CHECK_EQ(sum, 10);
  }

  // const reverse iterators.
  {
    const metl::fixed_vector<int, 4> v{10, 20, 30};
    int expected = 30;
    for (auto it = v.crbegin(); it != v.crend(); ++it) {
      CHECK_EQ(*it, expected);
      expected -= 10;
    }
  }

  // comparison operators.
  {
    metl::fixed_vector<int, 4> a{1, 2, 3};
    metl::fixed_vector<int, 4> b{1, 2, 3};
    metl::fixed_vector<int, 4> c{1, 2, 4};
    metl::fixed_vector<int, 4> d{1, 2};
    CHECK(a == b);
    CHECK(!(a != b));
    CHECK(a < c);
    CHECK(d < a);
    CHECK(a <= b);
    CHECK(a >= b);
    CHECK(c > a);
    CHECK(!(a > c));
  }

  // Different capacity comparison.
  {
    metl::fixed_vector<int, 4> a{1, 2, 3};
    metl::fixed_vector<int, 8> b{1, 2, 3};
    CHECK(a == b);
  }

  // erase_if.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3, 4, 5, 6};
    auto removed = metl::erase_if(v, [](int x) { return x % 2 == 0; });
    CHECK_EQ(removed, 3u);
    CHECK_EQ(v.size(), 3u);
    CHECK_EQ(v[0], 1);
    CHECK_EQ(v[1], 3);
    CHECK_EQ(v[2], 5);
  }

  // erase_if removes nothing.
  {
    metl::fixed_vector<int, 8> v{1, 3, 5};
    auto removed = metl::erase_if(v, [](int x) { return x > 100; });
    CHECK_EQ(removed, 0u);
    CHECK_EQ(v.size(), 3u);
  }

  // erase_if removes all.
  {
    metl::fixed_vector<int, 8> v{1, 2, 3};
    auto removed = metl::erase_if(v, [](int) { return true; });
    CHECK_EQ(removed, 3u);
    CHECK(v.empty());
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
    CHECK_EQ(v.size(), 4u);
    CHECK_EQ(v[1].value, 99);
    v.erase(v.begin());
    v.resize(2);
    v.assign(3, tracker(5));
    CHECK_EQ(v.size(), 3u);
    CHECK_EQ(v[0].value, 5);
    v.clear();
  }
  CHECK_EQ(tracker::constructions, tracker::destructions);

  return metl_test::exit_code();
}
