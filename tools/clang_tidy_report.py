#!/usr/bin/env python3
"""Deduplicating clang-tidy runner for METL's public headers.

Why this exists
---------------
METL is header-only, so there is nothing to analyse but headers. clang-tidy
needs a translation unit, so the CI job compiles each public header as its
own TU. With `HeaderFilterRegex: '.*'` every one of those runs also reports
the findings of every header it includes -- and METL's headers include each
other heavily. One finding is therefore reported once per includer.

Measured 2026-08-19 (clang-tidy 22, `include/metl/**.hpp`):

    raw warning lines   2564
    distinct findings    546      <- 79% of the raw count is echo

The duplication is a *reporting* problem, not an analysis problem, and the
tempting fix is the wrong one. Narrowing to main-file diagnostics
(`--header-filter='$^'`) drops the raw count to 528 with no duplication, but
it also loses 18 real findings: diagnostics that only exist when a header is
instantiated from another header's context (the `cert-dcl58-cpp` hits in
lock.hpp / register_access.hpp, the const-data-member hits in
fixed_function.hpp / function_ref.hpp) never fire when that header is
compiled alone. So this script keeps the wide analysis and deduplicates the
report instead.

Size the backlog off the distinct count. The raw number makes a day of work
look like a rewrite.

Usage:
    tools/clang_tidy_report.py [-p BUILD_DIR] [--max N] [--clang-tidy PATH]

    -p BUILD_DIR     directory holding compile_commands.json (default: build)
    --max N          exit 1 if the distinct-finding count exceeds N. This is the
                     ratchet for promoting clang-tidy from advisory to blocking:
                     set it to today's count, and it can only go down.
    --clang-tidy P   clang-tidy binary (default: clang-tidy on PATH)

Note that the count is clang-tidy-version dependent -- a newer binary knows
more checks. Pin the version wherever you pin the budget.
"""

import argparse
import collections
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
FINDING = re.compile(r"^[^ ]+:\d+:\d+: warning: .*\[([a-z][^]]*)\]$")


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-p", dest="build_dir", default="build")
    parser.add_argument("--max", type=int, default=None)
    parser.add_argument("--clang-tidy", default="clang-tidy")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)

    if not (Path(args.build_dir) / "compile_commands.json").is_file():
        print(f"error: {args.build_dir}/compile_commands.json not found.\n"
              f"       cmake -B {args.build_dir} -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
              file=sys.stderr)
        return 2

    # One TU per header, in parallel. clang-tidy exits nonzero on findings and
    # this script is the one deciding whether findings are fatal, so its exit
    # status is ignored.
    def tidy(header):
        result = subprocess.run([args.clang_tidy, "-p", args.build_dir, "--quiet", str(header)],
                                stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
        return result.stdout

    headers = sorted((REPO / "include" / "metl").rglob("*.hpp"))
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 1) as pool:
        raw = "\n".join(pool.map(tidy, headers))

    # A finding is identified by file:line:col plus its message and check name --
    # the same finding reported from two includers is byte-identical apart from
    # ordering, so a set is the whole dedup.
    lines = [line.replace(f"{REPO}/", "") for line in raw.splitlines()]
    findings = [line for line in lines if FINDING.match(line)]
    distinct = sorted(set(findings))

    print(f"clang-tidy: {len(distinct)} distinct findings ({len(findings)} raw warning lines)")
    print()
    if distinct:
        print("By check:")
        # A finding can carry several check names ([a,b]); count it under each.
        by_check = collections.Counter(name for line in distinct
                                       for name in FINDING.match(line).group(1).split(","))
        for name, count in sorted(by_check.items(), key=lambda item: (-item[1], item[0])):
            print(f"  {count:6d}  {name}")
        print()
        print("Distinct findings:")
        for line in distinct:
            print(f"  {line}")

    if args.max is not None:
        print()
        if len(distinct) > args.max:
            print(f"FAIL: {len(distinct)} distinct findings exceeds the budget of {args.max}.")
            return 1
        print(f"OK: {len(distinct)} distinct findings, within the budget of {args.max}.")
        if len(distinct) < args.max:
            print(f"     The budget is now slack by {args.max - len(distinct)}. "
                  f"Lower it to {len(distinct)}.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
