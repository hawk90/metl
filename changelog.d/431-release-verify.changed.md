- **A release is verified on the tagged commit itself.** A pull request's CI
  run is no longer repeated when it merges: `main.yml` checks that the run
  passed on the exact tree the merge put on `main` and measures the commit,
  and runs the full CI when it cannot show that. A `v*` tag publishes only a
  commit whose `main-gate` and CodeQL analysis passed, and only after the full
  CI has run again on the tagged commit (`tools/release_gate.py`, #431). The
  full CI also runs nightly on `main`.
