// Properties at the capacity edge, for every type with a fixed element count.
//
//   full_try_add_changes_nothing  a try_ that fails because the type is full
//                                 builds nothing, destroys nothing and leaves the
//                                 count as it was (SCOPE.md R1/R2)
//   reusable_after_clear          after clear, the full capacity is available
//                                 again (#270 was a capacity edge)

#include "conformance.hpp"

METL_CONFORMANCE_DECLARE_GAP(full_try_add_changes_nothing)
METL_CONFORMANCE_DECLARE_GAP(reusable_after_clear)

namespace {

template <typename Adapter>
bool fill(typename Adapter::type& c) {
  bool all = true;
  for (std::size_t i = 0; i < Adapter::capacity; ++i) {
    all = Adapter::try_add(c, static_cast<int>(i) + 1) && all;
  }
  return all;
}

struct full_try_add_changes_nothing {
  template <typename Adapter>
  bool operator()(Adapter) const {
    auto& c = conformance::instance<Adapter>();
    Adapter::clear(c);
    const bool filled = fill<Adapter>(c);
    const int live_before = conformance::probe::live();
    const bool added = Adapter::try_add(c, 99);
    const bool met = filled && !added && Adapter::held(c) == Adapter::capacity &&
                     conformance::probe::live() == live_before;
    if (!met) {
      std::fprintf(stderr,
                   "  %s: filled %d, try_add when full returned %d, held %zu of %zu, live %d -> %d\n",
                   Adapter::name,
                   filled,
                   added,
                   Adapter::held(c),
                   Adapter::capacity,
                   live_before,
                   conformance::probe::live());
    }
    Adapter::clear(c);
    conformance::probe::live() = 0;
    return met;
  }
};

struct reusable_after_clear {
  template <typename Adapter>
  bool operator()(Adapter) const {
    auto& c = conformance::instance<Adapter>();
    Adapter::clear(c);
    const bool first = fill<Adapter>(c);
    Adapter::clear(c);
    const bool emptied = Adapter::held(c) == 0 && conformance::probe::live() == 0;
    const bool second = fill<Adapter>(c);
    const bool met = first && emptied && second && Adapter::held(c) == Adapter::capacity;
    if (!met) {
      std::fprintf(stderr,
                   "  %s: first fill %d, emptied %d, second fill %d, held %zu\n",
                   Adapter::name,
                   first,
                   emptied,
                   second,
                   Adapter::held(c));
    }
    Adapter::clear(c);
    conformance::probe::live() = 0;
    return met;
  }
};

template <typename Adapter>
void run_for() {
  if constexpr (Adapter::fixed_count) {
    conformance::run_one<Adapter>("full_try_add_changes_nothing",
                                  METL_CONFORMANCE_GAP(full_try_add_changes_nothing, Adapter),
                                  full_try_add_changes_nothing{});
    conformance::run_one<Adapter>(
        "reusable_after_clear", METL_CONFORMANCE_GAP(reusable_after_clear, Adapter), reusable_after_clear{});
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
