#!/usr/bin/env python3
"""Build and run every Tier 1 test through the single-header amalgamation.

The release ships one file, metl-X.Y.Z-single.hpp. Every public header gets a
shim that includes it, so each test's `#include <metl/x.hpp>` resolves to the
amalgamation instead of include/, and the whole suite runs against what users
download. The tests of Tier 2 (metl/exp/) types are left out and listed: the
amalgamation holds only what the umbrella includes. A test that exits with 77 (metl_test::skip_code) had nothing to check
in this build and is reported as skipped, not passed. A test that CMakeLists.txt
builds from several translation units (a metl_cc_test whose SRCS lists more than
one file) is built from all of them, in the order listed, so a property that
only shows across translation units -- link order, ODR -- is checked against
the amalgamation too.

Every test is compiled with the METL_EXPECTED_VERSION_* values CMake gives
tests/core/version_test.cpp, read from project(VERSION) in CMakeLists.txt, so
the version the shipped header reports is compared with the release (#410).
Without them that test's comparison compiles out and it checks nothing. A
CMakeLists.txt whose version cannot be read stops the run.

This loop used to be written out twice in bash, in ci.yml and release.yml, and
a change to the suite's exit codes had to find both copies. CI, the release
workflow and tools/pre_push.py all run this instead.

Usage:
    tools/check_amalgamation.py [--header metl-single.hpp] [--cxx clang++] [-j N]
    tools/check_amalgamation.py --self-test   # prove the checker still bites
"""

import argparse
import concurrent.futures
import contextlib
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKIP_CODE = 77  # metl_test::skip_code in tests/metl_check.hpp
FLAGS = ["-std=c++17", "-O1", "-Wall", "-Wextra", "-Werror", "-pthread"]


PROJECT_VERSION = re.compile(r"\bproject\s*\([^)]*?\bVERSION\s+(\d+)\.(\d+)\.(\d+)", re.S)


def version_defines(cmake_text):
    """The -D flags CMakeLists.txt gives version_test, from project(VERSION).
    Raises ValueError when there is no X.Y.Z version to read."""
    match = PROJECT_VERSION.search(re.sub(r"#[^\n]*", "", cmake_text))
    if match is None:
        raise ValueError("no `project(... VERSION X.Y.Z ...)` in CMakeLists.txt")
    return [f"-DMETL_EXPECTED_VERSION_{part}={value}"
            for part, value in zip(("MAJOR", "MINOR", "PATCH"), match.groups())]


def make_shims(header, shim_root):
    """One shim per public header, all including the amalgamation."""
    (shim_root / "metl_amalgamated.hpp").write_text(header.read_text(encoding="utf-8"),
                                                    encoding="utf-8")
    include = REPO / "include"
    for public in include.rglob("*.hpp"):
        shim = shim_root / public.relative_to(include)
        shim.parent.mkdir(parents=True, exist_ok=True)
        shim.write_text('#pragma once\n#include "metl_amalgamated.hpp"\n', encoding="utf-8")


def multi_tu_sources(cmake_text):
    """{test file: [every source, in link order]} for each metl_cc_test whose
    SRCS names more than one file. CMakeLists.txt is the one place the list is
    written; a copy here would drift."""
    uncommented = re.sub(r"#[^\n]*", "", cmake_text)
    tests = {}
    for sources in re.findall(r"SRCS((?:\s+[^\s()]+)+)", uncommented):
        files = []
        for word in sources.split():
            if re.fullmatch(r"[A-Z_]+", word):  # the next keyword: DEPS, INCLUDES, ...
                break
            files.append(word)
        if len(files) < 2 or any("$" in f for f in files):
            continue
        for f in files:
            if f.endswith("_test.cpp"):
                tests[REPO / f] = [REPO / g for g in files]
    return tests


TIER2_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]metl/exp/', re.M)


def tier2_tests(tests):
    """The tests that include a Tier 2 header. The single header is built from
    the umbrella, which holds no Tier 2 type (docs/SCOPE.md section 3), so these
    cannot build against it; they are left out and listed, never skipped
    silently."""
    return [t for t in tests if TIER2_INCLUDE.search(t.read_text(encoding="utf-8"))]


def run_test(cxx, shim_root, out_dir, test, sources=None, defines=()):
    """Returns ("pass" | "skip" | "build" | "fail", detail)."""
    name = test.relative_to(REPO).as_posix() if test.is_relative_to(REPO) else test.name
    binary = out_dir / (name.replace("/", "_") + ".bin")
    sources = sources or [test]
    build = subprocess.run([cxx, *FLAGS, *defines, "-I", str(shim_root), "-I", str(REPO / "tests"),
                            *(str(s) for s in sources), "-o", str(binary)],
                           capture_output=True, text=True)
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


def check(header, cxx, jobs, tests, multi_tu, defines):
    with tempfile.TemporaryDirectory(prefix="metl-amalgamation-") as scratch:
        scratch = Path(scratch)
        shim_root = scratch / "shim"
        shim_root.mkdir()
        make_shims(header, shim_root)
        results = {}
        with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
            futures = {pool.submit(run_test, cxx, shim_root, scratch, t, multi_tu.get(t), defines): t
                       for t in tests}
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
        # A test whose main() uses a symbol defined in a second translation unit
        # links only if both are built, and the source list is read from SRCS.
        main_tu = scratch / "two_tu_test.cpp"
        main_tu.write_text("int other();\nint main() { return other(); }\n", encoding="utf-8")
        other_tu = scratch / "two_tu_other.cpp"
        other_tu.write_text("int other() { return 0; }\n", encoding="utf-8")
        listed = multi_tu_sources("metl_cc_test(NAME t\n  SRCS tests/a/one_test.cpp\n)\n"
                                  "metl_cc_test(NAME u # two\n"
                                  "  SRCS tests/b/x.cpp tests/b/y_test.cpp\n"
                                  "  INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/tests\n)\n")
        if listed != {REPO / "tests/b/y_test.cpp": [REPO / "tests/b/x.cpp",
                                                    REPO / "tests/b/y_test.cpp"]}:
            failures.append(f"SRCS lists were read as {listed}")
        got, _ = run_test(cxx, shim_root, scratch, main_tu, [other_tu, main_tu])
        if got != "pass":
            failures.append(f"a two-TU test built from both sources was classified {got!r}")
        got, _ = run_test(cxx, shim_root, scratch, main_tu)
        if got != "build":
            failures.append(f"a two-TU test built alone was classified {got!r}")
    with tempfile.TemporaryDirectory() as scratch:
        tier2 = Path(scratch) / "exp_test.cpp"
        tier2.write_text('#include <metl/exp/bitset.hpp>\nint main() { return 0; }\n')
        core = Path(scratch) / "core_test.cpp"
        core.write_text('// mentions metl/exp/ in a comment only\n#include <metl/span.hpp>\n')
        if tier2_tests([tier2, core]) != [tier2]:
            failures.append("a test including a Tier 2 header was not told apart")
    # The version: read from project(VERSION), passed to every compile, and a
    # CMakeLists.txt without one refused rather than skipped.
    try:
        defines = version_defines("project(\n  metl # name\n  VERSION 3.4.5\n  LANGUAGES CXX)\n")
    except ValueError:
        defines = []
    if defines != ["-DMETL_EXPECTED_VERSION_MAJOR=3", "-DMETL_EXPECTED_VERSION_MINOR=4",
                   "-DMETL_EXPECTED_VERSION_PATCH=5"]:
        failures.append(f"project(VERSION 3.4.5) was read as {defines}")
    try:
        version_defines("project(metl LANGUAGES CXX)\n")
        failures.append("a CMakeLists.txt with no project(VERSION) was accepted")
    except ValueError:
        pass
    with tempfile.TemporaryDirectory(prefix="metl-amalgamation-self-test-") as scratch:
        scratch = Path(scratch)
        (scratch / "shim").mkdir()
        (scratch / "shim" / "metl_amalgamated.hpp").write_text("#pragma once\n", encoding="utf-8")
        versioned = scratch / "versioned_test.cpp"
        versioned.write_text("static_assert(METL_EXPECTED_VERSION_MAJOR == 3, \"version\");\n"
                             "int main() { return 0; }\n", encoding="utf-8")
        got, _ = run_test(cxx, scratch / "shim", scratch, versioned, defines=defines)
        if got != "pass":
            failures.append(f"a test reading METL_EXPECTED_VERSION_MAJOR was classified {got!r}; "
                            f"the version defines did not reach the compile")
    # The verdict: a failure or a build failure fails the run, a skip does not.
    fixture = {REPO / "tests" / f"{kind}_test.cpp": (kind, "") for kind in ("pass", "skip")}
    with open(os.devnull, "w") as quiet, contextlib.redirect_stdout(quiet), \
            contextlib.redirect_stderr(quiet):
        verdicts = {kind: report({**fixture, REPO / "tests" / "x_test.cpp": (kind, "detail")})
                    for kind in ("pass", "skip", "build", "fail")}
    if verdicts != {"pass": 0, "skip": 0, "build": 1, "fail": 1}:
        failures.append(f"report() exit codes were {verdicts}")
    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("self-test passed: pass, skip (77), failure and build failure are told apart, "
          "a multi-TU test is built from its SRCS list, and project(VERSION) is read and "
          "passed to every compile -- or the run is refused")
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
    left_out = tier2_tests(tests)
    for test in left_out:
        print(f"not in the single header (Tier 2): {test.relative_to(REPO).as_posix()}")
    tests = [t for t in tests if t not in left_out]

    cmake_text = (REPO / "CMakeLists.txt").read_text(encoding="utf-8")
    multi_tu = multi_tu_sources(cmake_text)
    try:
        defines = version_defines(cmake_text)
    except ValueError as error:
        print(f"::error::{error}; without it version_test compares nothing against the "
              f"amalgamated header", file=sys.stderr)
        return 2
    if args.header is not None:
        if not args.header.is_file():
            print(f"::error::{args.header}: no such amalgamated header", file=sys.stderr)
            return 2
        return report(check(args.header.resolve(), args.cxx, args.jobs, tests, multi_tu, defines))
    with tempfile.TemporaryDirectory(prefix="metl-amalgamate-") as scratch:
        header = Path(scratch) / "metl-single.hpp"
        subprocess.run([sys.executable, str(REPO / "tools" / "amalgamate.py"), "-o", str(header)],
                       check=True, cwd=REPO, stdout=subprocess.DEVNULL)
        return report(check(header, args.cxx, args.jobs, tests, multi_tu, defines))


if __name__ == "__main__":
    sys.exit(main())
