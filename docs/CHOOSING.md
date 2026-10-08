# Which METL type do I use?

METL has around thirty public types and the names are deliberately close to the
standard library's, which makes the list easy to read and hard to *choose* from.
This page is organised by **what you are trying to do**, and every entry says both
when to reach for a type and when not to.

If you want working code rather than a comparison, go to the
[Cookbook](COOKBOOK.md) — it is the same material as recipes, with a compiled,
CI-run example behind each one.

**The one rule that applies almost everywhere:** an operation that can run out
of capacity comes in two forms. `X(...)` treats "full" as a programming error and
asserts; `try_X(...)` reports it by return value and leaves the container exactly
as it was. Every `try_X` is `[[nodiscard]]`. Two have only the reporting form:
`coro::scheduler` has only `try_attach*`, and `event_dispatcher::subscribe`
returns an empty `optional` when full. Full statement:
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
| Bigger table, hashing available, order does not matter | [`static_unordered_map`](../include/metl/static_unordered_map.hpp) / [`static_unordered_set`](../include/metl/static_unordered_set.hpp) | O(1) lookup, **unspecified iteration order**, tombstones on erase, reclaimed by an occasional in-place rebuild on a new-key insert (which invalidates all iterators) — only for nothrow-movable elements; otherwise never reclaimed |
| A handful of entries fixed at compile time | [`lookup_table<K, V, N>`](../include/metl/lookup_table.hpp) | Immutable, linear scan — usually `constexpr`, and smaller than either map |

> **`flat_map::operator[]` and `at()` take a POSITION, not a key** — the opposite
> of `std::map`. Look up by key with `find` / `contains` / `try_emplace`. This is
> the single most common surprise in the library.

### …and on an MCU, the deciding factor is usually RAM

The table above compares time. On a part with 32 KB of SRAM the question is
almost always bytes, and **the two are not close**. Measured `sizeof` on a 64-bit
host (32-bit targets differ by their smaller `size_type`), for
`<uint32_t, uint64_t>` — the caller's data is `capacity × 12` bytes:

| capacity | your data | `flat_map` | `static_unordered_map` |
|---|---|---|---|
| 128 | 1536 | 2064 (1.34×) | 4376 (2.85×) |
| **129** | 1548 | 2080 (1.34×) | **8728 (5.64×)** |
| 256 | 3072 | 4112 (1.34×) | 8728 (2.84×) |
| **257** | 3084 | 4128 (1.34×) | **17432 (5.65×)** |

Two things to take from it.

**`static_unordered_map` costs 2.8× to 5.6× your data, never less.** Its
`bucket_count` is `bit_ceil(capacity × 2)`, so the table always holds at least
twice the capacity you asked for, and every bucket carries a state byte next to
its slot. This is not waste — open addressing with linear probing needs the load
factor below one half, and a power-of-two count is what lets probing mask
instead of divide, which matters on a core with no divider. But it is a
multiplier.

**Asking for one more element can double the table.** `bit_ceil` means capacity
128 gets 256 buckets and capacity **129 gets 512** — 4352 more bytes for one
more element. From 256 to 257 the jump is 8728 → 17432, which on a 32 KB part is
half your RAM inside a gap between two capacities that look interchangeable.
**Pick a capacity at or just under a power of two.** 128, not 130; 256, not 300.

`flat_map` has no cliff — it is a flat 1.34×, and that 0.34 is pair padding
(`pair<uint32_t, uint64_t>` is 16 bytes, not 12) rather than bookkeeping. If your
table is small enough that O(n) insert is acceptable, it is also the one that
fits.

The exact bytes are not asserted (they depend on the ABI), but the shape is:
[`tests/core/ram_footprint_test.cpp`](../tests/core/ram_footprint_test.cpp)
fails the build if the bucket policy changes or a container outgrows its
payload-plus-bookkeeping bound.

> **Nothing measures your stack.** Every container here holds its elements
> inline, so a local `static_unordered_map<uint32_t, uint64_t, 256>` is an
> 8728-byte stack frame. METL has no heap, which means it never answers "did it
> fit" for the storage you declare — `try_emplace` tells you the container is
> full, and nothing at all tells you the frame did not fit. On a part with no
> MMU, that is a silent overwrite of `.bss`. Put the big ones in static storage,
> or as a member of something that already lives there.

## What an operation invalidates

No METL container ever reallocates -- storage is inline and fixed -- so
"invalidated" here only ever means *the element moved* or *was destroyed*. That
is narrower than `std`, but not empty, and the cases differ by container:

| Container | Never invalidates | Invalidates |
|---|---|---|
| `fixed_vector`, `fixed_string` | `push_back` / `emplace_back` / `append`: everything except `end()` | `insert` / `emplace` / `erase` at `pos`: iterators, pointers and references **at and after `pos`** (those elements shift). `pop_back`: the last element and `end()`. `clear` / `assign`: all. |
| `flat_map`, `flat_set` | lookups; assigning to an existing key's value | inserting a new key or `erase`: **at and after** its position (the array shifts). `clear`: all. With exceptions on, a throw during the shift or `erase` clears the container: all. |
| `static_unordered_map`, `static_unordered_set` | lookups; assigning to an existing key; **`erase`** (only the erased element) | inserting a **new** key: **all iterators, pointers and references** -- the tombstone rebuild runs there. This is the open-addressing rule (`absl::flat_hash_map`, `boost::unordered_flat_map`); node-based `std::unordered_map` keeps references across a rehash, these tables cannot. `clear`: all. With exceptions on, a hasher that throws during the rebuild empties the table: all. |
| `ring_buffer`, `fixed_deque` | pointers and references to an element, until that element is popped (storage never moves) | **iterators are positions** (an index from the front): after `pop_front` / `push_front` the same iterator names a different element, so treat every iterator as invalidated. Appending at the back (`push_back` on `fixed_deque`, `emplace_back` / `try_push_back` on `ring_buffer`) invalidates only `end()`. `push_overwrite` on a full ring is a `pop_front` plus an append. |
| `fixed_queue`, `fixed_stack`, `fixed_priority_queue` | -- | no iterators. A `front()` / `top()` reference survives pushes on `fixed_queue` and `fixed_stack` until its element is popped; on `fixed_priority_queue` any `push` or `pop` invalidates it (the heap reorders). |

The erase-while-iterating loop that is safe on `std::unordered_map`
(`auto k = it->key; ++it; m.erase(k);`) is safe here. The equivalent on
`fixed_vector` and `flat_map` is index-based (or `metl::erase_if` for
`fixed_vector`).

## I need to move data between an ISR and the main loop

| You want | Use | Requires |
|---|---|---|
| Whole objects, one producer, one consumer | [`spsc_queue<T, N>`](../include/metl/spsc_queue.hpp) | Nothing beyond C++17 atomics — the default |
| **Bytes**, with a pointer a peripheral can fill | [`spsc_byte_ring<N>`](../include/metl/spsc_byte_ring.hpp) | Same; see the driver section |
| A compound operation to be atomic (`if (!full) push`) | [`guarded<T, Lock>`](../include/metl/lock.hpp) with `irq_lock` | Single core: this is the correct ISR↔main lock. There is deliberately **no spin_lock** |
| Many producers or many consumers | [`static_message_queue<T, N>`](../include/metl/static_message_queue.hpp) wrapped in `guarded<..., irq_lock>` | Masking interrupts; works on Cortex-M0 too |
| One shared word (a flag) | [`atomic_ref<T>`](../include/metl/atomic_ref.hpp) | `load` / `store` only. Its read-modify-write operations (`fetch_add`, CAS) are lock-free retry loops: **not** for ISR↔main on one core |

**Lock-free is multi-core only.** [`mpmc_queue<T, N>`](../include/metl/mpmc_queue.hpp),
[`atomic_handle`](../include/metl/atomic_handle.hpp) and the read-modify-write half
of `atomic_ref` are retry loops: an ISR that preempts one on a single core can spin
forever. Use them between cores (they need a hardware CAS, ARMv7-M and up); between
an ISR and the main loop, use the rows above. The rule is
[SCOPE.md §1](SCOPE.md#1-the-five-invariants).

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
| Nested scratch that unwinds in LIFO order, running destructors | [`arena_allocator<N>`](../include/metl/arena_allocator.hpp) | `mark()` / `rewind()`, `reset()`, and its destructor; not copyable or movable |
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
| A stored callback to a method on an object you own | [`delegate<Sig>`](../include/metl/delegate.hpp) | No — 2 words. The method is a template parameter, so a call is one indirect jump to a thunk that calls it directly |
| A callable that must outlive the expression that made it | [`fixed_function<Sig, N>`](../include/metl/fixed_function.hpp) | **Yes** — N bytes inline, never a heap allocation. A capture that does not fit asserts (`try_assign` returns false); one whose move can throw is a compile error. The callable must be copyable |
| The same, for a move-only callable | [`fixed_any_invocable<Sig, N>`](../include/metl/fixed_function.hpp) | **Yes** — like `fixed_function`, but the wrapper is move-only and so is what it holds |
| A fixed list of listeners notified together | [`event_dispatcher<Sig, N>`](../include/metl/event_dispatcher.hpp) | No — holds delegates |
| Cleanup that must run on every exit path | [`scope_exit`](../include/metl/scope_exit.hpp) | Yes; the callable must be `noexcept` |

Worked example: [`examples/callbacks.cpp`](../examples/callbacks.cpp).

## I need to return something that might not exist, or might fail

| You want | Use |
|---|---|
| A value, or nothing | [`optional<T>`](../include/metl/optional.hpp) |
| A value, or an error explaining why not | [`expected<T, E>`](../include/metl/expected.hpp) (`T` may be `void`) |
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
| To turn text back into a number without stdlib | [`parse`](../include/metl/parse.hpp) — takes a `span`, never a `const char*`, and refuses an out-of-range value instead of wrapping it |
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
| A tick for it from a 16/32-bit timer that wraps | [`tick_extender<Bits>`](../include/metl/tick_extender.hpp) — 64-bit and monotonic; read and update in one critical section |
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
