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
  * `#if` arms of the other configurations, unless --configurations is given.
    Then each of CONFIGURATIONS is built and run too, and every line that only
    that configuration compiles must have run: the -fno-exceptions arms, the
    C++20 comparisons, the bitwise CRC loops. Their figures cannot be merged
    into this one -- llvm-cov keeps one mapping per function name, so handing it
    both builds' binaries reported exactly the default build's numbers -- and
    so they are a separate gate (#387).
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
  * A constant condition reads as a MISSED branch. llvm-cov's lcov export
    writes the folded side of a branch -- the `while (false)` of every
    METL_ASSERT, METL_HARDEN, METL_DASSERT and METL_PANIC -- as a branch never
    taken. `llvm-cov report` and the record's own BRF/BRH leave it out
    (llvm/llvm-project#231521, #363). So the gated branch figure, and
    Codecov's partial lines, read low: on 106de55e7, 77.5% branches where an
    export that skips folded sides gives 85.7%. The floors are set against the
    figure as exported. Do not correct it by matching source text; wait for
    the fixed export and reset the floors from CI.

So: this measures host-visible RUNTIME branch depth. It is a floor on quality,
not a summary of it.

ONE FILE, ONE FORMULA (#308). The gated figures are computed from the same
include/metl lcov export that Codecov receives, with Codecov's formula, so the
badge and the gate report the same number for the same commit:

  lines     hits / (hits + partials + misses), where a partial is a line that
            ran but has a branch that never went one of its ways. llvm-cov's
            own line figure counts a partial line as covered, which is why it
            read 91.5% while Codecov showed 87%.
  branches  branches taken / branches found, from the same lcov records.

THE NUMBERS ARE COMPILER-VERSION DEPENDENT. Different Clang releases lower the
same source to different numbers of instrumented branches -- this repo measured
1258 branches under Apple Clang and 1122 under Ubuntu's, giving 75.1% and 72.3%
for identical code. So a local run will not match CI, and the CI number is the
one the floor is set against. Do not chase a local figure.

Usage:
    tools/coverage.py [--min-lines N] [--min-branches N] [--lcov PATH] [--configurations]
    tools/coverage.py --self-test

--lcov writes the include/metl lcov file it measured, for Codecov to upload.
The floors here stay the gate; Codecov only displays.
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

# The configurations whose `#if` arms the default build does not compile, each
# with the flags that select it. Each is measured on its own (#387), and each
# must compile at least one line the default build does not: one that compiles
# none has nothing for this gate to check.
#
# METL_HARDENING=0 is not here, for exactly that reason. It only REMOVES checks,
# so it compiles no line of its own; it sat here reporting "0 lines, 0 of them
# ran" on every run. The suite at that level runs in the sanitizers job
# (asan-ubsan-hardening-none).
CONFIGURATIONS = {
    "cxx20": "-std=c++20",
    "no-exceptions": "-fno-exceptions -fno-rtti",
    "crc-table-off": "-DMETL_CRC_TABLE=0",
}


def find_tool(name):
    found = shutil.which(name)
    if found is None and shutil.which("xcrun"):
        result = subprocess.run(["xcrun", "--find", name], capture_output=True, text=True)
        found = result.stdout.strip() or None
    return found


def pct(total, missed):
    return 100.0 * (total - missed) / total if total else 0.0


def lcov_filtered(text, marker):
    """The lcov records whose source file path contains `marker`, paths made
    repo-relative so Codecov matches them to the tree without a path fix."""
    out, record, keep = [], [], False
    for line in text.splitlines():
        if line.startswith("SF:"):
            path = line[3:]
            keep = marker in path
            if keep and path.startswith(str(REPO) + os.sep):
                line = "SF:" + os.path.relpath(path, REPO)
        record.append(line)
        if line == "end_of_record":
            if keep:
                out.extend(record)
            record, keep = [], False
    return "\n".join(out) + "\n" if out else ""


def lcov_records_under(text, marker):
    return sum(1 for line in text.splitlines() if line.startswith("SF:") and marker in line)


def lcov_totals(text):
    """Line and branch counts from lcov records, as Codecov counts them.

    Returns (hits, partials, misses, branches_hit, branches_found). A line is a
    partial when it ran and one of its branches never went one of its ways
    (BRDA taken count 0 or '-')."""
    hits = partials = misses = branches_hit = branches_found = 0
    line_hits, line_has_untaken = {}, set()

    def flush():
        nonlocal hits, partials, misses
        for line, count in line_hits.items():
            if count == 0:
                misses += 1
            elif line in line_has_untaken:
                partials += 1
            else:
                hits += 1
        line_hits.clear()
        line_has_untaken.clear()

    for raw in text.splitlines():
        if raw.startswith("DA:"):
            fields = raw[3:].split(",")
            line, count = int(fields[0]), int(fields[1])
            line_hits[line] = max(line_hits.get(line, 0), count)
        elif raw.startswith("BRDA:"):
            line, _block, _branch, taken = raw[5:].split(",")
            branches_found += 1
            if taken not in ("-", "0"):
                branches_hit += 1
            else:
                line_has_untaken.add(int(line))
        elif raw == "end_of_record":
            flush()
    flush()
    return hits, partials, misses, branches_hit, branches_found


def line_counts(text):
    """{(repo-relative file, line): execution count} from lcov records."""
    counts, path = {}, None
    for raw in text.splitlines():
        if raw.startswith("SF:"):
            path = raw[3:]
        elif raw.startswith("DA:"):
            fields = raw[3:].split(",")
            key = (path, int(fields[0]))
            counts[key] = max(counts.get(key, 0), int(fields[1]))
    return counts


def carries_code(text):
    """Whether a source line holds code: not blank, a comment or a lone brace.
    Region boundaries differ between builds, so a configuration can report a
    blank line the default build does not."""
    stripped = text.strip()
    return bool(stripped) and not stripped.startswith(("//", "/*", "*")) and stripped not in ("{", "}", "};")


def configuration_only_unexecuted(default_lcov, configuration_lcov, source_line):
    """The code lines only `configuration_lcov` reports, and of those the ones
    that never ran. `source_line(path, number)` returns the line's text."""
    default = line_counts(default_lcov)
    only = {key: count for key, count in line_counts(configuration_lcov).items()
            if key not in default and carries_code(source_line(*key))}
    return only, sorted(key for key, count in only.items() if count == 0)


def configuration_failures(name, only, unexecuted, source_line):
    """FAIL lines for one configuration: each line only it compiles that no
    test ran -- and, if it compiles no line of its own, that, because then
    nothing was checked."""
    if not only:
        return [f"FAIL: the {name} configuration compiled no line the default build does not, so "
                f"there is nothing for it to check: its flags did not take effect, or it does not "
                f"belong in CONFIGURATIONS"]
    return [f"FAIL: {path}:{number} is compiled only in {name} and no test ran it: "
            f"{source_line(path, number).strip()}" for path, number in unexecuted]


def read_source_line(path, number):
    try:
        return (REPO / path).read_text().splitlines()[number - 1]
    except (OSError, IndexError):
        return ""


SELF_TEST_LCOV = """SF:include/metl/a.hpp
DA:1,5
DA:2,0
DA:3,2
DA:4,1
BRDA:3,0,0,2
BRDA:3,0,1,0
BRDA:4,0,0,1
BRDA:4,0,1,-
BRDA:5,0,0,3
end_of_record
SF:include/metl/b.hpp
DA:1,1
DA:1,0
end_of_record
"""


def self_test():
    # a.hpp: line 1 hit, 2 missed, 3 and 4 partial (a branch never taken);
    # b.hpp: a line listed twice counts once, as hit. 5 branches, 3 taken.
    got = lcov_totals(SELF_TEST_LCOV)
    want = (2, 2, 1, 3, 5)
    if got != want:
        print(f"SELF-TEST FAILED: lcov_totals gave {got}, expected {want}", file=sys.stderr)
        return 1
    # A configuration adds lines 7-9 to a.hpp: 7 ran, 8 did not, 9 is blank.
    default = "SF:include/metl/a.hpp\nDA:1,1\nDA:2,0\nend_of_record\n"
    other = "SF:include/metl/a.hpp\nDA:1,0\nDA:7,3\nDA:8,0\nDA:9,0\nend_of_record\n"
    source = {1: "int f();", 7: "  throw_or_abort();", 8: "  std::abort();", 9: ""}
    only, unexecuted = configuration_only_unexecuted(default, other, lambda _path, n: source.get(n, ""))
    if sorted(only) != [("include/metl/a.hpp", 7), ("include/metl/a.hpp", 8)] or \
            unexecuted != [("include/metl/a.hpp", 8)]:
        print(f"SELF-TEST FAILED: configuration-only lines {sorted(only)}, unexecuted {unexecuted}",
              file=sys.stderr)
        return 1
    if configuration_failures("fixture", only, unexecuted, lambda _path, n: source.get(n, "")) != [
            "FAIL: include/metl/a.hpp:8 is compiled only in fixture and no test ran it: std::abort();"]:
        print("SELF-TEST FAILED: a configuration-only line that never ran was not reported",
              file=sys.stderr)
        return 1
    # A configuration that compiles nothing of its own checks nothing.
    nothing, _ = configuration_only_unexecuted(default, default, lambda _path, n: source.get(n, ""))
    if not configuration_failures("fixture", nothing, [], lambda _path, _n: ""):
        print("SELF-TEST FAILED: a configuration with no lines of its own passed", file=sys.stderr)
        return 1
    # Only include/metl records are kept, with the repository prefix removed so
    # Codecov matches them to the tree; a test source's record is dropped.
    exported = (f"SF:{REPO}/include/metl/a.hpp\nDA:1,1\nend_of_record\n"
                f"SF:{REPO}/tests/a_test.cpp\nDA:1,0\nend_of_record\n")
    if lcov_filtered(exported, "include/metl/") != "SF:include/metl/a.hpp\nDA:1,1\nend_of_record\n":
        print(f"SELF-TEST FAILED: lcov_filtered kept {lcov_filtered(exported, 'include/metl/')!r}",
              file=sys.stderr)
        return 1
    if lcov_filtered(exported, "no-such-dir/"):
        print("SELF-TEST FAILED: lcov_filtered kept a record outside the marker", file=sys.stderr)
        return 1
    print("self-test passed: partial lines and untaken branches are counted as Codecov counts them, "
          "only include/metl records are measured, and a configuration's own code lines are found "
          "and checked -- and a configuration with none fails")
    return 0


def measure(build_dir, cxx_flags, profdata, cov):
    """Builds `build_dir` instrumented with `cxx_flags`, runs every test and
    returns (the lcov export, llvm-cov's report), or None after saying why."""
    prof_dir = build_dir / "profraw"
    shutil.rmtree(build_dir, ignore_errors=True)
    prof_dir.mkdir(parents=True)

    quiet = {"stdout": subprocess.DEVNULL}
    configure = ["cmake", "-B", str(build_dir), "-S", str(REPO), "-DCMAKE_BUILD_TYPE=Debug",
                 "-DMETL_ENABLE_COVERAGE=ON", "-DMETL_INSTALL=OFF"]
    if cxx_flags:
        configure.append(f"-DCMAKE_CXX_FLAGS={cxx_flags}")
    if subprocess.run(configure, **quiet).returncode:
        print(f"error: configuring {build_dir.name} failed", file=sys.stderr)
        return None
    if subprocess.run(["cmake", "--build", str(build_dir), "-j"], **quiet).returncode:
        print(f"error: building {build_dir.name} failed", file=sys.stderr)
        return None

    env = dict(os.environ, LLVM_PROFILE_FILE=str(prof_dir / "%p.profraw"))
    ctest = subprocess.run(["ctest", "--no-tests=error", "--test-dir", str(build_dir), "-j4",
                            "--output-on-failure"], env=env, capture_output=True, text=True)
    (build_dir / "ctest.log").write_text(ctest.stdout + ctest.stderr)
    if ctest.returncode != 0:
        # The whole log: the failing test's own output is what explains it.
        print(ctest.stdout + ctest.stderr)
        print("error: tests failed under instrumentation; coverage numbers would be meaningless",
              file=sys.stderr)
        return None
    print("\n".join((ctest.stdout + ctest.stderr).splitlines()[-3:]))

    profile = build_dir / "coverage.profdata"
    if subprocess.run([profdata, "merge", "-sparse", *map(str, prof_dir.glob("*.profraw")),
                       "-o", str(profile)]).returncode:
        print(f"error: llvm-profdata could not merge the profiles in {prof_dir}", file=sys.stderr)
        return None

    # Every test is its own binary, so every one has to be handed to llvm-cov or
    # the functions only that binary exercised are reported as unexecuted.
    objects = []
    for binary in sorted(build_dir.glob("metl_*")):
        if binary.is_file() and os.access(binary, os.X_OK):
            objects += ["-object", str(binary)]
    report = subprocess.run([cov, "report", *objects, f"-instr-profile={profile}"],
                            capture_output=True, text=True).stdout
    (build_dir / "report.txt").write_text(report)

    exported = subprocess.run([cov, "export", "-format=lcov", *objects,
                               f"-instr-profile={profile}"],
                              capture_output=True, text=True)
    if exported.returncode:
        print(exported.stderr, file=sys.stderr)
        return None
    return exported.stdout, report


def configurations_ran(default_lcov, profdata, cov):
    """Measures each of CONFIGURATIONS and checks that every line only it
    compiles ran. Returns whether all did."""
    print("\n=== configurations (#387): the lines only each one compiles must run ===")
    all_ran = True
    for name, flags in CONFIGURATIONS.items():
        print(f"--- {name} ({flags})")
        measured = measure(REPO / f"build-coverage-{name}", flags, profdata, cov)
        if measured is None:
            print(f"FAIL: the {name} configuration did not build and run")
            all_ran = False
            continue
        lcov = lcov_filtered(measured[0], "include/metl/")
        hits, partials, misses, br_hit, br_found = lcov_totals(lcov)
        only, unexecuted = configuration_only_unexecuted(default_lcov, lcov, read_source_line)
        lines = hits + partials + misses
        print(f"  lines {100.0 * hits / lines if lines else 0.0:6.2f}%   branches "
              f"{100.0 * br_hit / br_found if br_found else 0.0:6.2f}%   (for reference)")
        print(f"  lines only this configuration compiles: {len(only)}, "
              f"{len(only) - len(unexecuted)} of them ran")
        for failure in configuration_failures(name, only, unexecuted, read_source_line):
            print(failure)
            all_ran = False
    return all_ran


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--min-lines", type=float, default=89.4)
    parser.add_argument("--min-branches", type=float, default=83.0)
    parser.add_argument("--lcov", type=Path,
                        help="also write the include/metl lcov file it measured here")
    parser.add_argument("--configurations", action="store_true",
                        help="also build each of CONFIGURATIONS and require every line only it "
                             "compiles to have run")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    sys.stdout.reconfigure(line_buffering=True)

    profdata, cov = find_tool("llvm-profdata"), find_tool("llvm-cov")
    if profdata is None or cov is None:
        print("error: llvm-profdata / llvm-cov not found on PATH", file=sys.stderr)
        return 2

    measured = measure(BUILD_DIR, "", profdata, cov)
    if measured is None:
        return 1
    exported_lcov, report = measured
    kept = lcov_records_under(exported_lcov, "include/metl/")
    if kept == 0:
        print("error: the lcov export has no include/metl record", file=sys.stderr)
        return 1
    lcov = lcov_filtered(exported_lcov, "include/metl/")
    (BUILD_DIR / "metl.lcov").write_text(lcov)
    if args.lcov is not None:
        args.lcov.write_text(lcov)
        print(f"lcov: {kept} include/metl file(s) written to {args.lcov}")

    # Columns: file, regions, missed, cover, functions, missed, executed,
    # lines, missed, cover, branches, missed, cover.
    rows = [line.split() for line in report.splitlines() if "include/metl" in line]
    rows = [row for row in rows if len(row) >= 13]

    print("\n=== per-file, weakest branch coverage first ===")
    with_branches = [row for row in rows if float(row[10]) > 0]
    for row in sorted(with_branches, key=lambda row: float(row[12].rstrip("%"))):
        print(f"  branches {float(row[12].rstrip('%')):7.2f}%   lines {row[9]:<9} {row[0]}")

    total = [sum(int(row[i]) for row in rows) for i in (1, 2, 4, 5, 7, 8)]
    reg, regmiss, fn, fnmiss, ln, lnmiss = total
    hits, partials, misses, br_hit, br_found = lcov_totals(lcov)
    lines_pct = 100.0 * hits / (hits + partials + misses) if hits + partials + misses else 0.0
    branch_pct = 100.0 * br_hit / br_found if br_found else 0.0
    print("\n=== include/metl totals (gated, from the lcov file Codecov receives) ===")
    print(f"  lines     {lines_pct:6.2f}%  ({hits} hit, {partials} partial, {misses} missed)"
          f"   floor {args.min_lines:g}%")
    print(f"  branches  {branch_pct:6.2f}%  ({br_hit}/{br_found})   floor {args.min_branches:g}%")
    print("=== llvm-cov report, for reference (a partial line counts as covered) ===")
    print(f"  regions   {pct(reg, regmiss):6.2f}%  ({reg - regmiss}/{reg})")
    print(f"  functions {pct(fn, fnmiss):6.2f}%  ({fn - fnmiss}/{fn})")
    print(f"  lines     {pct(ln, lnmiss):6.2f}%  ({ln - lnmiss}/{ln})")

    failed = False
    if args.configurations:
        failed = not configurations_ran(lcov, profdata, cov)
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
