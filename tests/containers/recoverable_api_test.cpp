// Exercises the recoverable-API contract itself (SCOPE.md §9), not any one
// container: every `try_X` reports capacity failure by return value and leaves
// the container **exactly** as it was. The "unchanged" half is what these tests
// are really for — a `try_` form that half-applies and then reports false is
// worse than one that asserts, because the caller's recovery path runs against
// corrupted state.

#include "metl_check.hpp"

#include "metl/fixed_string.hpp"
#include "metl/fixed_vector.hpp"
#include "metl/flat_map.hpp"
#include "metl/static_unordered_map.hpp"

#include <array>
#include <cstddef>
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

}  // namespace

int main() {
  // ---------------------------------------------------------------------
  // fixed_vector::try_resize
  // ---------------------------------------------------------------------
  {
    metl::fixed_vector<int, 4> v;
    CHECK(v.try_resize(3));
    CHECK_EQ(v.size(), 3u);
    CHECK_EQ(v[0], 0);

    // Over capacity: refused, and the three existing elements survive.
    v[0] = 7;
    CHECK(!v.try_resize(5));
    CHECK_EQ(v.size(), 3u);
    CHECK_EQ(v[0], 7);

    // Shrinking always fits.
    CHECK(v.try_resize(1));
    CHECK_EQ(v.size(), 1u);
    CHECK_EQ(v[0], 7);

    CHECK(v.try_resize(4, 9));
    CHECK_EQ(v.size(), 4u);
    CHECK_EQ(v[3], 9);
    CHECK(!v.try_resize(5, 9));
    CHECK_EQ(v.size(), 4u);
  }

  // ---------------------------------------------------------------------
  // fixed_vector::try_assign — the check must happen before clear()
  // ---------------------------------------------------------------------
  {
    metl::fixed_vector<int, 4> v;
    CHECK(v.try_assign(2, 5));
    CHECK_EQ(v.size(), 2u);
    CHECK_EQ(v[1], 5);

    // The failing call must not have destroyed what was already there.
    CHECK(!v.try_assign(9, 1));
    CHECK_EQ(v.size(), 2u);
    CHECK_EQ(v[0], 5);
    CHECK_EQ(v[1], 5);

    const std::array<int, 3> src{{1, 2, 3}};
    CHECK(v.try_assign(src.begin(), src.end()));
    CHECK_EQ(v.size(), 3u);
    CHECK_EQ(v[2], 3);

    const std::array<int, 5> too_big{{1, 2, 3, 4, 5}};
    CHECK(!v.try_assign(too_big.begin(), too_big.end()));
    CHECK_EQ(v.size(), 3u);
    CHECK_EQ(v[0], 1);
  }

  // ---------------------------------------------------------------------
  // fixed_vector::try_insert / try_emplace — end() is the failure marker
  // ---------------------------------------------------------------------
  {
    metl::fixed_vector<int, 3> v;
    CHECK(v.try_push_back(1));
    CHECK(v.try_push_back(3));

    auto it = v.try_insert(v.begin() + 1, 2);
    CHECK(it != v.end());
    CHECK_EQ(*it, 2);
    CHECK_EQ(v.size(), 3u);

    // Full: refused, and a successful insert never returns end(), so the two
    // outcomes are distinguishable.
    CHECK(v.try_insert(v.begin(), 0) == v.end());
    CHECK_EQ(v.size(), 3u);
    CHECK_EQ(v[0], 1);

    CHECK(v.try_emplace(v.begin(), 0) == v.end());
    CHECK_EQ(v.size(), 3u);

    int movable = 42;
    CHECK(v.try_insert(v.begin(), std::move(movable)) == v.end());
    CHECK_EQ(v.size(), 3u);
  }

  // ---------------------------------------------------------------------
  // try_insert(pos, n, value) is all-or-nothing, never partial
  // ---------------------------------------------------------------------
  {
    metl::fixed_vector<int, 8> v;
    CHECK(v.try_assign(6, 1));

    // Room for 2 more, asked for 4: nothing at all may be written.
    CHECK(v.try_insert(v.begin(), 4, 9) == v.end());
    CHECK_EQ(v.size(), 6u);
    CHECK_EQ(v[0], 1);

    auto it = v.try_insert(v.begin(), 2, 9);
    CHECK(it != v.end());
    CHECK_EQ(v.size(), 8u);
    CHECK_EQ(v[0], 9);
    CHECK_EQ(v[1], 9);
    CHECK_EQ(v[2], 1);

    // n == 0 never fails, even on a full container.
    CHECK(v.try_insert(v.begin(), 0, 9) != v.end());
    CHECK_EQ(v.size(), 8u);
  }

  // A count large enough to wrap `size() + n` must still be refused. This is
  // the case a `size_ + n <= Capacity` check would let through.
  {
    metl::fixed_vector<int, 4> v;
    CHECK(v.try_push_back(1));
    const std::size_t huge = static_cast<std::size_t>(-1);
    CHECK(v.try_insert(v.begin(), huge, 9) == v.end());
    CHECK_EQ(v.size(), 1u);
  }

  // ---------------------------------------------------------------------
  // try_insert(pos, first, last)
  // ---------------------------------------------------------------------
  {
    metl::fixed_vector<int, 5> v;
    CHECK(v.try_assign(2, 7));

    const std::array<int, 4> too_many{{1, 2, 3, 4}};
    CHECK(v.try_insert(v.end(), too_many.begin(), too_many.end()) == v.end());
    CHECK_EQ(v.size(), 2u);

    const std::array<int, 3> fits{{1, 2, 3}};
    auto it = v.try_insert(v.end(), fits.begin(), fits.end());
    CHECK(it != v.end());
    CHECK_EQ(v.size(), 5u);
    CHECK_EQ(v[2], 1);
    CHECK_EQ(v[4], 3);
  }

  // The forward-iterator requirement is a hard `static_assert` in the header —
  // chosen over an `enable_if` because "no matching function for call to
  // try_assign" tells a user nothing, while the assert names the reason. The
  // cost is that it is not SFINAE-detectable, so the negative case cannot be
  // proved from inside a running test. It is proved instead by the
  // `api-contract` CI job, which compiles
  // tests/containers/forward_iterator_contract.cpp both ways and requires the
  // single-pass arm to FAIL — the same shape as the handle-atomics capability
  // gate. See tests/containers/forward_iterator_contract.cpp.

  // ---------------------------------------------------------------------
  // No leaks or double-destroys along the failing paths.
  // ---------------------------------------------------------------------
  tracker::constructions = 0;
  tracker::destructions = 0;
  {
    metl::fixed_vector<tracker, 4> v;
    CHECK(v.try_assign(3, tracker(1)));
    CHECK(!v.try_assign(9, tracker(2)));
    CHECK(!v.try_resize(7));
    CHECK(v.try_insert(v.begin(), 4, tracker(3)) == v.end());
    CHECK(v.try_emplace(v.begin(), 4) != v.end());
    CHECK_EQ(v.size(), 4u);
    CHECK(v.try_emplace(v.begin(), 5) == v.end());
    v.clear();
  }
  CHECK_EQ(tracker::constructions, tracker::destructions);

  // ---------------------------------------------------------------------
  // fixed_string: try_assign / try_append, and their asserting twins
  // ---------------------------------------------------------------------
  {
    metl::fixed_string<8> s;
    CHECK(s.try_assign("abc"));
    CHECK_EQ(s.size(), 3u);

    CHECK(!s.try_assign("far too long"));
    CHECK_EQ(s.size(), 3u);
    CHECK(s == metl::fixed_string<8>("abc"));

    CHECK(s.try_append("de"));
    CHECK_EQ(s.size(), 5u);

    CHECK(!s.try_append("far too long"));
    CHECK_EQ(s.size(), 5u);
    CHECK_EQ(s.c_str()[5], '\0');

    const char raw[] = {'f', 'g'};
    CHECK(s.try_append(metl::span<const char>(raw, 2)));
    CHECK_EQ(s.size(), 7u);

    // The asserting twins take the same arguments and do the same thing when
    // the precondition holds; only the overflow behaviour differs.
    metl::fixed_string<8> t;
    t.assign("abc");
    t.append("de");
    t.append(metl::span<const char>(raw, 2));
    CHECK(t == s);
  }

  // ---------------------------------------------------------------------
  // try_insert_or_assign: the bool answers "did it fit", not "was it inserted"
  // ---------------------------------------------------------------------
  {
    metl::flat_map<int, int, 2> m;
    CHECK(m.try_insert_or_assign(1, 10));
    CHECK(m.try_insert_or_assign(2, 20));
    CHECK(m.full());

    // Assigning over an existing key still fits, even when full.
    CHECK(m.try_insert_or_assign(1, 11));
    CHECK_EQ(*m.find(1), 11);
    CHECK_EQ(m.size(), 2u);

    // A *new* key does not.
    CHECK(!m.try_insert_or_assign(3, 30));
    CHECK_EQ(m.size(), 2u);
    CHECK(!m.contains(3));

    // The asserting twin hands back the element instead of a bool.
    CHECK_EQ(m.insert_or_assign(1, 12).value, 12);
  }

  {
    metl::static_unordered_map<int, int, 2> m;
    CHECK(m.try_insert_or_assign(1, 10));
    CHECK(m.try_insert_or_assign(2, 20));
    CHECK(m.full());

    CHECK(m.try_insert_or_assign(2, 22));
    CHECK_EQ(m.size(), 2u);
    CHECK(!m.try_insert_or_assign(3, 30));
    CHECK_EQ(m.size(), 2u);
    CHECK(!m.contains(3));

    CHECK_EQ(m.insert_or_assign(2, 23).value, 23);
    CHECK_EQ(m.size(), 2u);
  }

  return metl_test::exit_code();
}
