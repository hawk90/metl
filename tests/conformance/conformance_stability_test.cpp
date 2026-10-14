// Properties of each type's stated invalidation rule (#340).
//
//   adding_moves_no_other     where the header says adding leaves the elements
//                             already held in place, try_add moves, assigns
//                             over and destroys none of them
//   removing_moves_no_other   where the header says removing ends only the
//                             removed element, remove_one touches one element
//
// Each adapter states add_moves_others and remove_moves_others to match its
// header's "Iterator invalidation" or "Reference invalidation" paragraph; a
// type whose rule allows the move (a heap's sift, a sorted run's shift, an
// unordered rebuild) is reported as such and not checked.
//
// A move is seen through the probe: while moves_watched() is set it records
// the id of every armed probe moved, assigned over or destroyed. Positions are
// not compared, so the check needs no iterator and works for the queues too.

#include "conformance.hpp"

METL_CONFORMANCE_DECLARE_GAP(adding_moves_no_other)
METL_CONFORMANCE_DECLARE_GAP(removing_moves_no_other)

namespace {

using conformance::probe;

// Ids 1..n held; returns how many of them the watched step touched.
template <typename Step>
int touched_by(int n, Step step) {
  probe::moved() = {};
  probe::moves_watched() = true;
  step();
  probe::moves_watched() = false;
  int touched = 0;
  for (int id = 1; id <= n; ++id) {
    touched += probe::moved()[static_cast<std::size_t>(id)] ? 1 : 0;
  }
  return touched;
}

struct adding_moves_no_other {
  template <typename Adapter>
  bool operator()(Adapter) const {
    auto& c = conformance::instance<Adapter>();
    Adapter::clear(c);
    // One short of full, so the add has room.
    const int n = conformance::filled<Adapter> - 1;
    bool met = true;
    for (int id = 1; id <= n; ++id) {
      met = Adapter::try_add(c, id) && met;
    }
    bool added = false;
    const int touched = touched_by(n, [&] { added = Adapter::try_add(c, 99); });
    met = met && added && touched == 0;
    if (!met) {
      std::fprintf(
          stderr, "  %s: added %d; moved %d of the %d elements held\n", Adapter::name, added, touched, n);
    }
    Adapter::clear(c);
    probe::live() = 0;
    return met;
  }
};

struct removing_moves_no_other {
  template <typename Adapter>
  bool operator()(Adapter) const {
    auto& c = conformance::instance<Adapter>();
    Adapter::clear(c);
    const int n = conformance::filled<Adapter>;
    bool met = true;
    for (int id = 1; id <= n; ++id) {
      met = Adapter::try_add(c, id) && met;
    }
    const int touched = touched_by(n, [&] { Adapter::remove_one(c); });
    met = met && touched == 1 && Adapter::held(c) == static_cast<std::size_t>(n - 1);
    if (!met) {
      std::fprintf(
          stderr, "  %s: remove_one touched %d of %d elements (want 1)\n", Adapter::name, touched, n);
    }
    Adapter::clear(c);
    probe::live() = 0;
    return met;
  }
};

template <typename Adapter>
void run_for() {
  if constexpr (Adapter::add_moves_others) {
    std::printf("%s: adding_moves_no_other: may move, as its header states\n", Adapter::name);
  } else {
    conformance::run_one<Adapter>("adding_moves_no_other",
                                  METL_CONFORMANCE_GAP(adding_moves_no_other, Adapter),
                                  adding_moves_no_other{});
  }
  if constexpr (Adapter::remove_moves_others) {
    std::printf("%s: removing_moves_no_other: may move, as its header states\n", Adapter::name);
  } else {
    conformance::run_one<Adapter>("removing_moves_no_other",
                                  METL_CONFORMANCE_GAP(removing_moves_no_other, Adapter),
                                  removing_moves_no_other{});
  }
}

template <typename... Adapters>
void run_all(std::tuple<Adapters...>*) {
  (run_for<Adapters>(), ...);
}

}  // namespace

int main() {
  run_all(static_cast<conformance::registry*>(nullptr));
  return metl_test::exit_code();
}
