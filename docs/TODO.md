# metl — TODO / Backlog

Living backlog. Checked items are shipped (CI green); unchecked are open.
See `docs/AUDIT.md` for findings and `CHANGELOG.md` for what landed.

## ✅ Shipped

- [x] Correctness: 20 audited bugs fixed (scheduler reentrancy, static_unordered_map
  emplace, exception-safe variant/expected, mmio alignment, allocator overflow,
  fixed_function const-correctness, intrusive_ptr contract, fsm, hash constraint)
- [x] abseil-cpp techniques: `attributes.hpp` + `LIFETIME_BOUND`, `optimization.hpp`
  (PREDICT/ASSUME/cacheline), `METL_DASSERT`, `CONST_INIT`, `TRIVIAL_ABI`,
  ASan tail-poisoning for `fixed_vector`
- [x] constexpr honesty: `optional` genuinely constexpr on C++20 (via `detail/construct.hpp`)
- [x] Per-symbol Doxygen docs across every public header (51 at the time; 60 today)
  + non-standard-contract warnings
- [x] Examples (9, CI-compiled) + `docs/COOKBOOK.md`
- [x] Environment coverage: host (Linux/macOS/Windows × gcc/clang/MSVC × Debug/Release/MinSizeRel),
  ASan/UBSan, TSAN (real threaded tests), LTO, ARM Cortex-M (gcc + clang frontends),
  RISC-V rv64, PowerPC64 big-endian, per-header self-containment
- [x] Embedded libc: newlib-nano link + **picolibc + QEMU semihosting run**
- [x] RTOS/frameworks: Zephyr module + ESP-IDF component (+ samples) — **both CI jobs blocking** since 2026-08-21
- [x] Release + `-Werror` hygiene gate; Doxygen 1.9.8 compat
- [x] CI/CD: **preflight fail-fast gate** (dependency DAG, not flat fan-out),
  **GitHub Pages docs deploy** (gated on validation) → https://hawk90.github.io/metl/,
  Dependabot (github-actions), actions on Node-24 (checkout@v5 / upload-artifact@v7),
  macos-14 pinned

## ☐ Open — by priority

### 🔐 Bug bounty / security (highest signal for a safety-claiming lib)
- [x] **Fuzzing harnesses** (libFuzzer, built under ASan+UBSan): `fixed_string`
  from untrusted input, `flat_map`/`static_unordered_map` random op sequences,
  arena/static allocators, crc — under `fuzz/`, opt-in via Clang-only
  `METL_BUILD_FUZZERS`. Harnesses drive **contract-valid opcode streams only** so
  any sanitizer finding is a real defect. Blocking `fuzz-smoke` CI job runs each
  per push/PR. No library bug found (200k+ runs/target clean).
- [x] **Continuous fuzzing** — **ClusterFuzzLite** (OSS-Fuzz tech in GitHub
  Actions, no upstream registration): `.clusterfuzzlite/` + the `cflite-pr` and
  `cflite-batch` workflows, **both blocking since 2026-08-22**. They were
  `continue-on-error` before that, on the grounds that the Docker build could
  not be validated locally — which their own history disproved: twenty
  consecutive runs, zero failed steps. Removing it also surfaced that the
  nightly batch run had **never persisted a corpus**: the `clusterfuzzlite`
  storage branch did not exist, ClusterFuzzLite cannot create it, and it logs
  that failure and returns success. Six targets × two sanitizers × ten minutes,
  discarded nightly. Branch created, and the corpus push authenticated — the
  `storage-repo` URL carried no token, so ClusterFuzzLite committed the corpus
  locally and then failed on `git push` with "could not read Username". Their
  docs ask for a PAT because the default token "is not able to write to other
  repositories"; the storage repo here is this repository, so the job token
  plus `contents: write` is enough and expires with the job. The job now fails
  if the corpus branch is still empty afterwards, and that check fired on the
  first real run — which is how the auth gap was found.
- [ ] **OSS-Fuzz upstream registration** — **not queued, and not blocked on us.**
  OSS-Fuzz's stated bar is that a project "must have a significant user base
  and/or be critical to the global IT infrastructure"
  ([accepting new projects](https://google.github.io/oss-fuzz/getting-started/accepting-new-projects/)).
  METL is pre-1.0 with no known dependents, so it does not meet that bar today,
  and submitting anyway spends a reviewer's time on an answer that is already
  knowable. Two mechanical gaps remain for whenever it does qualify:
  `.clusterfuzzlite/project.yaml` lacks the `homepage` and `primary_contact`
  fields OSS-Fuzz requires, and `primary_contact` publishes a maintainer email
  in the google/oss-fuzz repository — a decision, not an edit. The build wiring
  itself is ready and is exercised on every PR.
- [x] **Fuzz harnesses for the headers nothing reaches** (2026-08-22, #71–#75).
  The first coverage run (`cflite-cron`) measured 80.23% line coverage — over
  **the 18 of 66 headers the harnesses actually touched**. Reported without that
  denominator it reads as "METL is 80% fuzzed", which is wrong.

  **Read the denominator, not the percentage.** The three measurements, in
  order: **18 / 66 headers at 80.23%**, then **25 / 66 at 63.33%**, then
  **36 / 66 at 82.66%** (14 targets, 15,439 corpus inputs). The middle one is
  the trap — coverage improved and the headline number fell, because a new
  harness pulls a whole header into the denominator while exercising part of it.
  Anyone tracking the percentage alone reads progress as regression. Track
  `headers reached / 66` alongside it. `fuzz_parse` closed the
  worst gap (`parse.hpp` is the one header whose documented job is accepting
  bytes somebody else chose, and it shipped in #66 without a harness).
  **All of the ones worth having have now landed**: `parse`,
  `spsc_byte_ring`, `format`, `flat_set`, `static_unordered_set`, and then
  `fuzz_sequence` (`ring_buffer`/`fixed_deque`/`fixed_queue`/`fixed_stack`),
  `fuzz_pools` (`object_pool`/`handle_pool`) and `fuzz_vocab`
  (`expected`/`optional`/`variant`). The last three use a payload type that
  counts its own live instances, because these containers hold elements in
  INLINE storage where a skipped or doubled destructor is invisible to ASan —
  it is a live member of a live object, so there is nothing for a sanitizer to
  report. Mutation-tested: nine deliberate defects, all killed, and a no-op
  control that correctly survived. **Not fuzz targets, and not
  gaps**: the macro/trait/config headers (`config`, `attributes`, `compiler`,
  `optimization`, `in_place`, `version`, `type_traits`, `metl.hpp`), the
  hardware ones (`mmio`, `register_access`, `bitfield`), and the concurrency
  ones (`spsc_queue`, `mpmc_queue`, `atomic_*`, `lock`, `wait`) — libFuzzer
  drives one thread, so a harness there would exercise the uncontended path and
  claim coverage it did not earn. TSan in `ci.yml` is what covers those.
- [x] **The tombstone reclaim is held to its claim.** `rehash_in_place` /
  `reclaim_if_needed` (#18) could be deleted and nothing would notice: replacing
  the trigger with `if (false)` in both headers left the whole suite green and
  every fuzz harness clean. Fixed two ways.
  **The documentation was wrong first.** #63's prose credited the reclaim with
  keeping the worst case from "drifting upward"; the worst case is the probe
  loop's `bucket_count` limit and cannot drift, with or without it. What the
  reclaim protects is the *typical* cost under churn, and its price is that
  `erase` occasionally move-constructs every live element — a latency spike the
  guarantee row did not mention. Both headers now say so.
  **The gate needs no new API**, which was the blocker when this was written up:
  `erase` moves nothing except when it triggers a rebuild, so a key type that
  counts its own moves observes rebuilds exactly.
  `tests/containers/unordered_reclaim_test.cpp` does that — a count, not a
  duration, so there is nothing to be flaky on a shared runner.
- [x] **SECURITY.md** — vulnerability disclosure policy (root).
- [x] **CodeQL** security scan workflow (`.github/workflows/codeql.yml`), push/PR
  plus a weekly schedule so newly published queries reach the code without
  waiting for a commit. Uses `build-mode: manual` on purpose: METL is
  header-only, so a template nothing instantiates is never analysed, and
  `autobuild` on a repo with no library to link would report a clean scan of
  almost nothing. The step builds the test suite precisely to instantiate the
  templates — if that build ever stops covering a header, this job keeps passing
  while covering less, so treat it as part of the gate, not scaffolding.
- [x] **OSSF Scorecard + SLSA/provenance** (2026-08-22, #67).
  `.github/workflows/scorecard.yml` runs weekly and on push, and reports to the
  Security tab with **`publish_results: false`** — publishing sends the score to
  the public OpenSSF API and is what enables the README badge, which is an
  outward-facing act and so stays a deliberate one-line change (it also needs
  `id-token: write` added when flipped). The first run reported exactly one
  finding, Branch-Protection 0; `main` is protected now. Releases additionally
  attest the amalgamation with `actions/attest-build-provenance` **and attach
  the bundle as a release asset** — the attached copy is the point, because
  METL's distribution is vendoring and the consumer has no package manager or
  checksum database behind the file.
  Not done, and not an oversight: `FROM gcr.io/oss-fuzz-base/base-builder` in
  `.clusterfuzzlite/` stays unpinned. Scorecard's Pinned-Dependencies check
  flags it, but OSS-Fuzz's docs specify exactly that untagged form and their
  infrastructure rebuilds against a moving base. Pinning scores better and
  breaks the thing the pin is for.

### 📦 Distribution / adoption
- [x] **First release** — `v0.1.0-alpha2`, 2026-08-21. Pre-release (the hyphen
  makes `release.yml` pass `--prerelease` on its own), with
  `metl-0.1.0-single.hpp` attached and attested. The version check compares the
  tag with the suffix stripped, so `project(VERSION 0.1.0)` needed no edit.
  The API is still moving and the alpha suffix says so; the point of cutting it
  was to unblock the two items below, both of which need something to point at.
- [ ] **vcpkg** port (`portfile.cmake` + `vcpkg.json`) → `vcpkg install metl`.
  **Unblocked as of the tag above**, and this was the reason it waited:
  `vcpkg_from_github` pins the **SHA512 of a release tarball**, and until
  2026-08-21 there was no tag to hash. A port carrying a placeholder hash would
  look finished and fail on a user's first `vcpkg install`, which is worse than
  not shipping one.
- [ ] **conan-center-index submission.** The recipe and its consumer check are
  done and gated in CI (see below); what CCI additionally wants is a published
  version to point at, which `v0.1.0-alpha2` now is. **Unblocked**, though CCI
  may well decline a pre-release — worth checking their policy before spending
  a reviewer's time, the same question OSS-Fuzz already answered no to.
- [x] **Conan** recipe (`conanfile.py` + `test_package/`), 2026-08-20, with a
  blocking `conan` CI job. The recipe reads its version from
  `project(VERSION)` in CMakeLists.txt rather than repeating it, so it cannot
  drift into a third answer alongside the tag check in `release.yml`. It exports
  `metl::metl` — the same target `find_package(metl)` gives — so a consumer's
  CMake does not change with how they obtained the library, and `validate()`
  rejects a pre-C++17 consumer at configure time instead of failing hundreds of
  lines into a header. Both behaviours are asserted by the CI job, not just
  written down. Submitting to conan-center-index is a separate step and wants a
  published release to point at.
- [ ] Submit to the ESP-IDF Component Registry (component manifest already present).

### 📊 Quality / claims
- [x] **`format.hpp` — bounded int-to-text** (2026-08-21) — the last of the four
  planned feature PRs. Narrow on purpose: no format-string parser, ever. Landed
  alongside a refinement of `docs/SCOPE.md`'s caller rule, since this and
  `spsc_byte_ring` are both leaf utilities that *cannot* have an in-library caller;
  the rule now names that category and replaces the requirement with two harder
  questions instead of granting exceptions one at a time.
- [x] **`spsc_byte_ring`** (2026-08-21) — zero-copy contiguous spans for the driver
  boundary; no DMA-safety claim, because METL cannot verify one. **The only new
  public type so far without an in-library caller**, and `docs/SCOPE.md` records
  that rather than glossing it: the gap is structural (no existing type can hand a
  peripheral a pointer, by `ring_core`'s own design note) and a driver-shaped
  example crosses the seam on 6 of 8 transfers, so the API is exercised by a real
  use. Found and fixed a defect no positive test could see — `commit_write`'s guard
  was bounded by the free space where the doc said the span, which differ at the
  seam — with a `fork()`-based negative control that fails if the old guard returns.
- [x] **`fixed_priority_queue` + `coro::deadline_scheduler`** (2026-08-21) — the
  first of the post-contract feature PRs, admitted under the SCOPE.md rule that a
  public type needs an in-library caller, so the queue and its caller shipped
  together. Notable beyond the code: the container and the scheduler were
  **mutation-tested**, and it found a real hole — the scheduler test as first
  written stayed green under a `sift_down` that no-ops at the root, because a
  three-task heap is too shallow to tell a working sift from a broken one. It now
  drains eight scrambled deadlines and catches that mutant, as does the new
  `fuzz_priority_queue` harness (heap property + size + top-dominates-array
  after every operation; 200k random inputs clean under ASan+UBSan locally).
- [x] **clang-tidy ratchet re-measured on CI: 148 → 142** (2026-08-21, run
  32393655273). `modernize-concat-nested-namespaces` was disabled with its reason
  (project style opens nested namespaces separately, so the check only ever asked
  for the one style the project does not use), which removed its accepted entries.
  Done in two steps on purpose: the commit that caused the drop left the ceiling
  at 148, CI reported 142, and only then was the number set from that report.
- [x] **Recoverable-API contract completed and gated** (2026-08-20, pre-1.0 and
  deliberately breaking — after v0.1.0 the same corrections would cost a
  deprecation cycle). The library always had the asserting/recoverable pair; what
  it did not have was consistency, and the inconsistency was invisible:
  **22 of 55 `try_*` entry points shipped without `METL_NODISCARD`**, split by
  *when each header was written* rather than by any principle, so
  `fixed_vector::try_push_back(x);` compiled silently where
  `fixed_queue::try_push(x);` warned. Three more (`fixed_string::assign`/`append`,
  `flat_map`/`static_unordered_map::insert_or_assign`) spelled the same contract
  without the `try_` prefix — and `insert_or_assign`'s bool meant *did it fit*
  where std's means *was it inserted rather than assigned*.
  Rules R1–R5 are now written down (`docs/SCOPE.md` §9); R2/R3 are enforced by the
  **`api-contract`** job with a `--self-test` canary, and R1's forward-iterator
  half by compiling `forward_iterator_required.cpp` both ways and requiring the
  single-pass arm to fail. `fixed_vector` gained the nine missing `try_*` forms.
  Measured: 88/88 ctest, clang-tidy **down 1** distinct locally (351 → 350, same
  toolchain both sides), coverage **up** to 90.39% lines / 76.19% branches.
- [x] **`flat_map`/`flat_set::emplace` one-past-the-end return** (Section D of
  `docs/AUDIT.md`) — closed while adding the `insert_or_assign` asserting twin,
  which had the identical shape. On a full container `index == size_ == Capacity`
  and `METL_ASSERT` is stripped at low hardening levels, so the returned reference
  was out of bounds. Now guarded by the never-stripped `METL_HARDEN`, the same way
  `static_unordered_map::construct_at` already guarded its own npos path.
- [ ] **`try_value()` on `expected`/`optional`** (`docs/AUDIT.md` item 11) —
  **re-adjudicated under §9 R5 and closed as not needed.** R5's line is whether a
  pre-check is available and non-racy; `has_value()` is exactly that on a
  single-threaded vocabulary type, and `value_or()` already covers the total
  accessor. A `try_value()` would add API surface without adding a capability.
- [x] **Benchmarks** — `metl_cc_benchmark` now builds for real (it used to
  silently `return()` whenever `benchmark::benchmark` was absent, which was
  always). Three suites under `bench/` — containers/lookup, object_pool vs
  handle_pool, spsc throughput — plus a `bench-smoke` CI job.
  **Deviation:** built on a dependency-free in-repo harness rather than
  google/benchmark, which would have been this repo's first external dependency
  and would cost CI a fetch plus a framework build; the same call was already
  made in choosing `tests/metl_check.hpp` over gtest. The harness reports the
  median of N repetitions *with the min/max spread*, so noise is visible.
  Deliberately no *wall-clock* performance gate — a threshold on a shared runner
  either fires spuriously or never fires.
  **Update (2026-08-21):** that argument does not cover code SIZE, which is a
  deterministic function of the source and a fixed cross toolchain. `bench-smoke`
  still asserts nothing about its numbers, and now says so; the gated performance
  claim is `tools/check_size.py`, a `.text` ratchet on the linked
  `invariant_probe.elf` per Cortex-M target in the `invariants` job. Budgets are
  filled from what that job reports and never from a local figure, with a +512
  tolerance for toolchain drift. Enforcing, not reporting. **The numbers live in
  `tools/check_size.py` and are not repeated here** — this sentence used to
  repeat them, #66 raised them, and the copy stayed behind for nine PRs;
  `check_docs.py` rule D5 now rejects any restatement.
  **`.rodata` is gated too, since #83.** It had been measured and printed by
  this step from the start and compared against nothing — a step that could only
  pass, inside the tool whose header argues against exactly that. #28 made CRC's
  nibble table the default, which is a deliberate trade of `.rodata` for speed,
  so the space is not idle.
- [x] **Measure RAM: `sizeof`, stack depth, and `.bss`/`.data`** (#77, #79/#80,
  #83). The ratchet above gates **flash**. The resource METL actually moved the
  cost onto is **RAM**, and for a long time nothing here measured it: no
  `-fstack-usage`, no `-Wstack-usage`, and `check_size.py` parsed `.bss`/`.data`
  out of the ELF and discarded them.

  All three halves are now gated. `tests/core/ram_footprint_test.cpp` pins
  `sizeof` per container with `static_assert`, so it runs on every cross-syntax
  target too (#77). `tools/check_stack.py` measures the deepest frame and
  rejects any `dynamic` one outright (#79, enforcing since #80).
  `check_size.py --ram-object` sums `.bss` + `.data` from the stack probe's
  object file (#83).

  **This entry claimed "not measured anywhere" for three PRs after they landed.**
  D5 was written because a *number* went stale; this was a claim about a GATE
  going stale, which D5 does not cover and nothing else did either.

  **I1 is the invariant that creates the exposure.** "No heap" is enforced by a
  symbol audit, which proves `malloc` is absent from the image and says nothing
  about what replaced it. What replaced it is inline storage sized by the
  caller's template argument. Measured locally at `-O2` (host, so indicative
  rather than a budget — the real numbers must come from the cross job, as
  always):

  | type | `sizeof` | frame |
  |---|---|---|
  | `static_unordered_map<u32,u64,128>` | 4376 | 4432 |
  | `fixed_vector<u32,256>` | 1032 | 1056 |

  One ordinary call takes **over half the SRAM of a Cortex-M0+ with 8 KB**. No
  gate, no warning, and no line in `docs/` that tells a caller to put it in
  static storage instead.

  The sharpest way to put it: **the heap has a failure signal and the stack does
  not.** `malloc` returns null, and METL's whole recoverable-API contract (§9)
  is built on answering "did it fit" — `try_push_back` returns false at
  capacity. The one resource METL hands to the caller is the one with no `try_`,
  and on an MCU without an MMU, overflowing it quietly rewrites `.bss`.

  `docs/RFP.md` listed **"Predictable memory usage"** as an objective and
  **"Stack usage"** first among its benchmark metrics. Every other requirement
  in that document became an invariant and a CI job. This one never got a
  number.

  **The naive fix would have been theatre, and was not shipped.**
  `invariant_probe.cpp` uses capacity 4–8 containers, so a `.bss` or stack
  ratchet bolted onto it would have measured approximately nothing and passed
  forever — the exact shape this repo has spent months removing. Capacity is the
  *caller's* parameter, so the probe that gates flash structurally cannot gate
  RAM. `tests/embedded/stack_probe.cpp` (#79) is the second probe that was
  needed: realistic capacities, containers in static storage. It was built for
  the stack measurement and turned out to be exactly what the `.bss` ratchet
  needed as well — which is why #83 adds no new artifact, only a `size -A` over
  the object file that job already produces.
- [x] **Host coverage measurement + blocking floor** (#35) — `tools/coverage.sh`
  and the `coverage` job report `include/metl` only (llvm-cov's default total
  also counts the test sources, which measures how well the tests cover
  themselves) and fail below **90% lines / 72% branches** (raised from 85/70 on
  2026-08-21; the old floors had 6.2 and 4.7 points of slack, so a gate that only
  fired after a collapse). CI measures **91.22% lines, 74.67% branches, 95.56%
  functions**. The branch margin is one observed toolchain spread wide -- local
  llvm-cov reports 77.31% on the same tree, a 2.6-point disagreement about what
  counts as a branch. "No coverage measurement" is no longer an
  accurate description of this repo.
- [ ] **Raise coverage depth** — the remaining `try_*` / full-container branches.
  Note the structural ceiling before chasing a number: a large share of what is
  uncovered cannot be reached by a *passing* test. `METL_ASSERT`/`METL_PANIC`
  failure paths abort; `variant`'s `valueless_by_exception` branches are
  unreachable because the type cannot become valueless here (default ctor engages
  alternative 0, `reset()` is private, no exceptions); and constexpr code
  exercised only by `static_assert` never executes, which is why `bit.hpp` reads
  ~29% lines at 100% branches. Reaching 100% would mean weakening the types.
- [x] **Audit the post-2026-07-07 headers** (2026-08-21) — `docs/AUDIT.md`
  Section F. Nine headers read; two real defects in `mpmc_queue` (a `size_approx`
  that breaks at the counter wrap and makes `full()` answer false on a full
  queue, and a destructor that silently required a default-constructible `T`),
  two smaller ones in `lock.hpp`. The Vyukov algorithm itself matched the
  canonical form line for line. The four headers from 2026-08-21 are excluded
  with a reason rather than counted.
- [ ] **Embedded / configuration coverage model** — the host number cannot see
  MCU-only paths (`irq_lock`'s PRIMASK arm, the ARMv6-M capability rejections,
  the `-fno-exceptions` arms of `expected.hpp`) or any `#if` arm this
  configuration does not compile. Their evidence today is `qemu-conformance` and
  `config-matrix`, not a coverage percentage.
- [x] Promote **clang-tidy** from advisory to blocking (2026-08-20). **Not** a matter of
  fixing findings and flipping the switch. CI prints **3,084 warnings**, but that
  is the raw line count: every header is analysed once per translation unit that
  includes it, so the same finding is reported many times. Deduplicated it is
  **739 unique findings** — 76% of the raw count is echo. Size the work off 739,
  never off 3,084; the distribution says it is three separate steps:
  1. *Tune the check list.* 178 are `cppcoreguidelines-avoid-do-while` firing on
     the `do { } while (0)` macro idiom, which is correct practice here, and
     another ~167 are magic-numbers / macro-to-enum / literal-suffix-case /
     named-parameter — style positions this project has already taken. Disable
     them **with the reason written in `.clang-tidy`**, so a future reader sees a
     decision rather than an omission.
  2. *One mechanical sweep.* `modernize-type-traits` alone was **308** (42% of the
     unique total): `std::is_same<...>::value` → `std::is_same_v<...>`. C++17 has
     the variable templates, so this is safe and test-verifiable in one pass.
  3. *Triage what remains.* **Done 2026-08-19, and the residue is not what the
     count suggested.** The two checks that looked like real-bug candidates were
     read at every site and are false positives for this codebase, every one:
     `cppcoreguidelines-rvalue-reference-param-not-moved` (25) fires because METL
     moves with `static_cast<T&&>(v)` rather than `std::move` — the check only
     recognises the latter — and on `variant::get`, which must return a reference
     into its argument rather than move it; `cppcoreguidelines-missing-std-forward`
     (46) fires on forwarding done in a constructor's member-init list
     (`: value_(std::forward<Args>(args)...)`) and on a forward used as the callee
     (`std::forward<Fn>(fn)(value_)`), neither of which the check counts. Treat the
     remaining count as a regression budget, not a defect list.
  > **The numbers in this entry are the promotion-time record and are deliberately
  > not updated** — they are what the three steps below were measured against. The
  > ratchet has since come down to **142** (run 32393655273), after
  > `modernize-concat-nested-namespaces` was disabled with its reason; see the
  > "clang-tidy ratchet re-measured" entry above for that. Rewriting 148 here would
  > detach the reasoning from the evidence it was drawn from.

  Steps 1–3 are landed and CI now blocks regressions with a **148**-finding
  ratchet (run 32274968429: 148 distinct, 491 raw). The budget has to be
  measured by the CI job itself, and a local number is not a substitute:
  macOS/libc++ and Ubuntu/libstdc++ disagree substantially even at the same
  clang-tidy version, and the versions differ too — locally this tree reports
  472 distinct and *zero* `modernize-type-traits`, where CI reported 739 and
  308. A ratchet set from the local 546 would have been ~3.7x slack and blocked
  nothing.

### 🛠️ CI/CD polish (finish #18)
- [ ] README badges (add a docs/Pages badge; CI + license already present).
- [x] **Release automation** (2026-08-20) — `tools/amalgamate.py` flattens the 60
  public headers into one file; `.github/workflows/release.yml` turns a `vX.Y.Z`
  tag into a GitHub Release with that file attached and the matching CHANGELOG
  section as the body. The tag triggers the release but is not trusted: the
  workflow rebuilds and re-tests the tagged tree, refuses to publish if the tag
  disagrees with `project(VERSION)` in CMakeLists.txt, and verifies the artifact
  the only way that means anything — it redirects every `metl/*.hpp` to the
  amalgamated file and runs the **entire test suite** through it (also a blocking
  `amalgamation` CI job, so it cannot drift between releases). Two things the
  generator has to get right and now does: `compiler.hpp` re-exports
  `attributes.hpp` from its last line while `attributes.hpp` includes
  `compiler.hpp` from its first, which is a cycle rather than a DAG — resolved
  positionally (an include before the file's own code orders it, one after only
  reaches it) rather than by a hardcoded exception; and a conditional
  `#include <intrin.h>` must stay inside its `_MSC_VER` guard, so only
  unconditional includes are hoisted.
- [x] Dedupe repeated apt installs via a composite action
  (`.github/actions/apt-install`), DONE 2026-08-19. It exists for reliability more
  than speed: the cross-toolchain jobs pull a **574 MB** `gcc-arm-none-eabi`, and
  when Ubuntu's Azure mirror wedges, `apt-get` stops making progress rather than
  failing — on 2026-08-19 that burned the full 15-minute budget in five jobs across
  three runs. Note that GitHub reports a `timeout-minutes` expiry as `cancelled`,
  not `timed_out`, so those runs looked like concurrency cancellations; tell the two
  apart by whether the job died at *exactly* the budget. The `timeout` wrappers in
  the action are the part that fixes this — a bare retry loop never reaches its
  second iteration when the command simply hangs.
- [ ] ccache caching.
- [ ] **(CI anti-pattern review 2026-08-05, deferred)** Collapse the five
  near-identical freestanding cross jobs (riscv-cross / arm-cross-clang /
  big-endian / newlib-link / picolibc-qemu) into one matrix or a composite
  `freestanding-syntax-check` action so the shared flag string lives once.
- [x] **(security)** Actions pinned to full commit SHAs. The third-party ones
  (`google/clusterfuzzlite`, `espressif/esp-idf-ci-action`) already were; what was
  still tag-pinned was GitHub's own `actions/*`, which OpenSSF Scorecard counts
  too. All of them now carry a SHA with the version in a trailing comment, so the
  human-readable version survives and Dependabot can still bump them.
- [x] **(caching)** Cache the Zephyr `west update` tree (re-cloned uncached every
  run, dominating the 60-min zephyr budget). The workspace cache is keyed to the
  pinned v3.7.0 release. apt is done, above.
- [ ] Cache pipx's clang-format environment separately; it is a small install
  and remains lower priority than the Zephyr workspace cache.
- [x] Root-cause fixes DONE 2026-08-05: hard-coded test-source paths → single
  `env:` source of truth; workflow-level `defaults.run.shell: bash`; fuzz-smoke
  harness list derived from built binaries; `.pre-commit-config.yaml` pins
  clang-format 18.1.8 so local == CI.
- [x] **gcc Release + `-Werror` hardening** (#14, done #43): gcc is back in the
  `release-werror` matrix. The old entry here named four diagnostics and got three
  of the four *locations* wrong — `-Wnull-dereference` was in the pointer-returning
  lookups in `handle_pool_test` / `static_unordered_map*_test`, not in
  `metl_check.hpp` (gcc only *reports* it there, after inlining `check_eq`), and
  `-Wclobbered` was reported against `assert.hpp`, not the setjmp test. Fixing from
  the old list would have edited the wrong files. Two process notes worth keeping:
  `make` stops at the first failing target, so a `-Werror` run only ever shows the
  diagnostics *up to* that point — measure with `-k` and warnings-as-errors **off**;
  and Apple's `g++` is clang, so gcc-specific diagnostics need a real gcc
  (`brew install gcc`) or the CI leg.
- [ ] **gcc 16 `-Waggressive-loop-optimizations` in `fixed_string::assign`** — gcc
  16 reports "iteration 4 invokes undefined behavior" at `fixed_string.hpp:179` for
  `fixed_string<3>::assign("toolong")`. It is a false positive: the
  `if (input_size > Capacity) return false;` guard makes the loop unreachable for
  that call, and the test runs clean under gcc-16 ASan+UBSan at `-O2`. Ubuntu's gcc
  does not emit it, so nothing is blocked. Left unchanged deliberately — reshaping a
  library loop to quiet a false positive costs more than it buys. Revisit if a
  release-line gcc starts reporting it.
- [x] **Zephyr** (#15) CI green — SDK wiring fixed (`ZEPHYR_SDK_INSTALL_DIR`); the
  `west build` structural gate and the QEMU twister run are **both** blocking as
  of 2026-08-21.
- [x] Get the **ESP-IDF** (#17) CI job to green and BLOCKING (2026-08-21): it was
  already green 24/24 across twelve main runs, so the flag was the only thing
  left. Same for the Zephyr twister run step (12/12).

### 📚 Library breadth (features)

> **Core is frozen for now.** The type surface is sufficient, and every addition
> costs test area, documented contract, constexpr/exception-safety review and
> release stability. New types wait for a concrete caller or workload rather than
> for someone to think of them.

- [x] **Container API completeness** (#37–#40) — `ring_buffer` and `fixed_deque`
  are iterable (shared random-access iterator on `detail::ring_core`, holding a
  logical index because a ring is not contiguous); `flat_map` and `flat_set` got
  the six relational operators they were missing while `fixed_vector` had six and
  `fixed_string` eight; `span`, `variant` and `expected` gained the runtime and
  short-circuit coverage they lacked.
- [ ] C++20-constexpr conversion of `expected` / `variant` / `fixed_vector` /
  `flat_map` (helper `detail/construct.hpp` is in place). Optional backlog, not a
  blocker: the C++17 baseline stays, and #36 established that the C++20 arm
  builds and passes. Recommended order — `expected`, `variant`, `fixed_vector`,
  `flat_map`/`flat_set` — one PR each, verifying size, alignment,
  exception-safety, ASan, C++17, C++20 and QEMU together.
- [x] **`intrusive_list` — decided, not pending.** Written up in `docs/SCOPE.md` §5
  (Green, "Not planned"): it reintroduces the use-after-free class that §7 argues
  handles were adopted to remove, and a caller would not change that argument. Not
  the same status as the two items below, which really are waiting for a caller.
- [ ] **Deferred pending a caller:** `fixed_bitset`, compile-time
  `static_string_map` / perfect hash. See the freeze note above.
- [ ] Iterator-invalidation contracts documented per container.
- [ ] Compile-time cost: continue trimming heavy std-header deps where safe (#11).

### 🌍 Environment breadth (deferred — integrate later)
- [ ] Zephyr covers many arches at once (once #15 is green). ESP32 Xtensa via #17.
- [ ] ARM Compiler 6 (partially proxied by arm-cross-clang) — document.
- [ ] IAR EWARM — proprietary, no free public CI; documented-only (no GNU-isms +
  `-Wpedantic` maximize compatibility).

---

### Cross-repo (not metl) — separate backlog
- [ ] traceglass: Biome format+lint + CI; grouping/baseline/diff features (see its ROADMAP).
- [ ] Add `.editorconfig` + clippy gate + rust-toolchain pin to qpci/firmwire/traceglass.
- [ ] Bump GitHub Actions to Node-24 majors in qpci/firmwire/traceglass.
- [ ] firmwire: confirm CI green after the libudev fix; wire more modules.
- [ ] qpci: CXL implementation (see `qpci/docs/CXL.md`) — ext-cap/DVSEC walker first.
