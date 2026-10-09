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

WHAT DOES NOT, and why: the ARM size/stack/RAM budgets and the instruction
counts are measured on CI's toolchains and a local number is a different
measurement (tools/check_size.py says so at length); QEMU, Zephyr and ESP-IDF
need toolchains a workstation rarely has; clang-tidy's count depends on its
version. Those stay CI-only. `--full` adds the mutation gate, which rebuilds
the tree several times.

Build directories live under build-prepush/ and are reused, so a second push
only recompiles what changed.

Usage:
    tools/pre_push.py            # what the hook runs
    tools/pre_push.py --quick    # steps 1-2 only (no builds)
    tools/pre_push.py --full     # also tools/check_mutants.py
    git push --no-verify         # skip the hook (CI still runs everything)
"""

import argparse
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


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--quick", action="store_true", help="self-tests and source gates only")
    mode.add_argument("--full", action="store_true", help="also run the mutation gate")
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

    if args.full:
        print("5. mutation gate")
        if not step("check_mutants.py", [python, "tools/check_mutants.py", "--build-dir",
                                         str(BUILD_ROOT / "sanitizers")]):
            return 1

    print("pre-push: all local checks passed (ARM budgets, QEMU, Zephyr, ESP-IDF and "
          "clang-tidy run on CI)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
