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
- [x] Per-symbol Doxygen docs across all 51 headers + non-standard-contract warnings
- [x] Examples (9, CI-compiled) + `docs/COOKBOOK.md`
- [x] Environment coverage: host (Linux/macOS/Windows × gcc/clang/MSVC × Debug/Release/MinSizeRel),
  ASan/UBSan, TSAN (real threaded tests), LTO, ARM Cortex-M (gcc + clang frontends),
  RISC-V rv64, PowerPC64 big-endian, per-header self-containment
- [x] Embedded libc: newlib-nano link + **picolibc + QEMU semihosting run**
- [x] RTOS/frameworks: Zephyr module + ESP-IDF component (+ samples) — *CI jobs provisional*
- [x] Release + `-Werror` hygiene gate; Doxygen 1.9.8 compat
- [x] CI/CD: **preflight fail-fast gate** (dependency DAG, not flat fan-out),
  **GitHub Pages docs deploy** (gated on validation) → https://hawk90.github.io/metl/,
  Dependabot (github-actions), actions on Node-24 (checkout@v5 / upload-artifact@v7),
  macos-14 pinned

## ☐ Open — by priority

### 🔐 Bug bounty / security (highest signal for a safety-claiming lib)
- [ ] **Fuzzing harnesses** (libFuzzer/AFL, built under ASan+UBSan): `fixed_string`
  from untrusted input, `flat_map`/`static_unordered_map` random op sequences,
  arena/static allocators, crc. Add a short CI fuzz-smoke per PR.
- [ ] **OSS-Fuzz registration** — free 24/7 continuous fuzzing + reporting (de-facto
  continuous bug bounty). Requires a project config PR upstream.
- [ ] **SECURITY.md** — vulnerability disclosure policy.
- [ ] **CodeQL** security scan workflow.
- [ ] **OSSF Scorecard** (posture badge) + optionally SLSA/signed releases.

### 📦 Distribution / adoption
- [ ] **vcpkg** port (`portfile.cmake` + `vcpkg.json`) → `vcpkg install metl`.
- [ ] **Conan** recipe (`conanfile.py`).
- [ ] Submit to the ESP-IDF Component Registry (component manifest already present).

### 📊 Quality / claims
- [ ] **Benchmarks** — replace the dead `metl_cc_benchmark` stub with real
  google/benchmark micro-benchmarks (push/pop, flat_map lookup, spsc throughput)
  + a CI job. (Backs the "benchmarks" scaffolding claim.)
- [ ] **Coverage** gate (llvm-cov/codecov) — audit gap; cover the `try_*`/full-container
  branches.
- [ ] Promote **clang-tidy** from advisory to blocking (after fixing findings).

### 🛠️ CI/CD polish (finish #18)
- [ ] README badges (add a docs/Pages badge; CI + license already present).
- [ ] **Release automation** — tag → GitHub Release with changelog + a **single-header
  amalgamation** artifact (great for a header-only lib).
- [ ] ccache caching; dedupe repeated checkout+apt via a composite action.
- [ ] **gcc Release + `-Werror` hardening** (#14): fix `-Wclobbered` (setjmp assert
  test → `volatile`/restructure), `-Wterminate` (expected swap), `-Waddress`
  (lookup_table_test), `-Wnull-dereference` (metl_check); then re-enable gcc in the
  release-werror matrix.
- [ ] Get the provisional **Zephyr** (#15) and **ESP-IDF** (#17) CI jobs to green
  (west workspace / IDF image + target tuning), then drop `continue-on-error`.

### 📚 Library breadth (features)
- [ ] Finish C++20-constexpr conversion of `expected` / `variant` / `fixed_vector` /
  `flat_map` (helper `detail/construct.hpp` is in place; each is mechanical now).
- [ ] New utilities: `fixed_bitset`, compile-time `static_string_map`/perfect-hash,
  `expected` monadic ops (`and_then`/`transform`/`or_else`), a header self-containment
  runtime test, iterator-invalidation contracts documented per container.
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
