#!/usr/bin/env python3
"""Build and run every test through the single-header amalgamation.

The release ships one file, metl-X.Y.Z-single.hpp. Every public header gets a
shim that includes it, so each test's `#include <metl/x.hpp>` resolves to the
amalgamation instead of include/, and the whole suite runs against what users
download. A test that exits with 77 (metl_test::skip_code) had nothing to check
in this build and is reported as skipped, not passed.

This loop used to be written out twice in bash, in ci.yml and release.yml, and
a change to the suite's exit codes had to find both copies. CI, the release
workflow and tools/pre_push.py all run this instead.

Usage:
    tools/check_amalgamation.py [--header metl-single.hpp] [--cxx clang++] [-j N]
    tools/check_amalgamation.py --self-test   # prove the checker still bites
"""

import argparse
import concurrent.futures
import os
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKIP_CODE = 77  # metl_test::skip_code in tests/metl_check.hpp
FLAGS = ["-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-pthread"]


def make_shims(header, shim_root):
    """One shim per public header, all including the amalgamation."""
    (shim_root / "metl_amalgamated.hpp").write_text(header.read_text(encoding="utf-8"),
                                                    encoding="utf-8")
    include = REPO / "include"
    for public in include.rglob("*.hpp"):
        shim = shim_root / public.relative_to(include)
        shim.parent.mkdir(parents=True, exist_ok=True)
        shim.write_text('#pragma once\n#include "metl_amalgamated.hpp"\n', encoding="utf-8")


def run_test(cxx, shim_root, out_dir, test):
    """Returns ("pass" | "skip" | "build" | "fail", detail)."""
    name = test.relative_to(REPO).as_posix() if test.is_relative_to(REPO) else test.name
    binary = out_dir / (name.replace("/", "_") + ".bin")
    build = subprocess.run([cxx, *FLAGS, "-I", str(shim_root), "-I", str(REPO / "tests"),
                            str(test), "-o", str(binary)], capture_output=True, text=True)
    if build.returncode != 0:
        return "build", (build.stdout + build.stderr).strip()
    try:
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=120)
    except subprocess.TimeoutExpired:
        return "fail", "timed out after 120 s"
    if run.returncode == 0:
        return "pass", ""
    if run.returncode == SKIP_CODE:
        return "skip", run.stdout.strip()
    return "fail", f"exit {run.returncode}\n{(run.stdout + run.stderr).strip()}"


def check(header, cxx, jobs, tests):
    with tempfile.TemporaryDirectory(prefix="metl-amalgamation-") as scratch:
        scratch = Path(scratch)
        shim_root = scratch / "shim"
        shim_root.mkdir()
        make_shims(header, shim_root)
        results = {}
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = {pool.submit(run_test, cxx, shim_root, scratch, t): t for t in tests}
            for future in concurrent.futures.as_completed(futures):
                results[futures[future]] = future.result()
    return results


def report(results):
    counts = {"pass": 0, "skip": 0, "build": 0, "fail": 0}
    for test in sorted(results):
        kind, detail = results[test]
        counts[kind] += 1
        name = test.relative_to(REPO).as_posix()
        if kind == "skip":
            print(f"skipped: {name}")
        elif kind in ("build", "fail"):
            label = "BUILD-FAIL" if kind == "build" else "FAIL"
            print(f"{label}: {name}", file=sys.stderr)
            print("\n".join("    " + line for line in detail.splitlines()[:40]), file=sys.stderr)
    print(f"{counts['pass']} passed, {counts['skip']} skipped, "
          f"{counts['build'] + counts['fail']} failed through the amalgamated header")
    return 1 if counts["build"] or counts["fail"] else 0


def self_test(cxx):
    """A passing, a skipping, a failing and a non-compiling test must each be
    classified as such -- in particular, 77 must not read as a failure and 1
    must not read as a skip."""
    cases = {
        "pass": "int main() { return 0; }\n",
        "skip": "int main() { return 77; }\n",
        "fail": "int main() { return 1; }\n",
        "build": "int main() { return not_declared; }\n",
    }
    failures = []
    with tempfile.TemporaryDirectory(prefix="metl-amalgamation-self-test-") as scratch:
        scratch = Path(scratch)
        shim_root = scratch / "shim"
        shim_root.mkdir()
        (shim_root / "metl_amalgamated.hpp").write_text("#pragma once\n", encoding="utf-8")
        for expected, source in cases.items():
            test = scratch / f"{expected}_test.cpp"
            test.write_text(source, encoding="utf-8")
            got, _ = run_test(cxx, shim_root, scratch, test)
            if got != expected:
                failures.append(f"a test that should {expected!r} was classified {got!r}")
    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("self-test passed: pass, skip (77), failure and build failure are told apart")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--header", type=Path,
                        help="the amalgamation to test (default: generate one)")
    parser.add_argument("--cxx", default=os.environ.get("CXX_AMALGAMATION", "clang++"))
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)

    if args.self_test:
        return self_test(args.cxx)

    tests = sorted(p for p in (REPO / "tests").rglob("*_test.cpp")
                   if "embedded" not in p.relative_to(REPO / "tests").parts)
    if not tests:
        print("error: no tests found", file=sys.stderr)
        return 2

    if args.header is not None:
        return report(check(args.header.resolve(), args.cxx, args.jobs, tests))
    with tempfile.TemporaryDirectory(prefix="metl-amalgamate-") as scratch:
        header = Path(scratch) / "metl-single.hpp"
        subprocess.run([sys.executable, str(REPO / "tools" / "amalgamate.py"), "-o", str(header)],
                       check=True, cwd=REPO, stdout=subprocess.DEVNULL)
        return report(check(header, args.cxx, args.jobs, tests))


if __name__ == "__main__":
    sys.exit(main())
