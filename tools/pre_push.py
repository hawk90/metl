#!/usr/bin/env python3
"""Run, before a push, every CI check that can run on this machine.

WHY. CI is the authority, but a round trip through it costs tens of minutes,
and most of what it checks does not need its runners. The 1.0.0 follow-up that
moved CI to ubuntu-26.04 was pushed with only CI watching, and the first red job
was a GCC 15 -Werror diagnostic that the GCC installed on the maintainer's
machine reproduced exactly. This script is the habit that would have caught it,
made automatic: `pre-commit install` registers it as a pre-push hook.

WHAT RUNS, cheapest first, stopping at the first failure:

  1. every tools/*.py --self-test   -- the checkers can still fail
  2. the source gates               -- docs, API contract, progress guarantees,
                                       the CI merge boundary, compile-fail cases
  3. Release + -Werror builds and ctest, once per compiler found:
       the default `c++` and the newest `g++-NN` on PATH. GCC and Clang
       disagree about warnings, and CI builds with both.
  4. an ASan + UBSan Debug build and ctest, as the `sanitizers / asan-ubsan`
     job does.
  5. the mutation gate (tools/check_mutants.py), which also catches a mutant
     whose anchor text a change has moved.
  6. every test built and run through the amalgamation
     (tools/check_amalgamation.py), as the `amalgamation` job does.
  7. clang-tidy, as a DELTA: the same local binary over the merge-base with
     origin/main and over this tree, failing if the distinct-finding count
     grew. CI's absolute `--max` cannot be checked here -- the count depends on
     the clang-tidy version -- but a change that adds findings adds them under
     any version. Skipped, and said so, when no clang-tidy is installed.
  8. every TU under a consumer's warning flags, as the `consumer-warnings`
     job does. The flags and the compiler/standard matrix are read from
     ci.yml, so there is one list.
  9. source-based coverage against its floors (tools/coverage.py), as the
     `coverage` job does. On macOS the profile tools come from Xcode: the
     Homebrew llvm-profdata cannot read Apple clang's profiles.

  Step 4b adds two more builds: ASan+UBSan at METL_HARDENING=0, and
  -fno-exceptions -fno-rtti -- the configurations that differ most from the
  default, and the two that found defects the default build did not.

WHAT DOES NOT, and why:
  * Measured on CI's toolchains, where a local number is a different
    measurement: the ARM size/stack/RAM budgets and instruction counts
    (tools/check_size.py says so at length), and the clang-tidy ABSOLUTE count
    (step 7 compares within one local binary instead).
  * Toolchains a workstation rarely has: QEMU, Zephyr, ESP-IDF, arm-hints,
    the cross and big-endian jobs, MSVC.
  * Further full builds, left to CI for time: TSan, LTO, ASan at
    RelWithDebInfo, C++20 and CRC-table-off in config-matrix. Each would add a
    complete build and test run to every push.
  * Packaging and tooling with their own dependencies: conan, install-check,
    fuzz-smoke (libFuzzer), bench-smoke, docs (Doxygen).

A gate left to CI is found red on a pushed branch instead of here; the
amalgamation's exit codes, the clang-tidy ratchet, a mutant's anchor and a
consumer's -Wconversion all were, before they moved into this script.

Build directories live under build-prepush/ and are reused, so a second push
only recompiles what changed.

Usage:
    tools/pre_push.py            # what the hook runs
    tools/pre_push.py --quick    # steps 1-2 only (no builds)
    git push --no-verify         # skip the hook (CI still runs everything)
"""

import argparse
import collections
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUILD_ROOT = REPO / "build-prepush"
GATES = [
    ["tools/check_docs.py"],
    ["tools/check_api_contract.py"],
    ["tools/check_source_rules.py"],
    ["tools/check_progress_guarantee.py"],
    ["tools/check_ci_gate.py"],
    ["tools/check_compile_fail.py"],
]


def step(title, command, **kwargs):
    """Runs one command, printing only a status line unless it fails."""
    started = time.monotonic()
    print(f"  {title} ...", end="", flush=True)
    # This script runs from git's pre-push hook, where git exports GIT_DIR and
    # friends. A checker that then runs git in a temporary directory (the
    # self-test fixtures do) would be answered about the real repository, so
    # every step gets an environment without them.
    merged = {**os.environ, **kwargs.pop("env", {})}
    env = {name: value for name, value in merged.items() if not name.startswith("GIT_")}
    result = subprocess.run(command, cwd=REPO, capture_output=True, text=True, env=env, **kwargs)
    elapsed = time.monotonic() - started
    if result.returncode != 0:
        print(f" FAILED ({elapsed:.0f}s)")
        output = (result.stdout + result.stderr).strip().splitlines()
        # Start at the first diagnostic, not at the tail: a parallel build's
        # last lines are other jobs finishing, not the reason it stopped.
        first = next((i for i, line in enumerate(output)
                      if re.search(r"\berror\b|FAIL|Failed", line)), max(0, len(output) - 40))
        print("\n".join("      | " + line for line in output[max(0, first - 4):first + 36]))
        return False
    print(f" ok ({elapsed:.0f}s)")
    return True


def newest_gcc():
    """The highest-numbered g++-NN on PATH, or a `g++` that really is GCC."""
    versions = []
    for directory in os.environ.get("PATH", "").split(os.pathsep):
        try:
            names = os.listdir(directory)
        except OSError:
            continue
        for name in names:
            match = re.fullmatch(r"g\+\+-(\d+)", name)
            if match:
                versions.append((int(match.group(1)), name))
    if versions:
        return max(versions)[1]
    gxx = shutil.which("g++")
    if gxx:
        banner = subprocess.run([gxx, "--version"], capture_output=True, text=True).stdout
        if "Free Software Foundation" in banner:  # not Apple's clang alias
            return "g++"
    return None


def build_and_test(name, cxx, cmake_args):
    build_dir = BUILD_ROOT / name
    env = dict(os.environ, CXX=cxx)
    configure = ["cmake", "-B", str(build_dir), "-S", str(REPO), "-DMETL_BUILD_EXAMPLES=ON",
                 "-DMETL_INSTALL=OFF", *cmake_args]
    jobs = str(os.cpu_count() or 2)
    return (step(f"{name}: configure", configure, env=env)
            and step(f"{name}: build", ["cmake", "--build", str(build_dir), "-j", jobs])
            and step(f"{name}: ctest", ["ctest", "--test-dir", str(build_dir), "-j", jobs,
                                        "--output-on-failure"]))


def find_clang_tidy():
    found = shutil.which("clang-tidy")
    if found:
        return found
    for candidate in ("/opt/homebrew/opt/llvm/bin/clang-tidy", "/usr/local/opt/llvm/bin/clang-tidy"):
        if Path(candidate).is_file():
            return candidate
    return None


def tidy_findings(python, tidy, source_dir, build_dir, env):
    """The distinct-finding lines of clang_tidy_report.py over `source_dir`.

    Both trees are configured with the same compiler and environment in the
    same run. The flags in compile_commands.json decide which #if branches
    clang-tidy sees, so a base measured under one PATH (a shell) and a head
    under another (git's hook) once differed by 136 findings."""
    configure = ["cmake", "-B", str(build_dir), "-S", str(source_dir), "--fresh",
                 "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON", "-DMETL_BUILD_DOCS=OFF", "-DMETL_INSTALL=OFF",
                 f"-DCMAKE_CXX_COMPILER={shutil.which('c++') or 'c++'}"]
    if sys.platform == "darwin":
        # CMake no longer writes -isysroot into compile_commands.json, and a
        # clang-tidy that is not Apple's then cannot find <cstdlib>: 67 of 71
        # headers stopped at that error and read as clean.
        sdk = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True, text=True).stdout.strip()
        if sdk:
            configure.append(f"-DCMAKE_OSX_SYSROOT={sdk}")
    subprocess.run(configure, cwd=source_dir, capture_output=True, check=True, env=env)
    report = subprocess.run([python, str(source_dir / "tools" / "clang_tidy_report.py"),
                             "-p", str(build_dir), "--clang-tidy", tidy],
                            cwd=source_dir, capture_output=True, text=True, check=True, env=env).stdout
    lines = report.split("Distinct findings:", 1)[-1].splitlines()
    # Line numbers move with any edit above a finding, so they are dropped --
    # but the result is a COUNT per message, not a set: two findings with the
    # same message in one file are two findings, and a new one must show.
    return collections.Counter(re.sub(r":\d+:\d+:", ":", line.strip())
                               for line in lines if "warning:" in line)


def clang_tidy_delta(python):
    tidy = find_clang_tidy()
    if tidy is None:
        print("  (no clang-tidy installed -- CI's clang-tidy job is the only check)")
        return True
    base = subprocess.run(["git", "merge-base", "HEAD", "origin/main"], cwd=REPO,
                          capture_output=True, text=True).stdout.strip()
    if not base:
        print("  (no merge-base with origin/main -- skipped)")
        return True
    started = time.monotonic()
    # Which binary ran is part of the result: two installed clang-tidys can
    # disagree about the same line, and the counts are only comparable within
    # one of them.
    print(f"  clang-tidy ({tidy}) over {base[:9]} and this tree ...", end="", flush=True)
    env = {name: value for name, value in os.environ.items() if not name.startswith("GIT_")}
    base_tree = BUILD_ROOT / "tidy-base"
    try:
        if base_tree.exists():
            subprocess.run(["git", "worktree", "remove", "--force", str(base_tree)], cwd=REPO,
                           capture_output=True, env=env)
        subprocess.run(["git", "worktree", "add", "--detach", str(base_tree), base], cwd=REPO,
                       capture_output=True, check=True, env=env)
        try:
            before = tidy_findings(python, tidy, base_tree, base_tree / "build-tidy", env)
        finally:
            subprocess.run(["git", "worktree", "remove", "--force", str(base_tree)], cwd=REPO,
                           capture_output=True, env=env)
        after = tidy_findings(python, tidy, REPO, BUILD_ROOT / "tidy-head", env)
    except subprocess.CalledProcessError as error:
        print(f" FAILED to run\n      | {error}")
        return False
    elapsed = time.monotonic() - started
    added = sorted((after - before).elements())
    if added:
        # A real regression is deterministic; run this tree once more so a
        # one-off difference is reported as such instead of blocking the push.
        again = tidy_findings(python, tidy, REPO, BUILD_ROOT / "tidy-head", env)
        if again != after:
            print(f" (first run: {sum(after.values())} findings, second: {sum(again.values())};"
                  f" clang-tidy output differed between two runs)", end="")
        after = again
        added = sorted((after - before).elements())
    total_before, total_after = sum(before.values()), sum(after.values())
    if added:
        print(f" FAILED ({elapsed:.0f}s): {total_before} -> {total_after} distinct findings, "
              f"{len(added)} new")
        for line in added:
            print(f"      | {line}")
        return False
    print(f" ok ({elapsed:.0f}s): {total_before} -> {total_after} distinct findings")
    return True


def consumer_matrix():
    """(flags, [(cxx, std), ...]) from the consumer-warnings job in ci.yml."""
    text = (REPO / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
    job = text[text.index("\n  consumer-warnings:\n"):]
    job = job[:re.search(r"\n  [a-z][a-z0-9-]*:\n", job[1:]).start() + 1]
    flags_block = re.search(r"METL_CONSUMER_WARNINGS: >-\n((?:[ \t]+[^\n]*\n)+?)\n", job).group(1)
    flags = " ".join(flags_block.split())
    cells = re.findall(r"cxx: ([^,}\s]+), std: ([^,}\s]+)", job)
    return flags, cells


def consumer_warnings():
    flags, cells = consumer_matrix()
    gcc = newest_gcc()
    files = sorted(str(p.relative_to(REPO)) for pattern in ("tests/**/*.cpp", "fuzz/*.cpp", "examples/*.cpp")
                   for p in REPO.glob(pattern)
                   if not str(p.relative_to(REPO)).startswith(("tests/embedded/", "tests/compile_fail/")))
    for cxx, std in cells:
        local = gcc if cxx == "g++" else cxx
        if local is None or shutil.which(local) is None:
            print(f"  ({cxx} not found -- CI's consumer-warnings / {cxx} is the only check)")
            continue
        command = [sys.executable, "-c", CONSUMER_WORKER, local, std, flags, *files]
        if not step(f"{local} -std={std}: {len(files)} TUs", command):
            return False
    return True


# Compiles each TU with -fsyntax-only in parallel and prints the first failure.
CONSUMER_WORKER = """
import concurrent.futures, os, subprocess, sys
cxx, std, flags, files = sys.argv[1], sys.argv[2], sys.argv[3].split(), sys.argv[4:]
def one(f):
    r = subprocess.run([cxx, f"-std={std}", "-fsyntax-only", "-Iinclude", "-Itests", "-Ifuzz",
                        *flags, "-Werror", f], capture_output=True, text=True)
    return f, r.returncode, r.stdout + r.stderr
with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 2) as pool:
    failed = [(f, out) for f, code, out in pool.map(one, files) if code != 0]
for f, out in failed[:3]:
    print("FAIL", f)
    print(out)
sys.exit(1 if failed else 0)
"""


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--quick", action="store_true", help="self-tests and source gates only")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    python = sys.executable

    print("pre-push: the CI checks that can run on this machine")
    print("1. checker self-tests")
    for tool in sorted((REPO / "tools").glob("*.py")):
        if "--self-test" in tool.read_text(encoding="utf-8") and tool.name != Path(__file__).name:
            if not step(tool.name, [python, str(tool), "--self-test"]):
                return 1

    print("2. source gates")
    for gate in GATES:
        if not step(gate[0], [python, *gate]):
            return 1
    if args.quick:
        print("pre-push: --quick, builds skipped")
        return 0

    print("3. Release + -Werror builds")
    werror = ["-DCMAKE_BUILD_TYPE=Release", "-DMETL_WARNINGS_AS_ERRORS=ON"]
    if not build_and_test("release-c++", "c++", werror):
        return 1
    gcc = newest_gcc()
    if gcc is None:
        print("  (no GCC on PATH -- CI's release-werror / gcc is the only GCC check)")
    elif not build_and_test(f"release-{gcc}", gcc, werror):
        return 1

    print("4. ASan + UBSan")
    sanitizers = ["-DCMAKE_BUILD_TYPE=Debug", "-DMETL_ENABLE_ASAN=ON", "-DMETL_ENABLE_UBSAN=ON",
                  "-DMETL_WARNINGS_AS_ERRORS=ON"]
    if not build_and_test("sanitizers", "c++", sanitizers):
        return 1

    print("4b. the two configurations most likely to differ")
    # METL_ASSERT stripped: what a misuse does when only METL_HARDEN is left.
    if not build_and_test("sanitizers-hardening-none", "c++",
                          sanitizers + ["-DCMAKE_CXX_FLAGS=-DMETL_HARDENING=0"]):
        return 1
    # The library's primary embedded configuration, on the host.
    if not build_and_test("no-exceptions", "c++", ["-DCMAKE_BUILD_TYPE=Debug", "-DMETL_WARNINGS_AS_ERRORS=ON",
                                                   "-DCMAKE_CXX_FLAGS=-fno-exceptions -fno-rtti"]):
        return 1

    print("5. mutation gate")
    if not step("check_mutants.py", [python, "tools/check_mutants.py", "--build-dir",
                                     str(BUILD_ROOT / "sanitizers")]):
        return 1

    print("6. amalgamation")
    if not step("check_amalgamation.py", [python, "tools/check_amalgamation.py"]):
        return 1

    print("7. clang-tidy delta against origin/main")
    if not clang_tidy_delta(python):
        return 1

    print("8. consumer warning flags")
    if not consumer_warnings():
        return 1

    print("9. coverage floors")
    env = None
    xcrun = shutil.which("xcrun")
    if xcrun:
        profdata = subprocess.run([xcrun, "-f", "llvm-profdata"], capture_output=True, text=True).stdout.strip()
        if profdata:
            env = {"PATH": f"{Path(profdata).parent}{os.pathsep}{os.environ.get('PATH', '')}"}
    if not step("coverage.py", [python, "tools/coverage.py"], **({"env": env} if env else {})):
        return 1

    print("pre-push: all local checks passed (ARM budgets, QEMU, Zephyr and ESP-IDF run on CI)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
