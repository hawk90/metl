#!/usr/bin/env python3
"""Instruction-count ratchet for the micro-benchmarks.

WHY THIS EXISTS. `.github/workflows/ci.yml` says of `bench-smoke`:

    Deliberately asserts nothing. A wall-clock threshold on a shared runner
    either fires spuriously or never fires.

That is correct, and it is an argument about TIME. It had quietly been taken to
cover performance as a whole, and so six `perf(...)` commits shipped with no
gate on any of them -- including #18, whose entire subject line is "reclaim
tombstones in place to BOUND LOOKUP LATENCY". The bound it exists to establish
has never been measured.

An instruction count is not a time. Given the same binary, cachegrind returns
the same number on a loaded runner, an idle one, and a laptop: it counts
instructions the program executed, not seconds the machine took. Every objection
in the paragraph above dissolves, and what is left is a number with the same
character as `.text` in tools/check_size.py -- a deterministic function of the
source and the toolchain.

WHAT IT DOES NOT COVER, said plainly: this runs on the x86 CI host, so it gates
the ALGORITHM, not Cortex-M code generation. A regression that is real on ARM and
invisible on x86 is possible; `.text` and the stack ratchet in the `invariants`
job cover that half. Cache and pipeline behaviour are not covered by anything
here and cannot be without real hardware.

HOW IT MEASURES.

  * One process per benchmark. Cachegrind reports per-process totals, and
    attributing them per function does not survive -O2: every `run()` call in
    bench/ instantiates the SAME template (all bodies have type
    `void(&)(std::uint64_t)`), so an inlined body lands in a symbol it shares
    with its neighbours. One process per benchmark needs no attribution.

  * A measured baseline, subtracted. `--baseline` runs the binary with no
    benchmark selected. The dynamic loader, libc startup and static
    constructors are thousands of instructions that have nothing to do with the
    code under test and that move when the runner image moves.

  * A fixed iteration count. The harness auto-tunes against wall-clock by
    default, which makes the amount of work a property of the machine. `--fixed`
    replaces the tuner. Without it there is nothing stable to count.

  * The work list comes from `--list`, not from a copy kept here. Benchmarks
    marked `[scenario]` are skipped: they run threads and report what the
    threads completed, so the scheduler decides the work. A benchmark that
    cannot be made deterministic must not be counted as though it were.

Usage:
    tools/check_instructions.py --bench-dir build-bench --report
    tools/check_instructions.py --bench-dir build-bench
    tools/check_instructions.py --self-test
"""

import argparse
import pathlib
import re
import shutil
import subprocess
import sys

# Instructions executed, per benchmark, with the process baseline subtracted.
# Keyed "<binary>::<benchmark name>".
#
# EMPTY ON PURPOSE, exactly as check_size.py's and check_stack.py's were. The
# numbers are measured by the CI job with --report and written down in a
# follow-up commit. A local figure from a different libc and a different
# compiler is not the same measurement.
#
# A budget goes DOWN freely -- a faster implementation should tighten it. It
# goes UP only when the commit says what got more expensive and why.
BUDGETS = {}

# Instruction counts move a little with the compiler and the C library: an
# inlining decision here, a different memcpy path there. 5% is far tighter than
# anything this gate exists to catch -- swapping a binary search for a linear
# scan over 256 elements is not a 5% change, it is a 20x one -- and loose enough
# to survive a toolchain bump.
TOLERANCE_FRACTION = 0.05

# Below this, the subtraction is dominated by process noise and the percentage
# tolerance is worth only a handful of instructions. Benchmarks this cheap
# should be given a larger --fixed count instead of a special case.
MIN_MEANINGFUL_COUNT = 10_000

IREFS = re.compile(r"^==\d+==\s+I\s+refs:\s+([\d,]+)", re.M)

SCENARIO_MARKER = "[scenario]"


def instruction_count(runner, argv):
    """Instructions executed by one run of `argv`, via cachegrind."""
    command = [
        runner,
        "--tool=cachegrind",
        "--cachegrind-out-file=/dev/null",
        *argv,
    ]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(
            f"{' '.join(argv)} exited {result.returncode}\n"
            f"{result.stdout}\n{result.stderr}"
        )
    match = IREFS.search(result.stderr)
    if match is None:
        raise RuntimeError(
            "cachegrind printed no 'I refs:' line -- the tool did not run.\n"
            f"{result.stderr}"
        )
    return int(match.group(1).replace(",", ""))


def benchmark_names(binary):
    """(countable, skipped) names, from the binary's own --list."""
    result = subprocess.run(
        [str(binary), "--list"], capture_output=True, text=True, check=True
    )
    countable, skipped = [], []
    for line in result.stdout.splitlines():
        name = line.strip()
        if not name:
            continue
        if name.endswith(SCENARIO_MARKER):
            skipped.append(name[: -len(SCENARIO_MARKER)].strip())
        else:
            countable.append(name)
    return countable, skipped


def measure(bench_dir, iterations, runner):
    """{key: instructions} for every countable benchmark, baseline removed."""
    binaries = sorted(
        p for p in pathlib.Path(bench_dir).rglob("metl_bench_*") if p.is_file()
    )
    if not binaries:
        raise RuntimeError(
            f"no metl_bench_* binaries under {bench_dir} -- a run that measured "
            f"nothing must not report success."
        )

    measured, notes = {}, []
    for binary in binaries:
        countable, skipped = benchmark_names(binary)
        for name in skipped:
            # Never silent. A gate that quietly drops part of its subject reads
            # as covering it.
            notes.append(f"  skipped (thread-timed, no deterministic count): {binary.name}::{name}")
        if not countable:
            continue
        base = instruction_count(runner, [str(binary), "--fixed", str(iterations), "--baseline"])
        for name in countable:
            total = instruction_count(
                runner, [str(binary), "--fixed", str(iterations), "--only", name]
            )
            measured[f"{binary.name}::{name}"] = total - base
    return measured, notes


def compare(measured, report):
    """Return (exit_code, lines_to_print)."""
    out = []

    if not measured:
        out.append("::error::no countable benchmarks were measured. A check over "
                   "nothing reports success, which is the failure mode this line "
                   "exists to prevent.")
        return 1, out

    failures = []
    for key in sorted(measured):
        count = measured[key]
        budget = BUDGETS.get(key)
        if budget is None:
            out.append(f"  {count:12,}  {key}   (no budget recorded)")
            if not report:
                failures.append(
                    f"::error::no instruction budget for {key}. Set "
                    f"BUDGETS['{key}'] = {count} in tools/check_instructions.py "
                    f"from THIS number, measured by this job."
                )
            continue

        ceiling = int(budget * (1.0 + TOLERANCE_FRACTION))
        if count > ceiling:
            failures.append(
                f"::error::{key} executed {count:,} instructions, over the "
                f"{budget:,} budget (+{int(TOLERANCE_FRACTION * 100)}% = {ceiling:,}). "
                f"That is {count / budget:.2f}x. A jump of this kind usually means "
                f"an operation changed complexity class; find out what before "
                f"touching the number."
            )
            continue

        slack = ceiling - count
        out.append(f"  {count:12,}  {key}   (budget {budget:,}, {slack:,} of room)")
        if count < int(budget * (1.0 - TOLERANCE_FRACTION)):
            out.append(f"      NOTE: {budget - count:,} fewer than the budget. "
                       f"Lower BUDGETS['{key}'] to {count} so the ratchet keeps its grip.")

    if report:
        out.append("  --report: printing only, not enforcing")
        return 0, out

    if failures:
        out.extend(failures)
        return 1, out
    return 0, out


def self_test():
    """A gate that cannot fail is not a gate. Prove the comparison bites."""
    failures = []
    saved = dict(BUDGETS)
    try:
        BUDGETS.clear()

        # No budget recorded is a failure when enforcing, and fine under --report.
        code, _ = compare({"b::x": 1_000_000}, report=False)
        if code == 0:
            failures.append("accepted a benchmark with no budget recorded")
        code, _ = compare({"b::x": 1_000_000}, report=True)
        if code != 0:
            failures.append("--report refused a benchmark with no budget")

        BUDGETS["b::x"] = 1_000_000

        # At budget, and at exactly budget+tolerance, must pass.
        code, _ = compare({"b::x": 1_000_000}, report=False)
        if code != 0:
            failures.append("rejected a count exactly at its budget")
        code, _ = compare({"b::x": 1_050_000}, report=False)
        if code != 0:
            failures.append("rejected a count at exactly budget+tolerance")

        # One instruction past the ceiling must fail, or the tolerance is a
        # second budget rather than a tolerance.
        code, out = compare({"b::x": 1_050_001}, report=False)
        if code == 0:
            failures.append("accepted a count one instruction over the ceiling")

        # The regression this exists to catch: binary search becomes linear.
        code, out = compare({"b::x": 20_000_000}, report=False)
        if code == 0:
            failures.append("accepted a 20x instruction-count regression")
        if not any("20.00x" in line for line in out):
            failures.append("a 20x regression was reported without naming the factor")

        # A run that measured nothing must fail, not pass. This is the shape of
        # every dead gate: the tool ran, found no subject, and said OK.
        code, _ = compare({}, report=False)
        if code == 0:
            failures.append("a run that measured NO benchmarks reported success")
        code, _ = compare({}, report=True)
        if code == 0:
            failures.append("an empty measurement passed under --report")
    finally:
        BUDGETS.clear()
        BUDGETS.update(saved)

    if failures:
        for failure in failures:
            print(f"SELF-TEST FAILED: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: over-budget, unbudgeted and empty measurements are "
          "rejected; at-budget and at-tolerance counts are accepted")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bench-dir", help="directory holding metl_bench_* binaries")
    parser.add_argument("--iterations", type=int, default=5000,
                        help="value passed to the harness's --fixed (default 5000)")
    parser.add_argument("--runner", default="valgrind")
    parser.add_argument("--report", action="store_true",
                        help="print the measured counts and exit 0 with no budget set")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    if not args.bench_dir:
        print("error: --bench-dir is required", file=sys.stderr)
        return 2

    if shutil.which(args.runner) is None:
        print(f"::error::{args.runner} not found. This gate measures nothing "
              f"without it, and a check that measures nothing must not pass.",
              file=sys.stderr)
        return 1

    try:
        measured, notes = measure(args.bench_dir, args.iterations, args.runner)
    except RuntimeError as error:
        print(f"::error::{error}", file=sys.stderr)
        return 1

    print(f"instructions per benchmark (--fixed {args.iterations}, baseline subtracted)")
    for note in notes:
        print(note)
    code, lines = compare(measured, args.report)
    for line in lines:
        print(line)
    return code


if __name__ == "__main__":
    sys.exit(main())
