# Contributing to METL

Thank you for your interest in contributing. METL targets deterministic,
embedded-oriented systems, so contributions are held to a high standard for
correctness, predictability, and portability.

## Is my change in scope?

Read [`docs/SCOPE.md`](docs/SCOPE.md) first. The test is not "does it work on an
MCU?" but whether the change keeps METL's five invariants: no heap, no
exceptions/RTTI, deterministic (bounded worst-case), header-only C++17, and
self-contained headers. Keep all five and the topic is in scope whatever it is;
break one and it is out of scope however embedded it sounds.

Two consequences worth knowing before you start:

- **A tier and its CI job arrive in the same PR.** If your change claims support
  for a target the CI matrix does not already cover, the job proving it belongs
  in your PR. No job, no tier.
- **Experimental work is welcome** under `metl::exp::` / `metl/exp/`, outside the
  umbrella header and outside the API stability promise.

## Reporting bugs

File bug reports as GitHub issues. A useful report includes:

- A short, self-contained reproduction.
- Compiler and target (host triple or MCU family).
- The exact CMake configuration used.
- Observed versus expected behavior.

## Building locally

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build -j
```

A release build is configured with `-DCMAKE_BUILD_TYPE=Release`. To enable
warnings-as-errors during development, add `-DMETL_WARNINGS_AS_ERRORS=ON`.

## Code style

All C++ source is formatted with `clang-format` using the project
[`.clang-format`](.clang-format) configuration, at the **exact version** the CI
`clang-format` job installs (23.1.2). Use the pinned pre-commit hook rather than
whatever `clang-format` is on your `PATH` -- a different major version reformats
lines the CI one leaves alone:

```sh
pipx install pre-commit && pre-commit install   # once
pre-commit run clang-format --files $(git diff --name-only --diff-filter=ACMR | grep -E '\.(hpp|cpp)$')
```

Format only the files you changed; do not reformat whole headers by hand. The
pre-commit `rev` and the CI pin move together (a CI step fails if they differ).

## Static checks

Run `clang-tidy` over changed translation units. The project ships a
`.clang-tidy` configuration; please address new diagnostics introduced by
your change rather than disabling them globally. CI enforces a **ratchet on the
count of distinct findings** (`tools/clang_tidy_report.py --max`): a change may
lower it, never raise it. Patterns that have tripped it: a forwarding reference
that is not `std::forward`ed, an rvalue-reference parameter that is not
`std::move`d, identical `if constexpr` branch bodies, `!(a && b)` inside
`METL_HARDEN`/`METL_ASSERT`, and a function name ending in `_`.

## Before you push

`pre-commit install` also registers a **pre-push hook**, `tools/pre_push.py`,
which runs every CI check that can run on a workstation before the push leaves
it: the checkers' self-tests, the source gates below, a Release `-Werror` build
and ctest with each compiler it finds (the default `c++` and the newest
`g++-NN`), and an ASan+UBSan build. About 20 s with `--quick` (no builds), a few
minutes cold, less once its `build-prepush/` directories are warm. `--full` adds
the mutation gate. The ARM size/stack/RAM budgets, the instruction counts,
QEMU, Zephyr, ESP-IDF and the clang-tidy count stay on CI: they are measured on
CI's toolchains. `git push --no-verify` skips the hook; CI still runs
everything.

## Gates

Beyond build and tests, CI runs scripts that each hold one claim of the
repository. `tools/pre_push.py` runs the ones that need no CI toolchain; run the
others your change touches:

| Script | Holds | Typical trigger |
|---|---|---|
| `tools/check_api_contract.py` | `try_*` naming / `[[nodiscard]]` rules | a new function returning `bool` (a query goes in its `BOOL_ALLOWLIST` with a reason) |
| `tools/check_progress_guarantee.py` | every public header states its progress guarantee | a new public header |
| `tools/check_compile_fail.py` | every user-facing `static_assert` is pinned by a case in `tests/compile_fail/` | a new `static_assert` outside `detail::` |
| `tools/check_docs.py` | `metl::` names, links, examples and README test counts are real | docs edits; a new test changes the QEMU count -- take it from `tools/run_qemu_tests.py --plan --cpu <cpu>` |
| `tools/check_mutants.py --build-dir <dir>` | the test suite still kills planted bugs | changing a function the mutants anchor on (e.g. a signature) |
| `tools/check_ci_gate.py` | `ci-gate` fans in every CI job | a new CI job |

A new header under `include/metl/detail/` must also be added to
`include/metl/metl.hpp` (the umbrella test covers `detail/`).

## Sanitizers

All host tests must pass cleanly under AddressSanitizer and
UndefinedBehaviorSanitizer:

```sh
cmake -B build-san -S . \
    -DMETL_ENABLE_ASAN=ON \
    -DMETL_ENABLE_UBSAN=ON \
    -DMETL_WARNINGS_AS_ERRORS=ON
cmake --build build-san -j
ctest --test-dir build-san -j
```

If your change involves concurrency primitives, also verify it under
ThreadSanitizer (`-DMETL_ENABLE_TSAN=ON`, configured separately).

## Test requirements

- Every public API must be covered by at least one test.
- Host-only tests are acceptable; tests must not require target hardware.
- Tests live under [`tests/`](tests/) and are registered by adding them to the
  `_metl_tests` list in the root `CMakeLists.txt` (or with `metl_cc_test()` when
  they need options). Tests run freestanding on QEMU too unless listed, with a
  reason, in `tools/run_qemu_tests.py`'s deny-list.
- New tests must pass under the sanitizer configuration above.
- **A regression test must fail first.** Run it against the unfixed headers
  (`git archive origin/main include | tar -x -C <dir>`), compiled on its own,
  and show it failing; a test that has never been red proves only that it
  compiles. For lifetime bugs, detect dead or moved-from objects with a type
  that poisons itself or registers its address -- a "destroyed" flag written in
  a destructor is a dead store the optimiser removes.

## Pull request checklist

Before requesting review:

- [ ] Build is warning-clean with `-DMETL_WARNINGS_AS_ERRORS=ON`.
- [ ] `ctest` passes locally.
- [ ] Tests pass under ASAN + UBSAN.
- [ ] `clang-format` (pinned hook) and `clang-tidy` have been run; the gates
      above that your change touches pass.
- [ ] A bug fix comes with a regression test that failed before the fix.
- [ ] [`CHANGELOG.md`](CHANGELOG.md) is updated under the `[Unreleased]`
      section. A change that breaks or silently alters existing code says so --
      after 1.0 that means a new major version (see README "Status").
- [ ] Public API changes are documented in headers and, where appropriate,
      in [`README.md`](README.md).
- [ ] The [`docs/SCOPE.md`](docs/SCOPE.md) invariant checklist is satisfied —
      including a stated progress guarantee (wait-free bounded / lock-free /
      blocking bounded) in every public header
      (`tools/check_progress_guarantee.py`), and a CI job for any new tier.

## Commit messages

Follow the [Conventional Commits](https://www.conventionalcommits.org/) format:

```
<type>(<scope>): <subject>

[optional body]

[optional footer]
```

Common types: `feat`, `fix`, `docs`, `refactor`, `perf`, `test`, `build`,
`chore`. Examples:

```
feat(fixed_vector): add insert_range overload
fix(spsc_queue): correct memory order on pop
docs(readme): document CMake integration
```

Keep the subject in the imperative mood and under 72 characters. Reference
issues in the footer with `Closes #NNN` where appropriate.
