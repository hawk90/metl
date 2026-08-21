# Changelog

All notable changes to METL are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed — BREAKING

These land before the first tag on purpose. After v0.1.0 the same corrections
would cost a deprecation cycle; today they cost a recompile.

- **The recoverable-API contract is now uniform and machine-checked**
  (`docs/SCOPE.md` §9). METL has always offered two forms of every operation that
  can run out of capacity — `X(...)` asserts, `try_X(...)` reports — but the
  contract was applied unevenly, and the unevenness was invisible:

  - **`METL_NODISCARD` was missing from 22 of the 55 `try_*` entry points**, split
    by *when each header was written* rather than by any principle.
    `fixed_vector::try_push_back(x);` compiled silently while
    `fixed_queue::try_push(x);` warned — the same idiom with opposite safety in
    the same library, and the silent one was in the most-used container. Ignoring
    a `try_*` result is now a warning everywhere.

  - **`fixed_string::assign` / `append` → `try_assign` / `try_append`.** They
    returned `bool` for "did it fit" without the `try_` prefix, contradicting the
    rule the cookbook states. Their plain names now mean what they mean
    everywhere else in the library: assert on overflow.

  - **`flat_map` / `static_unordered_map`: `insert_or_assign` → `try_insert_or_assign`.**
    Sharper than a naming slip. `std::map::insert_or_assign`'s `bool` means *was
    it inserted rather than assigned*; this one meant *did it fit*. Borrowing a
    standard name and changing what its result means is the silent mislabelling
    the design principles forbid. `insert_or_assign` now exists as the asserting
    form and returns a reference, like `emplace`.

  Migration is mechanical: add `try_` to those five names, and check results you
  were ignoring (the compiler now points at every one).

### Added

- **`format.hpp` — bounded integer-to-text.** `fixed_string` could hold text but
  had no way to put a *number* into it, so the options were `snprintf` (pulls stdio
  into the image, allocates on some libcs, and reports what *would* have been
  written rather than what was) or a hand-rolled digit loop in every project.
  Six functions in `try_`/asserting pairs: `format_uint`, `format_int`,
  `format_hex`, each with a `try_` form that returns an **empty span** when the
  buffer is too small and leaves it untouched.

  Four decisions worth stating, because each could have gone the other way
  silently:

  - **No format string, and there will not be one.** A `{}`-parser is code and
    tables in the image of every target that links it, for an ergonomic gain a few
    explicit calls already deliver.
  - **The scratch buffer is the caller's.** A hidden one would have to be `static`
    — not reentrant, and this library is used from ISRs — or a stack temporary the
    caller cannot size.
  - **A too-narrow fixed hex width is refused, not truncated.**
    `format_hex(out, 0xabcd, 2)` returns empty rather than `"cd"`: a register dump
    missing its high nibbles is worse than no dump.
  - **Passing a signed value to `format_uint`/`format_hex` is a compile error**
    naming the reason, rather than a huge number at runtime.

  The signed path takes its magnitude in the unsigned domain, because `-value` on
  `LLONG_MIN` is undefined. Worth recording how that is actually guarded: on
  two's-complement hardware the naive negate produces the *right* number, so no
  assertion can see it — mutating the line back leaves every test green and makes
  **UBSan** report "negation of -9223372036854775808 cannot be represented". The
  test says so, so nobody later reads those assertions as covering it.

  Also mutation-tested: losing the zero case in the digit count, dropping the
  too-few-hex-digits refusal, and an off-by-one in the fit check are each caught
  (the first at compile time, by the test's `constexpr` check).

- **`docs/SCOPE.md` now distinguishes a leaf utility from a speculative type.**
  The "no in-library caller, so not yet" rule is about speculation, and two entries
  have now hit its seam — a driver's DMA region and a log line are called from
  outside METL by definition. Rather than granting a second ad-hoc exception, the
  rule is refined: for a leaf utility at the application boundary, the caller
  requirement is replaced by two harder questions the PR must answer — is the gap
  *structural* rather than "it would be nice", and is it exercised by a real,
  self-checking example that reaches the hard case. Everything that *can* have an
  in-library caller still must have one, so `fixed_bitset` and the compile-time
  string map stay deferred on exactly that basis.

- **`spsc_byte_ring` — a lock-free SPSC byte ring that hands out contiguous
  spans.** A driver does not want to push bytes one at a time; it wants an address
  and a length to hand the peripheral, then one "I moved n bytes" call.
  `ring_buffer` structurally cannot provide that — `detail/ring_core.hpp` states
  that a ring's elements are not contiguous — and `spsc_queue<std::byte, N>` turns
  a 256-byte frame into 256 push/pop pairs.

  **It makes no DMA-safety claim, deliberately.** METL has no portable
  cache-maintenance operation and `qemu-conformance` cannot model DMA coherency,
  so a "DMA-safe" label would be a claim the project cannot verify — which its own
  discipline forbids. The header says plainly that invalidating and cleaning is the
  driver's job, and the type is named for its concurrency contract rather than for
  DMA so the name cannot be read as a guarantee.

  The wrap is part of the API rather than hidden behind it: both spans stop at the
  physical end of the buffer, so `writable_span().size()` can be smaller than
  `writable_size()`. Returning two spans would push that two-step onto every caller
  including the ones that never wrap; mapping the pages twice needs an MMU and an
  OS. What *is* guaranteed and tested: an empty span means full (or empty) and
  nothing else.

  **A defect found while writing it up, worth recording because no positive test
  could see it:** the doc comment said `commit_write(n)` was bounded by the span
  you were handed, while the guard checked the total free space. Those differ
  exactly at the seam — eight bytes free of which two are contiguous — so
  `commit_write(8)` after a two-byte span was accepted and published six bytes that
  were never written and could not have been. `writable_span` and `commit_write`
  now share one helper so they cannot drift again, and
  `spsc_byte_ring_overcommit_test` is the negative control: it sets
  `METL_HARDENING 0` (so only a never-stripped `METL_HARDEN` can fire) and requires
  a forked child to die on the over-commit. Restoring the old guard makes that test
  fail, which is the only reason to trust it.

  `consume(n)` is deliberately *not* symmetric — it stays bounded by
  `readable_size()`, because consuming past the seam only discards bytes, so
  "drop everything queued" is a legitimate single call.

  Verified: mutation-tested (ignoring the wrap in either direction, and an
  off-by-one in `try_write`, are each caught; an equivalent `% Capacity` for
  `& mask` is correctly *not* caught), and a two-thread test under TSAN is clean
  while turning `commit_write`'s release store into a relaxed one produces a race
  and an abort.

- **`fixed_priority_queue` — a bounded binary heap — and `coro::deadline_scheduler`,
  its caller.** Admitted under the rule in `docs/SCOPE.md` that a public type needs
  a user inside the library, so the two ship together: `coro::scheduler` is
  round-robin, which means a task that only wants to run in 500 ms is still visited
  on every pass and has to check the clock itself. The deadline scheduler keeps
  tasks in the heap ordered by when they next want to run and polls only what is
  due, so an idle loop can sleep to `next_deadline()` instead of spinning. The
  worked example wakes 7 times where a 1 ms poll loop would have made 60 passes.

  Contract choices worth knowing, each of which could have been made silently and
  was not:

  - **`top()` is const-only.** A mutable reference would let a caller change the
    key the heap is ordered by, breaking the invariant for every later operation
    with no diagnostic. `push`/`emplace` return `void` for the same class of
    reason: the new element is sifted immediately, so its construction slot is not
    where it ends up and a returned reference would point at whatever moved there.
  - **The scheduler's `Tick` must not wrap.** It is compared with plain `<`. The
    usual fix for a rolling hardware counter — comparing signed differences — is
    not a strict weak ordering once the spread exceeds half a period, and a heap
    requires one, so the queue would misorder *silently* rather than fail. The
    header says to widen the counter where the overflow is observed instead of
    hiding the problem behind a comparator.
  - **`run_due` takes a `max_dispatches` bound.** A task may legitimately re-arm at
    a deadline that is already due (a catch-up timer); without the bound that is an
    unbounded loop, which I3 forbids.
  - **One slot is reserved for the running task's re-arm** while its poll is on the
    stack, so a poll that fills the scheduler cannot make the re-arm fail;
    `try_schedule` reports full one slot early instead.

  `erase_if` is the only way to remove something that is not on top, and is
  deliberately O(Capacity) with a Floyd re-heapify — a heap has no ordering to
  search by, so finding the victims is a full scan either way. It is what makes
  `deadline_scheduler::cancel` possible.

  Verified beyond "the tests pass": a new `fuzz_priority_queue` harness checks the
  heap property, the size bookkeeping and `top()`-dominates-the-array after *every*
  operation, and both it and the unit tests were mutation-tested — a `sift_down`
  that no-ops at the root, and a `heapify` that starts too shallow, are each caught.
  That exercise found a real gap: the scheduler test as first written passed under
  the `sift_down` mutant, because a three-task heap is too shallow to distinguish a
  working sift from a broken one. It now drains eight scrambled deadlines.

- **`fixed_vector` gains the recoverable half of its mutating API** —
  `try_insert` (four overloads), `try_emplace(pos, ...)`, `try_resize` (two) and
  `try_assign` (two). `insert`/`resize`/`assign` were asserting-only, so code
  sizing a buffer from runtime input — a protocol frame, a sensor burst, a parse
  — had no recovery path short of pre-checking capacity by hand.

  `try_insert` returns `end()` on refusal rather than a `bool`, so a successful
  call still yields the std-shaped iterator to the new element; a successful
  insert can never return `end()`, so the outcomes are distinguishable.

  All of them are all-or-nothing: on refusal the container is byte-for-byte what
  it was. That is why the range overloads require a **forward** iterator — a
  single-pass source cannot be measured without being consumed, so the promise
  could not be kept. The rejection is a `static_assert` naming that reason rather
  than an `enable_if`, because "no matching function" explains nothing.

- **`api-contract` CI job** (`tools/check_api_contract.py`) enforces the two
  mechanical halves of §9: every `try_*` is `METL_NODISCARD` (R3), and `try_` is
  reserved for recoverable forms (R2), with a reasoned allowlist for the bools
  that answer a question instead of reporting a failure (`erase`, `contains`, …).
  It ships a `--self-test` canary — a gate that cannot fail is not a gate — and
  the job additionally compiles `forward_iterator_contract.cpp` both ways,
  requiring the single-pass arm to fail, the same shape as `handle-atomics`.

- **`fixed_string` gains asserting `assign`/`append`**, completing the pair for
  the names freed up by the renames above.

### Fixed

- **`fixed_vector::insert(pos, n, value)` could overflow its own bounds check.**
  The precondition was written `size_ + n <= Capacity`, which wraps for a large
  `n` and then passes. It is now a subtraction. Covered by a test that passes
  `SIZE_MAX`.

- **`flat_map` and `flat_set` gain the six relational operators.** They had
  **none** — `std::map` and `std::set` have all six, so two of these could not be
  compared at all, not even for equality. Cross-capacity like `fixed_vector`'s
  and `fixed_string`'s: two containers holding the same entries compare equal
  whatever their declared capacities, because capacity is a storage decision
  rather than part of the value. Ordering is lexicographic over the entry
  sequence, key before value for the map.
  The comparator type must match, and that restriction is deliberate: `Compare`
  determines the order entries are stored in, so a `flat_map<K, V, less>` and a
  `flat_map<K, V, greater>` holding identical entries hold them in opposite
  sequences — comparing the two would report a difference that describes the
  comparators rather than the contents.

- **`ring_buffer` and `fixed_deque` are iterable.** Neither had `begin()`/`end()`
  at all, which made the ordinary embedded job — drain a telemetry ring, walk a
  deque — impossible without an index loop, and left them the only fixed-capacity
  sequence containers in the library you could not use with a range-`for` or a
  standard algorithm. Both now expose `begin`/`end`/`cbegin`/`cend` plus the
  reverse forms, from a shared **random-access** iterator on `detail::ring_core`
  so the two cannot drift apart.
  The iterator holds a **logical index**, not a `T*`: a ring's elements are not
  contiguous, so pointer arithmetic over the storage array walks out of the live
  range and into unconstructed slots as soon as the buffer wraps. Indices go
  through the container's existing `physical_index` mapping, so there is one
  mapping rather than two that can disagree. Random access rather than
  bidirectional because the underlying `at()` is already O(1) — the stronger
  category costs nothing and lets algorithms that need it work. A mutable
  iterator converts to a `const_iterator`; the reverse is rejected, and a
  `static_assert` in the tests pins that.

- **The test suite now runs on emulated Cortex-M0/M3/M4/M7 (`qemu-conformance`),
  and `irq_lock` is verified against a real interrupt.** Until now the only thing
  executed on an MCU was one smoke program covering 7 of 56 headers; everything
  else was compile-and-link only. 66 of the 75 host tests now execute on target,
  with 9 deny-listed for stated reasons (threads, forked death tests, tests that
  deliberately `throw`, and one finding below). Deny-list rather than a curated
  runner, so a new test gets target execution by default.
  `irq_lock` was the sharpest gap: `docs/SCOPE.md` calls it the correct lock
  between an ISR and the main loop, which is a claim about *behaviour*, and
  nothing executed it. It now fires a real SysTick interrupt and observes that
  the handler does **not** run while the lock is held — after first checking that
  it *does* run when the lock is not held, so the result cannot be satisfied by an
  interrupt that never fired. Also covered: ticks resume after unlock,
  `guarded<>` holds the mask across a whole body, and an inner release does not
  unmask while an outer lock is held (the blanket-`cpsie i` bug the save/restore
  design exists to prevent).
  **Finding:** `metl::atomic_ref<T>` with an 8-byte `T` does not **link** on
  ARMv7-M — it lowers to `__atomic_load_8`/`__atomic_store_8` and bare-metal
  toolchains ship no libatomic. Not a compile error, an undefined reference, so
  the header could not have shown it. `atomic_ref.hpp` now warns.
  **Also corrected:** `lock_test` asserted `!has_irq_masking` unconditionally —
  true on a host, wrong on Cortex-M. The target run exposed it.
  M3/M4/M7 run 66 tests each. The **Cortex-M0 leg additionally asserts the
  capability gates**: `mpmc_queue`, `atomic_handle` and the atomic-refcounted
  `intrusive_ptr` must *fail to compile* there, and the job fails if one of them
  builds — a gate that cannot be observed rejecting is not a gate. (M0 runs an
  ARMv6-M build on an ARMv7-M core, since QEMU has no M0 MPS2 board; that proves
  the M0 *build* runs, not that an M0 *core* runs it, and `irq_masking_test`
  skips itself there because ARMv6-M has no VTOR rather than passing falsely.)

### Fixed

- **`metl::intrusive_ref_counter<Derived, refcount_kind::atomic>` now fails with
  a `static_assert` instead of a link error on ARMv6-M.** Cortex-M0/M0+ has no
  lock-free read-modify-write, so GCC emits `__atomic_fetch_add_4` /
  `__atomic_fetch_sub_4` and a bare-metal toolchain ships no libatomic. M3 and up
  inline it with `LDREX`/`STREX`, which is why nothing had noticed. What a user
  got was `undefined reference to __atomic_fetch_add_4` pointing into a mangled
  symbol inside `intrusive_ptr::reset` — true and useless. The assert now names
  the constraint and the alternative: `refcount_kind::non_atomic` plus
  `metl::guarded<..., metl::irq_lock>` around ISR-shared access, because on a
  single-core MCU masking interrupts is what makes that safe, not an atomic
  counter. Found by the Cortex-M0 conformance leg.

### Changed

- **CRC-8/16/32 now use a nibble table by default (`METL_CRC_TABLE`).** The
  benchmark added in #26 measured `crc32` at roughly **8 ns per byte** — a
  bit-at-a-time loop, eight shift-and-conditional-xor steps per byte. Two lookups
  from a 16-entry table replace them.
  **Measured, arm64 host:** 1 KiB of `crc32` went 7.68 µs → 4.42 µs, i.e.
  **1.74×** (less than the 4× the step count suggests — a data-dependent load is
  worth less against an out-of-order core's shift/xor chain than it is on an
  in-order MCU, where the win should be larger; that expectation is *not*
  measured here). Code size: **+64 bytes, exactly the table** — the surrounding
  code shrank enough to offset the rest (84 → 148 bytes for a `crc32` TU at
  `-Os`). Per width the table costs 16 bytes (CRC-8), 32 (CRC-16), 64 (CRC-32),
  and only for widths a program actually uses.
  Set `-DMETL_CRC_TABLE=0` to restore the previous table-free implementation on a
  flash-starved target. **Results are byte-for-byte identical either way**, and a
  new test pins that rather than assuming it: it walks every input byte against a
  wide spread of starting register values for all three widths, comparing against
  a bitwise reference held in the test (duplicated deliberately — calling into the
  library would let a change that broke both paths alike pass). A 256-entry byte
  table would be faster again but costs 1 KiB for CRC-32 alone, a poor trade on
  the parts this library targets; not offered until someone has the use case.

### Added

- **`metl::mpmc_queue` — bounded lock-free multi-producer / multi-consumer queue
  (Tier 1).** Sequence-number scheme (Vyukov): each slot carries a counter saying
  whose turn it is, so producers and consumers can distinguish "ready for me"
  from "someone got there first" with a plain single-word CAS — no double-width
  CAS, and no ABA window, because the counters only move forward.
  **Capability-gated** on `std::atomic<std::size_t>::is_always_lock_free`;
  ARMv6-M (Cortex-M0/M0+) has no CAS, so the `static_assert` fires rather than
  degrading to a lock. The fallback SCOPE.md promised for that target is now a
  real type: `guarded<static_message_queue<T, N>, irq_lock>`, from the previous
  release entry. The capability probe and the `handle-atomics` CI job were
  extended to cover it — a target where handle atomics were lock-free but
  `size_t`'s were not would otherwise only surface at a user's build.
  **Progress guarantee: lock-free, not wait-free** — a thread can lose the
  compare-exchange arbitrarily often — which under `docs/SCOPE.md` §1 restricts
  it to multi-core use and rules out ISR↔main-loop sharing on a single core.
  **It does not scale with thread count; it degrades.** All producers contend on
  one counter and all consumers on another, so more threads means more contention
  on a single word, not more parallelism. Measured (arm64 host, one machine):
  47 Mops/s at 1×1, 6.8 at 2×2, 3.5 at 4×4; uncontended round trip ~7.5 ns
  against ~5.6 ns for `spsc_queue`. The header says plainly to **prefer
  `spsc_queue` when the roles are fixed**, and `bench/bench_mpmc.cpp` is there to
  re-check the claim.

- **Micro-benchmarks (`bench/`) + a `bench-smoke` CI job.** `metl_cc_benchmark`
  had never built anything: it `return()`ed silently whenever
  `benchmark::benchmark` was absent, which was always. It now builds for real,
  and three suites ship with it — containers and lookup paths, `object_pool` vs
  `handle_pool`, and `spsc_queue` throughput including the two-thread case.
  **Deviation from `docs/TODO.md`:** built on a dependency-free in-repo harness
  (`bench/metl_bench.hpp`) rather than google/benchmark, which would have been
  this repo's first external dependency and would cost CI a network fetch plus a
  framework build — the same call already made in choosing `tests/metl_check.hpp`
  over gtest. The harness auto-tunes the iteration count, then reports the
  **median of N repetitions with the min/max spread**, because a single number
  from a single run invites conclusions the measurement cannot support.
  The CI job runs everything with `--quick` and is explicitly **not** a
  performance gate: a threshold on a shared runner either fires spuriously or is
  loose enough never to fire.
  **First finding:** `handle_pool` is not universally faster than `object_pool`.
  At capacity 4 the linear scan wins (2.8 ns vs 4.4 ns); the gap reverses by
  capacity 64 (24.9 ns vs 4.2 ns) and widens at 1024 (37.8 ns vs 5.6 ns).
  Handle resolution — the use-after-free check — costs about 0.9 ns. The table is
  now in the `handle_pool` header so the choice is made on the property needed
  rather than on asymptotics alone.

- **`metl/lock.hpp` — `irq_lock`, `null_lock`, `scoped_lock`, `guarded<T, Lock>`.**
  The roadmap called for retrofitting a lock policy onto the existing concurrency
  types; that was rejected on implementation and the reasoning is recorded in
  `docs/SCOPE.md` §6. **A container that locks each operation cannot make a
  compound operation atomic** — `if (!q.full()) q.push(x)` is still a race when
  both calls lock individually — so per-operation locking buys the *appearance*
  of thread safety at the price of a hidden cost in every call and a doubled API
  surface on every container. The standard library, Abseil and Folly all leave
  containers unsynchronised and offer a wrapper instead.
  `guarded<T, Lock>` is that wrapper: it owns the value and hands it out only
  inside a critical section, so one lock spans exactly as many operations as the
  invariant requires. There is no `get()` — an escape hatch would make it
  decorative. **No existing type changed.**
  `irq_lock` masks interrupts by **saving and restoring `PRIMASK`**, never by
  blanket-enabling: an `unlock()` that simply enabled interrupts would re-enable
  them inside a caller's own critical section that had deliberately disabled
  them, a classic and hard-to-find embedded bug. The saved state travels with the
  guard rather than the lock, so critical sections nest and the policy costs zero
  bytes (`sizeof(guarded<T, Lock>) == sizeof(T)`, pinned by a test and a
  freestanding `static_assert`). Real on ARM Cortex-M; elsewhere it degrades to a
  compiler barrier and provides **no** mutual exclusion — check
  `metl::has_irq_masking` / `METL_HAS_IRQ_MASKING`. The PRIMASK inline asm was
  verified to assemble on ARMv6-M, ARMv7-M and ARMv7E-M, and the freestanding
  smoke TU takes its address so `arm-cross` assembles it for real.
  **No `spin_lock`**, deliberately: on a single-core target a spinlock between an
  ISR and the main loop always deadlocks, and single- versus multi-core is not
  detectable at compile time, so — unlike every other capability here — misuse
  could not be turned into a compile error.

### Changed

- **`spsc_queue` caches the other side's index (DPDK/Folly-style).** Each side
  kept reading the other's atomic index on every operation, so every push touched
  the consumer's cache line and every pop touched the producer's — the classic
  ping-pong the cache-line alignment was already trying to avoid. Now the
  producer decides from a private copy of the consumer index and reloads the real
  one only when that copy says "full" (and mirror-image for the consumer). The
  copy is stale-but-conservative, so it can only ever be pessimistic; semantics
  are unchanged. **Measured 1.7×–4.9× throughput (median ~2.4×)** across six
  interleaved runs of a two-thread benchmark, and **zero size cost**:
  `sizeof(spsc_queue<uint32_t, 16>)` is 192 bytes before and after, because the
  cached copies fit in padding the cache-line alignment had already created — so
  it is free on an MCU too, not merely harmless. A dedicated regression test
  covers the refresh paths at the full/empty boundaries and across the ring wrap;
  it was mutation-checked (dropping either refresh, or an off-by-one in either
  authoritative check, all fail it).

### Added

- **`metl/wait.hpp` — `cpu_relax()`, `wait_for_event()`, `send_event()`.** Two
  different things, kept apart on purpose. `cpu_relax()` is a hint *inside a spin
  loop* (`PAUSE` on x86, `YIELD` on ARM, Zihintpause `PAUSE` on RISC-V, compiler
  barrier elsewhere); it never sleeps. `wait_for_event()` **stops the core**
  until an event arrives (ARM `WFE`, woken by an interrupt or another core's
  `SEV`), which is the real idle idiom on a microcontroller — a Cortex-M spinning
  on `cpu_relax()` draws the same current as a busy loop, because the `YIELD`
  ARMv7-M defines as a spin hint is permitted to be, and in practice is, a `NOP`.
  Giving both behaviours one name would be wrong on every target, so the caller
  chooses. Instruction emission was verified per target rather than assumed
  (`yield`/`wfe`/`sev` on ARM, `pause` on x86-64), and the freestanding smoke TU
  takes their addresses so the inline asm is actually assembled for Cortex-M.
  Documented limits: `wait_for_event()` must be called in a condition re-checking
  loop (it can return spuriously, and the event register is sticky), its progress
  guarantee is *blocking, bounded only if a wakeup is guaranteed*, and it must not
  be used inside an ISR on a single-core target.
- **`metl::compiler_barrier()`** in `register_access.hpp`, alongside the existing
  hardware fences: stops the optimizer reordering across a point while emitting
  no instruction. The right tool between a core and an ISR or signal handler on
  that same core; explicitly documented as **not** sufficient across cores or
  against DMA/peripherals.
- **`METL_PREFETCH(addr)`** in `optimization.hpp` — a pure hint that never faults
  (including on a null address, which the test pins) and collapses to nothing
  where the compiler has no prefetch builtin.

- **`metl::atomic_handle` — lock-free atomic cell for a `versioned_handle`
  (Tier 1).** This is the payoff of packing `{index, generation}` into one word:
  a lock-free free-list needs its head to carry a counter so a compare-exchange
  cannot be fooled by a slot freed and re-allocated in the interim (ABA), and the
  usual answers are a double-width CAS (64-bit only, `cmpxchg16b`/`CASP`) or
  stuffing a counter into a pointer's spare bits (breaks under AArch64 PAC/MTE,
  x86-64 LA57/LAM). A handle needs neither — the counter is already in the word,
  and the word is 32 bits, so a **plain single-word CAS is ABA-safe on a 32-bit
  MCU**. Provides `load`/`store`/`exchange`/`compare_exchange_{weak,strong}` with
  explicit memory orders; each operation is wait-free and bounded, while a
  caller's retry loop is lock-free (which is why it must not be used for
  ISR↔main-loop synchronisation on a single core — mask interrupts there).
  **Capability-gated:** instantiating requires `is_always_lock_free` for the
  packed type, i.e. a hardware CAS. ARMv7-M and up have `LDREX`/`STREX`; ARMv6-M
  (Cortex-M0/M0+) has none, so the `static_assert` fires instead of silently
  degrading to a lock — a lock-free algorithm that quietly becomes lock-based has
  different progress guarantees, and METL states progress guarantees. Branch on
  `metl::has_lock_free_handle_atomic_v<Handle>` at compile time. Including the
  header is safe on every target; only instantiation is gated.
  The new **`handle-atomics` CI job** keeps that claim honest per
  `docs/SCOPE.md` §3: for Cortex-M0/M3/M4/M7 it asserts the capability matches
  what the matrix declares, *and* that the opposite expectation fails to
  compile — so a toolchain change that moves a target across the line fails the
  build rather than changing METL's progress guarantees silently.

- **`metl::versioned_handle` + `metl::handle_pool` — generation-tagged slot
  handles.** A `versioned_handle` is `{index, generation}` packed into one
  unsigned integer (32 bits by default: 16-bit index + 16-bit generation),
  trivially copyable, with every operation wait-free and bounded.
  `handle_pool<T, Capacity>` is the pool that issues and validates them, and it
  improves on `object_pool` in two ways that fall straight out of dropping the
  raw pointer: **use-after-free is detected rather than undefined** (a stale
  handle resolves to `nullptr`, and a double destroy returns `false`), and
  **allocation is O(1)** via an intrusive index free-list where
  `object_pool::try_emplace` linearly scans. Handles are tagged with the pool
  type, so mixing handles between pools is a compile error.
  *Why a handle and not a tagged pointer:* alignment tagging yields ~3 bits,
  which is a flag and not a counter; upper-bit packing yields ~16 but assumes the
  top of a pointer belongs to software, which is decreasingly true (AArch64 PAC
  signs the upper bits, MTE claims 56–59, x86-64 LA57 leaves seven, Intel LAM /
  AMD UAI give them hardware semantics, HWASAN uses the top byte). A handle
  assumes nothing about pointer representation, carries a full-width generation
  counter, and is small enough that the atomic form fits a single-word CAS on a
  32-bit MCU. Documented limit: generations repeat after `2^(8*sizeof(GenT))-1`
  destroy cycles on the same slot (65535 with the default counter); widen `GenT`
  when handles are held for unbounded periods.
  The atomic form is a follow-up — it requires a single-word CAS, which
  Cortex-M0 lacks, making it Tier 1 under `docs/SCOPE.md` and therefore due to
  arrive with its own capability trait and CI job.

- **`docs/SCOPE.md` — invariant-based scope policy + roadmap.** METL's inclusion
  test is now stated as five invariants (no heap; no exceptions/RTTI;
  deterministic, i.e. bounded worst-case; header-only C++17; self-contained
  headers) rather than a target list: keep all five and a contribution is in
  scope whatever the topic, break one and it is out however embedded it sounds
  (a heap-fallback `small_vector` is the canonical permanent rejection). Adds a
  progress-guarantee vocabulary for the determinism invariant (wait-free bounded
  / lock-free / blocking bounded; blocking-unbounded is not acceptable), the
  Tier 0/1/2 definitions with the rule **a tier and its CI job arrive in the same
  PR**, an experimental area (`metl::exp::` / `metl/exp/`, outside the umbrella
  and outside the stability promise), and the roadmap.
- **`invariants` CI job + `tools/check_invariants.py` — machine-checked no-heap /
  no-exceptions / no-RTTI gate.** Links a stdio-free probe
  (`tests/embedded/invariant_probe.cpp`) for Cortex-M0/M3/M4/M7 against
  newlib-nano and audits the resulting image's **symbol table** with `nm`; any
  `malloc`/`free`/`sbrk`/`operator new`/`__cxa_throw`/`_Unwind_*`/RTTI symbol
  fails the build. A symbol audit rather than linker poisoning, because the
  poisoned-`operator new` trick depends on `--gc-sections` behaviour and answers
  differently under GNU ld, lld and IAR — and because reading the final symbol
  table proves all three properties in one pass. The image is linked but never
  executed, so the gate needs no emulator or semihosting (and must not use
  `--specs=rdimon.specs`, whose crt0 sets up the heap via `SYS_HEAPINFO`).
  `tests/embedded/invariant_canary.cpp` is a mandatory negative control that must
  fail the audit — a gate that cannot fail is not a gate.
  **First finding, worth knowing if you target newlib:** `METL_ASSERT` /
  `METL_HARDEN` end in `std::abort()` unconditionally, newlib's `abort()` calls
  `raise()`, and newlib's signal machinery allocates its handler table with
  `_malloc_r` — so on newlib, any image containing a METL assert transitively
  links `malloc` and `_sbrk`. This is a property of newlib rather than of METL
  (the probe's object file references nothing but `abort`), and the fix is the
  one bare-metal users already apply: supply your own `abort()` alongside `_exit`
  and the other stubs, as `tests/embedded/invariant_probe.cpp` now does.

- **`METL_HARDENING` runtime-check levels + always-on `METL_HARDEN` floor.** A
  consumer-controllable hardening knob modeled on libc++'s
  `_LIBCPP_HARDENING_MODE` and Abseil's `ABSL_HARDENING_ASSERT`:
  `METL_HARDENING` ∈ `NONE` (0) / `FAST` (1) / `DEBUG` (2), defaulting to `DEBUG`
  for debug builds (`METL_DEBUG` or `!NDEBUG`) and `FAST` for release, overridable
  via `-DMETL_HARDENING=<0|1|2>` independent of the consumer's own `NDEBUG`.
  **metl stays checked-by-default**: ordinary preconditions (`METL_ASSERT` —
  bounds, non-empty, capacity, allocator overflow) remain on in release and are
  compiled out only at `NONE`; `NONE` is an explicit opt-in for maximum
  performance, not the default. The new **`METL_HARDEN`** macro is a security
  floor that is *never* stripped (even at `NONE`) — reserved for defense-in-depth
  guards whose failure would be a wild out-of-bounds write. `METL_DASSERT`
  remains the debug-only tier for genuinely expensive checks. **ODR note:**
  `METL_HARDENING` must be uniform across all TUs of a program (same constraint as
  `NDEBUG`/`_LIBCPP_HARDENING_MODE`); documented at `config.hpp`. See
  `docs/AUDIT.md` Section E.
- **`metl_cc_test(... INCLUDES ...)`** — the Bazel-style test rule now accepts an
  `INCLUDES` attribute (mirroring `metl_cc_library`), used to put the shared
  `tests/metl_check.hpp` helper on every test's include path after the test-suite
  reorganization.
- **New regression / hardening tests.** `variant_selfassign`
  (converting-assign self-aliasing + in-place assignment), `arena_throwing_ctor`
  (exception-safety of the destructor record), `object_pool_foreign_ptr`
  (unrelated-pointer membership test), `harden_floor_none` (a forked death test
  proving `METL_HARDEN` still aborts at `METL_HARDENING_NONE`), and
  `hardening_{none,fast,debug}` (each pins a level and checks which of
  `METL_ASSERT`/`METL_DASSERT`/`METL_HARDEN` fire).
- **Fuzzing harnesses (libFuzzer, ASan+UBSan) + a blocking CI fuzz-smoke job.**
  Five `LLVMFuzzerTestOneInput` harnesses under `fuzz/`
  (`fuzz_fixed_string`, `fuzz_flat_map`, `fuzz_static_unordered_map`,
  `fuzz_allocators`, `fuzz_crc`) drive the targets with the fuzz bytes and are
  built with `-fsanitize=fuzzer,address,undefined` behind a new Clang-only,
  default-OFF CMake option `METL_BUILD_FUZZERS` (a non-clang or option-off
  configure is unaffected; default host/arm/sanitizer builds are untouched).
  Crucially, the harnesses respect metl's assert-based **contract**: metl
  containers abort on precondition violations (push past capacity, pop empty,
  OOB index), so the harnesses perform **only contract-valid operations** —
  `try_*` variants, `size()/capacity()` checks before any asserting call, and
  `% size()`-bounded indices — treating the input as an opcode stream. This
  makes any ASan/UBSan finding (heap/stack OOB, UB, use-after-poison, leak,
  uninitialized read) a genuine defect rather than a by-design abort.
  `fuzz_crc` also differential-checks overload agreement and the
  streaming/resumability property (a mismatch would be a real CRC bug). A new
  blocking `fuzz-smoke` CI job (`needs: preflight`) builds the harnesses and
  runs each for a bounded time (`-max_total_time=25`) against a tiny seed corpus
  under `fuzz/corpus/`, failing on any crash/leak/timeout. No library defect was
  found (200k+ runs per target clean); one over-strict harness invariant
  (`fixed_string` `strlen == size`, false by design for embedded NULs) was fixed
  during bring-up to `strlen <= size`. See `docs/AUDIT.md` Section C.
- **ClusterFuzzLite continuous fuzzing (OSS-Fuzz tech, no upstream
  registration).** `.clusterfuzzlite/` (`Dockerfile`, `build.sh`, `project.yaml`)
  plus non-blocking `cflite-pr` (per-PR, code-change mode) and `cflite-batch`
  (scheduled) GitHub Actions workflows run the OSS-Fuzz toolchain directly in
  CI. The `build.sh`/`Dockerfile` are OSS-Fuzz-compatible, so upstream
  google/oss-fuzz registration remains a drop-in follow-up (tracked in
  `docs/TODO.md`). These workflows are `continue-on-error` (non-blocking); the
  blocking, always-green memory-safety gate is the in-repo `fuzz-smoke` job.
- **`SECURITY.md` — vulnerability disclosure policy.** How to report privately
  (GitHub security advisory), supported versions, response expectations, and a
  clear statement that an abort from a documented precondition violation is
  contractually correct (use `try_*`) — while a memory-safety failure reachable
  through a contract-valid API is a reportable security issue.
- **Per-symbol Doxygen API documentation across all public headers.** Every
  public class/struct in `include/metl/` (plus `coro/` and the public-facing
  `detail/construct.hpp`) now carries a `///` brief and a short contract note
  (fixed capacity, no heap allocation, thread-safety where relevant); key
  members and free functions document `@param`/`@tparam`/`@return`/`@pre` where
  non-obvious. The non-standard, easy-to-trip contracts are surfaced with
  `@warning`/`@note` at the symbols themselves: `at()`/`value()`/`error()` and
  `variant` `get<>()`/`visit()` **assert (abort by default), they do not throw**
  the corresponding `std::` exceptions; `flat_map`/`flat_set`
  `operator[]`/`at()` are **positional index accessors, not key lookup** (use
  `find()`/`nth()`); `function_ref` **rejects rvalue callables** (dangling
  prevention); the failed-assert/`panic` path is **`[[noreturn]]`** even with a
  user handler; `static_message_queue` is **single-threaded / not ISR-safe**
  (use `spsc_queue`); fixed-capacity overflow asserts while the `try_*` variants
  return `false`. Additive only — no API, signature, or behavior change; the
  strict `docs` job (`WARN_AS_ERROR=FAIL_ON_WARNINGS`) builds clean.
- **Zephyr module support.** metl is now consumable as a header-only
  [Zephyr module](https://docs.zephyrproject.org/latest/develop/modules.html):
  `zephyr/module.yml` (manifest) plus a minimal `zephyr/CMakeLists.txt` +
  `zephyr/Kconfig` shim expose metl's `include/` to Zephyr applications
  (interface-only — no sources compiled into the RTOS image). Applications opt
  in with `CONFIG_METL=y`. A runnable sample lives under
  `samples/zephyr/metl_hello/` (`CMakeLists.txt`, `prj.conf`, `src/main.cpp`,
  `sample.yaml`) exercising `fixed_vector` + `expected` + `span` on
  `qemu_cortex_m3` / `native_sim`. A new `zephyr` CI job builds the sample in
  the official Zephyr CI Docker image and runs it under QEMU via twister,
  asserting a success sentinel. See the README "Zephyr" section.
- **ESP-IDF component support (ESP32).** metl is now consumable as a header-only
  [ESP-IDF component](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html):
  `components/metl/CMakeLists.txt` registers metl's `include/` via
  `idf_component_register(INCLUDE_DIRS ...)` with no `SRCS` (interface-only —
  nothing compiled into the image), and `components/metl/idf_component.yml` is a
  manifest for the IDF Component Manager. A consumer declares `REQUIRES metl` to
  get `<metl/...>` on its include path. A runnable sample lives under
  `samples/esp-idf/metl_hello/` (top-level `CMakeLists.txt` wiring
  `EXTRA_COMPONENT_DIRS` → this repo's `components/`, `main/CMakeLists.txt`
  with `REQUIRES metl`, and `main/main.cpp`) exercising `fixed_vector` +
  `expected` + `span` in `app_main()`. A new **non-blocking** `esp-idf` CI job
  (`continue-on-error: true`, matching the `zephyr` pattern) uses the official
  `espressif/esp-idf-ci-action` to `idf.py build` the sample for **`esp32`
  (Xtensa)** and **`esp32c3` (RISC-V)** in the pinned `espressif/idf:v5.3.3`
  Docker image — the Xtensa target adds a compiler frontend not covered by any
  other job. Provisional (non-blocking) until the Docker build wiring is
  validated green. See the README "ESP-IDF (ESP32)" and "Platform support
  matrix" sections.
- **Platform support matrix (README).** New "Platform support matrix" section
  enumerating CI-verified coverage (host gcc/clang/MSVC × Debug/Release/
  MinSizeRel/LTO, ASan/UBSan/TSan, ARM Cortex-M gcc+clang, RISC-V rv64, Xtensa
  ESP32 [provisional], PowerPC64 big-endian run, newlib-nano link, picolibc +
  QEMU run, Zephyr module [provisional], ESP-IDF component [provisional]) versus
  documented-only toolchains that are not automatically verified (IAR EWARM —
  proprietary, no free public CI; ARM Compiler 6 — LLVM-based, partially proxied
  by the `arm-cross-clang` job).
- **Portable attribute layer (`metl/attributes.hpp`, abseil `attributes.h`
  style):** `__has_cpp_attribute`/`__has_attribute`-gated macros with empty
  fallbacks, so applying one is always safe (honored or a no-op). Consolidates
  `METL_NODISCARD` and adds `METL_NORETURN`, `METL_ALWAYS_INLINE`,
  `METL_MAYBE_UNUSED`, `METL_DEPRECATED(msg)`, `METL_LIFETIME_BOUND`
  (`[[clang::lifetimebound]]`), `METL_CONST_INIT`
  (`constinit` / `[[clang::require_constant_initialization]]`), and
  `METL_ATTRIBUTE_TRIVIAL_ABI` (`[[clang::trivial_abi]]`). `compiler.hpp`
  includes it so every current include site keeps `METL_NODISCARD`.
- **Optimization hints (`metl/optimization.hpp`, abseil `optimization.h`
  style):** `METL_PREDICT_TRUE`/`METL_PREDICT_FALSE` (`__builtin_expect`,
  identity fallback), `METL_ASSUME(cond)`, `METL_CACHELINE_SIZE`, and
  `METL_CACHELINE_ALIGNED` (portable `alignas`).
- **Feature detection (`compiler.hpp`, abseil `config.h` style):**
  `METL_HAVE_BUILTIN(x)`, `METL_HAVE_FEATURE(x)`, `METL_HAVE_INCLUDE(x)` wrappers
  with safe `0` fallbacks so they are always valid in `#if`.
- **`METL_DASSERT`** — a debug-only assertion (DCHECK) alongside the always-on,
  hardened `METL_ASSERT`. Active when `!NDEBUG` or `METL_DEBUG`; otherwise
  evaluates its expression only in an unevaluated context (no side effects, no
  unused warnings). `METL_ASSERT` is never downgraded to `METL_DASSERT`.

### Changed

- **Anti-pattern cleanup (follow-up).** Removed the duplicate feature-detection
  macro `METL_HAVE_BUILTIN` (a byte-for-byte copy of `METL_HAS_BUILTIN`); its one
  use now spells `METL_HAS_BUILTIN`. `metl::aligned_storage_t<T>` now aliases
  metl's own `storage_for<T>` instead of the C++23-deprecated
  `std::aligned_storage`. CI: the Zephyr `west build` structural gate is now
  **blocking** (was entirely non-blocking); only the QEMU twister *run* keeps a
  step-level `continue-on-error` for harness flakiness.
- **Test suite grouped into subdirectories.** `tests/*_test.cpp` moved into
  `tests/{containers,memory,sync,vocab,bits,control,core}/` for navigation; the
  freestanding `embedded_smoke.cpp` moved under `tests/embedded/`. Registration
  stays an explicit list (not a glob) to match the Bazel-style `metl_cc_*` rules —
  sources are enumerated, reviewable, and reproducible. No test was dropped
  (`git mv` preserves history).
- **CI robustness (follow-up to the reorg).** The freestanding cross jobs that
  compile test sources directly (outside CMake) now reference them through a
  single `env:` source of truth instead of hard-coded paths; a workflow-level
  `defaults.run.shell: bash` fixes container jobs where dash rejected
  `set -o pipefail`; the blocking `fuzz-smoke` gate derives its harness list from
  the built binaries (no longer a hand-maintained list that could silently skip a
  new fuzzer); and a `.pre-commit-config.yaml` pins clang-format 18.1.8 so local
  formatting matches CI. Deeper CI cleanups (cross-job matrix dedup, caching,
  action SHA pinning) are tracked in `docs/TODO.md`.
- **optional — genuine `constexpr` on C++20:** `metl::optional` now stores its
  value in a union (the active member is named directly, no `std::launder`) and
  routes its object lifetime through the new `metl::detail::construct_at` /
  `destroy_at` helpers (`metl/detail/construct.hpp`), which forward to
  `std::construct_at` / `std::destroy_at` (constant-evaluable since C++20) and
  fall back to placement-new / explicit destruction on C++17. As a result
  `constexpr metl::optional<int> o{42}; static_assert(*o == 42);` is a real
  constant expression on a C++20 toolchain. On C++17 behavior, size, and
  alignment are unchanged. The `const`-qualified `value()` overloads are now
  `constexpr` too. The remaining laundered-storage types
  (`expected`/`variant`/`fixed_vector`/`flat_map`/`flat_set`) carry an honest
  source note that their `constexpr` labels are effective only outside constant
  evaluation; a genuine conversion is deferred (see `docs/AUDIT.md`).
- **function_ref — dropped the `<memory>` include.** It was pulled in solely
  for `std::addressof` in one constructor; replaced with a tiny local
  `metl::detail::function_ref_addressof` (`__builtin_addressof`, universally
  available on the gcc/clang/MSVC matrix, with the standard operator&-defeating
  fallback). Behavior and API are unchanged; the header is lighter to include.
  The `<functional>` includes in `hash`, `optional`, `flat_map`/`flat_set`,
  `static_unordered_map`/`set`, and `intrusive_ptr` are deliberately kept: they
  back `std::hash` / `std::equal_to` / `std::less` that are genuinely required
  (and, for the map/set defaults, are part of the public type), so trimming them
  would change the API rather than just an include.
- **function_ref / span:** the callable/container/array constructors mark the
  bound referent `METL_LIFETIME_BOUND`, so clang (`-Wdangling`) diagnoses a
  view that would outlive its referent at the call site. This complements the
  existing deleted rvalue-binding overloads; valid lvalue usage is unaffected.
- **assert:** the global assert/panic handler storage is now `METL_CONST_INIT`,
  making its constant initialization explicit and static-init-order-safe (and
  rejecting any future change to dynamic initialization).
- **intrusive_ptr:** applies `METL_ATTRIBUTE_TRIVIAL_ABI` so the single-owner
  pointer is passed/returned in a register and destroyed by the callee, matching
  a raw pointer's calling convention. Observable behavior is unchanged (verified
  under ASan/UBSan).
- **spsc_queue:** the per-role cache-line padding now uses
  `METL_CACHELINE_ALIGNED` / `METL_CACHELINE_SIZE` instead of a hand-rolled
  `alignas(64)` (identical layout).
- **METL_ASSERT:** the failed-check branch is marked `METL_PREDICT_FALSE` so the
  success path stays the straight-line case (behavior unchanged).

### Documentation

- Added `docs/COOKBOOK.md` — task-oriented recipes (fixed-capacity vector,
  key/value lookup with `flat_map`, bounded FIFO, error handling without
  exceptions, per-tick scratch allocators, ISR↔main-loop SPSC queue,
  memory-mapped register access, a small FSM, cooperative protothreads). Each
  snippet mirrors a compiled example. Includes an explicit table of the
  non-standard contracts (`at()`/`value()`/`get()` assert instead of throwing;
  `flat_map::operator[]`/`at` are **positional**, not key lookups; the assert
  path is provably `[[noreturn]]`; `function_ref` rejects rvalue callables).
- Added a `docs/Doxyfile.in`, a CMake `docs` target (`cmake --build build
  --target docs`), and a `docs` CI job that generates HTML API docs from
  `include/metl` and fails on malformed doc comments (undocumented symbols are
  tolerated for now).
- README: linked each module family to its worked example, added a
  "Documentation" section (Cookbook + examples table + Doxygen instructions),
  and called out the non-standard contracts inline.

### Examples

- Added one focused, CI-compiled example per module family, each a
  self-contained `main()` returning 0 with in-program self-checks and built
  under `-Wall -Wextra -Werror -std=c++17`:
  `containers.cpp` (`fixed_vector` + `flat_map` + `ring_buffer`),
  `allocators.cpp` (`arena_allocator` + `monotonic_buffer`),
  `spsc_isr.cpp` (`spsc_queue` ISR↔main-loop pattern),
  `mmio_peripheral.cpp` (`mmio` + `register_access` + `bitfield` driving a fake
  peripheral), `error_handling.cpp` (`expected` + `optional` + `variant`), and
  `coroutine_task.cpp` (`coro/protothread`).
- All examples (new and pre-existing) are now wired into the examples CMake as
  CTest smoke tests and built + run by a new `examples` CI job.

### Fixed

- **variant:** the converting `operator=(T&&)` now assigns in place when the
  active alternative already holds the target type, instead of always routing
  through `emplace()` (which `reset()`-destroyed the active alternative before
  reading the source). Fixes a use-after-destruction for a self-aliasing
  assignment such as `v = get<T>(v)`. Distinct from the earlier copy/move-assign
  exception-safety fix.
- **atomic_ref:** added `static_assert(sizeof(std::atomic<T>) == sizeof(T))` so
  the reinterpret-based backport can never read/write past the referenced object.
  (Requiring size-compatibility, not lock-freedom: `std::atomic_ref` works on
  non-lock-free `T` via an external lock pool, and a stricter lock-free assert
  broke size-compatible-but-not-lock-free targets such as Cortex-M0.) Alignment is
  still enforced at construction; `is_always_lock_free` remains exposed.
- **arena_allocator:** `try_emplace` now constructs the object *before*
  registering its destructor record. The previous order left a record pointing at
  unconstructed storage if `T`'s constructor threw, so a later `rewind`/`reset`
  ran `~T()` on raw memory. Also hard-guards a power-of-two `alignment`.
- **object_pool:** `contains`/`index_of` no longer apply relational operators
  (`<`, `>=`) to an unrelated caller-supplied pointer (UB); the containment test
  now compares integer addresses (`uintptr_t`), with no `<functional>` dependency.
- **static_unordered_map / static_unordered_set / arena_allocator:** the
  defense-in-depth guards that prevent a full-table insert (or a bad alignment)
  from becoming a wild out-of-bounds write now use the always-on `METL_HARDEN`
  instead of `METL_ASSERT`, so the security floor holds even at
  `METL_HARDENING_NONE`.
- **endian:** `endian::native` no longer silently assumes little-endian when the
  byte order can't be detected. `__BYTE_ORDER__` (defined by every supported GCC/Clang
  cross target, big-endian included) remains the authoritative signal; a chain of
  well-known secondary macros (`__BIG_ENDIAN__`, `__ARMEB__`, `__AARCH64EB__`,
  `__MIPSEB__`, … and their LE counterparts) is checked next, and if none resolve the
  header stops with an actionable `#error` instead of guessing little-endian (which
  would miscompile `to_/from_*_endian` on an undetected big-endian target). Now
  exercised by a big-endian (`powerpc64`) CI job.
- **assert:** the failed-assert and panic paths are now provably `[[noreturn]]`.
  After invoking the (customizable) handler, `detail::assertion_failed`,
  `detail::panic_failed`, and `panic` unconditionally `std::abort()`, so a
  user-installed handler that incorrectly returns can no longer fall through
  past a failed precondition into undefined behavior. The customization point is
  unchanged (handlers still receive expression/file/line).
- **function_ref:** rvalue callables are rejected instead of silently binding to
  a temporary that dangles at the end of the full-expression. Following
  `std::function_ref` (P0792), the callable constructor is lvalue-only and the
  rvalue overload is deleted; lvalue callables and function pointers are
  unaffected.
- **fixed_string:** the `const char*` constructor now asserts on overflow rather
  than silently producing an empty string. `assign()` remains the recoverable,
  non-asserting path (returns `false` and leaves the string unchanged).
- **coro/scheduler:** `run_once()` is now reentrancy-safe. It snapshots the set
  of tasks attached at entry and polls exactly that set, skipping any task a
  prior poll detached. A task that `detach()`es (itself or another task) or
  attaches a task from within its `poll()` can no longer shift the underlying
  vector out from under the cached index/size (previously a stale/out-of-bounds
  read). Added `is_attached()`.
- **static_unordered_map:** `emplace(key, value)` now finds an existing key
  first. A duplicate-key `emplace` is a no-op returning the existing element
  (std::unordered_map semantics: it does NOT overwrite), instead of
  double-constructing over the live element and incrementing `size_` twice.
  `construct_at` also hard-guards its index so a full-table insert can never
  reach an out-of-bounds `construct_at(npos, …)`.
- **variant / expected:** cross- and same-state assignment (and
  `expected::swap`) are now exception-safe. A throwing copy/move constructor can
  no longer leave a destroyed member paired with an unchanged discriminant
  (which double-destroyed on the next destructor). `variant` marks itself
  valueless across a throwing same-index reconstruct; `expected` uses the
  std::expected "reinit" pattern (construct into a temporary / restore the old
  member on throw).
- **variant:** comparison operators now compare the active alternative *by
  index* rather than by `get<T>`, so they compile and work for a `variant` with
  duplicate alternative types.
- **mmio:** `mmio_register<T, Address>` static_asserts `Address` is aligned to
  `alignof(T)`, and `mmio_ptr<T>(uintptr_t)` asserts alignment at runtime; a
  misaligned volatile access is undefined behavior. The `mmio_ptr(uintptr_t)`
  constructor is no longer `constexpr` — its only path is an integer→pointer
  `reinterpret_cast`, so a `constexpr` qualifier there was ill-formed (IFNDR).
- **arena_allocator / static_allocator:** size math is now overflow-safe. A huge
  byte/element request that would wrap `size_type` before the bounds check is
  rejected (`nullptr`) instead of overrunning the buffer.
- **hash:** `fnv1a_hash`'s default (raw-object-representation) overload is now
  constrained via `static_assert` to types with
  `has_unique_object_representations`, so it can't silently break the
  hash/equality invariant for padded types, pointers, or floating point.
- **fixed_function / fixed_any_invocable:** the type-erased callable is stored in
  a `mutable` buffer, so the `const` `operator()` invoking a mutable target is
  well-defined rather than mutating a `const` subobject through `const_cast`.
- **intrusive_ptr:** `intrusive_ref_counter<Derived>` now `static_assert`s that
  `Derived` is `final` or has a virtual destructor, since reference-count
  release destroys the object through `Derived`. This makes the "Derived must be
  the most-derived type" contract explicit and rejects the silently-sliced
  deeper-non-virtual-hierarchy case.
- **fsm:** `dispatch()` commits the new state *before* running the transition
  action, so an action that reentrantly dispatches observes the new state and
  can no longer re-fire the transition in progress.

### Changed

- **flat_map / flat_set:** documented that `operator[]` and `at()` are
  **positional** (index into sorted order), NOT key lookups — the opposite of
  `std::map`/`std::set`. Added an explicit `nth()` positional accessor as a
  self-documenting alias; key access remains via `find()` / `contains()` /
  `lower_bound()`. Signatures are unchanged (no API break).
- **static_message_queue:** documented that it is a single-threaded FIFO with
  plain (non-atomic) indices — NOT concurrent and NOT ISR-safe. Use
  `spsc_queue` for interrupt↔main-loop hand-off.

### Testing

- **Release coverage hole closed:** `atomic_ref_test` and `register_access_test`
  used bare `assert()` on locals that were consumed *only* by the assert. Under
  NDEBUG (Release) the asserts — and therefore those checks — compiled out, both
  hiding a real coverage gap and tripping `-Wunused-variable` under `-Werror`.
  Both are migrated to `CHECK` / `CHECK_EQ` from `tests/metl_check.hpp`, so the
  assertions now run in Release too and the variables are always used.
- Added `tests/optional_constexpr_test.cpp`, which proves `metl::optional` is
  constant-evaluable on C++20 (`static_assert`s guarded by
  `#if __cplusplus >= 202002L`, a no-op on the C++17 matrix) and runs a runtime
  smoke everywhere.
- Added `tests/metl_check.hpp` providing `CHECK` / `CHECK_EQ`, which report
  `file:line` and the offending values on failure instead of only an exit code.
  `fixed_vector_test` and `optional_test` migrated as a demonstration.
- Added `tests/spsc_queue_threaded_test.cpp`, a bounded, deterministic
  multi-threaded producer/consumer test for `spsc_queue` and a concurrent
  `atomic_ref` counter, so the ThreadSanitizer CI job validates real
  concurrency.
- Added focused correctness regression tests (all using `CHECK`/`CHECK_EQ`) for
  the Section A fixes: `coro_scheduler_reentrancy`, `static_unordered_map_emplace`,
  `variant_regression`, `expected_regression`, `mmio_regression`,
  `allocator_overflow`, `fixed_function_const`, `fsm_reentrancy`,
  `hash_unique_repr`, and `intrusive_ptr_contract`. Several exercise the fixed
  paths under ASan/UBSan (memory safety) and throwing-constructor rollback.
- Added `tests/fixed_vector_asan_test.cpp`. Under AddressSanitizer it asserts
  the poison boundaries are exact (live elements addressable, tail poisoned, the
  boundary tracking `push_back`/`pop_back`) and that a real read into the
  poisoned tail is trapped (a forked child performs the OOB and is killed). It is
  a trivial pass in non-ASan configurations.

### Build

- **arm-cross code-size visibility** — the `arm-cross` job now also does an
  explicit `MinSizeRel` (`-Os`, NDEBUG) build of the embedded smoke library and
  prints `arm-none-eabi-size` for each Cortex-M target, so per-build binary size
  is visible in the log. Informational only (no hard threshold yet) — the number
  to watch for size regressions.
- **`release-werror` CI job** — builds and tests in `Release`
  (`-DCMAKE_BUILD_TYPE=Release -DMETL_WARNINGS_AS_ERRORS=ON`) with both gcc and
  clang, gating warning hygiene on the optimized/NDEBUG surface that the
  Debug-only sanitizer/arm jobs never see (e.g. asserts and their operands
  compiling out). Added only after the assert-only-variable tests were migrated
  to `CHECK`/`CHECK_EQ`, so it lands green.
- **Embedded / environment validation CI matrix** (mirrors the existing
  `arm-cross` job; existing jobs unchanged):
  - `cmake/riscv-none-elf.cmake` toolchain (analogue of `arm-none-eabi.cmake`) and a
    `riscv-cross` job building the freestanding embedded-smoke library with the
    bare-metal RISC-V GNU toolchain for `rv32imac` (ilp32) and `rv64` (lp64), selected
    via the new `METL_RISCV_ARCH` option. Compile + size only.
  - `arm-cross-clang` job — a second compiler frontend (clang, `--target=arm-none-eabi
    -mcpu=cortex-m4 -ffreestanding -fsyntax-only`) validates the public headers for
    bare-metal ARM, reusing `gcc-arm-none-eabi`'s freestanding libstdc++ headers.
  - `big-endian` job — `powerpc64-linux-gnu` syntax-checks every header big-endian and
    builds + runs `endian_test` under `qemu-user`, exercising `endian.hpp` on a real
    big-endian target.
  - `newlib-link` job — links `tests/embedded/semihost_smoke.cpp` against newlib-nano
    (`--specs=nano.specs --specs=nosys.specs`) for Cortex-M3, proving metl links
    against a real bare-metal libc (link + size, no run).
  - `picolibc-qemu` job — links the same program against picolibc with semihosting
    (`tests/embedded/mps2-an385.ld`) and runs it under `qemu-system-arm -semihosting`,
    asserting a success sentinel — proving metl links *and runs* on a real embedded
    libc. (picolibc + libstdc++ linking is version-sensitive; the `newlib-link` job is
    the guaranteed-green libc-link fallback.)
- Added per-header self-containment and umbrella-completeness checks: the
  `metl_header_self_contained` target compiles one translation unit per public
  header, `cmake/CheckUmbrella.cmake` verifies `metl.hpp` includes every other
  header, both registered with CTest, plus a `header-checks` CI job.
- **fixed_vector:** under AddressSanitizer, the unused-capacity tail
  `[size(), capacity())` of the inline buffer is poisoned (à la
  `absl::InlinedVector`) so an out-of-bounds access past `size()` is trapped even
  though the whole buffer is one object. Mutating operations unpoison the buffer
  while they rearrange elements and re-poison the tail on exit; the destructor
  unpoisons everything so no stale poison outlives the storage. Fully gated on
  ASan detection — a no-op (and still `constexpr`-constructible) otherwise.
- New public headers `metl/attributes.hpp` and `metl/optimization.hpp` added to
  the umbrella, the installed header set, and the self-containment checks. The
  Doxyfile `PREDEFINED` list strips the new attribute macros.

## [0.1.0-alpha1]

Initial pre-alpha snapshot. The library is feature-incomplete and the public
API is subject to change before the 1.0 release.

### Added

Core utilities:

- `metl::span` — non-owning view over a contiguous range (C++20-style backport).
- `metl::optional` — value-or-empty wrapper with P2505 monadic operations
  (`and_then`, `transform`, `or_else`).
- `metl::expected` — value-or-error type, including the `void` value
  specialization.
- `metl::variant` — type-safe tagged union with visitation.
- `metl::in_place_t`, `metl::in_place_type_t`, `metl::in_place_index_t` tag
  types.
- `metl::type_traits` extensions, including `storage_for<T>` aligned storage
  helper.
- `metl::hash` — FNV-1a hashing primitive and `hash_combine` helper.

Containers:

- `metl::fixed_vector` — fixed-capacity contiguous container.
- `metl::fixed_string` — fixed-capacity null-terminated string.
- `metl::fixed_queue`, `metl::fixed_stack`, `metl::fixed_deque` — fixed-capacity
  adapter containers.
- `metl::ring_buffer` — circular buffer over fixed storage.
- `metl::flat_map`, `metl::flat_set` — sorted-vector associative containers.
- `metl::static_unordered_map`, `metl::static_unordered_set` — open-addressed
  hash containers with power-of-two capacity and heterogeneous lookup.

Function objects:

- `metl::fixed_function` — small-buffer-optimized function wrapper, including
  the `noexcept` signature specialization.
- `metl::fixed_any_invocable` — move-only invocable, sibling to `fixed_function`.
- `metl::function_ref` — non-owning callable reference.
- `metl::delegate` — typed callable bound to an object and member function.
- `metl::event_dispatcher` — fixed-capacity multicast dispatcher.

Memory:

- `metl::intrusive_ptr` and CRTP `intrusive_ref_counter` base.
- `metl::arena_allocator` — bump-pointer arena.
- `metl::monotonic_buffer` — monotonic growing buffer over fixed storage.
- `metl::static_allocator` — allocator adapter over user-owned storage.
- `metl::object_pool` — fixed-capacity object pool with freelist.

Concurrency:

- `metl::spsc_queue` — single-producer/single-consumer lock-free queue.
- `metl::static_message_queue` — fixed-capacity message queue.
- `metl::atomic_ref` — non-owning atomic view over an existing object.

Utility:

- `metl::scope_exit` — RAII deferred-action helper.
- `metl::fsm` — table-driven finite state machine.
- `metl::lookup_table` — compile-time lookup table.
- `metl::bit` — bit manipulation helpers.
- `metl::endian` — byte-order utilities.
- `metl::crc8`, `metl::crc16`, `metl::crc32` — CRC primitives.

Embedded:

- `metl::mmio` — memory-mapped I/O accessors.
- `metl::bitfield` — typed bitfield helpers.
- `metl::register_access` — typed register access utilities.

### Changed

- N/A (initial release).

### Build

- CMake build with Bazel-style helper functions
  (`metl_cc_library`, `metl_cc_test`, `metl_cc_binary`, `metl_cc_benchmark`).
- ASAN, UBSAN, and TSAN toggles
  (`METL_ENABLE_ASAN`, `METL_ENABLE_UBSAN`, `METL_ENABLE_TSAN`).
- Install rules and CMake package config export
  (`find_package(metl)` provides `metl::metl`).
- `METL_WARNINGS_AS_ERRORS` toggle to promote warnings to errors.

### Testing

- 42 host test binaries registered with CTest.
- All tests pass under ASAN + UBSAN with `-Werror` enabled.

[Unreleased]: ./
[0.1.0-alpha1]: ./
