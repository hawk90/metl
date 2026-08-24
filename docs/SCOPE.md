# METL Scope

What belongs in METL, what does not, and how a contributor can tell the
difference without asking.

The axis is **not** "does it work on an MCU?" — that question blocks useful
techniques for the wrong reason and is hard to review. The axis is
**invariants**. Keep all five and the topic is in scope, whatever it is. Break
one and it is out of scope, however embedded it sounds.

---

## 1. The five invariants

| # | Invariant | Enforced by |
|---|---|---|
| I1 | **No heap.** No `malloc`/`free`/`operator new`/`sbrk` reachable from any public API. | `invariants` CI job (symbol audit) |
| I2 | **No exceptions, no RTTI.** No `throw`, no `dynamic_cast`, no `typeid`. | `invariants` CI job + `-fno-exceptions -fno-rtti` builds |
| I3 | **Deterministic.** Every public operation has a bounded worst-case execution time — no unbounded loops on data, no unbounded retry, no allocation-shaped latency cliffs. | Review, plus `api-contract` CI job (`check_progress_guarantee.py`) for the *stated* guarantee |
| — | *(not an invariant, but the same discipline)* **Claims about hardware behaviour are verified by executing on the hardware.** `qemu-conformance` runs the test suite on an emulated Cortex-M3; `irq_lock` is checked against a real SysTick interrupt rather than by reading its own register back. | `qemu-conformance` CI job |
| I4 | **Header-only, C++17.** No separately compiled TU required; no C++20+ features in public headers. | `host-test`, `cross-syntax` |
| I5 | **Self-contained headers.** Every public header compiles standalone; every public header is reachable from the umbrella (or is explicitly opt-in — see §4). | `header-checks` CI job |

Four of the five are machine-checked. I3 is the one that needs a human to decide
*what* the bound is, which is why it gets its own vocabulary below.

What a machine can check is whether a human decided at all, and
`tools/check_progress_guarantee.py` does: every public header must state a
guarantee, in this vocabulary, in its doc comment. It was added after the
checklist item below was measured for the first time and found to be honoured by
10 headers out of 51. The script has an `EXEMPT` list for headers with no
runtime operation to bound — macros, tag types, compile-time traits — and every
entry carries its reason.

### I3 in detail — what "deterministic" means here

"Deterministic" is a narrow claim, not a vibe. A public operation must state
which of these it provides:

| Guarantee | Meaning | Acceptable where |
|---|---|---|
| **wait-free, bounded** | Completes in a fixed number of steps known at compile time. | Anywhere, including ISR context |
| **lock-free** | System-wide progress; an individual thread may retry. Retry count is *not* bounded. | Multi-core only; never ISR↔main on a single core |
| **blocking, bounded** | Blocks, but the critical section is bounded and interrupt-safe. | With `irq_lock` on single-core |
| **blocking, unbounded** | — | **Not acceptable in METL.** |

Complexity alone is not a progress guarantee. `O(1) amortized` with a rehash
cliff is not deterministic; `O(n)` with a compile-time-known `n` is.

---

## 2. Permanent rejections

These are settled. A PR proposing one gets closed with a link to this section,
not a debate.

| Rejected | Invariant broken |
|---|---|
| Any container with a heap fallback (`small_vector` that spills to `new`, etc.) | I1 |
| Exceptions or RTTI in any form, including "opt-in" builds | I2 |
| Unbounded retry loops, unbounded backoff, unbounded spinning | I3 |
| Separately compiled library component (`.cpp` that must be built and linked) | I4 |
| Anything requiring an OS (futex, pthreads, `std::thread`, filesystem) in the core | I4/I1 |

"It sounds embedded" is not an argument. A heap-fallback `small_vector` is the
canonical example: it is marketed at embedded users and is still a permanent no.

---

## 3. Tiers

A tier is a set of targets a feature is claimed to work on. Tiers are cheap to
declare and expensive to keep honest, so:

> **Rule: a tier and its CI job arrive in the same PR. No job, no tier.**

This is the whole governance model. It is not a maintainer veto — it is a
mechanical condition anyone can satisfy. GitHub provides free ARM64 runners, and
`qemu-user` execution tests attach exactly the way the existing `big-endian /
powerpc64` job does. The barrier is lower than it looks.

### Tier 0 — Core

Works on every target in the CI matrix today: Cortex-M0/M3/M4/M7, RV32/RV64,
PPC64 BE, x86-64, AArch64 (host). No capability requirements beyond C++17.

Everything in `metl/` reachable from `metl/metl.hpp` is Tier 0. New Tier 0 code
needs no new CI job — the existing matrix already covers it.

### Tier 1 — Capability-gated

Requires a hardware capability that not every Tier 0 target has. Must be gated
by a trait/`static_assert`, must have a documented fallback or a clean compile
error, and must bring its own CI job.

Examples and their gates:

| Feature | Requires | Fallback |
|---|---|---|
| `atomic_handle` | lock-free single-word CAS (ARMv7-M+) | **none** — `static_assert` fires; mask interrupts instead. Job: `handle-atomics` |
| bounded MPMC queue | CAS (ARMv7-M+, RV32A) | `guarded<static_message_queue, irq_lock>` on Cortex-M0 — **landed**, so the fallback is a real type, not a promise |
| ~~`atomic<bitfield<...>>`~~ | — | **Not planned** as a distinct type; compose `std::atomic<Carrier>` with `bitfield` — see §5 |
| ~~double-width CAS free-list~~ | — | **Not planned** — superseded by `versioned_handle`; see §5 |
| ~~bounded hazard domain~~ | — | **Not planned** — no caller; see §5 |
| SIMD (NEON/SSE) accelerated CRC, `flat_map` probe | compile-time ISA selection only | scalar path |

Runtime ISA dispatch is **not** allowed — it breaks I3 (branchy, unbounded in
the worst case) and bloats the image for the targets that matter.

### Tier 2 — Experimental

Lives under `metl::exp::` / `metl/exp/`, is **not** included by the umbrella
header, and carries an explicit "may be removed before 1.0" notice. API
stability promises do not apply.

Because `header-checks` verifies umbrella completeness and per-header
self-containment *separately*, an opt-in header that a user must name
explicitly (`#include <metl/exp/mpmc_queue.hpp>`) does not violate the
"host and embedded parity" principle in the README: the user opted out of parity
themselves. This is the extension space, and it already exists in the CI
structure — no policy change needed to use it.

`CODEOWNERS` may delegate an experimental area to the contributor who brought
it.

---

## 4. PR checklist

Copy into the PR description:

```
- [ ] I1  no heap: `invariants` job green (no malloc/new/sbrk in the image)
- [ ] I2  no exceptions/RTTI: `invariants` job green
- [ ] I3  progress guarantee stated in the header doc comment
          (wait-free bounded / lock-free / blocking bounded) — the
          `api-contract` job checks that one is stated; you decide what it is
- [ ] I4  header-only, C++17, self-contained
- [ ] I5  new header added to the umbrella, or explicitly Tier 2 opt-in
- [ ] new container/allocator exercised in tests/embedded/invariant_probe.cpp
      (the gate only proves what the probe links)
- [ ] tier declared (0/1/2); if 1 or 2, the CI job is in THIS PR
- [ ] single-core ISR safety: lock policy documented or `irq_lock` default
- [ ] R1 (§9): every capacity-failing operation has BOTH an asserting form and a
      `try_` form — `api-contract` checks R2/R3 mechanically but not this
```

---

## 5. Roadmap

Ordered by (value / cost), not by novelty.

### A note on the caller rule

Several entries below are justified by "no caller inside the library, so not yet".
That rule is about **speculation**: a type invented before anyone needs it buys an
API-stability commitment and returns nothing. It was never meant to exclude a
whole category, and two entries have now hit the seam, so the distinction is
written down rather than re-argued each time.

A **leaf utility for the application boundary** — `spsc_byte_ring` for a driver's
DMA region, `format.hpp` for a log line — cannot have an in-library caller, because
the thing that calls it is by definition outside METL. For those, the caller rule is
replaced by two harder questions, both of which have to be answered in the PR:

1. **Is the gap structural or speculative?** `ring_core.hpp` states that a ring's
   elements are not contiguous, so no existing type *can* hand a peripheral a
   pointer. Nothing in `include/metl` includes `<charconv>` or `<cstdio>`, so no
   existing type *can* turn a number into text. Those are structural. "It would be
   nice to have" is not.
2. **Is it exercised by a real use, not an assertion that it is useful?** The PR
   ships an example that does the actual job and self-checks, and it must reach the
   hard case: `uart_byte_ring.cpp` crosses the ring's seam on six of its eight
   transfers, and fails loudly if it ever stops doing so.

Everything that *can* have an in-library caller still must have one. `fixed_bitset`
and the compile-time string map remain deferred on exactly that basis.

### Green — works on every current CI target, zero policy change

| Item | Note |
|---|---|
| `cpu_relax()` / `wait_for_event()` / `send_event()` | **Landed.** Split deliberately: on Cortex-M `yield` is effectively a no-op, so the real idle idiom is `WFE`/`SEV`. One name for two semantics would be a lie. Emission verified per target. |
| `METL_PREFETCH` / `compiler_barrier()` | **Landed.** Branch hints (`METL_PREDICT_TRUE/FALSE`) and `METL_ASSUME` already existed in `optimization.hpp`; only the prefetch hint and the compiler-only barrier were missing. |
| cache-line isolation | **Already present.** `optimization.hpp` has `METL_CACHELINE_SIZE`/`METL_CACHELINE_ALIGNED` with its own `constexpr` size (`std::hardware_*_interference_size` carries a libstdc++ ABI warning), and `spsc_queue` already puts `head_`, `tail_` and the ring on separate lines. No `cacheline_padded<T>` wrapper was needed. |
| `versioned_handle` + `handle_pool` | **Landed.** See §6. `handle_pool` is O(1) where `object_pool` scans, and a stale handle resolves to `nullptr` instead of a recycled slot. |
| lock policy (`irq_lock` / `null_lock`) + `guarded<T, Lock>` | **Landed**, but *not* retrofitted onto existing types — see below. `spin_lock` deliberately omitted. |
| `intrusive_list<T, Hook>` / `intrusive_forward_list` | **Not planned**, and for a sharper reason than "no caller yet". §7 below is an argument that METL replaced raw-pointer linkage with handles *specifically* to remove a failure class: a `handle_pool` slot re-validates a generation on every access, so a stale reference resolves to `nullptr` instead of to recycled memory. An intrusive list puts that class straight back — the node must outlive its removal, a hook may belong to exactly one list at a time, and neither is checkable. The usual justification is "allocation-free linkage", which is what a fixed-capacity pool already is. So this is not waiting for a caller; a caller would not change the argument. **Reopen only with a case that a handle-based structure genuinely cannot serve**, and even then it belongs in Tier 2 `metl::exp::` rather than the umbrella, so the lifetime contract is opt-in. |
| `tagged_ptr<T, Bits>` | **Not planned.** Alignment-derived tagging is portable and harmless, but it no longer has a job here: the free-list ABA problem that motivated it is solved better by `versioned_handle` (§7), and "a small tag beside a pointer" is already covered by `variant` and `bitfield`. Adding a public type with no user inside the library buys an API-stability commitment and nothing else. Reopen if a concrete caller appears. |
| `fixed_priority_queue` + `coro::deadline_scheduler` | **Landed.** Admitted under the caller rule, not on novelty: `coro::scheduler` is round-robin, so a task that wants to run in 500 ms is still visited every pass and has to check the clock itself. The scheduler is the queue's in-library caller and shipped in the same PR. I3 is a compile-time bound — push/pop touch `floor(log2(Capacity))` levels — with two things stated rather than assumed: `Compare` must itself be bounded, and `run_due` takes a `max_dispatches` bound because a task may legitimately re-arm at a deadline that is already due. The scheduler's `Tick` is compared with plain `<` and the header refuses to hide a wrapping counter: a signed-difference comparison is not a strict weak ordering over the full range, and a heap needs one, so it would misorder silently instead of failing. |
| `format.hpp` (bounded int-to-text) | **Landed.** `fixed_string` could hold text but had no way to put a NUMBER in it, so the options were `snprintf` — stdio, allocating on some libcs, and a return value people misread on truncation — or a hand-rolled loop per project. A leaf utility by the note above: the gap is structural (nothing in `include/metl` includes `<charconv>` or `<cstdio>`, and this keeps it that way) and `examples/log_line.cpp` does the real job. **Deliberately not a format-string library**: a `{}`-parser is code and tables in every image that links it, for an ergonomic gain a few explicit calls already give. The caller owns the scratch buffer on purpose — a hidden one would have to be `static` (not reentrant, and this library is used from ISRs) or a stack temporary the caller cannot size. |
| `parse.hpp` (bounded text-to-int) | **Landed**, and the exact mirror of `format.hpp` — which is what made the caller-rule questions easy to answer. **Structural, not speculative**: nothing in `include/metl` includes `<charconv>` or `<cstdlib>`, so no existing type *can* turn characters back into a number, and `format.hpp` had already made the outbound half a solved problem while the inbound half had none. **Exercised by a real use**: `examples/wire_values.cpp` parses a telemetry line and reaches the hard case on purpose — `256` into a `std::uint8_t` and `-32769` into an `std::int16_t` are refused rather than wrapped, and the example fails loudly if either stops being refused. The input is a `span<const char>` and never a `const char*`: METL cannot bound a NUL scan (see the progress guarantees on `hash`, `fixed_string` and the CRC headers), and wire data has no terminator. **No floating point**, matching `format.hpp` — correct rounding is a table in every image, and firmware reading `21.5` wants tenths. **Not constant-evaluable yet**, and this is the one place the mirror breaks: `format` returns a `span` (a literal type) while `parse` returns `metl::expected`, whose laundered storage is not constant-evaluable before the union-of-{T,E} rewrite deferred in AUDIT.md Section A. The arithmetic core is `constexpr`-tested so that rewrite lands on covered code. |
| `spsc_byte_ring` | **Landed**, and the one entry here that did NOT have an in-library caller — worth recording rather than glossing. The caller rule exists to stop speculative types buying an API-stability commitment for nothing; this is a leaf utility for the driver boundary, which is outside the library by construction, so no METL type will ever call it. What stood in for the rule: `detail/ring_core.hpp` states that a ring's elements are not contiguous, so no existing type can hand a peripheral a pointer and a length — the gap is structural, not speculative — and the PR shipped a driver-shaped example that crosses the seam six times out of eight transfers, so the API is exercised by a real use rather than asserted to be useful. **No DMA-safety claim**: METL has no portable cache-maintenance operation and `qemu-conformance` cannot model DMA coherency, so the header says outright that keeping the cache honest is the driver's job. The type is named for its concurrency contract, not for DMA, so that no one reads a guarantee into the name. |
| spsc_queue cached-index | **Landed.** Each side caches the other's index and reloads only when its copy says full/empty. Measured 1.7×–4.9× throughput (median ~2.4×) on a 2-thread benchmark, and **zero size cost** — the cached copies fit in padding the cache-line alignment already created. |

### Yellow — needs a capability trait + its own CI job (Tier 1)

**Landed:** `atomic_handle` — capability trait `has_lock_free_handle_atomic_v`,
gated by `static_assert`, proved by the `handle-atomics` job which asserts the
expected answer per target *and* that the opposite expectation fails to compile.

**Landed:** `mpmc_queue` — sequence-number (Vyukov) bounded queue, gated on
`std::atomic<std::size_t>::is_always_lock_free`, sharing the `handle-atomics`
capability probe. Documented as *lock-free, not wait-free*, and measured to
**degrade** with thread count rather than scale, so the header says plainly to
prefer `spsc_queue` when the roles are fixed.

**Open:** compile-time SIMD — but see below; it needs a concrete workload before
it is worth a tier.

**Not planned**, each for a specific reason rather than lack of time:

| Item | Why not |
|---|---|
| double-width CAS free-list | Superseded by `versioned_handle` before it was ever built. §7 argues the handle is strictly better *for this job*: a full-width counter instead of the bits a pointer can spare, a plain 32-bit CAS instead of `cmpxchg16b`/`CASP`, and no assumption about pointer representation. Building it now would be building the thing that argument says is unnecessary. |
| bounded hazard domain | Hazard pointers solve "dereference a pointer another thread may free". METL avoids that shape by design — `handle_pool` re-validates a generation on every access instead, so a stale handle resolves to `nullptr` rather than to freed memory. No caller inside the library. Reopen if a pointer-based lock-free structure is ever added. |
| `atomic<bitfield<...>>` as a distinct type | `bitfield.hpp` already packs; `std::atomic<Carrier>` already sequences. What is left is a CAS retry loop, which `atomic_handle` already demonstrates for a packed word. A new public type for that is API-stability commitment bought with roughly fifteen lines of composition. Reopen if a caller needs single-field atomic update badly enough to justify the Tier 1 job that would come with it. |

Compile-time SIMD stays open but is demoted. Its headline motivation was CRC, and
that turned out to be a much duller problem: the implementation was
bit-at-a-time, and a 16-entry nibble table bought 1.7x with no capability gate,
no ISA-specific code and no new CI job (see the `METL_CRC_TABLE` entry in the
changelog). A SIMD CRC would need all three, and would only help large buffers on
hosts. The `flat_map` probe idea is speculative until a workload asks for it.

### Red — possible but a bad trade, or permanently out

| Item | Verdict |
|---|---|
| upper-bit `packed_sync_ptr` | Not impossible, but lowest priority. AArch64 PAC signs the top bits, MTE claims 56–59, x86-64 LA57 leaves 7, Intel LAM / AMD UAI give the top bits hardware semantics, and HWASAN uses the top byte. Folly can do this because Meta controls the fleet; a library that does not know its deployment target cannot. The real supported set collapses to "x86-64/AArch64 with no PAC, no MTE, no HWASAN, and LA48" — not worth a tier. Build-time opt-in at best. |
| heap-fallback containers | Permanent no (I1). |
| OS-assisted tier (futex etc.) | Breaks freestanding; low value for METL's users. |
| exceptions / RTTI | Permanent no (I2). |

---

## 6. Why `guarded<T, Lock>`, not a lock policy on every container

The roadmap originally said "retrofit a lock policy onto the existing
concurrency types". That was rejected on implementation, for a reason worth
recording:

**A container that locks each operation cannot make a compound operation
atomic.** `if (!q.full()) q.push(x)` is still a race when both calls lock
individually. Per-operation locking therefore buys the *appearance* of thread
safety, at the price of a hidden cost in every call and a doubled API surface on
every container. It is why the standard library, Abseil and Folly all leave
containers unsynchronised and offer a wrapper (`folly::Synchronized`) instead.

`guarded<T, Lock>` is that wrapper: it owns the value and hands it out only
inside a critical section, so one lock can span exactly as many operations as the
invariant requires. It has no `get()` — an escape hatch would make it decorative.

`irq_lock` masks interrupts by **saving and restoring PRIMASK**, never by
blanket-enabling: an `unlock()` that simply enabled interrupts would re-enable
them inside a caller's own critical section that had deliberately disabled them.
The state travels with the guard rather than the lock, so sections nest and the
policy costs zero bytes.

**No `spin_lock`.** On a single-core target a spinlock between an ISR and the
main loop always deadlocks, and single- versus multi-core is not detectable at
compile time — so unlike every other capability here, misuse could not be turned
into a compile error. A primitive whose misuse is both easy and silent is not
worth the convenience.

---

## 7. Why handles, not tagged pointers

Worth writing down because it is the template for translating a technique
rather than importing it.

The observation Folly starts from is "a pointer word has unused bits." The
translation into METL's constraints is not "let's use the unused bits" — it is:
METL is *fixed-capacity*, so **there is no reason to store a pointer at all**.

```cpp
struct pool_handle {          // 4 bytes
    std::uint16_t index;      // slot in a pool of N <= 65535
    std::uint16_t generation; // ABA + use-after-free detection
};
static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
```

Against the alternatives:

| Approach | ABA bits | Portability |
|---|---|---|
| alignment tagging | 3 (`alignof(Node) == 8`) | portable, but 3 bits is not an ABA counter |
| upper-bit packing | ~16 | breaks under PAC/MTE/LA57; 64-bit only |
| **handle** | **16** | **zero assumptions about pointer representation** |

What falls out:

- 32-bit CAS suffices → works on Cortex-M3/M4/M7 and RV32 as-is (M0 uses `irq_lock` regardless).
- Immune to PAC, MTE, LAM, HWASAN — it never touches a pointer.
- Generation mismatch catches use-after-free, which hands `intrusive_ptr` a weak reference for free.
- Half the size → better false-sharing and cache-footprint behaviour.

So `atomic_handle` lands in **Tier 0** and does the job `atomic_tagged_ptr`
was proposed for, better. Splitting a tier is what you do when the translation
fails; this is what it looks like when it succeeds.

---

## 8. The gates

Every machine-checked claim in this repository is one of these. The table is
enforced: `tools/check_docs.py` rule **D6** fails the build if a
`tools/check_*.py` is missing from it, or if it names one that does not exist.

That rule exists because of a specific failure. `docs/TODO.md` said "`.bss`,
`.data` and the stack are not measured anywhere in this repository" for three
PRs *after* they were — #77 pinned `sizeof`, #79/#80 gated stack depth, #83
gated `.bss`. Rule D5 was written because a **number** went stale; this was a
claim about a **gate** going stale, and nothing was checking those.

| Gate | What it measures | Proves it still bites |
|---|---|---|
| [`check_invariants.py`](../tools/check_invariants.py) | I1/I2: no `malloc`, no throw path, no RTTI, in a linked ARM image | a canary TU that must FAIL the audit, **and** `--self-test` — the canary is built `-fno-exceptions -fno-rtti` alongside the probe, so it can only violate `heap`; the self-test is what proves the other two categories still bite |
| [`check_size.py`](../tools/check_size.py) | `.text`, `.rodata`, and `.bss`+`.data` against per-target budgets | `--self-test` |
| [`check_stack.py`](../tools/check_stack.py) | deepest stack frame; rejects any `dynamic` frame outright | `--self-test` |
| [`check_instructions.py`](../tools/check_instructions.py) | instructions executed per benchmark, via cachegrind | `--self-test` |
| [`check_api_contract.py`](../tools/check_api_contract.py) | §9 R2/R3 across every public header | `--self-test` |
| [`check_progress_guarantee.py`](../tools/check_progress_guarantee.py) | I3: every public header states a progress guarantee | `--self-test` |
| [`check_docs.py`](../tools/check_docs.py) | D1–D7: the documentation claims a machine can settle | `--self-test` |
| [`check_compile_fail.py`](../tools/check_compile_fail.py) | that the public `static_assert`s actually fire, and that the gap between how many there are and how many are pinned only shrinks | `--self-test` |
| [`check_mutants.py`](../tools/check_mutants.py) | that the gates above notice a deliberately broken library | `--self-test` |

Not in the table because they are not scripts: the `sizeof` ratchet
(`tests/core/ram_footprint_test.cpp`), the comparison-count bound
(`tests/containers/operation_count_test.cpp`), the coverage floor
(`tools/coverage.sh`), the clang-tidy finding ratchet, and the
consumer-warning matrix in `ci.yml`.

### How the invariant gate works

`tools/check_invariants.py` reads the **symbol table of a fully linked image**
(`nm`) and fails if forbidden symbols are present. It does not run the binary.

Two design notes worth preserving:

**Why symbol audit, not linker poisoning.** The classic trick — define
`operator new` so it references an undefined symbol — depends on `--gc-sections`
behaviour and gives different answers under GNU ld, lld, and IAR. Reading the
final symbol table is linker-agnostic, produces a readable error, and proves
I1, I2, and I3's exception half in one pass.

**Why the probe cannot be an existing example.** A single `printf` sinks the
gate: newlib's `vfprintf` uses `malloc` for internal buffering. The probe must
be stdio-free and accumulate its results into a `volatile` sink returned from
`main()`. (picolibc's tinystdio does not allocate, so this trap only springs on
newlib — but the probe must pass under both.)

**The probe supplies its own `abort()`, and the reason is a real finding.**
`METL_ASSERT` / `METL_HARDEN` bottom out in `std::abort()` unconditionally —
even a custom handler is followed by `abort()` so the path is provably
`[[noreturn]]` (see `assert.hpp`). newlib's `abort()` calls `raise()`, and
newlib's signal machinery allocates its handler table with `_malloc_r`. So **on
newlib, any image containing a METL assert transitively links `malloc` and
`_sbrk`** — which is what the first run of this gate found. The probe's own
object file references nothing but `abort`, so this is a property of newlib, not
of METL, and supplying `abort()` is what a bare-metal user does anyway alongside
`_exit` and the other stubs. Users on newlib who need a provably heap-free image
must do the same.

**The gate proves what the probe links, and nothing more.** The probe exercises
a hand-picked set of allocation-prone containers rather than every public type
(`embedded_smoke.cpp` instantiates everything, but is deliberately not linked
here — see the note above about `__cxa_atexit`). So coverage does not grow by
itself: adding a container to the probe is one function, and it is on the PR
checklist in §4 for that reason. A gate whose coverage silently lags the library
degrades into decoration.

**A canary is mandatory.** `tests/embedded/invariant_canary.cpp` deliberately
links `operator new`, and its ctest entry is `WILL_FAIL TRUE`. A gate that
cannot fail is not a gate; if the canary starts passing, the audit logic is
dead.

**Semihosting is not needed, and must not be used.** The gate links but never
executes, so no QEMU and no semihosting. That is a feature: a new target gets
the gate with a cross toolchain alone, no emulator. And `--specs=rdimon.specs`
would actively poison it — its crt0 sets up the heap via the `SYS_HEAPINFO`
semihosting call, and that startup object is linked unconditionally, so heap
symbols enter the image with nothing to do with METL. `nosys.specs` is fine:
libnosys is an archive, so `_sbrk` is only pulled in if something references it
— meaning **`_sbrk` appearing under nosys is a true positive and must never be
allowlisted**. `check_invariants.py` refuses `--allow _sbrk` outright rather than
leaving that rule in prose, where it would erode.

| specs | Gate |
|---|---|
| `-nostdlib -nostartfiles` | cleanest |
| `--specs=nosys.specs` | OK — `_sbrk` here is a real violation |
| `--specs=rdimon.specs` | ✗ semihosted startup drags in heap setup |

The existing `picolibc-qemu` job stays as-is: it proves the code *runs*, the
gate proves certain symbols *are absent*. Different axes, complementary.

A runtime trap (override `_sbrk` to `abort()` and run under QEMU) is
deliberately not used: it only covers paths the test actually executed, whereas
the static audit covers everything linked into the image.

---

## 9. The recoverable-API contract

A fixed-capacity library lives or dies on one question: **when an operation does
not fit, is that a bug or a normal Tuesday?** Both answers are legitimate, and
they need different APIs. METL provides both, and this section is what "both"
means precisely.

### R1 — the pair

Any operation that can fail because the container is **full** has exactly two
forms:

| Form | On failure |
|---|---|
| `X(...)` | Treats it as a precondition violation: asserts and aborts. |
| `try_X(...)` | Reports it by return value and leaves the container **unchanged**. |

"Unchanged" is the load-bearing half. A `try_` form that half-applies and then
returns `false` is *worse* than one that asserts, because the caller's recovery
path then runs against corrupted state. This is why `try_assign`/`try_insert`
require a **forward** iterator: a single-pass source cannot be measured without
being consumed, so the promise could not be kept. That is a `static_assert` with
a named reason rather than an `enable_if`, because "no matching function for
call to `try_assign`" tells a user nothing.

### R2 — the name

The `try_` prefix is **reserved for, and required by,** R1's recoverable form. No
other public function returns a bare `bool` or pointer meaning "the operation did
not happen."

This rule was added after `flat_map::insert_or_assign` was found returning a
`bool` that meant *did it fit*, while `std::map::insert_or_assign`'s bool means
*was it inserted rather than assigned*. Borrowing a standard name and changing
what its result means is the "silent surprise" the design principles forbid.

### R3 — nodiscard

Every `try_X` is `METL_NODISCARD`. Dropping the result is the exact bug the pair
exists to prevent: `v.try_push_back(x);` as a statement is a silent overflow.

### R4 — the exception, and only this one

A `bool` that is **an answer to a question** rather than a failure report keeps
its plain name and stays discardable: `erase(key)` ("was it present"),
`object_pool::destroy(ptr)` ("was that a live slot"), `contains`, `empty`,
`full`. `m.erase(k);` is a legitimate idiom and must not warn.

The full list lives in `tools/check_api_contract.py` as `BOOL_ALLOWLIST`, one
entry per name **with its reason**, so a future reader sees a decision rather
than an omission.

### R5 — where `try_pop` belongs

Underflow gets a `try_` form only where a pre-check is unavailable or unsafe —
that is, on the concurrent queues (`spsc_queue`, `mpmc_queue`,
`static_message_queue`), where `if (!q.empty()) q.pop();` is a race. On a
single-threaded container `if (!v.empty()) v.pop_back();` is already exact, so no
underflow twin is added. `fixed_string::try_pop_back` predates this rule and is
kept for character-at-a-time parse loops; the header says so.

### How it is enforced

R2 and R3 are mechanical and are checked by the **`api-contract`** job
(`tools/check_api_contract.py`), which also has a `--self-test` canary — a gate
that cannot fail is not a gate, the same argument as §8's `invariant_canary`. The
forward-iterator constraint in R1 is proved by compiling
`tests/compile_fail/forward_iterator_required.cpp` both ways and requiring the
single-pass arm to fail, the same shape as the `handle-atomics` capability gate.

R1's "both forms exist" half needs a human and is on the §4 checklist.

**Why any of this is a gate rather than a review item:** review had already
looked at every one of these. 22 of the 55 `try_*` entry points shipped without
`METL_NODISCARD`, and the boundary followed *when each header was written* rather
than any principle — `fixed_vector::try_push_back(x);` compiled silently while
`fixed_queue::try_push(x);` warned, the same idiom with opposite safety in the
same library.
