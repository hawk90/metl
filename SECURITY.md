# Security Policy

metl is a header-only, zero-dependency C++17 library. It runs entirely in the
address space of the program that includes it — there is no service, network
surface, or privileged component. "Security" for metl therefore means **memory
safety and contract correctness**: a defect that lets untrusted input trigger
out-of-bounds access, undefined behavior, a use-after-free, or a
memory-corruption path is treated as a security issue.

## Supported versions

metl is pre-1.0 and ships from a single line of development.

| Version        | Supported                          |
| -------------- | ---------------------------------- |
| `main` (HEAD)  | ✅ Yes — fixes land here first     |
| latest `0.1.x` | ✅ Yes                             |
| older tags     | ❌ No — please upgrade             |

Because metl is header-only, "patching" is upgrading the vendored headers; there
are no binary artifacts to rebuild or redistribute.

## Reporting a vulnerability

Please report suspected vulnerabilities **privately** — do not open a public
issue for a security-relevant defect until a fix is available.

- Preferred: open a
  [GitHub private security advisory](https://github.com/hawk90/metl/security/advisories/new)
  ("Report a vulnerability"). This keeps the report confidential and lets us
  collaborate on a fix and coordinated disclosure.
- Alternatively, email the maintainer with `metl security` in the subject.

Please include, where possible:

- the affected header(s) and metl version / commit,
- a minimal reproducer (ideally a fuzz input or a short `LLVMFuzzerTestOneInput`
  slice — see `fuzz/`),
- the sanitizer report (ASan/UBSan stack trace) if you have one, and
- the compiler, standard version, and target (host / ARM / RISC-V / etc.).

## Response expectations

This is a small open-source project maintained on a best-effort basis. We aim to:

- acknowledge a report within **7 days**,
- confirm/triage and share an initial assessment within **30 days**, and
- release a fix on `main` (with a `CHANGELOG.md` / `docs/AUDIT.md` entry)
  before any public disclosure, coordinating timing with the reporter.

We credit reporters in the advisory and changelog unless you ask otherwise.

## A note on contracts (what is NOT a vulnerability)

metl deliberately uses **assert-based preconditions**. By design, violating a
documented precondition — pushing past a fixed capacity, popping an empty
container, indexing out of range — triggers `METL_ASSERT`, whose default handler
is `[[noreturn]]` and calls `std::abort()`. This is *contractually correct*
fail-fast behavior, not a vulnerability: it turns a programming error into an
immediate, well-defined abort rather than silent corruption.

Accordingly:

- An abort from a **documented precondition violation** is expected behavior.
  Use the `try_*` members (e.g. `try_push_back`, `try_emplace`, `try_allocate`)
  or check `size()`/`capacity()`/`empty()` first for a non-aborting path.
- A **memory-safety failure reachable through a contract-valid API** (only using
  `try_*`/bounded operations and still getting a heap/stack overflow, UB, leak,
  or uninitialized read) **is** a security issue — please report it.

## Continuous fuzzing

metl is continuously exercised for memory safety:

- **In-repo fuzz smoke** — the `fuzz-smoke` CI job builds the libFuzzer
  harnesses under `fuzz/` with `-fsanitize=fuzzer,address,undefined` and runs
  each on every push/PR. The harnesses drive the containers with **contract-
  valid operation streams only**, so any ASan/UBSan finding is a real defect.
- **ClusterFuzzLite** — `.clusterfuzzlite/` plus the `cflite-*` workflows run
  the OSS-Fuzz toolchain directly in GitHub Actions (per-PR fuzzing + a
  scheduled batch run). No upstream registration is required.
- **OSS-Fuzz (optional/future)** — the ClusterFuzzLite build (`build.sh` +
  `Dockerfile`) is OSS-Fuzz-compatible, so upstream registration with
  google/oss-fuzz for free 24/7 continuous fuzzing is a drop-in follow-up if
  desired. See `docs/TODO.md`.

You can run the harnesses locally with a libFuzzer-capable Clang:

```sh
cmake -B build-fuzz -S . -DMETL_BUILD_TESTS=OFF -DMETL_BUILD_FUZZERS=ON
cmake --build build-fuzz -j
./build-fuzz/fuzz/metl_fuzz_flat_map -max_total_time=30 fuzz/corpus/fuzz_flat_map
```
