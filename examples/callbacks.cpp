// callbacks.cpp
//
// Four ways to hold a callable without `std::function`, and how to tell them
// apart. All four are allocation-free; the difference is OWNERSHIP and SIZE.
//
//   function_ref<Sig>          non-owning, 2 words. For a PARAMETER.
//   delegate<Sig>              non-owning, 2 words. Binds a member function with
//                              zero indirection: the method is a template
//                              parameter, so there is no stored pointer-to-member.
//   fixed_function<Sig, N>     OWNING, N bytes inline. For a member that must
//                              outlive the caller's expression.
//   event_dispatcher<Sig, N>   a fixed list of delegates, dispatched together.
//
// Plus scope_exit, which is not a callback holder but belongs in the same
// conversation: it runs a cleanup when a scope ends, including an early return.
// Its callable must be `noexcept` -- it runs during unwinding-free teardown in a
// library that has no exceptions, so a throwing cleanup has nowhere to go. That
// is a static_assert, not a runtime surprise.
//
// The rule of thumb the example demonstrates: take `function_ref` as a parameter,
// store a `delegate` when the target is a long-lived object you own, and reach for
// `fixed_function` only when you must own the callable itself (a lambda with
// captures that has to outlive the call site).
//
// Self-checking: every value is asserted, and the program returns non-zero on any
// mismatch.

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <metl/delegate.hpp>
#include <metl/event_dispatcher.hpp>
#include <metl/fixed_function.hpp>
#include <metl/function_ref.hpp>
#include <metl/scope_exit.hpp>
#include <metl/span.hpp>

namespace {

int g_log = 0;

// ---------------------------------------------------------------------------
// function_ref: the right type for a PARAMETER
// ---------------------------------------------------------------------------

/// Takes any callable without a template, without allocating, and without
/// copying it. `function_ref` does not own the callable, which is fine here --
/// it cannot outlive the call.
/// @note It binds LVALUES only. `for_each_sample(v, [](int){...})` is a compile
///       error on purpose, because the temporary would dangle. Name it first.
int sum_via(metl::span<const int> samples, metl::function_ref<int(int)> transform) {
  int total = 0;
  for (int sample : samples) {
    total += transform(sample);
  }
  return total;
}

int doubled(int value) noexcept {
  return value * 2;
}

// ---------------------------------------------------------------------------
// delegate: a member function, bound with no indirection
// ---------------------------------------------------------------------------

class thermostat {
 public:
  void on_temperature(int celsius) noexcept {
    last_seen_ = celsius;
    if (celsius > threshold_) {
      ++trips_;
    }
  }

  int last_seen() const noexcept { return last_seen_; }
  int trips() const noexcept { return trips_; }

 private:
  int threshold_ = 30;
  int last_seen_ = 0;
  int trips_ = 0;
};

void audit(int celsius) noexcept {
  g_log += celsius;
}

}  // namespace

int main() {
  // -------------------------------------------------------------------------
  // function_ref
  // -------------------------------------------------------------------------
  {
    const int samples[] = {1, 2, 3, 4};
    const metl::span<const int> view(samples, 4);

    // A free function.
    if (sum_via(view, &doubled) != 20) {
      return 1;
    }

    // A named lambda. Naming it is what keeps it alive across the call -- an
    // unnamed temporary is rejected at compile time rather than dangling.
    auto plus_ten = [](int value) { return value + 10; };
    if (sum_via(view, plus_ten) != 50) {
      return 2;
    }

    // A lambda with captures works too: function_ref stores a pointer to it.
    int offset = 100;
    auto shifted = [&offset](int value) { return value + offset; };
    if (sum_via(view, shifted) != 410) {
      return 3;
    }
  }

  // -------------------------------------------------------------------------
  // delegate + event_dispatcher
  // -------------------------------------------------------------------------
  {
    thermostat unit;
    using temperature_event = metl::event_dispatcher<void(int), 4>;
    temperature_event bus;

    // bind<&T::method>(instance): the method is a template parameter, so the
    // delegate is just {object pointer, static thunk}. The instance must outlive
    // the subscription -- non-owning, like everything else here.
    const auto unit_id =
        bus.subscribe(temperature_event::delegate_type::bind<thermostat, &thermostat::on_temperature>(unit));
    const auto audit_id = bus.subscribe(temperature_event::delegate_type::from_function<&audit>());
    if (!unit_id.has_value() || !audit_id.has_value()) {
      return 4;
    }
    if (bus.size() != 2) {
      return 5;
    }

    g_log = 0;
    bus.dispatch(25);
    bus.dispatch(35);  // above the 30 threshold
    if (unit.last_seen() != 35 || unit.trips() != 1) {
      return 6;
    }
    if (g_log != 60) {
      return 7;
    }

    // unsubscribe answers "was it subscribed" -- a question, not a failure.
    if (!bus.unsubscribe(*audit_id)) {
      return 8;
    }
    if (bus.unsubscribe(*audit_id)) {
      return 9;  // already gone
    }

    g_log = 0;
    bus.dispatch(40);
    if (g_log != 0 || unit.trips() != 2) {
      return 10;  // only the remaining listener ran
    }

    // Capacity is fixed: the fifth subscription is refused, not grown.
    thermostat spares[4];
    std::size_t accepted = 0;
    for (thermostat& spare : spares) {
      if (bus.subscribe(
                 temperature_event::delegate_type::bind<thermostat, &thermostat::on_temperature>(spare))
              .has_value()) {
        ++accepted;
      }
    }
    if (accepted != 3 || bus.size() != 4) {
      return 11;  // one slot was already taken by `unit`
    }
  }

  // -------------------------------------------------------------------------
  // fixed_function: the OWNING one
  // -------------------------------------------------------------------------
  {
    // Owns the callable in N bytes of inline storage, so it can be a member and
    // outlive the expression that created it. The size is yours to pick; a
    // capture that does not fit is a compile error rather than a heap allocation.
    int calls = 0;
    metl::fixed_function<int(int), 32> op = [&calls](int value) {
      ++calls;
      return value * 3;
    };
    if (!op || op(7) != 21 || calls != 1) {
      return 12;
    }

    // Reassignable, and empty-checkable.
    op = [](int value) { return value - 1; };
    if (op(7) != 6) {
      return 13;
    }
    op.reset();
    if (op) {
      return 14;
    }
  }

  // -------------------------------------------------------------------------
  // scope_exit: cleanup that survives an early return
  // -------------------------------------------------------------------------
  {
    int released = 0;
    {
      metl::scope_exit guard([&released]() noexcept { ++released; });
      if (released != 0) {
        return 15;  // has not run yet
      }
    }
    if (released != 1) {
      return 16;  // ran at scope end
    }

    // release() cancels it -- the "commit" idiom: arm a rollback, then disarm it
    // once the operation has succeeded.
    int rolled_back = 0;
    {
      metl::scope_exit rollback([&rolled_back]() noexcept { ++rolled_back; });
      rollback.release();
    }
    if (rolled_back != 0) {
      return 17;
    }
  }

  std::printf("callbacks: function_ref (parameter), delegate (bound member),\n");
  std::printf("           fixed_function (owning), event_dispatcher (fan-out),\n");
  std::printf("           scope_exit (cleanup) — all allocation-free.\n");
  return 0;
}
