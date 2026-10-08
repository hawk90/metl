# Security Policy

metl is a header-only, zero-dependency C++17 library. It runs entirely in the
address space of the program that includes it — there is no service, network
surface, or privileged component. "Security" for metl therefore means **memory
safety and contract correctness**: a defect that lets untrusted input trigger
out-of-bounds access, undefined behavior, a use-after-free, or a
memory-corruption path is treated as a security issue.

## Supported versions

metl follows Semantic Versioning from 1.0.0 and ships from a single line of
development: security fixes land on `main` and in the next release of the
current major version.

| Version          | Supported                                    |
| ---------------- | -------------------------------------------- |
| `main` (HEAD)    | ✅ Yes — fixes land here first               |
| latest `1.x`     | ✅ Yes                                       |
| `0.1.0-alpha*`   | ❌ No — pre-releases; please upgrade to 1.x  |

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
- release a fix on `main` (with a `CHANGELOG.md` entry and a regression test)
  before any public disclosure, coordinating timing with the reporter.

We credit reporters in the advisory and changelog unless you ask otherwise.

## A note on contracts (what is NOT a vulnerability)

metl deliberately uses **assert-based preconditions**. By design, violating a
documented precondition — pushing past a fixed capacity, popping an empty
container, indexing out of range — triggers `METL_ASSERT`, whose default handler
is `[[noreturn]]` and calls `std::abort()`. This is *contractually correct*
fail-fast behavior, not a vulnerability: it turns a programming error into an
immediate, well-defined abort rather than silent corruption.
Building with `METL_HARDENING=METL_HARDENING_NONE` compiles `METL_ASSERT` out,
and a violated precondition is then undefined behaviour; only the `METL_HARDEN`
guards against out-of-bounds writes stay on (see `include/metl/config.hpp`).

Accordingly:

- An abort from a **documented precondition violation** is expected behavior.
  Use the `try_*` members (e.g. `try_push_back`, `try_emplace`, `try_allocate`)
  or check `size()`/`capacity()`/`empty()` first for a non-aborting path.
- A **memory-safety failure reachable through a contract-valid API** (only using
  `try_*`/bounded operations and still getting a heap/stack overflow, UB, leak,
  or uninitialized read) **is** a security issue — please report it.

## Verifying what you vendored

metl is normally used by copying headers into your tree, which means that from
the moment you copy them there is no package manager, checksum database, or
update path standing behind the files. So every release publishes **build
provenance**: a Sigstore-signed statement that the single-header artifact came
out of this repository's release workflow, at a named commit, on GitHub's
runners. It is signed with a short-lived certificate, so there is no long-lived
key for this project to hold or lose.

```sh
gh attestation verify metl-<version>-single.hpp --repo hawk90/metl
```

Each release also attaches the attestation bundle
(`metl-<version>-single.hpp.intoto.jsonl`) so the check still works offline,
from a mirror, or if this repository is gone:

```sh
gh attestation verify metl-<version>-single.hpp \
  --bundle metl-<version>-single.hpp.intoto.jsonl \
  --repo hawk90/metl
```

Provenance says where a file came from. It does not say the code is correct —
that is what everything else in this document and in CI is for.

## Supply-chain posture

- **Every GitHub Action is pinned to a commit SHA**, not a tag. A tag can be
  moved; a SHA cannot.
- **Renovate** proposes updates weekly: action SHAs, and the clang-format,
  Conan, Zephyr and ESP-IDF pins. A new version waits until it has been
  published for a few days before it is proposed. Non-major updates merge
  automatically once CI is green, except Zephyr and ESP-IDF minors, which —
  like every major — wait for approval on the dependency dashboard. Security
  alerts arrive whenever they are published. `renovate.json` is the source of
  truth.
- **CodeQL** runs on every push and PR, and weekly so a newly published query
  finds the code without waiting for a commit.
- **OpenSSF Scorecard** runs weekly against this repository. Its results go to
  the Security tab and are **not published** to the public OpenSSF API; the
  `scorecard.yml` header explains what the score is and is not good for, and
  which of its checks are wrong for a header-only library.

## Continuous fuzzing

metl is continuously exercised for memory safety:

- **In-repo fuzz smoke** — the `fuzz-smoke` CI job builds the libFuzzer
  harnesses under `fuzz/` with `-fsanitize=fuzzer,address,undefined` and runs
  each on every push/PR. The harnesses drive the containers with **contract-
  valid operation streams only**, so any ASan/UBSan finding is a real defect.
- **ClusterFuzzLite** — `.clusterfuzzlite/` plus the `cflite-*` workflows run
  the OSS-Fuzz toolchain directly in GitHub Actions: per-PR fuzzing of the code
  a change touches, a nightly batch run that grows a corpus so the PR runs start
  warm, and a daily cron that prunes that corpus and measures what the harnesses
  actually reach. All of them **block**; none did until 2026-08-22, and the
  nightly run had been discarding its corpus for its entire history.
- **OSS-Fuzz — not submitted, and not "pending".** OSS-Fuzz's stated bar is that
  a project "must have a significant user base and/or be critical to the global
  IT infrastructure". METL has only just reached 1.0 and has no known dependents, so it does not
  meet that bar, and the honest thing is to say so rather than leave it on a
  list as if it were queued. The build wiring is ready and is exercised on every
  PR; the project is not. Revisit when there are users to point at.

You can run the harnesses locally with a libFuzzer-capable Clang:

```sh
cmake -B build-fuzz -S . -DMETL_BUILD_TESTS=OFF -DMETL_BUILD_FUZZERS=ON
cmake --build build-fuzz -j
./build-fuzz/fuzz/metl_fuzz_flat_map -max_total_time=30 fuzz/corpus/fuzz_flat_map
```
