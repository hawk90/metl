# METL Cookbook

Task-oriented recipes for common embedded / freestanding problems, using
METL's header-only, heap-free, exception-free building blocks.

Every snippet below mirrors a **compiled, CI-run example** under
[`examples/`](../examples), so the code is known to build clean under
`-Wall -Wextra -Werror -std=c++17` and to pass its own self-checks. Build the
examples with:

```sh
cmake -B build -S . -DMETL_BUILD_EXAMPLES=ON
cmake --build build -j
ctest --test-dir build -R metl_example --output-on-failure
```

## Contents

- [Non-standard contracts you must know](#non-standard-contracts-you-must-know)
- [A fixed-capacity vector without the heap](#a-fixed-capacity-vector-without-the-heap)
- [Key/value lookup with flat_map (positional vs. key access!)](#keyvalue-lookup-with-flat_map)
- [A bounded FIFO / rolling window with ring_buffer](#a-bounded-fifo--rolling-window-with-ring_buffer)
- [Error handling without exceptions](#error-handling-without-exceptions)
- [Per-tick scratch memory without malloc](#per-tick-scratch-memory-without-malloc)
- [An SPSC queue between an ISR and the main loop](#an-spsc-queue-between-an-isr-and-the-main-loop)
- [Memory-mapped register access](#memory-mapped-register-access)
- [A small finite state machine](#a-small-finite-state-machine)
- [A cooperative task (protothread)](#a-cooperative-task-protothread)

---

## Non-standard contracts you must know

METL deliberately diverges from the standard library in a few places. These are
the ones that bite people; read them before anything else.

| Behaviour | METL | `std::` equivalent |
| --- | --- | --- |
| `at(i)` out of range | **asserts** (aborts by default) | throws `std::out_of_range` |
| `optional::value()` when empty | **asserts** | throws `std::bad_optional_access` |
| `expected::value()` on error | **asserts** | throws `std::bad_expected_access` |
| `variant` `get<T>()` wrong alternative | **asserts** | throws `std::bad_variant_access` |
| `flat_map::operator[]` / `at()` | **positional index** into sorted storage | key lookup / insert (`std::map`) |
| A failed assert | is provably `[[noreturn]]` — **aborts**, never falls through | n/a |
| `function_ref` construction | **rejects rvalue callables** (deleted overload) | — (P0792 also deletes this) |

Consequences:

- **There are no `metl::bad_*_access` exceptions.** METL is exception-free.
  Unchecked accessors (`value()`, `operator*`, `get<>()`, `at()`) assert on a
  broken precondition and, by default, `std::abort()`. **Always branch on
  `has_value()` / `operator bool` / `holds_alternative<>()` first**, or use the
  total accessors `value_or(default)` / `find()` / `get_if()`.
- **The assert path cannot continue.** `metl::detail::assertion_failed` and
  `panic` are `[[noreturn]]` and call `std::abort()` after invoking the
  (customizable) handler, so even a user handler that mistakenly `return`s
  cannot fall through a failed precondition into undefined behaviour.
- **`flat_map` is not `std::map`.** `operator[]` and `at()` take an integer
  **position** into the key-sorted storage (like a vector), *not* a key. To work
  by key, use `find(key)` (returns `mapped_type*` / `nullptr`), `contains(key)`,
  `try_emplace(key, value)`, `insert_or_assign(key, value)`, or `erase(key)`.
- **`function_ref` binds lvalues only.** Constructing one from a temporary
  callable is a compile error, because the reference would dangle at the end of
  the full expression. Bind a named callable or a function pointer instead.

---

## A fixed-capacity vector without the heap

*Full example: [`examples/containers.cpp`](../examples/containers.cpp)*

```cpp
#include <metl/fixed_vector.hpp>

metl::fixed_vector<int, 8> v;  // capacity fixed at 8, storage is inline

v.push_back(10);               // asserts if it would overflow capacity
if (!v.try_push_back(20)) {    // returns false instead of asserting
    // handle "full"
}

for (int x : v) { /* ... */ }  // iterates like std::vector
v.pop_back();
int last = v.back();
```

Use `try_push_back` on any path where "full" is a normal, recoverable outcome;
use `push_back` only where overflow is a programming error you want to catch.

## Key/value lookup with flat_map

*Full example: [`examples/containers.cpp`](../examples/containers.cpp)*

`flat_map` keeps entries sorted by key in inline storage and looks them up with
binary search. **Its `operator[]` / `at()` are positional, not key lookups** —
see [contracts](#non-standard-contracts-you-must-know).

```cpp
#include <metl/flat_map.hpp>

metl::flat_map<std::uint8_t, std::int32_t, 16> readings;

readings.try_emplace(3, 300);          // insert; returns false if key exists / full
readings.insert_or_assign(2, 250);     // insert-or-update by key

if (const std::int32_t* r = readings.find(2)) {   // KEY lookup -> pointer or null
    use(*r);
}
if (readings.contains(5)) { /* ... */ }

for (const auto& kv : readings) {      // ascending key order
    use(kv.key, kv.value);
}

auto& first = readings[0];             // POSITIONAL: lowest-key entry, NOT key 0
readings.erase(1);                     // erase BY key
```

## A bounded FIFO / rolling window with ring_buffer

*Full example: [`examples/containers.cpp`](../examples/containers.cpp)*

```cpp
#include <metl/ring_buffer.hpp>

metl::ring_buffer<int, 4> rb;

rb.try_push_back(1);        // returns false when full (won't clobber)
rb.push_overwrite(2);       // drops the oldest element to make room when full

int oldest = rb.front();
int newest = rb.back();
rb.pop_front();             // consume oldest, queue-style
int nth = rb[0];            // indexed from the front
```

`push_overwrite` is the "keep the newest N samples" idiom for signal windows;
`try_push_back` is the strict bounded-FIFO idiom that refuses to overwrite.

## Error handling without exceptions

*Full example: [`examples/error_handling.cpp`](../examples/error_handling.cpp)*

```cpp
#include <metl/expected.hpp>
#include <metl/optional.hpp>
#include <metl/variant.hpp>

enum class parse_error : std::uint8_t { empty, not_a_digit, overflow };

metl::expected<std::uint32_t, parse_error> parse_u32(const char* s) noexcept {
    if (!s || !*s) return metl::unexpected<parse_error>(parse_error::empty);
    // ...
    return value;   // implicit success
}

// Branch first; read value() only on the happy path (it ASSERTS on error).
if (auto r = parse_u32(text)) {
    use(r.value());
} else {
    switch (r.error()) { /* ... */ }
}

// optional: value_or() is the safe, branch-free default path.
std::int32_t gain = config_lookup("gain").value_or(1);

// variant: dispatch with visit (handles every alternative).
using result = metl::variant<ok_result, busy_result>;
std::uint32_t code = metl::visit([](auto&& alt) -> std::uint32_t {
    using A = std::decay_t<decltype(alt)>;
    if constexpr (std::is_same_v<A, busy_result>) return 1000u + alt.retry_ms;
    else return alt.value;
}, run_command(busy));
```

Remember: `value()`, `operator*`, and `get<T>()` **assert** — there is no
`bad_*_access` exception in METL. Check presence first, or use `value_or` /
`get_if` / `find`.

## Per-tick scratch memory without malloc

*Full example: [`examples/allocators.cpp`](../examples/allocators.cpp)*

```cpp
#include <metl/monotonic_buffer.hpp>
#include <metl/arena_allocator.hpp>

// Bump allocator wiped as a whole each control-loop tick. No destructors run,
// so use it for trivially-destructible scratch only.
metl::monotonic_buffer<256> scratch;
for (;;) {
    auto* block = scratch.try_emplace<sample_block>();  // nullptr if it won't fit
    void* raw   = scratch.allocate(16, alignof(std::max_align_t));
    // ... use block / raw ...
    scratch.reset();   // O(1) reclaim of the whole tick
}

// Arena that records destructors and supports LIFO checkpoints.
metl::arena_allocator<512> arena;
auto* base = arena.try_emplace<connection>(1);
auto mark  = arena.mark();
auto* tmp  = arena.try_emplace<connection>(2);
arena.rewind(mark);   // destroys tmp (LIFO), keeps base
arena.reset();        // unwinds everything
```

## An SPSC queue between an ISR and the main loop

*Full example: [`examples/spsc_isr.cpp`](../examples/spsc_isr.cpp) (single-thread,
deterministic); [`examples/sensor_pipeline.cpp`](../examples/sensor_pipeline.cpp)
(two real threads, TSAN-checked)*

`spsc_queue` is wait-free with **exactly one** producer and **exactly one**
consumer. The canonical use is ISR (producer) to main loop (consumer):

```cpp
#include <metl/spsc_queue.hpp>

metl::spsc_queue<adc_sample, 8> q;   // capacity must be a power of two;
                                     // 8 -> 7 usable slots

// ---- in the ISR ----
void adc_isr() {
    adc_sample s = read_adc();
    if (!q.try_push(s)) {
        ++g_overruns;   // full: DROP + count, never block in an ISR
    }
}

// ---- in the main loop ----
adc_sample s;
while (q.try_pop(s)) {
    process(s);
}
```

Rules: one producer role, one consumer role, power-of-two capacity, and treat a
full queue as an expected drop condition rather than blocking.

## Memory-mapped register access

*Full example: [`examples/mmio_peripheral.cpp`](../examples/mmio_peripheral.cpp)*

```cpp
#include <metl/mmio.hpp>
#include <metl/bitfield.hpp>

// Name the fields instead of using magic numbers.
using cr_enable   = metl::bitfield_u32<0, 1>;
using cr_baud_div = metl::bitfield_u32<8, 16>;

// Compile-time fixed address:
using UART_CR = metl::mmio_register<std::uint32_t, 0x40013800>;
UART_CR::set_bits(cr_enable::mask);
UART_CR::modify(cr_baud_div::mask, cr_baud_div::insert(0u, divisor));

// Or a runtime address / pointer:
metl::mmio_ptr<std::uint32_t> cr{reinterpret_cast<volatile std::uint32_t*>(0x40013800)};
std::uint32_t v = cr.read();     // goes through read_once (not elided/reordered)
cr.write(v | cr_enable::mask);

// Raw primitives, if you don't want a wrapper:
std::uint32_t x = metl::read_once(reg_ptr);
metl::write_once(reg_ptr, x);
metl::barrier_full();            // std::atomic_thread_fence, maps to DMB/DSB/mfence
```

All accesses route through `read_once` / `write_once` so the optimizer cannot
fold, reorder, or eliminate them.

## A small finite state machine

*Full example: [`examples/blinky_fsm.cpp`](../examples/blinky_fsm.cpp)*

```cpp
#include <metl/fsm.hpp>
#include <metl/delegate.hpp>

using transition = metl::fsm_transition<State, Event>;
using action_t   = metl::delegate<void(State, Event, State)>;

const std::array<transition, N> transitions{{
    {State::off, Event::turn_on,  State::on,       {}},
    {State::on,  Event::tick,     State::on,       tick_action},  // self-loop w/ action
    {State::on,  Event::turn_off, State::off,      {}},
}};

metl::fsm<State, Event, transitions.size(), entry_hooks.size(), 0>
    machine(State::off, transitions, entry_hooks, {});

if (machine.dispatch(Event::turn_on)) {   // false if no matching transition
    // ... entry hooks and transition action already ran ...
}
State s = machine.current_state();
```

## A cooperative task (protothread)

*Full example: [`examples/coroutine_task.cpp`](../examples/coroutine_task.cpp)*

A `protothread` is a stackless task you poll from a super-loop. State that must
survive a yield lives in **class members** (never locals), and no two yield
points may share a source line.

```cpp
#include <metl/coro/protothread.hpp>

class blink_task : public metl::coro::protothread {
 public:
    bool run() noexcept {           // returns false=yielded, true=finished
        METL_PT_BEGIN();
        for (blinks_ = 0; blinks_ < 4; ++blinks_) {
            led_ = true;
            deadline_ = tick_ + 3;                 // member: survives the yield
            METL_PT_WAIT_UNTIL(tick_ >= deadline_);
            led_ = false;
            deadline_ = tick_ + 3;
            METL_PT_WAIT_UNTIL(tick_ >= deadline_);
        }
        METL_PT_END();
    }
    void set_tick(std::uint32_t t) noexcept { tick_ = t; }
 private:
    std::uint32_t tick_ = 0, deadline_ = 0;
    unsigned blinks_ = 0;
    bool led_ = false;
};

// super-loop:
blink_task t;
for (std::uint32_t tick = 0; !t.run(); ++tick) t.set_tick(tick);
```

For an explicit-state alternative without macros, see
[`metl/coro/stepper.hpp`](../include/metl/coro/stepper.hpp).
