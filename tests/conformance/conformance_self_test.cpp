// Property: assigning or swapping a container with itself changes nothing.
//
// Every movable container is also copyable, and the headers that say so call
// self-assignment safe. One that clears before it copies loses the elements,
// and one that moves elements onto themselves can destroy one twice. The
// property fills three elements, assigns and swaps the container with itself,
// and then removes the elements one by one: each removal must end exactly one
// live element. (Counted by liveness, not by destructor calls: a heap's pop
// ends the top element by assigning over it.)

#include "conformance.hpp"

METL_CONFORMANCE_DECLARE_GAP(self_assignment_changes_nothing)

namespace {

struct self_assignment_changes_nothing {
  template <typename Adapter>
  bool operator()(Adapter) const {
    using type = typename Adapter::type;
    bool met = true;
    std::size_t after_copy = 0;
    std::size_t after_move = 0;
    std::size_t after_swap = 0;
    int removed_in_step = -1;  // the first removal that did not end exactly one element
    {
      type c;
      for (int id = 1; id <= 3; ++id) {
        met = Adapter::try_add(c, id) && met;
      }
      type& alias = c;
      c = alias;
      after_copy = Adapter::held(c);
      c = std::move(alias);
      after_move = Adapter::held(c);
      using std::swap;
      swap(c, alias);
      after_swap = Adapter::held(c);
      met = met && after_copy == 3 && after_move == 3 && after_swap == 3 && conformance::probe::live() == 3;

      for (int left = 2; left >= 0; --left) {
        Adapter::remove_one(c);
        const bool one_ended =
            Adapter::held(c) == static_cast<std::size_t>(left) && conformance::probe::live() == left;
        removed_in_step = one_ended ? removed_in_step : left;
        met = met && one_ended;
      }
    }
    met = met && conformance::probe::live() == 0 && conformance::probe::destroyed_twice() == 0;
    if (!met) {
      std::fprintf(stderr,
                   "  %s: held after self copy %zu, move %zu, swap %zu; removal leaving %d went wrong; "
                   "live %d, destroyed twice %d\n",
                   Adapter::name,
                   after_copy,
                   after_move,
                   after_swap,
                   removed_in_step,
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
    conformance::run_one<Adapter>("self_assignment_changes_nothing",
                                  METL_CONFORMANCE_GAP(self_assignment_changes_nothing, Adapter),
                                  self_assignment_changes_nothing{});
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
