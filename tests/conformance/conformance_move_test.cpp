// Property: a moved-from container is empty and can be filled again.
//
// Every movable container documents that moving leaves the source empty. This
// checks the half that holds today, with stateless functors. Whether 2.0 keeps
// "empty" or moves to std's "valid but unspecified", and what happens to a
// stateful comparator or hasher, is #317.

#include "conformance.hpp"

METL_CONFORMANCE_DECLARE_GAP(moved_from_is_empty_and_reusable)

namespace {

struct moved_from_is_empty_and_reusable {
  template <typename Adapter>
  bool operator()(Adapter) const {
    using type = typename Adapter::type;
    bool met = true;
    {
      type source;
      for (int id = 1; id <= 3; ++id) {
        met = Adapter::try_add(source, id) && met;
      }
      type constructed(std::move(source));
      met = met && Adapter::held(constructed) == 3 && Adapter::held(source) == 0 &&
            conformance::probe::live() == 3;

      type assigned;
      met = Adapter::try_add(assigned, 7) && met;
      assigned = std::move(constructed);
      met = met && Adapter::held(assigned) == 3 && Adapter::held(constructed) == 0 &&
            conformance::probe::live() == 3;

      // Both sources are usable to full capacity.
      for (std::size_t i = 0; i < Adapter::capacity; ++i) {
        met = Adapter::try_add(source, 10 + static_cast<int>(i)) && met;
        met = Adapter::try_add(constructed, 20 + static_cast<int>(i)) && met;
      }
      met = met && Adapter::held(source) == Adapter::capacity &&
            Adapter::held(constructed) == Adapter::capacity;
      if (!met) {
        std::fprintf(stderr,
                     "  %s: held source %zu, constructed %zu, assigned %zu; live %d\n",
                     Adapter::name,
                     Adapter::held(source),
                     Adapter::held(constructed),
                     Adapter::held(assigned),
                     conformance::probe::live());
      }
    }
    met = met && conformance::probe::live() == 0;
    conformance::probe::live() = 0;
    return met;
  }
};

template <typename Adapter>
void run_for() {
  if constexpr (Adapter::movable) {
    conformance::run_one<Adapter>("moved_from_is_empty_and_reusable",
                                  METL_CONFORMANCE_GAP(moved_from_is_empty_and_reusable, Adapter),
                                  moved_from_is_empty_and_reusable{});
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
