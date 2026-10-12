// erase(iterator) and erase_if on the four associative containers (#313).
//
// Only erase(key) existed. A flat container's iterator is a pointer, and a
// pointer converts to bool, so flat_set<bool>::erase(s.begin()) erased the key
// `true` instead of the first element. Nor could a loop erase as it iterated.

#include "metl_check.hpp"

#include <cstddef>
#include <functional>

#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

void flat_set_of_bool_erases_the_position() {
  metl::flat_set<bool, 2> s;
  CHECK(s.try_emplace(false));
  CHECK(s.try_emplace(true));
  const auto next = s.erase(s.begin());  // the first element, `false`
  CHECK_EQ(s.size(), 1u);
  CHECK(s.contains(true));
  CHECK(!s.contains(false));
  CHECK(next == s.begin());
  CHECK(*next);
}

// With a transparent comparator, erase(const K&) is a template that would take
// an iterator argument exactly; it must leave iterators to erase(iterator).
void transparent_set_of_pointers_erases_the_position() {
  int a = 0;
  int b = 0;
  metl::flat_set<const int*, 4, std::less<>> s;
  CHECK(s.try_emplace(&a));
  CHECK(s.try_emplace(&b));
  const int* first = *s.begin();
  (void)s.erase(s.begin());
  CHECK_EQ(s.size(), 1u);
  CHECK(!s.contains(first));
}

void flat_map_erases_while_iterating() {
  metl::flat_map<int, int, 8> m;
  for (int k = 0; k < 6; ++k) {
    CHECK(m.try_emplace(k, k * 10));
  }
  for (auto it = m.begin(); it != m.end();) {
    if (it->key % 2 == 0) {
      it = m.erase(it);
    } else {
      ++it;
    }
  }
  CHECK_EQ(m.size(), 3u);
  CHECK(m.contains(1));
  CHECK(m.contains(3));
  CHECK(m.contains(5));
  // Erasing the last element returns end().
  auto last = m.begin() + 2;
  CHECK(m.erase(last) == m.end());

  metl::flat_map<int, int, 8> n;
  for (int k = 0; k < 6; ++k) {
    CHECK(n.try_emplace(k, k));
  }
  CHECK_EQ(erase_if(n, [](const auto& entry) { return entry.key >= 4; }), 2u);
  CHECK_EQ(n.size(), 4u);

  metl::flat_set<int, 8> s;
  for (int k = 0; k < 6; ++k) {
    CHECK(s.try_emplace(k));
  }
  CHECK_EQ(erase_if(s, [](int k) { return k % 3 == 0; }), 2u);
  CHECK_EQ(s.size(), 4u);
  CHECK(!s.contains(0));
  CHECK(!s.contains(3));
}

void unordered_erase_while_iterating() {
  metl::static_unordered_map<int, int, 8> m;
  for (int k = 0; k < 8; ++k) {
    CHECK(m.try_emplace(k, k));
  }
  std::size_t visited = 0;
  for (auto it = m.begin(); it != m.end();) {
    ++visited;
    if (it->key % 2 == 0) {
      it = m.erase(it);
    } else {
      ++it;
    }
  }
  CHECK_EQ(visited, 8u);  // each element seen once, none skipped
  CHECK_EQ(m.size(), 4u);
  for (int k = 0; k < 8; ++k) {
    CHECK_EQ(m.contains(k), k % 2 == 1);
  }

  // Erasing never moves an element, so another iterator stays valid.
  auto keep = m.begin();
  auto other = keep;
  ++other;
  const int kept_key = keep->key;
  (void)m.erase(other);
  CHECK_EQ(keep->key, kept_key);

  metl::static_unordered_set<int, 8> s;
  for (int k = 0; k < 8; ++k) {
    CHECK(s.try_emplace(k));
  }
  CHECK_EQ(erase_if(s, [](int k) { return k < 3; }), 3u);
  CHECK_EQ(s.size(), 5u);
  const std::size_t before = m.size();
  CHECK_EQ(erase_if(m, [kept_key](const auto& entry) { return entry.key == kept_key; }), 1u);
  CHECK_EQ(m.size(), before - 1);
  CHECK(!m.contains(kept_key));
}

}  // namespace

int main() {
  flat_set_of_bool_erases_the_position();
  transparent_set_of_pointers_erases_the_position();
  flat_map_erases_while_iterating();
  unordered_erase_while_iterating();
  return metl_test::exit_code();
}
