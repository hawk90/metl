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
    --self-test      check the failure classification against fixed outputs

A header clang-tidy could not analyse -- a compiler error, or a run that
failed outright -- is a failure whatever the budget says. It contributes no
findings, so it would otherwise read as a clean header, and a toolchain change
that broke every header would pass the ratchet at zero.

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
COMPILE_ERROR = re.compile(r"^[^ ]+:\d+:\d+: (?:fatal )?error: ")


def unanalysed(header, returncode, stdout, stderr):
    """Why `header` was not analysed, or None if clang-tidy got through it.

    clang-tidy exits 1 for warnings as well as for errors, so the status alone
    cannot tell them apart: a compiler error shows as an `error:` diagnostic
    and as "Error while processing" on stderr. Any other status is a crash."""
    errors = [line for line in stdout.splitlines() if COMPILE_ERROR.match(line)]
    if errors:
        return f"{header}: {errors[0]}"
    stderr_errors = [line for line in stderr.splitlines() if line.startswith("Error")]
    if stderr_errors:
        return f"{header}: {stderr_errors[-1]}"
    if returncode not in (0, 1):
        return f"{header}: clang-tidy exited {returncode}"
    return None


def self_test():
    cases = [
        (("a.hpp", 0, "", ""), False),
        (("a.hpp", 1, "x.hpp:1:2: warning: w [readability-x]", "1 warning generated."), False),
        (("a.hpp", 1, "x.hpp:1:2: error: unknown type name 'foo' [clang-diagnostic-error]",
          "Found compiler error(s)."), True),
        (("a.hpp", 0, "x.hpp:1:2: fatal error: 'y.hpp' file not found", ""), True),
        (("a.hpp", 1, "", "Error while processing a.hpp."), True),
        (("a.hpp", 1, "", "Error: no checks enabled."), True),
        (("a.hpp", -9, "", ""), True),
    ]
    for args, refused in cases:
        if (unanalysed(*args) is not None) != refused:
            print(f"self-test FAILED: {args} should {'' if refused else 'not '}be a failure")
            return 1
    print(f"self-test passed: {len(cases)} clang-tidy outcomes classified, "
          "including a compiler error, a missing include and a killed run")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-p", dest="build_dir", default="build")
    parser.add_argument("--max", type=int, default=None)
    parser.add_argument("--clang-tidy", default="clang-tidy")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    if args.self_test:
        return self_test()

    if not (Path(args.build_dir) / "compile_commands.json").is_file():
        print(f"error: {args.build_dir}/compile_commands.json not found.\n"
              f"       cmake -B {args.build_dir} -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
              file=sys.stderr)
        return 2

    # One TU per header, in parallel.
    def tidy(header):
        result = subprocess.run([args.clang_tidy, "-p", args.build_dir, "--quiet", str(header)],
                                capture_output=True, text=True)
        return header.relative_to(REPO), result

    headers = sorted((REPO / "include" / "metl").rglob("*.hpp"))
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 1) as pool:
        results = list(pool.map(tidy, headers))
    raw = "\n".join(result.stdout for _, result in results)
    failures = [reason for header, result in results
                if (reason := unanalysed(header, result.returncode, result.stdout, result.stderr))]

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

    if failures:
        print()
        print(f"FAIL: clang-tidy could not analyse {len(failures)} of {len(headers)} headers; "
              "their findings are missing from the count above:")
        for reason in failures:
            print(f"  {reason}")
        return 2

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
