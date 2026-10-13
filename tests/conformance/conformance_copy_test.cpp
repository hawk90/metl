// Property: a copy holds the same elements as its source, independently.
//
// Copy construction and copy assignment over a container that already holds
// elements must end up with exactly the source's elements, built anew, and
// leave the source as it was: removing from one does not touch the other.
// Assignment that forgets to clear the target, or shares storage with the
// source, fails the counts here.

#include "conformance.hpp"

METL_CONFORMANCE_DECLARE_GAP(copy_is_equal_and_independent)

namespace {

struct copy_is_equal_and_independent {
  template <typename Adapter>
  bool operator()(Adapter) const {
    using type = typename Adapter::type;
    bool met = true;
    std::size_t constructed_held = 0;
    std::size_t assigned_held = 0;
    int live_after_copies = 0;
    {
      type source;
      for (int id = 1; id <= 3; ++id) {
        met = Adapter::try_add(source, id) && met;
      }
      const type constructed(source);
      type assigned;
      met = Adapter::try_add(assigned, 7) && met;
      assigned = source;
      constructed_held = Adapter::held(constructed);
      assigned_held = Adapter::held(assigned);
      live_after_copies = conformance::probe::live();
      met = met && constructed_held == 3 && assigned_held == 3 && live_after_copies == 9;

      // Emptying the source leaves both copies whole.
      Adapter::clear(source);
      met = met && Adapter::held(constructed) == 3 && Adapter::held(assigned) == 3 &&
            conformance::probe::live() == 6;
    }
    met = met && conformance::probe::live() == 0 && conformance::probe::destroyed_twice() == 0;
    if (!met) {
      std::fprintf(stderr,
                   "  %s: held by copy-constructed %zu, copy-assigned %zu; live after copies %d (want 9), "
                   "at end %d, destroyed twice %d\n",
                   Adapter::name,
                   constructed_held,
                   assigned_held,
                   live_after_copies,
                   conformance::probe::live(),
                   conformance::probe::destroyed_twice());
    }
    conformance::probe::live() = 0;
    conformance::probe::destroyed_twice() = 0;
    return met;
  }
};

template <typename Adapter>
void run_for() {
  if constexpr (Adapter::movable) {
    conformance::run_one<Adapter>("copy_is_equal_and_independent",
                                  METL_CONFORMANCE_GAP(copy_is_equal_and_independent, Adapter),
                                  copy_is_equal_and_independent{});
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
