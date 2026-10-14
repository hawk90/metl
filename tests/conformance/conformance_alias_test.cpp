// Property: adding a copy of an element the type already holds (#390).
//
//   copying_own_element_keeps_it   `v.push_back(v[0])`, `v.insert(v.begin(), v[1])`,
//                                  `m.try_emplace(k, m[0].value)`, `o.emplace(*o)`:
//                                  the argument refers into the type itself, and an
//                                  operation that shifts, evicts, rebuilds or
//                                  destroys before it reads the argument copies a
//                                  moved-from or destroyed element instead.
//
// Each adapter lists the operations that take an element by reference
// (`copy_own`), or says why it has none (`copy_own_refused`). For every
// operation and every held element, the property fills the type, copies that
// element in, and checks that the type holds as many elements as it should,
// that every one of them is live, and that the copied element's id is on as
// many live probes as it should be. A copy made from a moved-from or destroyed
// probe is not armed, so it is one live element too few. A copy of whatever a
// shift or rebuild moved into the argument's place is live but carries the
// wrong id. A second destruction shows up in probe::destroyed_twice().

#include "conformance.hpp"

METL_CONFORMANCE_DECLARE_GAP(copying_own_element_keeps_it)

namespace {

using conformance::probe;

template <typename A, typename = void>
struct has_copy_own : std::false_type {};
template <typename A>
struct has_copy_own<A, std::void_t<decltype(A::copy_own_ops)>> : std::true_type {};

template <typename A, typename = void>
struct refuses_copy_own : std::false_type {};
template <typename A>
struct refuses_copy_own<A, std::void_t<decltype(A::copy_own_refused)>> : std::true_type {};

struct copying_own_element_keeps_it {
  template <typename Adapter>
  bool operator()(Adapter) const {
    auto& c = conformance::instance<Adapter>();
    bool met = true;
    for (int op = 0; op < Adapter::copy_own_ops; ++op) {
      for (int k = 0; k < Adapter::copy_own_fill; ++k) {
        Adapter::clear(c);
        probe::live() = 0;
        probe::live_ids() = {};
        const int twice_before = probe::destroyed_twice();
        bool filled = true;
        for (int id = 1; id <= Adapter::copy_own_fill; ++id) {
          filled = Adapter::try_add(c, id) && filled;
        }
        const conformance::copy_result want = Adapter::copy_own(c, op, k);
        const std::size_t held = Adapter::held(c);
        const int live = probe::live();
        const int copies = probe::live_ids()[static_cast<std::size_t>(want.source_id)];
        const int twice = probe::destroyed_twice() - twice_before;
        if (!filled || held != want.held || live != static_cast<int>(want.held) || copies != want.copies ||
            twice != 0) {
          std::fprintf(stderr,
                       "  %s: operation %d copying element %d (id %d): holds %zu (want %zu), %d live, "
                       "%d with its id (want %d), %d destroyed twice\n",
                       Adapter::name,
                       op,
                       k,
                       want.source_id,
                       held,
                       want.held,
                       live,
                       copies,
                       want.copies,
                       twice);
          met = false;
        }
      }
    }
    Adapter::clear(c);
    probe::live() = 0;
    probe::live_ids() = {};
    return met;
  }
};

template <typename Adapter>
void run_for() {
  static_assert(has_copy_own<Adapter>::value != refuses_copy_own<Adapter>::value,
                "an adapter lists copy_own operations or says why it has none (copy_own_refused)");
  if constexpr (refuses_copy_own<Adapter>::value) {
    std::printf(
        "%s: copying_own_element_keeps_it: not applicable: %s\n", Adapter::name, Adapter::copy_own_refused);
  } else {
    conformance::run_one<Adapter>("copying_own_element_keeps_it",
                                  METL_CONFORMANCE_GAP(copying_own_element_keeps_it, Adapter),
                                  copying_own_element_keeps_it{});
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
