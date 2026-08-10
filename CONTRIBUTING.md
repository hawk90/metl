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
[`.clang-format`](.clang-format) configuration. Format your changes before
opening a pull request:

```sh
clang-format -i $(git diff --name-only --diff-filter=ACMR | grep -E '\.(hpp|cpp)$')
```

## Static checks

Run `clang-tidy` over changed translation units. The project ships a
`.clang-tidy` configuration; please address new diagnostics introduced by
your change rather than disabling them globally.

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
- Tests live under [`tests/`](tests/) and are registered via
  `metl_cc_test()` in the corresponding `CMakeLists.txt`.
- New tests must pass under the sanitizer configuration above.

## Pull request checklist

Before requesting review:

- [ ] Build is warning-clean with `-DMETL_WARNINGS_AS_ERRORS=ON`.
- [ ] `ctest` passes locally.
- [ ] Tests pass under ASAN + UBSAN.
- [ ] `clang-format` and `clang-tidy` have been run.
- [ ] [`CHANGELOG.md`](CHANGELOG.md) is updated under the `[Unreleased]`
      section.
- [ ] Public API changes are documented in headers and, where appropriate,
      in [`README.md`](README.md).
- [ ] The [`docs/SCOPE.md`](docs/SCOPE.md) invariant checklist is satisfied —
      including a stated progress guarantee (wait-free bounded / lock-free /
      blocking bounded) for anything concurrent, and a CI job for any new tier.

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
