#!/usr/bin/env python3
"""Require every METL_ASSERT precondition to be tripped by a test (#364).

A precondition check that no test makes fail can be deleted or inverted with
every test still passing. METL_HARDEN failures are tripped on purpose
(tests/core/hardening_common.h, the harden_floor_* tests); METL_ASSERT ones
mostly were not.

HOW. Tests trip an assert through tests/assert_trip.hpp, whose handler appends
the `file:line` it was handed to the file named by METL_ASSERT_TRIP_LOG. This
script runs ctest with that variable set, then compares the log with every
METL_ASSERT invocation in include/metl. A log line counts for an invocation
when it falls inside the invocation's lines, since compilers differ on which
line a macro spanning several reports.

THE RATCHET. Each header may have at most ALLOWED[header] asserts no test trips
yet, with the reason. The count may only fall: more untripped than allowed
fails and names them, and fewer fails too, asking for the lower number, so the
list stays true. A header absent from ALLOWED must have every assert tripped.
Counts, not lines, are recorded, so editing a header does not invalidate them.

Usage:
    tools/check_assert_trips.py --build-dir build     # runs ctest, then checks
    tools/check_assert_trips.py --log trips.log       # checks an existing log
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
    "arena_allocator.hpp": (2, "not tripped yet; #364 covers it in a later batch"),
    "coro/deadline_scheduler.hpp": (1, "not tripped yet; #364 covers it in a later batch"),
    "detail/ring_core.hpp": (7, "not tripped yet; #364 covers it in a later batch"),
    "expected.hpp": (21, "not tripped yet; #364 covers it in a later batch"),
    "fixed_deque.hpp": (1, "not tripped yet; #364 covers it in a later batch"),
    "fixed_string.hpp": (12, "not tripped yet; #364 covers it in a later batch"),
    "fixed_vector.hpp": (14, "not tripped yet; #364 covers it in a later batch"),
    "flat_map.hpp": (8, "not tripped yet; #364 covers it in a later batch"),
    "flat_set.hpp": (7, "not tripped yet; #364 covers it in a later batch"),
    "format.hpp": (3, "not tripped yet; #364 covers it in a later batch"),
    "handle_pool.hpp": (2, "not tripped yet; #364 covers it in a later batch"),
    "lookup_table.hpp": (2, "not tripped yet; #364 covers it in a later batch"),
    "monotonic_buffer.hpp": (1, "not tripped yet; #364 covers it in a later batch"),
    "object_pool.hpp": (1, "not tripped yet; #364 covers it in a later batch"),
    "spsc_byte_ring.hpp": (1, "not tripped yet; #364 covers it in a later batch"),
    "static_allocator.hpp": (4, "not tripped yet; #364 covers it in a later batch"),
    "static_message_queue.hpp": (3, "not tripped yet; #364 covers it in a later batch"),
    "static_unordered_map.hpp": (10, "not tripped yet; #364 covers it in a later batch"),
    "static_unordered_set.hpp": (3, "not tripped yet; #364 covers it in a later batch"),
    "tick_extender.hpp": (1, "not tripped yet; #364 covers it in a later batch"),
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


def read_log(path):
    """(header, line) pairs from a trip log, keeping only include/metl headers."""
    tripped = set()
    if not pathlib.Path(path).exists():
        return tripped
    for entry in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        file, _, line = entry.rpartition(":")
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


def run_ctest(build_dir, log):
    env = dict(os.environ, METL_ASSERT_TRIP_LOG=str(log))
    result = subprocess.run(["ctest", "--test-dir", str(build_dir), "-j", str(os.cpu_count() or 2),
                             "--output-on-failure", "--timeout", "120"],
                            env=env, cwd=REPO, capture_output=True, text=True)
    if result.returncode != 0:
        sys.stdout.write(result.stdout[-4000:])
        print("error: ctest failed; trips are only counted from a passing run", file=sys.stderr)
        return False
    return True


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
        log = root / "log"
        log.write_text("/x/include/metl/a.hpp:5\nnot a line\n/y/elsewhere.hpp:3\n", encoding="utf-8")
        tripped = read_log(log)
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
    source.add_argument("--build-dir", type=pathlib.Path, help="run ctest here with the trip log enabled")
    source.add_argument("--log", type=pathlib.Path, help="check an existing trip log")
    source.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()

    if args.build_dir is not None:
        log = (args.build_dir / "assert_trips.log").resolve()
        log.unlink(missing_ok=True)
        if not run_ctest(args.build_dir, log):
            return 1
    else:
        log = args.log

    problems = check(find_sites(), read_log(log), ALLOWED)
    for problem in problems:
        print(f"::error::{problem}" if os.environ.get("GITHUB_ACTIONS") else f"error: {problem}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
