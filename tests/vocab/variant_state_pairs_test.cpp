// Every pair of states for variant's copy, move, assignment, emplace and
// comparisons, including the valueless state.
//
// Changing which alternative is active, or leaving or entering the valueless
// state, is where a lifetime bug lives: an alternative destroyed twice, or a
// destroyed one left under a live index. Each case runs on alternatives that
// count their live objects and checks the counts after the operation.
//
// The alternatives take both of assignment's routes: `tracked` cannot throw,
// so a switch to it constructs in place; `brittle` can, so a switch to it
// goes through a backup, and it has no assignment operator, so an assignment
// between two brittles destroys and reconstructs. A throw in that
// reconstruction is what makes a variant valueless here, so the valueless
// cases need exceptions.

#include "metl_check.hpp"

#include <cstddef>
#include <utility>

#include <metl/in_place.hpp>
#include <metl/variant.hpp>

namespace {

struct tracked {
  static int live;
  int value;

  explicit tracked(int v) noexcept : value(v) { ++live; }
  tracked(const tracked& other) noexcept : value(other.value) { ++live; }
  tracked(tracked&& other) noexcept : value(other.value) { ++live; }
  tracked& operator=(const tracked&) noexcept = default;
  tracked& operator=(tracked&&) noexcept = default;
  ~tracked() { --live; }

  friend bool operator==(const tracked& a, const tracked& b) { return a.value == b.value; }
  friend bool operator!=(const tracked& a, const tracked& b) { return a.value != b.value; }
  friend bool operator<(const tracked& a, const tracked& b) { return a.value < b.value; }
  friend bool operator<=(const tracked& a, const tracked& b) { return a.value <= b.value; }
  friend bool operator>(const tracked& a, const tracked& b) { return a.value > b.value; }
  friend bool operator>=(const tracked& a, const tracked& b) { return a.value >= b.value; }
};

int tracked::live = 0;

struct brittle {
  static int live;
#if !METL_NO_EXCEPTIONS
  static bool arm;  // when set, the next copy throws
#endif
  int value;

  explicit brittle(int v) noexcept : value(v) { ++live; }
  brittle(const brittle& other) : value(other.value) {
#if !METL_NO_EXCEPTIONS
    if (arm) {
      arm = false;
      throw 0;
    }
#endif
    ++live;
  }
  brittle& operator=(const brittle&) = delete;
  ~brittle() { --live; }

  friend bool operator==(const brittle& a, const brittle& b) { return a.value == b.value; }
  friend bool operator!=(const brittle& a, const brittle& b) { return a.value != b.value; }
  friend bool operator<(const brittle& a, const brittle& b) { return a.value < b.value; }
  friend bool operator<=(const brittle& a, const brittle& b) { return a.value <= b.value; }
  friend bool operator>(const brittle& a, const brittle& b) { return a.value > b.value; }
  friend bool operator>=(const brittle& a, const brittle& b) { return a.value >= b.value; }
};

int brittle::live = 0;
#if !METL_NO_EXCEPTIONS
bool brittle::arm = false;
#endif

// Cannot be moved, so emplace builds it in place, after the old alternative
// is gone. A negative value throws from the constructor.
struct pinned {
  static int live;
  int value;

  explicit pinned(int v) : value(v) {
#if !METL_NO_EXCEPTIONS
    if (v < 0) {
      throw 0;
    }
#endif
    ++live;
  }
  pinned(const pinned&) = delete;
  pinned& operator=(const pinned&) = delete;
  ~pinned() { --live; }
};

int pinned::live = 0;

using var = metl::variant<int, tracked, brittle>;
using pinned_var = metl::variant<int, pinned>;

struct state {
  std::size_t index;  // metl::variant_npos for valueless
  int value;
};

constexpr state states[] = {
    {0, 1},
    {1, 1},
    {1, 2},
    {2, 1},
    {2, 2},
#if !METL_NO_EXCEPTIONS
    {metl::variant_npos, 0},
#endif
};

var make(state s) {
  switch (s.index) {
    case 0:
      return var{metl::in_place_index<0>, s.value};
    case 1:
      return var{metl::in_place_index<1>, s.value};
    case 2:
      return var{metl::in_place_index<2>, s.value};
    default:
      break;
  }
  var v{metl::in_place_index<2>, 0};
#if !METL_NO_EXCEPTIONS
  const var source{metl::in_place_index<2>, 0};
  brittle::arm = true;
  try {
    v = source;  // destroy, then a reconstruction that throws
  } catch (int) {
  }
#endif
  return v;
}

bool holds(const var& v, state s) {
  if (v.index() != s.index) {
    return false;
  }
  switch (s.index) {
    case 0:
      return metl::get<0>(v) == s.value;
    case 1:
      return metl::get<1>(v).value == s.value;
    case 2:
      return metl::get<2>(v).value == s.value;
    default:
      return v.valueless_by_exception();
  }
}

int live_of(state s, std::size_t index) {
  return s.index == index ? 1 : 0;
}

bool counts_are(int t, int b) {
  return tracked::live == t && brittle::live == b;
}

// Orders like the variant: valueless first, then by index, then by value.
int key(state s) {
  return s.index == metl::variant_npos ? -1 : static_cast<int>(s.index) * 100 + s.value;
}

void construction(state s) {
  {
    const var source = make(s);
    const var copy(source);
    CHECK(holds(copy, s));
    CHECK(holds(source, s));
    CHECK(counts_are(2 * live_of(s, 1), 2 * live_of(s, 2)));
  }
  {
    var source = make(s);
    const var moved(std::move(source));
    CHECK(holds(moved, s));
    CHECK_EQ(source.index(), s.index);  // a moved-from variant keeps its alternative
    CHECK(counts_are(2 * live_of(s, 1), 2 * live_of(s, 2)));
  }
  CHECK(counts_are(0, 0));
}

void self_assignment(state s) {
  var x = make(s);
  var& alias = x;
  x = alias;
  CHECK(holds(x, s));
  x = std::move(alias);
  CHECK(holds(x, s));
  CHECK(counts_are(live_of(s, 1), live_of(s, 2)));
}

void assignment(state a, state b) {
  {
    var x = make(a);
    const var y = make(b);
    x = y;
    CHECK(holds(x, b));
    CHECK(holds(y, b));
    CHECK(counts_are(2 * live_of(b, 1), 2 * live_of(b, 2)));
  }
  {
    var x = make(a);
    var y = make(b);
    x = std::move(y);
    CHECK(holds(x, b));
    CHECK_EQ(y.index(), b.index);
    CHECK(counts_are(2 * live_of(b, 1), 2 * live_of(b, 2)));
  }
  CHECK(counts_are(0, 0));
}

void comparisons(state a, state b) {
  const var x = make(a);
  const var y = make(b);
  CHECK_EQ(x == y, key(a) == key(b));
  CHECK_EQ(x != y, key(a) != key(b));
  CHECK_EQ(x < y, key(a) < key(b));
  CHECK_EQ(x <= y, key(a) <= key(b));
  CHECK_EQ(x > y, key(a) > key(b));
  CHECK_EQ(x >= y, key(a) >= key(b));
}

// emplace from every state, to an alternative that can throw and one that
// cannot: each leaves exactly the new alternative alive.
void emplacement(state s) {
  var x = make(s);
  x.emplace<1>(7);
  CHECK(holds(x, state{1, 7}));
  CHECK(counts_are(1, 0));
  x.emplace<2>(8);
  CHECK(holds(x, state{2, 8}));
  CHECK(counts_are(0, 1));
}

// emplace of an alternative built in place, from a value, from itself, and
// from valueless -- which a throw from that same construction leaves.
void emplacement_in_place() {
  pinned_var x{metl::in_place_index<0>, 1};
  x.emplace<1>(2);
  CHECK_EQ(metl::get<1>(x).value, 2);
  x.emplace<1>(3);
  CHECK_EQ(metl::get<1>(x).value, 3);
  CHECK_EQ(pinned::live, 1);
#if !METL_NO_EXCEPTIONS
  try {
    x.emplace<1>(-1);
  } catch (int) {
  }
  CHECK(x.valueless_by_exception());
  CHECK_EQ(pinned::live, 0);
  x.emplace<1>(4);
  CHECK_EQ(metl::get<1>(x).value, 4);
  CHECK_EQ(pinned::live, 1);
#endif
}

// get_if takes a pointer, and a null one answers null, by type and by index.
void get_if_on_null() {
  var* none = nullptr;
  const var* const_none = nullptr;
  CHECK(metl::get_if<int>(none) == nullptr);
  CHECK(metl::get_if<int>(const_none) == nullptr);
  CHECK(metl::get_if<0>(none) == nullptr);
  CHECK(metl::get_if<0>(const_none) == nullptr);
}

}  // namespace

int main() {
  for (const state& a : states) {
    construction(a);
    self_assignment(a);
    emplacement(a);
    CHECK(counts_are(0, 0));
    for (const state& b : states) {
      assignment(a, b);
      comparisons(a, b);
    }
  }
  emplacement_in_place();
  CHECK_EQ(pinned::live, 0);
  get_if_on_null();
  CHECK(counts_are(0, 0));
  return metl_test::exit_code();
}
