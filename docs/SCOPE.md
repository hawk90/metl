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
| I3 | **Deterministic.** Every public operation has a bounded worst-case execution time — no unbounded loops on data, no unbounded retry, no allocation-shaped latency cliffs. | Review + documented progress guarantee |
| I4 | **Header-only, C++17.** No separately compiled TU required; no C++20+ features in public headers. | `host-test`, `cross-syntax` |
| I5 | **Self-contained headers.** Every public header compiles standalone; every public header is reachable from the umbrella (or is explicitly opt-in — see §4). | `header-checks` CI job |

Four of the five are machine-checked. I3 is the one that needs a human, which
is why it gets its own vocabulary below.

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
| bounded MPMC queue | CAS (ARMv7-M+, RV32A) | `irq_lock` on Cortex-M0 |
| `atomic<bitfield<...>>` | `is_always_lock_free` | `static_assert` failure |
| double-width CAS free-list | `cmpxchg16b` / LSE `CASP`, 64-bit, `-mcx16` | Tier 0 handle free-list |
| bounded hazard domain | fixed slot count (keeps I1) | — |
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
          (wait-free bounded / lock-free / blocking bounded)
- [ ] I4  header-only, C++17, self-contained
- [ ] I5  new header added to the umbrella, or explicitly Tier 2 opt-in
- [ ] new container/allocator exercised in tests/embedded/invariant_probe.cpp
      (the gate only proves what the probe links)
- [ ] tier declared (0/1/2); if 1 or 2, the CI job is in THIS PR
- [ ] single-core ISR safety: lock policy documented or `irq_lock` default
```

---

## 5. Roadmap

Ordered by (value / cost), not by novelty.

### Green — works on every current CI target, zero policy change

| Item | Note |
|---|---|
| `cpu_relax()` / `wait_for_event()` / `send_event()` | **Landed.** Split deliberately: on Cortex-M `yield` is effectively a no-op, so the real idle idiom is `WFE`/`SEV`. One name for two semantics would be a lie. Emission verified per target. |
| `METL_PREFETCH` / `compiler_barrier()` | **Landed.** Branch hints (`METL_PREDICT_TRUE/FALSE`) and `METL_ASSUME` already existed in `optimization.hpp`; only the prefetch hint and the compiler-only barrier were missing. |
| cache-line isolation | **Already present.** `optimization.hpp` has `METL_CACHELINE_SIZE`/`METL_CACHELINE_ALIGNED` with its own `constexpr` size (`std::hardware_*_interference_size` carries a libstdc++ ABI warning), and `spsc_queue` already puts `head_`, `tail_` and the ring on separate lines. No `cacheline_padded<T>` wrapper was needed. |
| `versioned_handle` + `handle_pool` | **Landed.** See §6. `handle_pool` is O(1) where `object_pool` scans, and a stale handle resolves to `nullptr` instead of a recycled slot. |
| lock policy (`irq_lock` / `spin_lock` / `null_lock`) | Retrofit onto existing concurrency types. |
| `tagged_ptr<T, Bits>` | Alignment-derived bits only. Portable, harmless. |
| spsc_queue cached-index | **Landed.** Each side caches the other's index and reloads only when its copy says full/empty. Measured 1.7×–4.9× throughput (median ~2.4×) on a 2-thread benchmark, and **zero size cost** — the cached copies fit in padding the cache-line alignment already created. |

### Yellow — needs a capability trait + its own CI job (Tier 1)

**Landed:** `atomic_handle` — capability trait `has_lock_free_handle_atomic_v`,
gated by `static_assert`, proved by the `handle-atomics` job which asserts the
expected answer per target *and* that the opposite expectation fails to compile.

**Open:** bounded MPMC queue · `atomic<bitfield<...>>` · double-width CAS
free-list · bounded hazard domain · compile-time SIMD.

### Red — possible but a bad trade, or permanently out

| Item | Verdict |
|---|---|
| upper-bit `packed_sync_ptr` | Not impossible, but lowest priority. AArch64 PAC signs the top bits, MTE claims 56–59, x86-64 LA57 leaves 7, Intel LAM / AMD UAI give the top bits hardware semantics, and HWASAN uses the top byte. Folly can do this because Meta controls the fleet; a library that does not know its deployment target cannot. The real supported set collapses to "x86-64/AArch64 with no PAC, no MTE, no HWASAN, and LA48" — not worth a tier. Build-time opt-in at best. |
| heap-fallback containers | Permanent no (I1). |
| OS-assisted tier (futex etc.) | Breaks freestanding; low value for METL's users. |
| exceptions / RTTI | Permanent no (I2). |

---

## 6. Why handles, not tagged pointers

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

## 7. How the invariant gate works

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
