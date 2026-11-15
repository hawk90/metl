// Zero-length tables take no room for an element on any standard library (#696).
//
// libc++'s std::array<T, 0> reserves room for one T, so fsm's default "no
// hooks" tables and an empty lookup_table were as big as one entry with libc++
// and 1 B with libstdc++. Each size is pinned against a struct with the
// layout the type should have, so the same test holds on 32- and 64-bit hosts
// and on both libraries. The macOS host-test job builds it against libc++.

#include "assert_trip.hpp"
#include "metl_check.hpp"

#include <array>
#include <cstdint>

#include <metl/fsm.hpp>
#include <metl/lookup_table.hpp>

namespace {

enum class st : std::uint8_t { a, b };
enum class ev : std::uint8_t { go };

using transition = metl::fsm_transition<st, ev>;
using hook = metl::fsm_state_hook<st>;
using entry = metl::lookup_entry<std::uint32_t, std::uint32_t>;

struct empty {};

// fsm's members, in order: the state, two flags, then the three tables.
template <typename Transitions, typename Entry, typename Exit>
struct fsm_layout {
  st state;
  bool entered;
  bool exiting;
  Transitions transitions;
  Entry entry_hooks;
  Exit exit_hooks;
};

static_assert(sizeof(metl::fsm<st, ev, 1>) == sizeof(fsm_layout<transition[1], empty, empty>),
              "an fsm without hooks holds no hook");
static_assert(sizeof(metl::fsm<st, ev, 0>) == sizeof(fsm_layout<empty, empty, empty>),
              "an fsm without transitions or hooks holds none");
static_assert(sizeof(metl::fsm<st, ev, 0>) == 6, "the state, two flags and three 1-byte empty tables");
static_assert(sizeof(metl::fsm<st, ev, 1, 1, 0>) == sizeof(fsm_layout<transition[1], hook[1], empty>),
              "only the exit-hook table is empty");
static_assert(sizeof(metl::fsm<st, ev, 1, 0, 1>) == sizeof(fsm_layout<transition[1], empty, hook[1]>),
              "only the entry-hook table is empty");
static_assert(sizeof(metl::fsm<st, ev, 2, 1, 1>) == sizeof(fsm_layout<transition[2], hook[1], hook[1]>),
              "non-empty tables are unchanged");

static_assert(sizeof(metl::lookup_table<std::uint32_t, std::uint32_t, 0>) == 1,
              "an empty table holds no entry");
static_assert(sizeof(metl::lookup_table<std::uint32_t, std::uint32_t, 2>) == sizeof(entry[2]),
              "a non-empty table is its entries");

int on_go = 0;
void count_go(st /*from*/, ev /*event*/, st /*to*/) {
  ++on_go;
}

// Kept outside the tripping callable: a trip longjmps out without running
// destructors.
const metl::lookup_table<std::uint32_t, std::uint32_t, 0> empty_table{};
const void* volatile named = nullptr;

}  // namespace

int main() {
  // No transitions: nothing dispatches, and the state stays.
  {
    metl::fsm<st, ev, 0> machine(st::a, std::array<transition, 0>{});
    CHECK(!machine.can_dispatch(ev::go));
    CHECK(!machine.dispatch(ev::go));
    CHECK(machine.current_state() == st::a);
  }

  // One transition and no hooks, through both constructors.
  {
    const std::array<transition, 1> table{
        {{st::a, ev::go, st::b, metl::delegate<void(st, ev, st)>::from_function<&count_go>()}}};
    metl::fsm<st, ev, 1> plain(st::a, table);
    CHECK(plain.dispatch(ev::go));
    CHECK(plain.current_state() == st::b);
    CHECK(!plain.dispatch(ev::go));

    metl::fsm<st, ev, 1> hooked(st::a, table, std::array<hook, 0>{}, std::array<hook, 0>{});
    CHECK(hooked.dispatch(ev::go));
    CHECK(hooked.current_state() == st::b);
    CHECK_EQ(on_go, 2);
  }

  // An empty lookup_table, at compile time and at run time.
  {
    constexpr metl::lookup_table<std::uint32_t, std::uint32_t, 0> folded{};
    static_assert(folded.find(1U) == nullptr, "an empty table finds nothing");
    static_assert(folded.value_or(1U, 7U) == 7U, "an empty table falls back");

    volatile std::uint32_t seed = 3;
    const std::uint32_t key = seed;
    const auto table = metl::make_lookup_table(std::array<entry, 0>{});
    CHECK(table.empty());
    CHECK(table.find(key) == nullptr);
    CHECK(!table.contains(key));
    CHECK_EQ(table.value_or(key, 9U), 9U);
  }

  // nth() on an empty table names no entry, so its assert fires. The store
  // after the call is caller code that follows it: an inlined __assume(0) made
  // MSVC Release report such code unreachable (C4702) in #707, and this is
  // the instantiation that would show it here.
  if (metl_test::asserts_active) {
    CHECK(metl_test::trips([] { named = &empty_table.nth(0); }));
  }

  return metl_test::exit_code();
}
