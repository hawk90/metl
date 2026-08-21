#!/usr/bin/env python3
"""Code-size ratchet for the linked invariant probe.

Why size and not speed. `bench-smoke` builds the benchmarks, runs them and
prints the numbers -- and asserts nothing, so it is a job that can only pass.
docs/TODO.md already rejected the obvious fix ("a threshold on a shared runner
either fires spuriously or never fires"), and that rejection is right: wall-clock
on a shared GitHub runner is noise.

Code size is the measurement that argument does *not* apply to. Cross-compiled
with fixed flags, `.text` is a deterministic function of the source and the
toolchain -- the same tree produces the same byte count every time. And on the
target METL is actually for, an unnoticed kilobyte matters more than a
percentage of throughput on somebody's laptop.

What is measured: the LINKED `invariant_probe.elf`, not the archive. An archive
carries every object whether or not a linker would keep it; the ELF is what a
user pays for. The probe exercises a hand-picked set of containers (see
docs/SCOPE.md section 8), so this ratchet tracks *that set* -- adding a type to
the library moves nothing here until the probe uses it, which is the same
coverage caveat the invariant audit carries and for the same reason.

Usage:
    tools/check_size.py --elf invariant_probe.elf --cpu cortex-m3
    tools/check_size.py --elf invariant_probe.elf --cpu cortex-m3 --report
    tools/check_size.py --self-test
"""

import argparse
import re
import subprocess
import sys

# Budgets in bytes for `.text` of the linked probe, per CPU, measured by the
# `invariants` CI job itself. A local number is not a substitute and never has
# been in this repo: there is no ARM toolchain on the dev machine, and even where
# a local toolchain exists it has disagreed with CI three times running (a
# clang-tidy figure that was 3.7x slack, a delta with the wrong sign, and a
# header that reported clean locally and two findings on CI).
#
# So these start as None. The job prints the measured sizes with --report, and
# the numbers are set from that output in a follow-up commit -- the same
# two-step the clang-tidy ratchet used, for the same reason.
#
# A budget can only go DOWN. If a toolchain bump moves these, re-measure and say
# so in the commit; do not pad.
BUDGETS = {
    "cortex-m0": None,
    "cortex-m3": None,
    "cortex-m4": None,
    "cortex-m7": None,
}

# Room for a toolchain that shifts codegen slightly without anything regressing.
# Ubuntu's arm-none-eabi-gcc moves with the runner image, and a minor version can
# change inlining decisions by a few hundred bytes on an image this small.
TOLERANCE_BYTES = 512

SECTION_LINE = re.compile(r"^(\.\S+)\s+(\d+)\s+")


def section_sizes(elf, size_tool):
    """{section: bytes} from `<size_tool> -A <elf>`."""
    output = subprocess.run(
        [size_tool, "-A", elf], capture_output=True, text=True, check=True
    ).stdout
    sizes = {}
    for line in output.splitlines():
        match = SECTION_LINE.match(line.strip())
        if match:
            sizes[match.group(1)] = int(match.group(2))
    return sizes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf")
    parser.add_argument("--cpu")
    parser.add_argument("--size-tool", default="arm-none-eabi-size")
    parser.add_argument(
        "--report",
        action="store_true",
        help="print the measured size and exit 0 even with no budget set",
    )
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    if not args.elf or not args.cpu:
        print("error: --elf and --cpu are required", file=sys.stderr)
        return 2

    sizes = section_sizes(args.elf, args.size_tool)
    text = sizes.get(".text", 0)
    rodata = sizes.get(".rodata", 0)
    print(f"{args.cpu}: .text={text} .rodata={rodata} bytes  ({args.elf})")

    budget = BUDGETS.get(args.cpu)
    if budget is None:
        print(
            f"  no budget recorded for {args.cpu} yet -- set BUDGETS['{args.cpu}'] = {text} "
            f"in tools/check_size.py from THIS number, not a local one"
        )
        return 0 if args.report else 0

    ceiling = budget + TOLERANCE_BYTES
    if text > ceiling:
        print(
            f"::error::{args.cpu} .text grew to {text} bytes, over the {budget}-byte "
            f"budget (+{TOLERANCE_BYTES} tolerance = {ceiling}).",
            file=sys.stderr,
        )
        print(
            f"::error::That is {text - budget} bytes more than the recorded size. If a "
            f"toolchain bump moved it, re-measure with --report and say so; do not pad.",
            file=sys.stderr,
        )
        return 1

    slack = ceiling - text
    print(f"  within budget {budget} (+{TOLERANCE_BYTES} tolerance), {slack} bytes of room")
    if text < budget - TOLERANCE_BYTES:
        print(
            f"  NOTE: {budget - text} bytes SMALLER than the budget. Lower "
            f"BUDGETS['{args.cpu}'] to {text} so the ratchet keeps its grip."
        )
    return 0


# A gate that cannot fail is not a gate. These fixtures prove the comparison
# still bites and still lets a legitimate size through.
def self_test():
    failures = []

    saved = dict(BUDGETS)
    try:
        BUDGETS["fixture"] = 10_000
        over = _decide("fixture", 10_000 + TOLERANCE_BYTES + 1)
        if over == 0:
            failures.append("an over-budget size was accepted -- the ratchet is dead")
        under = _decide("fixture", 10_000)
        if under != 0:
            failures.append("an at-budget size was rejected -- the ratchet is too tight")
        edge = _decide("fixture", 10_000 + TOLERANCE_BYTES)
        if edge != 0:
            failures.append("a size exactly at the tolerance edge was rejected")
    finally:
        BUDGETS.clear()
        BUDGETS.update(saved)

    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("self-test passed: over-budget is rejected, at-budget and edge are accepted")
    return 0


def _decide(cpu, text):
    """The comparison alone, so the self-test does not need an ELF."""
    budget = BUDGETS[cpu]
    return 1 if text > budget + TOLERANCE_BYTES else 0


if __name__ == "__main__":
    sys.exit(main())
