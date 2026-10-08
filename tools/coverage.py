#!/usr/bin/env python3
"""Source-based coverage for the host test suite (Clang / llvm-cov).

Reports on include/metl only. The whole-run total that llvm-cov prints by
default also counts the test sources, which drags the number down and measures
the wrong thing: how well the tests cover themselves is not interesting.

WHAT THIS CANNOT SEE, and why the number is not the whole story:

  * `#if` arms that this configuration does not compile. Coverage describes the
    code the build produced, so a branch nobody builds does not appear as
    uncovered -- it does not appear at all. METL_CRC_TABLE=0 has its own CI job
    for that reason; see config-matrix.
  * Everything gated on being an MCU. irq_lock's PRIMASK path, the ARMv6-M
    capability rejections, the -fno-exceptions arms of expected.hpp -- none of
    that code exists in a host build. Its evidence is the qemu-conformance job,
    not this one.
  * Anything evaluated at COMPILE time. A constexpr function exercised only by
    static_assert never runs, so its lines read as uncovered: bit.hpp reports
    ~29% lines with 100% branches for exactly that reason, and so do
    lookup_table.hpp and detail/crc.hpp. Those are measurement artifacts, not
    test gaps -- do not "fix" them by adding runtime tests that duplicate a
    static_assert.

So: this measures host-visible RUNTIME branch depth. It is a floor on quality,
not a summary of it.

THE NUMBERS ARE COMPILER-VERSION DEPENDENT. Different Clang releases lower the
same source to different numbers of instrumented branches -- this repo measured
1258 branches under Apple Clang and 1122 under Ubuntu's, giving 75.1% and 72.3%
for identical code. So a local run will not match CI, and the CI number is the
one the floor is set against. Do not chase a local figure.

Usage:
    tools/coverage.py [--min-lines 90] [--min-branches 72]
"""

import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO / "build-coverage"
PROF_DIR = BUILD_DIR / "profraw"


def find_tool(name):
    found = shutil.which(name)
    if found is None and shutil.which("xcrun"):
        result = subprocess.run(["xcrun", "--find", name], capture_output=True, text=True)
        found = result.stdout.strip() or None
    return found


def pct(total, missed):
    return 100.0 * (total - missed) / total if total else 0.0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--min-lines", type=float, default=90)
    parser.add_argument("--min-branches", type=float, default=72)
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)

    profdata, cov = find_tool("llvm-profdata"), find_tool("llvm-cov")
    if profdata is None or cov is None:
        print("error: llvm-profdata / llvm-cov not found on PATH", file=sys.stderr)
        return 2

    shutil.rmtree(BUILD_DIR, ignore_errors=True)
    PROF_DIR.mkdir(parents=True)

    quiet = {"stdout": subprocess.DEVNULL}
    if subprocess.run(["cmake", "-B", str(BUILD_DIR), "-S", str(REPO), "-DCMAKE_BUILD_TYPE=Debug",
                       "-DMETL_ENABLE_COVERAGE=ON", "-DMETL_INSTALL=OFF"], **quiet).returncode:
        return 1
    if subprocess.run(["cmake", "--build", str(BUILD_DIR), "-j"], **quiet).returncode:
        return 1

    env = dict(os.environ, LLVM_PROFILE_FILE=str(PROF_DIR / "%p.profraw"))
    ctest = subprocess.run(["ctest", "--test-dir", str(BUILD_DIR), "-j4", "--output-on-failure"],
                           env=env, capture_output=True, text=True)
    (BUILD_DIR / "ctest.log").write_text(ctest.stdout + ctest.stderr)
    print("\n".join((ctest.stdout + ctest.stderr).splitlines()[-3:]))
    if ctest.returncode != 0:
        print("error: tests failed under instrumentation; coverage numbers would be meaningless",
              file=sys.stderr)
        return 1

    profile = BUILD_DIR / "coverage.profdata"
    if subprocess.run([profdata, "merge", "-sparse", *map(str, PROF_DIR.glob("*.profraw")),
                       "-o", str(profile)]).returncode:
        return 1

    # Every test is its own binary, so every one has to be handed to llvm-cov or
    # the functions only that binary exercised are reported as unexecuted.
    objects = []
    for binary in sorted(BUILD_DIR.glob("metl_*")):
        if binary.is_file() and os.access(binary, os.X_OK):
            objects += ["-object", str(binary)]
    report = subprocess.run([cov, "report", *objects, f"-instr-profile={profile}"],
                            capture_output=True, text=True).stdout
    (BUILD_DIR / "report.txt").write_text(report)

    # Columns: file, regions, missed, cover, functions, missed, executed,
    # lines, missed, cover, branches, missed, cover.
    rows = [line.split() for line in report.splitlines() if "include/metl" in line]
    rows = [row for row in rows if len(row) >= 13]

    print("\n=== per-file, weakest branch coverage first ===")
    with_branches = [row for row in rows if float(row[10]) > 0]
    for row in sorted(with_branches, key=lambda row: float(row[12].rstrip("%"))):
        print(f"  branches {float(row[12].rstrip('%')):7.2f}%   lines {row[9]:<9} {row[0]}")

    total = [sum(int(row[i]) for row in rows) for i in (1, 2, 4, 5, 7, 8, 10, 11)]
    reg, regmiss, fn, fnmiss, ln, lnmiss, br, brmiss = total
    lines_pct, branch_pct = pct(ln, lnmiss), pct(br, brmiss)
    print("\n=== include/metl totals ===")
    print(f"  regions   {pct(reg, regmiss):6.2f}%  ({reg - regmiss}/{reg})")
    print(f"  functions {pct(fn, fnmiss):6.2f}%  ({fn - fnmiss}/{fn})")
    print(f"  lines     {lines_pct:6.2f}%  ({ln - lnmiss}/{ln})   floor {args.min_lines:g}%")
    print(f"  branches  {branch_pct:6.2f}%  ({br - brmiss}/{br})   floor {args.min_branches:g}%")

    failed = False
    if lines_pct < args.min_lines:
        print(f"\nFAIL: line coverage {lines_pct:.2f}% is below the {args.min_lines:g}% floor")
        failed = True
    if branch_pct < args.min_branches:
        print(f"\nFAIL: branch coverage {branch_pct:.2f}% is below the "
              f"{args.min_branches:g}% floor")
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
