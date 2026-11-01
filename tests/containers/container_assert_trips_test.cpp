// Trips every METL_ASSERT precondition of the containers once (#364):
// fixed_vector, fixed_string, ring_buffer and fixed_deque (ring_core),
// flat_map, flat_set, static_message_queue, and static_unordered_map/set's
// capacity checks.
//
// Each is a precondition a caller can break: an index past the end, front()
// on an empty container, the asserting form of an insert when full, a range
// longer than the capacity. Every overload is tripped on its own. The trip
// longjmps out, so each container lives in static storage, and a constructor
// that trips is run on storage that is never destroyed.
//
// flat_map and flat_set's erase(pos) also HARDENs that `pos` is one of the
// elements: end(), or an address inside the elements that is not an
// element's start, would make the shift move and destroy outside them.
//
// static_unordered_map/set also assert, after `size_ < Capacity`, that a slot
// was found and that it is free. Once the size check holds, a free slot always
// exists, so those two are internal invariants no caller can break; they stay
// in tools/check_assert_trips.py's ALLOWED with that reason.

#include "assert_trip.hpp"
#include "metl_check.hpp"

#include <array>
#include <initializer_list>
#include <new>

#include <metl/fixed_deque.hpp>
#include <metl/fixed_string.hpp>
#include <metl/fixed_vector.hpp>
#include <metl/flat_map.hpp>
#include <metl/flat_set.hpp>
#include <metl/ring_buffer.hpp>
#include <metl/span.hpp>
#include <metl/static_message_queue.hpp>
#include <metl/static_unordered_map.hpp>
#include <metl/static_unordered_set.hpp>

namespace {

using metl_test::trips;

// Builds a T in storage that is never destroyed, so a constructor may trip.
template <typename T, typename... Args>
void construct_abandoned(Args&&... args) {
  alignas(T) static unsigned char raw[sizeof(T)];
  ::new (static_cast<void*>(raw)) T(static_cast<Args&&>(args)...);
}

// ---- fixed_vector ------------------------------------------------------------------

void fixed_vector_trips() {
  using vec = metl::fixed_vector<int, 2>;
  static vec empty;
  static vec two{1, 2};
  CHECK(trips([] { construct_abandoned<vec>(std::initializer_list<int>{1, 2, 3}); }));
  CHECK(trips([] { (void)two[2]; }));
  CHECK(trips([] { (void)static_cast<const vec&>(two)[2]; }));
  CHECK(trips([] { (void)two.at(2); }));
  CHECK(trips([] { (void)static_cast<const vec&>(two).at(2); }));
  CHECK(trips([] { (void)empty.front(); }));
  CHECK(trips([] { (void)static_cast<const vec&>(empty).front(); }));
  CHECK(trips([] { (void)empty.back(); }));
  CHECK(trips([] { (void)static_cast<const vec&>(empty).back(); }));
  CHECK(trips([] { (void)two.emplace_back(3); }));
  CHECK(trips([] { (void)two.insert(two.begin(), 1, 9); }));
  CHECK(trips([] { (void)two.insert(two.begin(), two.begin(), two.end()); }));  // its own range
  CHECK(trips([] { two.assign(3, 9); }));
  static const int three[] = {7, 8, 9};
  CHECK(trips([] { two.assign(three, three + 3); }));
}

// ---- fixed_string --------------------------------------------------------------------

void fixed_string_trips() {
  using str = metl::fixed_string<3>;
  static str empty;
  static str full("abc");
  CHECK(trips([] { construct_abandoned<str>("abcd"); }));
  CHECK(trips([] { (void)full[3]; }));
  CHECK(trips([] { (void)static_cast<const str&>(full)[3]; }));
  CHECK(trips([] { (void)empty.front(); }));
  CHECK(trips([] { (void)static_cast<const str&>(empty).front(); }));
  CHECK(trips([] { (void)empty.back(); }));
  CHECK(trips([] { (void)static_cast<const str&>(empty).back(); }));
  CHECK(trips([] { full.push_back('d'); }));
  CHECK(trips([] { empty.pop_back(); }));
  CHECK(trips([] { empty.assign("abcd"); }));
  CHECK(trips([] { full.append("d"); }));
  static const char d[] = {'d'};
  CHECK(trips([] { full.append(metl::span<const char>(d, 1)); }));
}

// ---- ring_buffer / fixed_deque (ring_core) --------------------------------------------------

void ring_trips() {
  using ring = metl::ring_buffer<int, 2>;
  static ring empty;
  static ring full;
  (void)full.try_emplace_back(1);
  (void)full.try_emplace_back(2);
  CHECK(trips([] { (void)empty.front(); }));
  CHECK(trips([] { (void)static_cast<const ring&>(empty).front(); }));
  CHECK(trips([] { (void)empty.back(); }));
  CHECK(trips([] { (void)static_cast<const ring&>(empty).back(); }));
  CHECK(trips([] { (void)full.at(2); }));
  CHECK(trips([] { (void)static_cast<const ring&>(full).at(2); }));
  CHECK(trips([] { (void)full.emplace_back(3); }));
  static const int value = 3;
  CHECK(trips([] { (void)full.push_back(value); }));
  CHECK(trips([] { (void)full.push_back(3); }));

  static metl::fixed_deque<int, 1> deque;
  (void)deque.try_emplace_back(1);
  CHECK(trips([] { (void)deque.emplace_front(2); }));
}

// ---- flat_map / flat_set ------------------------------------------------------------------

void flat_trips() {
  using map = metl::flat_map<int, int, 1>;
  static map full;
  (void)full.try_emplace(1, 10);
  CHECK(trips([] { (void)full[1]; }));
  CHECK(trips([] { (void)static_cast<const map&>(full)[1]; }));
  CHECK(trips([] { (void)full.at(1); }));
  CHECK(trips([] { (void)static_cast<const map&>(full).at(1); }));
  CHECK(trips([] { (void)full.nth(1); }));
  CHECK(trips([] { (void)static_cast<const map&>(full).nth(1); }));
  CHECK(trips([] { (void)full.emplace(2, 20); }));
  CHECK(trips([] { (void)full.insert_or_assign(2, 20); }));

  using set = metl::flat_set<int, 1>;
  static set one;
  (void)one.try_emplace(1);
  CHECK(trips([] { (void)one[1]; }));
  CHECK(trips([] { (void)static_cast<const set&>(one)[1]; }));
  CHECK(trips([] { (void)one.at(1); }));
  CHECK(trips([] { (void)static_cast<const set&>(one).at(1); }));
  CHECK(trips([] { (void)one.nth(1); }));
  CHECK(trips([] { (void)static_cast<const set&>(one).nth(1); }));
  CHECK(trips([] { (void)one.emplace(2); }));

  // erase(pos) with a position that is not an element. Two-byte elements of
  // alignment one, so a pointer one byte into an element is still a valid
  // pointer value and only the stride check rejects it.
  using byte_map = metl::flat_map<char, char, 2>;
  static byte_map pairs;
  (void)pairs.try_emplace('a', 'x');
  (void)pairs.try_emplace('b', 'y');
  CHECK(trips([] { (void)pairs.erase(pairs.end()); }));
  CHECK(trips([] {
    (void)pairs.erase(
        reinterpret_cast<byte_map::const_iterator>(reinterpret_cast<const char*>(pairs.begin()) + 1));
  }));
  CHECK(pairs.size() == 2);

  using byte_set = metl::flat_set<std::array<char, 2>, 2>;
  static byte_set keys;
  (void)keys.try_emplace(std::array<char, 2>{'a', 'x'});
  (void)keys.try_emplace(std::array<char, 2>{'b', 'y'});
  CHECK(trips([] { (void)keys.erase(keys.end()); }));
  CHECK(trips([] {
    (void)keys.erase(
        reinterpret_cast<byte_set::const_iterator>(reinterpret_cast<const char*>(keys.begin()) + 1));
  }));
  CHECK(keys.size() == 2);
}

// ---- static_message_queue ---------------------------------------------------------------------

void message_queue_trips() {
  using queue = metl::static_message_queue<int, 1>;
  static queue empty;
  static queue full;
  (void)full.try_emplace(1);
  CHECK(trips([] { (void)empty.front(); }));
  CHECK(trips([] { (void)static_cast<const queue&>(empty).front(); }));
  CHECK(trips([] { (void)full.emplace(2); }));
}

// ---- static_unordered_map / static_unordered_set -------------------------------------------------

void unordered_capacity_trips() {
  using map = metl::static_unordered_map<int, int, 1>;
  static map full;
  (void)full.try_emplace(1, 10);
  CHECK(trips([] { (void)full.emplace(2, 20); }));
  CHECK(trips([] { (void)full.insert_or_assign(2, 20); }));
  static const int key = 3;
  CHECK(trips([] { (void)full[key]; }));
  CHECK(trips([] { (void)full[4]; }));

  static metl::static_unordered_set<int, 1> set;
  (void)set.try_emplace(1);
  CHECK(trips([] { (void)set.emplace(2); }));
}

}  // namespace

int main() {
  if (!metl_test::asserts_active) {
    return metl_test::skip("container_assert_trips_test", "METL_ASSERT is stripped at this hardening level");
  }
  fixed_vector_trips();
  fixed_string_trips();
  ring_trips();
  flat_trips();
  message_queue_trips();
  unordered_capacity_trips();
  return metl_test::exit_code();
}
