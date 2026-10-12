// Property: removal commits before the element is destroyed.
//
// While ~T() runs, the container must no longer count the element. A destructor
// that looks itself up to unregister must not find itself (it would be
// destroyed twice), and one that asks the container its size must get the
// size after the removal. Fixed type by type for the containers (#190) and
// found again in the arena (#312), which is why it is a property here.

#include "conformance.hpp"

METL_CONFORMANCE_DECLARE_GAP(removal_commits_first)

namespace {

struct observation {
  int destroyed = 0;
  int still_counted = 0;  // destructions during which the container still held the element
};

observation& seen() {
  static observation o;
  return o;
}

template <typename Adapter>
void observe(const conformance::probe&) {
  ++seen().destroyed;
  // live() has already dropped by one, so it is what the container should hold.
  if (Adapter::held(conformance::instance<Adapter>()) >
      static_cast<std::size_t>(conformance::probe::live())) {
    ++seen().still_counted;
  }
}

struct removal_commits_first {
  template <typename Adapter>
  bool operator()(Adapter) const {
    auto& c = conformance::instance<Adapter>();
    Adapter::clear(c);
    bool met = true;
    for (int id = 1; id <= 3; ++id) {
      met = Adapter::try_add(c, id) && met;
    }

    seen() = observation{};
    conformance::probe::on_destroy() = &observe<Adapter>;
    Adapter::remove_one(c);
    const observation single = seen();
    const int live_after_one = conformance::probe::live();

    seen() = observation{};
    Adapter::clear(c);
    const observation all = seen();
    conformance::probe::on_destroy() = nullptr;

    // One value ends. A heap ends it by assigning over it, so its destructor
    // need not run; whichever destructors do run must see it already gone.
    met = met && live_after_one == 2 && single.destroyed <= 1 && single.still_counted == 0;
    met = met && all.still_counted == 0;
    met = met && Adapter::held(c) == 0 && conformance::probe::live() == 0;
    if (!met) {
      std::fprintf(
          stderr,
          "  %s: remove_one left %d live, destroyed %d (%d still counted); clear destroyed %d (%d still "
          "counted); held %zu, live %d\n",
          Adapter::name,
          live_after_one,
          single.destroyed,
          single.still_counted,
          all.destroyed,
          all.still_counted,
          Adapter::held(c),
          conformance::probe::live());
    }
    // Leave no armed probe behind for the next type.
    conformance::probe::live() = 0;
    return met;
  }
};

template <typename... Adapters>
void run_all(std::tuple<Adapters...>*) {
  (conformance::run_one<Adapters>("removal_commits_first",
                                  METL_CONFORMANCE_GAP(removal_commits_first, Adapters),
                                  removal_commits_first{}),
   ...);
}

}  // namespace

int main() {
  run_all(static_cast<conformance::registry*>(nullptr));
  return metl_test::exit_code();
}
