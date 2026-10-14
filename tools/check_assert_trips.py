#!/usr/bin/env python3
"""Require every METL_ASSERT precondition to be tripped by a test (#364).

A precondition check that no test makes fail can be deleted or inverted with
every test still passing. METL_HARDEN failures are tripped on purpose
(tests/core/hardening_common.h, the harden_floor_* tests); METL_ASSERT ones
mostly were not.

HOW. Tests trip an assert through tests/assert_trip.hpp, whose handler prints
`metl-assert-trip <file>:<line>` for the location it was handed. This script
runs ctest verbosely, then compares those lines with every METL_ASSERT
invocation in include/metl. A trip counts for an invocation when its line falls
inside the invocation's lines, since compilers differ on which line a macro
spanning several reports. (Output rather than a file: nothing is created, and
it works on QEMU too.)

THE RATCHET. Each header may have at most ALLOWED[header] asserts no test trips
yet, with the reason. The count may only fall: more untripped than allowed
fails and names them, and fewer fails too, asking for the lower number, so the
list stays true. A header absent from ALLOWED must have every assert tripped.
Counts, not lines, are recorded, so editing a header does not invalidate them.

Usage:
    tools/check_assert_trips.py --build-dir build     # runs ctest, then checks
    tools/check_assert_trips.py --log ctest.txt       # checks saved `ctest -V` output
    tools/check_assert_trips.py --self-test
"""

import argparse
import os
import pathlib
import re
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent
INCLUDE = REPO / "include" / "metl"
ASSERT = re.compile(r"\bMETL_ASSERT\(")

# header -> (asserts not yet tripped by any test, why). May only shrink.
ALLOWED = {
    "static_unordered_map.hpp": (6, "after size_ < Capacity holds a free slot always exists, so `available` and the free-slot check are internal invariants no caller can break"),
    "static_unordered_set.hpp": (2, "after size_ < Capacity holds a free slot always exists, so `available` and the free-slot check are internal invariants no caller can break"),
}


def strip_comment(line):
    """The part of a line before a // comment (asserts do not hold string literals with //)."""
    index = line.find("//")
    return line if index < 0 else line[:index]


def find_sites(include_dir=INCLUDE):
    """Every METL_ASSERT invocation: (header, first line, last line, text)."""
    sites = []
    for path in sorted(include_dir.rglob("*.hpp")):
        header = path.relative_to(include_dir).as_posix()
        lines = path.read_text(encoding="utf-8").splitlines()
        for number, line in enumerate(lines, start=1):
            code = strip_comment(line)
            if code.lstrip().startswith("#"):
                continue  # the macro's own definition
            for match in ASSERT.finditer(code):
                depth, last, text = 0, number, []
                for offset, rest in enumerate(lines[number - 1:]):
                    segment = strip_comment(rest)[match.start() if offset == 0 else 0:]
                    for char in segment:
                        text.append(char)
                        depth += char == "("
                        depth -= char == ")"
                        if depth == 0 and char == ")":
                            break
                    if depth == 0:
                        last = number + offset
                        break
                sites.append((header, number, last, "".join(text).strip()))
    return sites


TRIP = re.compile(r"metl-assert-trip (\S+)$")


def read_trips(text):
    """(header, line) pairs from test output, keeping only include/metl headers."""
    tripped = set()
    for raw in text.splitlines():
        found = TRIP.search(raw.rstrip())
        if found is None:
            continue
        file, _, line = found.group(1).rpartition(":")
        normalized = file.replace("\\", "/")
        marker = "include/metl/"
        if marker not in normalized or not line.isdigit():
            continue
        tripped.add((normalized.split(marker, 1)[1], int(line)))
    return tripped


def untripped(sites, tripped):
    by_header = {}
    for header, first, last, text in sites:
        if not any(h == header and first <= line <= last for h, line in tripped):
            by_header.setdefault(header, []).append((first, text))
    return by_header


def check(sites, tripped, allowed, quiet=False):
    """Problems as text; empty when the ratchet holds."""
    missing = untripped(sites, tripped)
    problems = []
    headers = {header for header, *_ in sites}
    for header in sorted(headers | set(allowed)):
        count = len(missing.get(header, []))
        limit, _reason = allowed.get(header, (0, ""))
        if header not in headers:
            problems.append(f"{header}: in ALLOWED but has no METL_ASSERT; remove the entry")
        elif count > limit:
            listing = "\n".join(f"    line {line}: {text}" for line, text in missing[header])
            problems.append(f"{header}: {count} assert(s) no test trips, {limit} allowed:\n{listing}")
        elif count < limit:
            problems.append(f"{header}: {count} assert(s) untripped but ALLOWED says {limit}; "
                            f"lower it to {count}" + ("" if count else " (or remove the entry)"))
    if not quiet:
        total = len(sites)
        hit = total - sum(len(v) for v in missing.values())
        print(f"assert trips: {hit} of {total} METL_ASSERTs in include/metl tripped by a test")
    return problems


def run_ctest(build_dir):
    """ctest's verbose output, or None if a test failed."""
    result = subprocess.run(["ctest", "--test-dir", str(build_dir), "-j", str(os.cpu_count() or 2),
                             "-V", "--timeout", "120"],
                            cwd=REPO, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stdout.write(result.stdout[-4000:])
        print("error: ctest failed; trips are only counted from a passing run", file=sys.stderr)
        return None
    return result.stdout


def self_test():
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "a.hpp").write_text(
            "#define METL_ASSERT(x) (void)0\n"
            "void f(int i) {\n"
            "  METL_ASSERT(i > 0);  // METL_ASSERT(not this)\n"
            "  METL_ASSERT(i <\n"
            "              9);\n"
            "}\n", encoding="utf-8")
        sites = find_sites(root)
        assert [(s[1], s[2]) for s in sites] == [(3, 3), (4, 5)], sites
        tripped = read_trips("12: metl-assert-trip /x/include/metl/a.hpp:5\nnot a line\n"
                             "metl-assert-trip /y/elsewhere.hpp:3\n")
        assert tripped == {("a.hpp", 5)}, tripped
        assert check(sites, tripped, {"a.hpp": (1, "r")}, quiet=True) == []
        assert check(sites, tripped, {}, quiet=True), "an untripped assert must fail"
        assert check(sites, tripped, {"a.hpp": (2, "r")}, quiet=True), "a stale allowance must fail"
        assert check(sites, tripped, {"a.hpp": (1, "r"), "gone.hpp": (1, "r")}, quiet=True)
        assert check(sites, tripped | {("a.hpp", 3)}, {}, quiet=True) == []
    print("self-test passed: untripped asserts, stale allowances and multi-line invocations are handled")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--build-dir", type=pathlib.Path, help="run ctest here and read its output")
    source.add_argument("--log", type=pathlib.Path, help="check saved `ctest -V` output")
    source.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()

    if args.build_dir is not None:
        output = run_ctest(args.build_dir)
        if output is None:
            return 1
    else:
        output = args.log.read_text(encoding="utf-8")

    problems = check(find_sites(), read_trips(output), ALLOWED)
    for problem in problems:
        print(f"::error::{problem}" if os.environ.get("GITHUB_ACTIONS") else f"error: {problem}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
