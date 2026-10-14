// Properties when an element operation throws (#340). Exceptions builds only:
// METL itself never throws, but a user's element may.
//
//   throwing_add_changes_nothing  a try_ add whose element construction throws
//                                 leaves the count and the live objects as
//                                 they were
//   throwing_copy_leaks_nothing   a copy construction, or a copy assignment,
//                                 that throws part-way destroys every element
//                                 it built and none twice, and leaves the
//                                 source as it was
//
// The throw comes from the probe: armed with probe::throw_after() = n, the
// n-th construction or copy of an armed probe throws before it counts as live.
//
// The sequences, queues, pools, the arena and the vocabulary types build the
// added element inside the type, so throwing_add_changes_nothing checks their
// own rollback. flat_map, flat_set, the unordered containers and
// fixed_function take an element built by the caller, so for them the throw
// happens before the type is called and the property only confirms that
// nothing was added. Copying copies inside every type, so
// throwing_copy_leaks_nothing checks them all.

#include "conformance.hpp"

METL_CONFORMANCE_DECLARE_GAP(throwing_add_changes_nothing)
METL_CONFORMANCE_DECLARE_GAP(throwing_copy_leaks_nothing)

#if !METL_NO_EXCEPTIONS

namespace {

using conformance::probe;

struct throwing_add_changes_nothing {
  template <typename Adapter>
  bool operator()(Adapter) const {
    auto& c = conformance::instance<Adapter>();
    Adapter::clear(c);
    bool met = true;
    // One short of full, so the add would otherwise succeed.
    for (int id = 1; id < conformance::filled<Adapter>; ++id) {
      met = Adapter::try_add(c, id) && met;
    }
    const std::size_t held_before = Adapter::held(c);
    const int live_before = probe::live();
    bool threw = false;
    probe::throw_after() = 1;
    try {
      (void)Adapter::try_add(c, 99);
    } catch (const probe::thrown&) {
      threw = true;
    }
    probe::throw_after() = 0;
    met = met && threw && Adapter::held(c) == held_before && probe::live() == live_before &&
          probe::destroyed_twice() == 0;
    if (!met) {
      std::fprintf(stderr,
                   "  %s: threw %d; held %zu -> %zu, live %d -> %d, destroyed twice %d\n",
                   Adapter::name,
                   threw,
                   held_before,
                   Adapter::held(c),
                   live_before,
                   probe::live(),
                   probe::destroyed_twice());
    }
    // Still usable: it fills up again.
    Adapter::clear(c);
    met = met && Adapter::try_add(c, 1) && probe::live() == 1;
    Adapter::clear(c);
    probe::live() = 0;
    probe::destroyed_twice() = 0;
    return met;
  }
};

struct throwing_copy_leaks_nothing {
  template <typename Adapter>
  bool operator()(Adapter) const {
    using type = typename Adapter::type;
    const int n = conformance::filled<Adapter>;
    bool met = true;
    bool construction_threw = false;
    bool assignment_threw = false;
    int live_after_construction = 0;
    int live_after_assignment = 0;
    std::size_t target_held = 0;
    {
      type source;
      for (int id = 1; id <= n; ++id) {
        met = Adapter::try_add(source, id) && met;
      }

      // The last element's copy throws, after the others were built.
      probe::throw_after() = n;
      try {
        const type copy(source);
        (void)copy;
      } catch (const probe::thrown&) {
        construction_threw = true;
      }
      probe::throw_after() = 0;
      live_after_construction = probe::live();

      type target;
      met = Adapter::try_add(target, 7) && met;
      probe::throw_after() = n;
      try {
        target = source;
      } catch (const probe::thrown&) {
        assignment_threw = true;
      }
      probe::throw_after() = 0;
      target_held = Adapter::held(target);
      live_after_assignment = probe::live();

      // The source is whole; the target holds what it counts, each once.
      met = met && construction_threw && assignment_threw && live_after_construction == n &&
            Adapter::held(source) == static_cast<std::size_t>(n) &&
            live_after_assignment == n + static_cast<int>(target_held) && probe::destroyed_twice() == 0;
    }
    met = met && probe::live() == 0 && probe::destroyed_twice() == 0;
    if (!met) {
      std::fprintf(stderr,
                   "  %s: construction threw %d (live %d, want %d); assignment threw %d (live %d, target "
                   "holds %zu); at end live %d, destroyed twice %d\n",
                   Adapter::name,
                   construction_threw,
                   live_after_construction,
                   n,
                   assignment_threw,
                   live_after_assignment,
                   target_held,
                   probe::live(),
                   probe::destroyed_twice());
    }
    probe::live() = 0;
    probe::destroyed_twice() = 0;
    return met;
  }
};

template <typename Adapter>
void run_for() {
  conformance::run_one<Adapter>("throwing_add_changes_nothing",
                                METL_CONFORMANCE_GAP(throwing_add_changes_nothing, Adapter),
                                throwing_add_changes_nothing{});
  if constexpr (Adapter::movable) {
    conformance::run_one<Adapter>("throwing_copy_leaks_nothing",
                                  METL_CONFORMANCE_GAP(throwing_copy_leaks_nothing, Adapter),
                                  throwing_copy_leaks_nothing{});
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

#else

int main() {
  return metl_test::skip("conformance_throwing_test", "built without exceptions");
}

#endif  // !METL_NO_EXCEPTIONS
