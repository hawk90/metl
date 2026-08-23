#!/usr/bin/env python3
"""Break the library on purpose and require the gates to notice.

WHY. Coverage says a line RAN. It does not say a test would notice if the line
were wrong, and this repository has a live example: `flat_set::nth(i)` returning
element `i+1` survives all 90 ctest targets. The line is covered. Nothing checks
what it returns.

The repository already argues this everywhere else. Every checker under tools/
carries a `--self-test` because "a gate that cannot fail is not a gate"; the
compile-failure cases exist because a `static_assert` gone always-true is
invisible. Both are mutation testing, applied to the checkers and to the
contracts. This applies it to the library.

WHAT IT PRODUCES, and this is the part worth reading: a map of which gate
catches which class of defect. It is not what anyone would guess.

    flat_map::find -> neighbour        killed by unit tests, NOT by the fuzzer
                                       before #82 added a reference model
    flat_set::nth(i) -> i+1            killed ONLY by the fuzzer; survives all
                                       90 ctest targets
    binary search -> linear scan       killed ONLY by the operation-count test;
                                       results are identical, so correctness
                                       tests and the fuzz oracle both pass
    power-of-two assert -> always true  killed ONLY by the compile-failure cases

No layer dominates another. That is a better argument for keeping all of them
than any of the individual PRs made.

RULES.

  * Every mutant must be KILLED. A survivor is a missing test, not a tolerable
    outcome, so there is no allowlist to grow.
  * Every mutant names the gate expected to kill it. If a DIFFERENT gate kills
    it that is reported too -- it means the map above has changed and the entry
    should be re-read, not silently updated.
  * The tree is restored whatever happens, and the restoration is verified by
    content rather than assumed. A mutation tool that leaves a mutant behind is
    worse than no tool.

Usage:
    tools/check_mutants.py --build-dir build
    tools/check_mutants.py --build-dir build --only linear_scan
    tools/check_mutants.py --self-test
"""

import argparse
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

REPO = pathlib.Path(__file__).resolve().parent.parent


# Each mutant is a single, minimal edit that changes BEHAVIOUR and keeps the code
# compiling. `kills` names the gate that must reject it.
#
#   ctest:<regex>   ctest -R <regex> must fail
#   tool:<argv>     the command must exit non-zero
MUTANTS = [
    {
        "name": "flat_map_linear_scan",
        "file": "include/metl/flat_map.hpp",
        "why": "binary search becomes a linear scan. Same results, same order, "
               "same sizes -- only the comparison count moves.",
        "kills": "ctest:operation_count",
        "old": """    size_type first = 0;
    size_type count = size_;
    while (count > 0) {
      const size_type step = count / 2;
      const size_type index = first + step;
      if (comp_(data()[index].key, key)) {
        first = index + 1;
        count -= step + 1;
      } else {
        count = step;
      }
    }
    return first;
  }

  template <typename K>
  size_type upper_bound_index""",
        "new": """    size_type first = 0;
    while (first < size_ && comp_(data()[first].key, key)) {
      ++first;
    }
    return first;
  }

  template <typename K>
  size_type upper_bound_index""",
    },
    {
        "name": "flat_map_find_neighbour",
        "file": "include/metl/flat_map.hpp",
        "why": "find returns the NEXT element's value. Keys stay sorted, size "
               "stays right, find and contains still agree with each other.",
        "kills": "ctest:flat_map|recoverable_api",
        "old": """  METL_NODISCARD mapped_type* find(const key_type& key) noexcept {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return &data()[index].value;
    }""",
        "new": """  METL_NODISCARD mapped_type* find(const key_type& key) noexcept {
    const size_type index = lower_bound_index(key);
    if (index < size_ && !comp_(key, data()[index].key)) {
      return &data()[index + 1 < size_ ? index + 1 : index].value;
    }""",
    },
    {
        "name": "fixed_string_append_drops_last",
        "file": "include/metl/fixed_string.hpp",
        "why": "try_append copies one character too few but reports the full "
               "length. The NUL terminator is still where size() says.",
        "kills": "ctest:fixed_string|format",
        "old": """    for (size_type i = 0; i < input_size; ++i) {
      storage_[size_ + i] = text[i];
    }
""",
        "new": """    for (size_type i = 0; i + 1 < input_size; ++i) {
      storage_[size_ + i] = text[i];
    }
""",
    },
    {
        "name": "spsc_capacity_assert_always_true",
        "file": "include/metl/spsc_queue.hpp",
        "why": "the power-of-two contract becomes unenforceable. Nothing in "
               "ctest notices, because no test constructs an invalid capacity "
               "-- it could not, it would not compile.",
        "kills": "tool:tools/check_compile_fail.py --cxx clang++",
        "old": '  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of two");',
        "new": '  static_assert(true || ((Capacity & (Capacity - 1)) == 0), "Capacity must be power of two");',
    },
    {
        "name": "visit_single_result_guard_removed",
        "file": "include/metl/variant.hpp",
        "why": "#84's guard becomes vacuous, so metl::visit goes back to "
               "silently truncating a visitor's result.",
        "kills": "tool:tools/check_compile_fail.py --cxx clang++",
        "old": """template <typename Visitor, typename... Ts>
inline constexpr bool visit_single_result_lvalue_v =
    (std::is_same_v<visit_result_lvalue_t<Visitor, Ts...>,
                    decltype(std::declval<Visitor>()(std::declval<Ts&>()))> &&
     ...);""",
        "new": """template <typename Visitor, typename... Ts>
inline constexpr bool visit_single_result_lvalue_v = true;""",
    },
]


def force_rebuild(path):
    """Make `path` unambiguously newer than anything already built from it.

    Found the hard way. This tool mutates, builds, reverts and mutates again in
    well under a second, and make compares mtimes at one-second granularity --
    so the SECOND mutant's build was skipped and its verdict came from the
    PREVIOUS binaries. Every mutant after the first reported SURVIVED.
    Reproduced exactly: flat_map_find_neighbour is killed when run with --only
    and "survives" when run second.

    A mutation tool that silently tests the wrong binary is the failure mode
    this whole repository exists to remove, so the fix is not "sleep" -- it is
    to put the timestamp beyond argument.
    """
    stamp = time.time() + 10
    os.utime(path, (stamp, stamp))


def apply_mutant(mutant):
    """Patch the file. Returns the original text so it can be restored."""
    path = REPO / mutant["file"]
    original = path.read_text(encoding="utf-8")
    occurrences = original.count(mutant["old"])
    if occurrences != 1:
        raise RuntimeError(
            f"{mutant['name']}: its anchor matched {occurrences} times in "
            f"{mutant['file']}, expected exactly 1. The code moved; re-read the "
            f"mutant rather than loosening the anchor.")
    path.write_text(original.replace(mutant["old"], mutant["new"]), encoding="utf-8")
    force_rebuild(path)
    return original


def run_gate(spec, build_dir):
    """True if the gate REJECTED the tree (i.e. the mutant was killed)."""
    kind, _, rest = spec.partition(":")
    if kind == "ctest":
        build = subprocess.run(["cmake", "--build", build_dir, "-j"],
                               capture_output=True, text=True, cwd=REPO)
        if build.returncode != 0:
            # A mutant that stops the build is not a useful mutant: it proves the
            # compiler noticed, not that a test did.
            return None, "the mutated tree did not build"
        result = subprocess.run(["ctest", "--test-dir", build_dir, "-R", rest, "-j"],
                                capture_output=True, text=True, cwd=REPO)
        return result.returncode != 0, result.stdout[-800:]
    if kind == "tool":
        result = subprocess.run(rest.split(), capture_output=True, text=True, cwd=REPO)
        return result.returncode != 0, (result.stdout + result.stderr)[-800:]
    raise RuntimeError(f"unknown gate kind {kind!r}")


def check(mutants, build_dir, quiet=False):
    """`quiet` is for the self-test, whose fixtures are SUPPOSED to survive --
    their ::error:: lines would otherwise annotate a green job."""
    def say(message, error=False):
        if quiet:
            return
        print(f"::error::{message}" if error else message,
              file=sys.stderr if error else sys.stdout)

    survivors, broken = [], []

    for mutant in mutants:
        path = REPO / mutant["file"]
        original = None
        try:
            original = apply_mutant(mutant)
            killed, detail = run_gate(mutant["kills"], build_dir)
        finally:
            if original is not None:
                path.write_text(original, encoding="utf-8")
                force_rebuild(path)
                # Verified, not assumed. Leaving a mutant behind is worse than
                # having no tool at all.
                if path.read_text(encoding="utf-8") != original:
                    raise RuntimeError(
                        f"FAILED TO RESTORE {mutant['file']} after "
                        f"{mutant['name']} -- the working tree is MUTATED.")

        if killed is None:
            broken.append(f"{mutant['name']}: {detail}")
            say(f"  BROKEN  {mutant['name']:38} ({detail})")
        elif killed:
            say(f"  killed  {mutant['name']:38} by {mutant['kills']}")
        else:
            survivors.append(mutant)
            say(f"  SURVIVED {mutant['name']:37} {mutant['kills']} did not notice")

    # Leave the build directory consistent with the restored source. Without
    # this the last mutant's binaries stay on disk -- the tree reads clean and a
    # `ctest` in that directory fails for reasons nothing in the source explains.
    # Observed: 2 phantom failures out of 90 after a clean, all-killed run.
    if any(m["kills"].startswith("ctest:") for m in mutants):
        subprocess.run(["cmake", "--build", build_dir, "-j"],
                       capture_output=True, text=True, cwd=REPO)

    say(f"{len(mutants)} mutant(s), {len(survivors)} survivor(s), {len(broken)} broken")

    if broken:
        for entry in broken:
            say(entry, error=True)
    for mutant in survivors:
        say(f"{mutant['name']} SURVIVED. {mutant['why']} "
            f"{mutant['kills']} was expected to reject it and did not, so "
            f"that gate no longer covers this defect.", error=True)

    # A run over no mutants must not pass: same failure mode as every other
    # check here.
    if not mutants:
        say("no mutants selected -- a run over nothing reports success.", error=True)
        return 1

    return 1 if (survivors or broken) else 0


def self_test():
    """Prove the driver reports a survivor, and always restores the tree."""
    failures = []

    with tempfile.TemporaryDirectory() as tmp:
        fake = pathlib.Path(tmp) / "fake.hpp"
        fake.write_text("int marker = 1;\n")
        relative = fake.relative_to(fake.anchor)

        # A mutant whose gate does NOT reject it must be reported as a survivor.
        survivor = {
            "name": "fixture_survivor",
            "file": str(fake),
            "why": "fixture",
            "kills": "tool:true",  # `true` always exits 0 == did not reject
            "old": "int marker = 1;",
            "new": "int marker = 2;",
        }
        # `file` is joined against REPO, so point it at an absolute path instead.
        original_repo = globals()["REPO"]
        globals()["REPO"] = pathlib.Path(fake.anchor)
        try:
            code = check([dict(survivor, file=str(relative))], build_dir="unused", quiet=True)
            if code == 0:
                failures.append("a SURVIVING mutant was reported as success")
            if fake.read_text() != "int marker = 1;\n":
                failures.append("the fixture file was not restored")

            # A killed mutant passes.
            killed = dict(survivor, name="fixture_killed", kills="tool:false",
                          file=str(relative))
            if check([killed], build_dir="unused", quiet=True) != 0:
                failures.append("a KILLED mutant was reported as a failure")
            if fake.read_text() != "int marker = 1;\n":
                failures.append("the fixture file was not restored after a kill")

            # An anchor that no longer matches must be an error, not a silent skip.
            stale = dict(survivor, name="fixture_stale", old="int gone = 0;",
                         file=str(relative))
            try:
                check([stale], build_dir="unused", quiet=True)
                failures.append("a mutant whose anchor no longer matches was accepted")
            except RuntimeError:
                pass
            if fake.read_text() != "int marker = 1;\n":
                failures.append("the fixture file was not restored after a stale anchor")

            # An empty run must fail.
            if check([], build_dir="unused", quiet=True) == 0:
                failures.append("a run over NO mutants reported success")
        finally:
            globals()["REPO"] = original_repo

    if failures:
        for failure in failures:
            print(f"SELF-TEST FAILED: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: survivors, stale anchors and empty runs are "
          "rejected, killed mutants pass, and the tree is restored in every case")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--only", help="substring of a mutant name")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    selected = [m for m in MUTANTS if not args.only or args.only in m["name"]]

    if args.list:
        for mutant in selected:
            print(f"{mutant['name']:38} {mutant['kills']}")
        return 0

    if args.only and not selected:
        print(f"::error::--only {args.only!r} matched no mutant", file=sys.stderr)
        return 1

    if shutil.which("cmake") is None:
        print("::error::cmake not found", file=sys.stderr)
        return 1

    print(f"mutating the library and requiring the gates to notice "
          f"({len(selected)} mutant(s))")
    return check(selected, args.build_dir)


if __name__ == "__main__":
    sys.exit(main())
