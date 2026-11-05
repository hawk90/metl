#include <new>
#include <type_traits>
#include <utility>

#include <metl/coro/stepper.hpp>
#include <metl/object_pool.hpp>

namespace {

// Three-state stepper: read -> process -> done.
class three_state : public metl::coro::stepper {
 public:
  enum class state : unsigned char { read, process, finished };
  state s = state::read;
  int read_count = 0;
  int process_count = 0;
  int steps = 0;

  metl::coro::step_result step() noexcept override {
    ++steps;
    switch (s) {
      case state::read:
        ++read_count;
        s = state::process;
        return metl::coro::step_result::yield;
      case state::process:
        ++process_count;
        s = state::finished;
        return metl::coro::step_result::yield;
      case state::finished:
        return metl::coro::step_result::done;
    }
    return metl::coro::step_result::error;
  }

 protected:
  void on_reset() noexcept override {
    s = state::read;
    read_count = 0;
    process_count = 0;
    steps = 0;
  }
};

// Stepper that fails after one yield.
class failing_stepper : public metl::coro::stepper {
 public:
  int steps = 0;

  metl::coro::step_result step() noexcept override {
    ++steps;
    if (steps == 1) {
      return metl::coro::step_result::yield;
    }
    return metl::coro::step_result::error;
  }
};

// A stepper is never deleted through a pointer and never made with a plain
// `new` (#548): its class-scope operator delete is protected, which keeps the
// global one, and on newlib malloc, out of the image. Access checking is part
// of substitution, so these detect it without a compile-fail case. GCC makes an
// inaccessible operator delete a hard error even inside decltype, so the check
// runs under clang; GCC still rejects both forms, only not detectably here.
#if defined(__clang__)
template <typename P, typename = void>
struct can_delete : std::false_type {};
template <typename P>
struct can_delete<P, std::void_t<decltype(delete std::declval<P>())>> : std::true_type {};
template <typename T, typename = void>
struct can_plain_new : std::false_type {};
template <typename T>
struct can_plain_new<T, std::void_t<decltype(new T)>> : std::true_type {};
template <typename T, typename = void>
struct can_global_placement_new : std::false_type {};
template <typename T>
struct can_global_placement_new<T, std::void_t<decltype(::new (std::declval<void*>()) T)>> : std::true_type {
};

struct plain {};
static_assert(can_delete<plain*>::value && can_plain_new<plain>::value, "the detectors see a deletable type");
// Checked at run time, not by static_assert, so that a mutant which makes them
// compile fails the test instead of breaking the build.
constexpr bool heap_forms_rejected = !can_delete<metl::coro::stepper*>::value &&
                                     !can_delete<three_state*>::value && !can_plain_new<three_state>::value;
constexpr bool placement_allowed = can_global_placement_new<three_state>::value;
#else
constexpr bool heap_forms_rejected = true;
constexpr bool placement_allowed = true;
#endif

}  // namespace

int main() {
  // ---- three_state: full happy path --------------------------------------
  {
    three_state t;
    if (t.is_done()) {
      return 1;
    }
    if (t.is_error()) {
      return 2;
    }

    // First poll: read -> yield. Still running.
    if (!t.poll()) {
      return 3;
    }
    if (t.read_count != 1) {
      return 4;
    }
    if (t.process_count != 0) {
      return 5;
    }

    // Second poll: process -> yield. Still running.
    if (!t.poll()) {
      return 6;
    }
    if (t.process_count != 1) {
      return 7;
    }

    // Third poll: finished -> done. Stops running.
    if (t.poll()) {
      return 8;
    }
    if (!t.is_done()) {
      return 9;
    }
    if (t.is_error()) {
      return 10;
    }

    // Further polls are no-ops: they do not even reach step().
    if (t.poll()) {
      return 11;
    }
    if (!t.is_done()) {
      return 12;
    }
    if (t.steps != 3) {
      return 20;
    }

    // reset() returns to the initial state.
    t.reset();
    if (t.is_done()) {
      return 13;
    }
    if (t.is_error()) {
      return 14;
    }
    if (t.read_count != 0 || t.process_count != 0) {
      return 15;
    }

    // Drive through once more after reset.
    if (!t.poll()) {
      return 16;
    }
    if (!t.poll()) {
      return 17;
    }
    if (t.poll()) {
      return 18;
    }
    if (!t.is_done()) {
      return 19;
    }
  }

  // ---- failing_stepper: error path ---------------------------------------
  {
    failing_stepper f;
    if (!f.poll()) {  // first call yields
      return 100;
    }
    if (f.is_done() || f.is_error()) {
      return 101;
    }

    if (f.poll()) {  // second call errors -> stops running
      return 102;
    }
    if (!f.is_done()) {
      return 103;
    }
    if (!f.is_error()) {
      return 104;
    }

    // Once errored, further polls remain idle.
    if (f.poll()) {
      return 105;
    }
    if (f.steps != 2) {
      return 106;
    }

    // reset() clears both flags.
    f.reset();
    if (f.is_done() || f.is_error()) {
      return 107;
    }
  }

  // ---- storage a stepper may live in (#548) --------------------------------
  if (!heap_forms_rejected) {
    return 203;  // delete or plain new of a stepper compiles (#548)
  }
  if (!placement_allowed) {
    return 204;
  }
  // Its protected operator delete rejects `delete` and plain `new`; static,
  // automatic, pool and `::new` placement storage all still work.
  {
    static three_state in_static;
    if (!in_static.poll()) {
      return 200;
    }
    metl::object_pool<three_state, 1> pool;
    three_state* pooled = pool.try_emplace();
    if (pooled == nullptr || !pooled->poll()) {
      return 201;
    }
    pool.destroy(pooled);
    alignas(three_state) unsigned char buffer[sizeof(three_state)];
    three_state* placed = ::new (static_cast<void*>(buffer)) three_state;
    if (!placed->poll()) {
      return 202;
    }
    placed->~three_state();
  }

  return 0;
}
