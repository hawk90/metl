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

THREE METRICS, and the second one is why this file changed.

  .text    the code. Gated since #56.
  .rodata  constants -- lookup tables, string literals. Also flash, also
           deterministic, and it was being MEASURED AND PRINTED on every run of
           every invariants job since the beginning while being compared against
           nothing. `rodata = sizes.get(".rodata", 0)` fed a printf and stopped
           there. That is the "step that can only pass" this file's own header
           argues against, sitting inside the file that argues it.

           It is not idle either: #28 turned CRC's bit-at-a-time loop into a
           nibble table by default, which is a deliberate trade of .rodata for
           speed. A repository that gates .text and not .rodata says that trade
           is free.

  ram      `.bss` + `.data` -- the static storage a caller declares. This is the
           other half of the RAM question that #77 and #79 opened: #77 pins
           `sizeof` per container with static_assert, #79 measures the stack.
           Neither can see a hidden static inside a header, a function-local
           static and its guard variable, or a table that moved from .rodata
           into .data. Measured on the stack probe's OBJECT file, because that
           probe already declares realistic-capacity containers in static
           storage -- which is exactly what a .bss ratchet needs, and it existed
           already.

Sections are summed by PREFIX. Under -ffunction-sections/-fdata-sections the
compiler emits `.bss._ZL7storage` rather than one `.bss`, so an exact-name
lookup reads zero and a ratchet over zero passes forever.

Usage:
    tools/check_size.py --elf invariant_probe.elf --cpu cortex-m3
    tools/check_size.py --elf invariant_probe.elf --cpu cortex-m3 --report
    tools/check_size.py --ram-object stack_probe.o --cpu cortex-m3
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
# A budget goes DOWN freely. It goes UP only when the probe was deliberately
# given more to link, and the commit that raises it has to say what and why --
# never to make a red job green. "Do not pad" is the whole point: a budget set
# above what was measured is a ratchet that has stopped ratcheting.
#
# Raised once so far, and it earned its keep. Adding metl/parse.hpp to the probe
# cost +1504 bytes on Cortex-M0 against +812 on M3/M4/M7 -- the outlier said the
# fold was using 64-bit arithmetic and a runtime divide on the one target with
# neither. Fixing that brought M0 to +660 and the spread to 660..704. Had the
# budget simply been raised to fit the first number, that bug would have shipped.
#
# Measured by the `invariants` job on run 32483066357 (PR #66, after the fix).
BUDGETS = {
    "cortex-m0": 3440,
    "cortex-m3": 4236,
    "cortex-m4": 4244,
    "cortex-m7": 4260,
}

# Room for a toolchain that shifts codegen slightly without anything regressing.
# Ubuntu's arm-none-eabi-gcc moves with the runner image, and a minor version can
# change inlining decisions by a few hundred bytes on an image this small.
TOLERANCE_BYTES = 512

# Budgets for `.rodata` of the same linked probe. Measured by the `invariants`
# job on main, run 32586215841: all four targets reported 408, which is what a
# section of pure constants should do -- it does not depend on instruction
# selection the way `.text` does.
#
# The tolerance is deliberately much tighter than .text's 512. A toolchain bump
# moves code generation; it does not move the number of constant bytes the
# source declares. Slack here would only hide the thing this is for -- a table
# quietly arriving in the image.
RODATA_BUDGETS = {
    "cortex-m0": 408,
    "cortex-m3": 408,
    "cortex-m4": 408,
    "cortex-m7": 408,
}
RODATA_TOLERANCE_BYTES = 64

# Budgets for `.bss` + `.data` of the stack probe's object file. EMPTY ON
# PURPOSE: this is a new measurement and CI has never printed it, so it goes
# through the same two-step every other ratchet here did -- --report first, the
# numbers written down from that output afterwards.
RAM_BUDGETS = {}
RAM_TOLERANCE_BYTES = 64

SECTION_LINE = re.compile(r"^(\.\S+)\s+(\d+)\s+")


def section_sizes(artifact, size_tool):
    """{section: bytes} from `<size_tool> -A <artifact>`."""
    result = subprocess.run([size_tool, "-A", artifact], capture_output=True, text=True)
    if result.returncode != 0:
        # A traceback here would be read as a broken script rather than as a
        # broken measurement, and the job would need someone to go and look.
        raise RuntimeError(
            f"{size_tool} -A {artifact} exited {result.returncode}: "
            f"{result.stderr.strip() or 'no diagnostic'}"
        )
    output = result.stdout
    sizes = {}
    for line in output.splitlines():
        match = SECTION_LINE.match(line.strip())
        if match:
            sizes[match.group(1)] = int(match.group(2))
    return sizes


def sum_sections(sizes, *names):
    """Total of every section called `name` or starting with `name.`.

    -ffunction-sections/-fdata-sections split a section per symbol, so an object
    file has `.bss._ZL7storage` and no plain `.bss` at all. Looking the bare name
    up returns 0, and a ratchet against 0 is a ratchet against nothing.
    """
    total = 0
    for section, size in sizes.items():
        for name in names:
            if section == name or section.startswith(name + "."):
                total += size
                break
    return total


def compare(label, measured, budget, tolerance, cpu, report, key):
    """Return (exit_code, lines). Shared by all three metrics."""
    out = []

    if budget is None:
        out.append(f"  {label}={measured} bytes  (no budget recorded)")
        if report:
            return 0, out
        out.append(f"::error::no {label} budget recorded for {cpu}. Set "
                   f"{key}['{cpu}'] = {measured} in tools/check_size.py from THIS "
                   f"number, measured by this job, not a local one.")
        return 1, out

    ceiling = budget + tolerance
    if measured > ceiling:
        out.append(f"::error::{cpu} {label} grew to {measured} bytes, over the "
                   f"{budget}-byte budget (+{tolerance} tolerance = {ceiling}).")
        out.append(f"::error::That is {measured - budget} bytes more than the "
                   f"recorded size. If a toolchain bump moved it, re-measure with "
                   f"--report and say so in the commit; do not pad.")
        return 1, out

    out.append(f"  {label}={measured} bytes, within budget {budget} "
               f"(+{tolerance} tolerance), {ceiling - measured} bytes of room")
    if measured < budget - tolerance:
        out.append(f"      NOTE: {budget - measured} bytes SMALLER than the "
                   f"budget. Lower {key}['{cpu}'] to {measured} so the ratchet "
                   f"keeps its grip.")
    return 0, out


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", help="linked probe: gates .text and .rodata")
    parser.add_argument("--ram-object", help="stack probe object: gates .bss + .data")
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

    if not args.cpu or bool(args.elf) == bool(args.ram_object):
        print("error: --cpu plus exactly one of --elf / --ram-object", file=sys.stderr)
        return 2

    artifact = args.elf or args.ram_object
    try:
        sizes = section_sizes(artifact, args.size_tool)
    except RuntimeError as error:
        print(f"::error::{error}", file=sys.stderr)
        return 1

    if args.elf:
        text = sum_sections(sizes, ".text")
        rodata = sum_sections(sizes, ".rodata")
        print(f"{args.cpu}: .text={text} .rodata={rodata} bytes  ({artifact})")

        # A linked probe with no .text is not a small probe, it is a broken
        # measurement -- a wrong path, a size tool that printed nothing, a
        # section name that moved. Zero must never look like a pass.
        if text == 0:
            print(f"::error::{artifact} reports .text=0. A ratchet over an empty "
                  f"measurement passes forever.", file=sys.stderr)
            return 1

        code_text, lines_text = compare(
            ".text", text, BUDGETS.get(args.cpu), TOLERANCE_BYTES,
            args.cpu, args.report, "BUDGETS")
        code_rodata, lines_rodata = compare(
            ".rodata", rodata, RODATA_BUDGETS.get(args.cpu), RODATA_TOLERANCE_BYTES,
            args.cpu, args.report, "RODATA_BUDGETS")
        for line in lines_text + lines_rodata:
            print(line)
        if args.report:
            print("  --report: printing only, not enforcing")
        return max(code_text, code_rodata)

    ram = sum_sections(sizes, ".bss", ".data")
    print(f"{args.cpu}: .bss+.data={ram} bytes  ({artifact})")

    # The stack probe declares realistic-capacity containers in static storage.
    # If that sums to zero the probe was not the artifact measured.
    if ram == 0:
        print(f"::error::{artifact} reports no .bss or .data at all. The RAM probe "
              f"declares static containers, so zero means the wrong file was "
              f"measured or the section names moved.", file=sys.stderr)
        return 1

    code, lines = compare(".bss+.data", ram, RAM_BUDGETS.get(args.cpu),
                          RAM_TOLERANCE_BYTES, args.cpu, args.report, "RAM_BUDGETS")
    for line in lines:
        print(line)
    if args.report:
        print("  --report: printing only, not enforcing")
    return code


# A gate that cannot fail is not a gate. These fixtures prove every comparison
# still bites and still lets a legitimate size through.
def self_test():
    failures = []

    cases = (
        ("BUDGETS", ".text", TOLERANCE_BYTES),
        ("RODATA_BUDGETS", ".rodata", RODATA_TOLERANCE_BYTES),
        ("RAM_BUDGETS", ".bss+.data", RAM_TOLERANCE_BYTES),
    )
    for key, label, tolerance in cases:
        budget = 10_000
        code, _ = compare(label, budget + tolerance + 1, budget, tolerance,
                          "fixture", report=False, key=key)
        if code == 0:
            failures.append(f"{label}: an over-budget size was accepted")
        code, _ = compare(label, budget, budget, tolerance,
                          "fixture", report=False, key=key)
        if code != 0:
            failures.append(f"{label}: an at-budget size was rejected")
        code, _ = compare(label, budget + tolerance, budget, tolerance,
                          "fixture", report=False, key=key)
        if code != 0:
            failures.append(f"{label}: a size exactly at the tolerance edge was rejected")

        # No budget recorded fails when enforcing and is fine under --report,
        # which is the whole mechanism of the two-step.
        code, _ = compare(label, 1234, None, tolerance, "fixture",
                          report=False, key=key)
        if code == 0:
            failures.append(f"{label}: a missing budget was accepted while enforcing")
        code, _ = compare(label, 1234, None, tolerance, "fixture",
                          report=True, key=key)
        if code != 0:
            failures.append(f"{label}: --report refused a missing budget")

    # Prefix summing. Under -fdata-sections there is no plain `.bss`, so an
    # exact-name lookup reads zero and the ratchet guards nothing.
    split = {".bss._ZL7storage": 4096, ".bss._ZL5table": 1024, ".data.rel.ro": 8,
             ".text": 100, ".rodata.str1.1": 32}
    if sum_sections(split, ".bss", ".data") != 4096 + 1024 + 8:
        failures.append("sum_sections missed per-symbol .bss/.data sections")
    if sum_sections(split, ".rodata") != 32:
        failures.append("sum_sections missed a split .rodata section")
    if sum_sections(split, ".bss") == 0:
        failures.append("an exact-name .bss lookup would have read zero -- "
                        "the prefix sum is what stops that")

    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("self-test passed: for .text, .rodata and .bss+.data alike, over-budget "
          "and missing-budget are rejected while at-budget and edge are accepted; "
          "sections split by -fdata-sections are summed, not missed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
