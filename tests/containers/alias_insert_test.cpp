// Regression tests for inserting a value that refers to an element of the same
// container (docs/AUDIT.md, Section G).
//
// Each of these used to move or destroy existing elements before reading the
// argument, so `v.insert(v.begin(), v[0])` copied a moved-from or destroyed
// object. `poisoned` makes that visible without a sanitizer: a moved-from
// value reads -2 and a destroyed one reads -1, so the checks hold on a
// freestanding target too.

#include "metl_check.hpp"

#include <cstddef>
#include <initializer_list>

#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/ring_buffer.hpp>

namespace {

struct poisoned {
  int value;

  explicit poisoned(int v = 0) noexcept : value(v) {}
  poisoned(const poisoned& o) noexcept : value(o.value) {}
  poisoned(poisoned&& o) noexcept : value(o.value) { o.value = -2; }
  poisoned& operator=(const poisoned& o) noexcept {
    value = o.value;
    return *this;
  }
  poisoned& operator=(poisoned&& o) noexcept {
    value = o.value;
    o.value = -2;
    return *this;
  }
  ~poisoned() { value = -1; }
};

template <typename V>
bool equals(const V& v, std::initializer_list<int> expected) {
  if (v.size() != expected.size()) {
    return false;
  }
  std::size_t i = 0;
  for (int e : expected) {
    if (v[i].value != e) {
      return false;
    }
    ++i;
  }
  return true;
}

}  // namespace

int main() {
  // fixed_vector::insert(pos, const T&) with an element of the same vector.
  {
    metl::fixed_vector<poisoned, 8> v;
    v.emplace_back(10);
    v.emplace_back(20);
    v.emplace_back(30);
    v.insert(v.begin(), v[0]);
    CHECK(equals(v, {10, 10, 20, 30}));
  }
  {
    metl::fixed_vector<poisoned, 8> v;
    v.emplace_back(10);
    v.emplace_back(20);
    v.emplace_back(30);
    v.insert(v.begin(), v[2]);  // the element the shift moves last
    CHECK(equals(v, {30, 10, 20, 30}));
  }
  // emplace with constructor arguments taken from an element.
  {
    metl::fixed_vector<poisoned, 8> v;
    v.emplace_back(10);
    v.emplace_back(20);
    v.emplace(v.begin(), v[1].value);  // v[1] is shifted before it is read
    CHECK(equals(v, {20, 10, 20}));
  }
  // insert(pos, n, value): every copy must be of the original value.
  {
    metl::fixed_vector<poisoned, 8> v;
    v.emplace_back(10);
    v.emplace_back(20);
    v.emplace_back(30);
    v.insert(v.begin(), 2, v[1]);
    CHECK(equals(v, {20, 20, 10, 20, 30}));
  }

  // flat_map: mapped value taken from an entry the insert shifts.
  {
    metl::flat_map<int, poisoned, 8> m;
    CHECK(m.try_emplace(5, poisoned(50)));
    CHECK(m.try_emplace(7, poisoned(70)));
    CHECK(m.try_emplace(1, m.nth(0).value));
    CHECK_EQ(m.size(), 3u);
    CHECK_EQ(m.nth(0).value.value, 50);
    CHECK_EQ(m.nth(1).value.value, 50);
    CHECK_EQ(m.nth(2).value.value, 70);
  }
  {
    metl::flat_map<int, poisoned, 8> m;
    m.emplace(5, poisoned(50));
    m.insert_or_assign(1, m.nth(0).value);
    CHECK_EQ(m.nth(0).value.value, 50);
    m.emplace(0, m.nth(1).value);
    CHECK_EQ(m.nth(0).value.value, 50);
  }

  // ring_buffer::push_overwrite on a full ring, with the evicted element.
  {
    metl::ring_buffer<poisoned, 3> rb;
    rb.emplace_back(1);
    rb.emplace_back(2);
    rb.emplace_back(3);
    rb.push_overwrite(rb.front());
    CHECK_EQ(rb.size(), 3u);
    CHECK_EQ(rb.front().value, 2);
    CHECK_EQ(rb.back().value, 1);
  }

  return metl_test::exit_code();
}
