// blinky_fsm.cpp
//
// LED blink state machine driven by metl::fsm, with a mock GPIO output
// implemented as a host-side volatile uint32 driven through
// metl::mmio_ptr (which is API-compatible with metl::mmio_register).
//
// The example exercises:
//   - metl::fsm with entry/exit hooks and transition actions
//   - metl::mmio_ptr writing to a "GPIO output data register"
//   - metl::delegate bound to a host class
//
// No heap, no exceptions, no RTTI, no iostreams.

#include <array>
#include <cstdint>
#include <cstdio>

#include <metl/delegate.hpp>
#include <metl/fsm.hpp>
#include <metl/mmio.hpp>

namespace {

// Mock GPIO output data register. On real hardware this would be a
// peripheral register at a fixed address (e.g. GPIOA->ODR on STM32).
volatile std::uint32_t g_gpio_odr = 0u;

constexpr std::uint32_t kLedBit = 1u << 5;  // arbitrary "LED on pin 5"

enum class led_state : std::uint8_t {
  off,
  on,
  blinking,
  faulted,
};

enum class led_event : std::uint8_t {
  turn_on,
  turn_off,
  start_blink,
  tick,
  fault,
  recover,
};

// Driver that owns the mock register and is invoked by the FSM hooks.
class led_driver {
 public:
  explicit led_driver(volatile std::uint32_t* reg) noexcept : reg_(reg), on_(false), ticks_(0), faults_(0) {}

  void enter_off(led_state) noexcept {
    reg_.clear_bits(kLedBit);
    on_ = false;
  }

  void enter_on(led_state) noexcept {
    reg_.set_bits(kLedBit);
    on_ = true;
  }

  void enter_blinking(led_state) noexcept {
    // Initial state of blink: leave LED in whatever it was; toggle on tick.
  }

  void enter_faulted(led_state) noexcept {
    reg_.clear_bits(kLedBit);
    on_ = false;
    ++faults_;
  }

  void on_tick(led_state, led_event, led_state) noexcept {
    // Self-loop transition only invoked while in blinking state.
    if (on_) {
      reg_.clear_bits(kLedBit);
    } else {
      reg_.set_bits(kLedBit);
    }
    on_ = !on_;
    ++ticks_;
  }

  bool on() const noexcept { return on_; }
  unsigned ticks() const noexcept { return ticks_; }
  unsigned faults() const noexcept { return faults_; }

 private:
  metl::mmio_ptr<std::uint32_t> reg_;
  bool on_;
  unsigned ticks_;
  unsigned faults_;
};

}  // namespace

int main() {
  led_driver driver(&g_gpio_odr);

  using transition = metl::fsm_transition<led_state, led_event>;
  using state_hook = metl::fsm_state_hook<led_state>;
  using action_t = metl::delegate<void(led_state, led_event, led_state)>;
  using hook_t = metl::delegate<void(led_state)>;

  const auto tick_action = action_t::bind<led_driver, &led_driver::on_tick>(driver);

  const std::array<transition, 8> transitions{{
      {led_state::off, led_event::turn_on, led_state::on, {}},
      {led_state::on, led_event::turn_off, led_state::off, {}},
      {led_state::off, led_event::start_blink, led_state::blinking, {}},
      {led_state::on, led_event::start_blink, led_state::blinking, {}},
      {led_state::blinking, led_event::tick, led_state::blinking, tick_action},
      {led_state::blinking, led_event::turn_off, led_state::off, {}},
      {led_state::on, led_event::fault, led_state::faulted, {}},
      {led_state::faulted, led_event::recover, led_state::off, {}},
  }};

  const std::array<state_hook, 4> entry_hooks{{
      {led_state::off, hook_t::bind<led_driver, &led_driver::enter_off>(driver)},
      {led_state::on, hook_t::bind<led_driver, &led_driver::enter_on>(driver)},
      {led_state::blinking, hook_t::bind<led_driver, &led_driver::enter_blinking>(driver)},
      {led_state::faulted, hook_t::bind<led_driver, &led_driver::enter_faulted>(driver)},
  }};

  metl::fsm<led_state, led_event, transitions.size(), entry_hooks.size(), 0> machine(
      led_state::off, transitions, entry_hooks, {});

  // ---- Scripted sequence ---------------------------------------------------

  // off -> on
  if (!machine.dispatch(led_event::turn_on))
    return 1;
  if (machine.current_state() != led_state::on)
    return 2;
  if ((g_gpio_odr & kLedBit) == 0)
    return 3;

  // on -> blinking, then 4 ticks toggle the LED 4 times.
  if (!machine.dispatch(led_event::start_blink))
    return 4;
  for (int i = 0; i < 4; ++i) {
    if (!machine.dispatch(led_event::tick))
      return 10 + i;
  }
  if (driver.ticks() != 4)
    return 20;

  // Stop blinking.
  if (!machine.dispatch(led_event::turn_off))
    return 21;
  if (machine.current_state() != led_state::off)
    return 22;
  if ((g_gpio_odr & kLedBit) != 0)
    return 23;

  // Invalid transition (off -> turn_off) must fail without changing state.
  if (machine.dispatch(led_event::turn_off))
    return 24;
  if (machine.current_state() != led_state::off)
    return 25;

  // Fault recovery path: on -> faulted -> off.
  if (!machine.dispatch(led_event::turn_on))
    return 26;
  if (!machine.dispatch(led_event::fault))
    return 27;
  if (machine.current_state() != led_state::faulted)
    return 28;
  if (driver.faults() != 1)
    return 29;
  if (!machine.dispatch(led_event::recover))
    return 30;
  if (machine.current_state() != led_state::off)
    return 31;

  std::printf("blinky_fsm: ticks=%u faults=%u final_state=off\n", driver.ticks(), driver.faults());
  return 0;
}
