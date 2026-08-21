# Which METL type do I use?

METL has around thirty public types and the names are deliberately close to the
standard library's, which makes the list easy to read and hard to *choose* from.
This page is organised by **what you are trying to do**, and every entry says both
when to reach for a type and when not to.

If you want working code rather than a comparison, go to the
[Cookbook](COOKBOOK.md) — it is the same material as recipes, with a compiled,
CI-run example behind each one.

**The one rule that applies everywhere:** any operation that can run out of
capacity comes in two forms. `X(...)` treats "full" as a programming error and
asserts; `try_X(...)` reports it by return value and leaves the container exactly
as it was. Every `try_X` is `[[nodiscard]]`. Full statement:
[SCOPE.md §9](SCOPE.md#9-the-recoverable-api-contract).

---

## I need to store a sequence of things

| You want | Use | Not this, because |
|---|---|---|
| A growable-up-to-N array | [`fixed_vector<T, N>`](../include/metl/fixed_vector.hpp) | — the default choice |
| Text | [`fixed_string<N>`](../include/metl/fixed_string.hpp) | `fixed_vector<char>` — no NUL handling, no `c_str()` |
| FIFO, one thread | [`fixed_queue<T, N>`](../include/metl/fixed_queue.hpp) | `fixed_vector` — erasing the front is O(n) |
| LIFO | [`fixed_stack<T, N>`](../include/metl/fixed_stack.hpp) | `fixed_vector` works too; this just narrows the API to what you mean |
| Push and pop at **both** ends | [`fixed_deque<T, N>`](../include/metl/fixed_deque.hpp) | — |
| A rolling window that **drops the oldest** when full | [`ring_buffer<T, N>`](../include/metl/ring_buffer.hpp) | `fixed_queue` refuses when full; this one has `push_overwrite` |
| Highest-priority item first | [`fixed_priority_queue<T, N, Compare>`](../include/metl/fixed_priority_queue.hpp) | sorting a `fixed_vector` — O(n log n) per insert |

`ring_buffer` and `fixed_deque` iterate, but their elements are **not
contiguous** — a ring wraps, so there is no pointer-and-length to hand out. If you
need one, see the driver section below.

## I need to look something up by key

| You want | Use | Trade-off |
|---|---|---|
| Small table, sorted iteration, or `<` is all you have | [`flat_map`](../include/metl/flat_map.hpp) / [`flat_set`](../include/metl/flat_set.hpp) | O(log n) lookup, **O(n) insert** (shifts). Iterates in key order. |
| Bigger table, hashing available, order does not matter | [`static_unordered_map`](../include/metl/static_unordered_map.hpp) / [`static_unordered_set`](../include/metl/static_unordered_set.hpp) | O(1) lookup, **unspecified iteration order**, tombstones on erase |
| A handful of entries fixed at compile time | [`lookup_table<K, V, N>`](../include/metl/lookup_table.hpp) | Immutable, linear scan — usually `constexpr`, and smaller than either map |

> **`flat_map::operator[]` and `at()` take a POSITION, not a key** — the opposite
> of `std::map`. Look up by key with `find` / `contains` / `try_emplace`. This is
> the single most common surprise in the library.

## I need to move data between an ISR and the main loop

| You want | Use | Requires |
|---|---|---|
| Whole objects, one producer, one consumer | [`spsc_queue<T, N>`](../include/metl/spsc_queue.hpp) | Nothing beyond C++17 atomics — the default |
| **Bytes**, with a pointer a peripheral can fill | [`spsc_byte_ring<N>`](../include/metl/spsc_byte_ring.hpp) | Same; see the driver section |
| Many producers or many consumers | [`mpmc_queue<T, N>`](../include/metl/mpmc_queue.hpp) | A hardware CAS (ARMv7-M and up). **Prefer `spsc_queue` when the roles are fixed** — the header has measured contention numbers |
| A compound operation to be atomic (`if (!full) push`) | [`guarded<T, Lock>`](../include/metl/lock.hpp) with `irq_lock` | Single core: this is the correct ISR↔main lock. There is deliberately **no spin_lock** |
| Many producers on a target with **no CAS** (Cortex-M0) | [`static_message_queue<T, N>`](../include/metl/static_message_queue.hpp) wrapped in `guarded<..., irq_lock>` | This is the real fallback `mpmc_queue` names, not a promise — the type exists |
| One shared word (a flag, a counter) | [`atomic_ref<T>`](../include/metl/atomic_ref.hpp) | — |
| One shared *handle* into a pool | [`atomic_handle`](../include/metl/atomic_handle.hpp) | A hardware CAS; `static_assert` fires on Cortex-M0 rather than degrading silently |

A per-operation lock inside a container would not make `if (!q.full()) q.push(x)`
atomic — both calls would lock separately and the gap is still a race. That is why
METL has `guarded<T, Lock>` instead of locking containers; see
[SCOPE.md §6](SCOPE.md#6-why-guardedt-lock-not-a-lock-policy-on-every-container).

## I need to hand out storage

| You want | Use | Reclaims |
|---|---|---|
| A pool of live objects, addressed by pointer | [`object_pool<T, N>`](../include/metl/object_pool.hpp) | Per object, via `destroy(ptr)` |
| The same, but a stale reference must be **detectable** | [`handle_pool<T, N>`](../include/metl/handle_pool.hpp) | Per object; a freed handle resolves to `nullptr` instead of a recycled object |
| Scratch memory for one tick, thrown away wholesale | [`monotonic_buffer<N>`](../include/metl/monotonic_buffer.hpp) | Only in bulk, via `reset()` |
| The same, but typed | [`static_allocator<T, N>`](../include/metl/static_allocator.hpp) | Only in bulk |
| Nested scratch that unwinds in LIFO order, running destructors | [`arena_allocator<N>`](../include/metl/arena_allocator.hpp) | `mark()` / `rewind()` |
| Shared ownership of a long-lived object | [`intrusive_ptr<T>`](../include/metl/intrusive_ptr.hpp) | Refcount in the object itself — no control block, no allocation |

A handle is a [`versioned_handle`](../include/metl/versioned_handle.hpp): four
bytes of `{index, generation}`, trivially copyable, so it fits in a table, a
message, or a single atomic word — which is what makes
[`atomic_handle`](../include/metl/atomic_handle.hpp) possible with a plain 32-bit
CAS and no pointer-bit stuffing.

**Pointer or handle?** They look interchangeable until something is freed: a
pointer into a recycled slot has the same address, the same type, and the pool will
even confirm it owns it. A handle carries a generation counter, so the staleness is
*detected*. [`examples/handles_and_pools.cpp`](../examples/handles_and_pools.cpp)
runs that difference and prints it; the design argument is
[SCOPE.md §7](SCOPE.md#7-why-handles-not-tagged-pointers).

## I need to hold a callback

| You want | Use | Owns the callable? |
|---|---|---|
| A **parameter** that takes any callable | [`function_ref<Sig>`](../include/metl/function_ref.hpp) | No — 2 words. Binds **lvalues only**, so a temporary cannot dangle |
| A stored callback to a method on an object you own | [`delegate<Sig>`](../include/metl/delegate.hpp) | No — 2 words. The method is a template parameter, so there is no indirection |
| A callable that must outlive the expression that made it | [`fixed_function<Sig, N>`](../include/metl/fixed_function.hpp) | **Yes** — N bytes inline; a capture that does not fit is a compile error, never a heap allocation |
| A fixed list of listeners notified together | [`event_dispatcher<Sig, N>`](../include/metl/event_dispatcher.hpp) | No — holds delegates |
| Cleanup that must run on every exit path | [`scope_exit`](../include/metl/scope_exit.hpp) | Yes; the callable must be `noexcept` |

Worked example: [`examples/callbacks.cpp`](../examples/callbacks.cpp).

## I need to return something that might not exist, or might fail

| You want | Use |
|---|---|
| A value, or nothing | [`optional<T>`](../include/metl/optional.hpp) |
| A value, or an error explaining why not | [`expected<T, E>`](../include/metl/expected.hpp) (`E` may be `void`) |
| One of several alternatives | [`variant<Ts...>`](../include/metl/variant.hpp) |
| A view of someone else's contiguous data | [`span<T>`](../include/metl/span.hpp) |

> **There are no `bad_*_access` exceptions.** METL is exception-free, so
> `value()`, `operator*`, `get<T>()` and `at()` **assert** (abort by default) on the
> empty / wrong / out-of-range case. Branch on `has_value()` /
> `holds_alternative<>()` first, or use the total accessors `value_or`, `get_if`,
> `find`.

## I need to talk to hardware

| You want | Use |
|---|---|
| A memory-mapped register | [`mmio`](../include/metl/mmio.hpp) + [`register_access`](../include/metl/register_access.hpp) |
| Named bit fields in a word | [`bitfield`](../include/metl/bitfield.hpp) |
| Byte-order conversion on a wire format | [`endian`](../include/metl/endian.hpp) |
| A frame checksum | [`crc8`](../include/metl/crc8.hpp) / [`crc16`](../include/metl/crc16.hpp) / [`crc32`](../include/metl/crc32.hpp) |
| Bit twiddling (popcount, log2, power-of-two) | [`bit`](../include/metl/bit.hpp) |
| A DMA/UART region to fill in place | [`spsc_byte_ring<N>`](../include/metl/spsc_byte_ring.hpp) |
| To turn a number into text without stdio | [`format`](../include/metl/format.hpp) |
| To spin or idle politely | [`wait`](../include/metl/wait.hpp) — `cpu_relax()` to spin, `wait_for_event()` to actually idle |

`spsc_byte_ring` hands out contiguous spans, which is the one thing `ring_buffer`
cannot do. It performs **no cache maintenance** — if your DMA engine is not
coherent with the data cache, invalidating and cleaning is still your job, and the
header says so rather than implying otherwise.

## I need cooperative tasks without an RTOS

| You want | Use |
|---|---|
| A task that yields mid-function and resumes there | [`coro::protothread`](../include/metl/coro/protothread.hpp) |
| A task written as an explicit step function | [`coro::stepper`](../include/metl/coro/stepper.hpp) |
| To poll every task each pass | [`coro::scheduler<N>`](../include/metl/coro/scheduler.hpp) |
| To run tasks **by deadline**, and sleep in between | [`coro::deadline_scheduler<N, Tick>`](../include/metl/coro/deadline_scheduler.hpp) |
| A state machine with a transition table | [`fsm`](../include/metl/fsm.hpp) |

A protothread is stackless: state that must survive a yield lives in a **class
member**, never a local, and no two yield points may share a source line.

---

## Still unsure between two?

- **`fixed_queue` vs `ring_buffer`** — what should happen when it is full? Refuse
  (`fixed_queue`) or drop the oldest (`ring_buffer::push_overwrite`).
- **`flat_map` vs `static_unordered_map`** — do you iterate in key order (flat) or
  look up far more often than you insert (unordered)?
- **`object_pool` vs `handle_pool`** — can a reference outlive the object? If it
  can, you want the one that detects it.
- **`spsc_queue` vs `spsc_byte_ring`** — are you moving *objects* or a *byte
  stream* that something else fills?
- **`function_ref` vs `fixed_function`** — does the callable need to outlive the
  call? Only then do you need to own it.

If none of these fit, the type probably is not here, and
[SCOPE.md](SCOPE.md) says what METL will and will not add — including the things
that were considered and declined, with the reasons.
