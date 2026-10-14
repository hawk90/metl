#!/usr/bin/env python3
"""Break the library on purpose and require the gates to notice.

WHY. Coverage says a line RAN. It does not say a test would notice if the line
were wrong, and this repository had a live example: `flat_set::nth(i)` returning
element `i+1` survived every ctest target. The line was covered. Nothing checked
what it returned, until the fuzz harness's oracle was made runnable under ctest
(replay_fuzz_flat_set) -- see the map below.

The repository already argues this everywhere else. Every checker under tools/
carries a `--self-test` because "a gate that cannot fail is not a gate"; the
compile-failure cases exist because a `static_assert` gone always-true is
invisible. Both are mutation testing, applied to the checkers and to the
contracts. This applies it to the library.

WHAT IT PRODUCES, and this is the part worth reading: a map of which gate
catches which class of defect. It is not what anyone would guess.

    flat_map::find -> neighbour        killed by unit tests, NOT by the fuzzer
                                       before #82 added a reference model
    flat_set::nth(i) -> i+1            killed ONLY by the fuzz harness's oracle.
                                       It survived every other ctest target, and
                                       was unreachable from here at all until
                                       fuzz/replay_main.cpp let the harnesses run
                                       without libFuzzer.
    binary search -> linear scan       killed ONLY by the operation-count test;
                                       results are identical, so correctness
                                       tests and the fuzz oracle both pass
    power-of-two assert -> always true  killed ONLY by the compile-failure cases

No layer dominates another. That is a better argument for keeping all of them
than any of the individual PRs made.

RULES.

  * Every mutant must be KILLED. A survivor is a missing test, not a tolerable
    outcome, so there is no allowlist to grow.
  * Every mutant names the gate expected to kill it.
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
import runpy
import shutil
import subprocess
import sys
import tempfile
import time

REPO = pathlib.Path(__file__).resolve().parent.parent

# Per-test ceiling while a mutant is applied; see run_gate.
CTEST_TIMEOUT_SECONDS = 120


# Each mutant is a single, minimal edit that changes BEHAVIOUR and keeps the code
# compiling. `kills` names the gate that must reject it.
#
#   ctest:<regex>   ctest -R <regex> must fail
#   tsan:<target>   the test target, built with ThreadSanitizer, must fail
#   qemu:<cpu>:<test>  the test, cross-built for that Cortex-M core and run
#                   under QEMU by tools/run_qemu_tests.py, must fail
#   tool:<argv>     the command must exit non-zero
#
# tsan: is for a weakened memory order. On real hardware the reordering it
# allows almost never happens -- a relaxed load where an acquire belongs
# survived 20 of 20 threaded runs on an Apple M2 -- while ThreadSanitizer, which
# tracks happens-before rather than what the cores did, reports the race on
# every run (#341). Those targets build in `<build-dir>-tsan`, configured on
# first use.
#
# qemu: is for code only a Cortex-M build compiles -- irq_lock's PRIMASK
# sequence. The host suite cannot kill such a mutant, since the mutated line is
# not even built there (#388). The ARM toolchain and QEMU must be installed; a
# run without them is broken, not killed, and the CI `mutants` job leaves these
# to `qemu-conformance` with --skip-kinds qemu. Each qemu: gate is run once on
# the unmutated tree first and must pass, so a test failing for another reason
# cannot pass for a kill.
#
# Each mutant lives in its own file, tools/mutants/<name>.py, which defines one
# dict, MUTANT. One list in this file meant every PR that added a mutant
# inserted at the same place, so each merge put every other open PR into
# conflict. One file per mutant cannot collide with another.
MUTANT_DIR = REPO / "tools" / "mutants"
MUTANT_KEYS = {"name", "file", "why", "kills", "old", "new"}


def load_mutants(directory=MUTANT_DIR):
    """Every tools/mutants/*.py, in name order. Raises on a malformed file:
    a mutant that silently fails to load is a mutant that never runs."""
    mutants = []
    for path in sorted(pathlib.Path(directory).glob("*.py")):
        mutant = runpy.run_path(str(path)).get("MUTANT")
        if not isinstance(mutant, dict):
            raise RuntimeError(f"{path.name}: defines no MUTANT dict")
        if set(mutant) != MUTANT_KEYS:
            raise RuntimeError(f"{path.name}: keys {sorted(mutant)}, expected {sorted(MUTANT_KEYS)}")
        if mutant["name"] != path.stem:
            raise RuntimeError(f"{path.name}: names itself {mutant['name']!r}; the file "
                               f"name and the mutant name must agree")
        mutants.append(dict(mutant, source=f"tools/mutants/{path.name}"))
    return mutants


MUTANTS = load_mutants()


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


def tsan_build_dir(build_dir):
    return f"{build_dir}-tsan"


def build_tsan_target(build_dir, target):
    """Configures the ThreadSanitizer tree if needed, then builds `target`."""
    tsan_dir = tsan_build_dir(build_dir)
    if not (REPO / tsan_dir / "CMakeCache.txt").exists():
        # The same compiler as the baseline tree: CI picks Clang through CC/CXX
        # in its configure step only, so the environment here would pick GCC.
        command = ["cmake", "-B", tsan_dir, "-S", str(REPO), "-DCMAKE_BUILD_TYPE=Debug",
                   "-DMETL_ENABLE_TSAN=ON", "-DMETL_INSTALL=OFF"]
        cache = REPO / build_dir / "CMakeCache.txt"
        if cache.exists():
            for line in cache.read_text().splitlines():
                if line.startswith(("CMAKE_C_COMPILER:", "CMAKE_CXX_COMPILER:", "CMAKE_CXX_COMPILER_LAUNCHER:")):
                    name, _, value = line.partition("=")
                    command.append(f"-D{name}={value}")
        configure = subprocess.run(command, capture_output=True, text=True, cwd=REPO)
        if configure.returncode != 0:
            return configure
    return subprocess.run(["cmake", "--build", tsan_dir, "--target", target, "-j"],
                          capture_output=True, text=True, cwd=REPO)


def run_gate(spec, build_dir):
    """True if the gate REJECTED the tree (i.e. the mutant was killed)."""
    kind, _, rest = spec.partition(":")
    if kind == "tsan":
        build = build_tsan_target(build_dir, rest)
        if build.returncode != 0:
            errors = [line for line in (build.stdout + build.stderr).splitlines() if "error" in line]
            first = errors[0].strip() if errors else "(no error line in the build output)"
            return None, f"the mutated tree did not build under ThreadSanitizer: {first}"
        result = subprocess.run(["ctest", "--test-dir", tsan_build_dir(build_dir), "-R", f"^{rest}$",
                                 "--timeout", str(CTEST_TIMEOUT_SECONDS), "--no-tests=error"],
                                capture_output=True, text=True, cwd=REPO)
        return result.returncode != 0, result.stdout[-800:]
    if kind == "ctest":
        build = subprocess.run(["cmake", "--build", build_dir, "-j"],
                               capture_output=True, text=True, cwd=REPO)
        if build.returncode != 0:
            # A mutant that stops the build is not a useful mutant: it proves the
            # compiler noticed, not that a test did.
            errors = [line for line in (build.stdout + build.stderr).splitlines() if "error" in line]
            first = errors[0].strip() if errors else "(no error line in the build output)"
            return None, f"the mutated tree did not build: {first}"
        # A mutant can make a test loop forever. ctest's default timeout is 1500 s,
        # so one such mutant cost 25 minutes and stalled pre_push.py; a timed-out
        # test fails, which is a kill, but a gate should not need half an hour to
        # say so. The slowest target here runs in seconds.
        result = subprocess.run(["ctest", "--test-dir", build_dir, "-R", rest, "-j",
                                 "--timeout", str(CTEST_TIMEOUT_SECONDS)],
                                capture_output=True, text=True, cwd=REPO)
        return result.returncode != 0, result.stdout[-800:]
    if kind == "qemu":
        result = run_qemu_gate(rest)
        if result.returncode == QEMU_NOT_RUN:
            return None, "the ARM toolchain or QEMU is missing (use --skip-kinds qemu)"
        if result.returncode == QEMU_BAD_ARGUMENTS:
            return None, (result.stdout + result.stderr).strip()[-300:]
        return result.returncode != 0, result.stdout[-800:]
    if kind == "tool":
        result = subprocess.run(rest.split(), capture_output=True, text=True, cwd=REPO)
        return result.returncode != 0, (result.stdout + result.stderr)[-800:]
    raise RuntimeError(f"unknown gate kind {kind!r}")


# tools/run_qemu_tests.py's exit statuses that are not a test result.
QEMU_BAD_ARGUMENTS = 2
QEMU_NOT_RUN = 3


def run_qemu_gate(spec):
    """`spec` is `<cpu>:<test>`, the test as run_qemu_tests.py discovers it. The
    runner's default machine, mps2-an385, is a Cortex-M3: name cortex-m3, or a
    core that board runs, here."""
    cpu, _, test = spec.partition(":")
    return subprocess.run([sys.executable, str(REPO / "tools" / "run_qemu_tests.py"), "--cpu", cpu,
                           "--only", test], capture_output=True, text=True, cwd=REPO)


def qemu_baselines_pass(mutants, say):
    """Each distinct qemu: gate must pass on the unmutated tree."""
    ok = True
    for spec in sorted({m["kills"].partition(":")[2] for m in mutants if m["kills"].startswith("qemu:")}):
        result = run_qemu_gate(spec)
        if result.returncode == QEMU_NOT_RUN:
            say(f"qemu:{spec} cannot run: the ARM toolchain or QEMU is missing "
                f"(use --skip-kinds qemu)", error=True)
            ok = False
        elif result.returncode != 0:
            say(f"qemu:{spec} fails on the unmutated tree, so no mutant could be told killed:\n"
                f"{result.stdout[-800:]}", error=True)
            ok = False
    return ok


def check(mutants, build_dir, quiet=False):
    """`quiet` is for the self-test, whose fixtures are SUPPOSED to survive --
    their ::error:: lines would otherwise annotate a green job."""
    def say(message, error=False):
        if quiet:
            return
        print(f"::error::{message}" if error else message,
              file=sys.stderr if error else sys.stdout)

    survivors, broken = [], []

    if not qemu_baselines_pass(mutants, say):
        return 1

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
    for target in sorted({m["kills"].partition(":")[2] for m in mutants if m["kills"].startswith("tsan:")}):
        build_tsan_target(build_dir, target)

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

            # A qemu: gate that cannot run -- no toolchain or QEMU -- must fail
            # the run before mutating anything, never read as a kill; and one
            # whose test already fails on the unmutated tree must too.
            original_gate = globals()["run_qemu_gate"]
            qemu_mutant = dict(survivor, name="fixture_qemu", file=str(relative),
                               kills="qemu:cortex-m3:sync/lock_test.cpp")
            try:
                for status, label in ((QEMU_NOT_RUN, "a missing toolchain"),
                                      (1, "a test failing on the unmutated tree")):
                    globals()["run_qemu_gate"] = (
                        lambda _spec, status=status: subprocess.CompletedProcess([], status, "", ""))
                    if check([qemu_mutant], build_dir="unused", quiet=True) == 0:
                        failures.append(f"a qemu: gate with {label} passed")
                    if fake.read_text() != "int marker = 1;\n":
                        failures.append(f"the fixture changed although {label} should stop the run first")
            finally:
                globals()["run_qemu_gate"] = original_gate
        finally:
            globals()["REPO"] = original_repo

    # The loader: a good file loads, and a misnamed or malformed one is refused
    # rather than skipped.
    with tempfile.TemporaryDirectory() as tmp:
        directory = pathlib.Path(tmp)
        good = ('MUTANT = {"name": "good", "file": "f", "why": "w", "kills": "tool:false", '
                '"old": "a", "new": "b"}\n')
        (directory / "good.py").write_text(good)
        loaded = load_mutants(directory)
        if [m["name"] for m in loaded] != ["good"] or loaded[0]["source"] != "tools/mutants/good.py":
            failures.append(f"a well-formed mutant file did not load as expected: {loaded}")
        for bad_name, bad_text in (("misnamed", good),
                                   ("missing_key", 'MUTANT = {"name": "missing_key"}\n'),
                                   ("no_dict", "OTHER = 1\n")):
            (directory / f"{bad_name}.py").write_text(bad_text)
            try:
                load_mutants(directory)
                failures.append(f"a {bad_name} mutant file was accepted")
            except RuntimeError:
                pass
            (directory / f"{bad_name}.py").unlink()

    if failures:
        for failure in failures:
            print(f"SELF-TEST FAILED: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: survivors, stale anchors, empty runs and malformed mutant "
          "files are rejected, killed mutants pass, and the tree is restored in every case")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build-dir", default="build")
    parser.add_argument("--only", help="substring of a mutant name")
    parser.add_argument("--files", help="comma-separated paths: run only the mutants of these "
                                        "files (an empty list selects none)")
    parser.add_argument("--kinds", help="comma-separated gate kinds: run only the mutants they kill")
    parser.add_argument("--skip-kinds", help="comma-separated gate kinds whose mutants to leave out")
    parser.add_argument("--list", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    selected = [m for m in MUTANTS if not args.only or args.only in m["name"]]
    if args.kinds:
        kinds = set(args.kinds.split(","))
        selected = [m for m in selected if m["kills"].partition(":")[0] in kinds]
    if args.skip_kinds:
        skipped = set(args.skip_kinds.split(","))
        selected = [m for m in selected if m["kills"].partition(":")[0] not in skipped]
    if args.files is not None:
        wanted = {path for path in args.files.split(",") if path}
        # A mutant runs when the header it mutates changed, or its own file did.
        selected = [m for m in selected if m["file"] in wanted or m["source"] in wanted]
        if not selected:
            print(f"no mutant targets {sorted(wanted) or 'any changed file'}; nothing to run")
            return 0

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
