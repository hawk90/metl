#!/usr/bin/env python3
"""Stack-depth ratchet for METL's own transient use.

The other half of the RAM question. `tools/check_size.py` ratchets `.text` --
flash. `tests/core/ram_footprint_test.cpp` pins `sizeof` -- the storage a caller
declares. Neither can see the memory METL uses *while an operation runs*, in the
frame of whatever function is executing.

WHAT THIS GUARDS. `static_unordered_map` reclaims tombstones through
`rehash_in_place` when a new key is inserted, and "in place" is the whole claim: it rebuilds the table
without a second copy. The simpler implementation -- build into a local array,
copy back -- is correct, passes every test here and every fuzz harness, and puts
`bucket_count * sizeof(value_type)` on the stack. At the capacity in the probe
that is EIGHT KILOBYTES, reachable through an insert, on parts that have 8 KB of
SRAM in total. Nothing in this repository could currently see that happen.

Measured before this was written, so the claim is not theoretical: today the
largest frame in the probe is small and `rehash_in_place` itself needs a few
tens of bytes. This gate has nothing to catch right now. That is the normal
state of a ratchet -- `check_size.py` found no bug either -- and is different
from a gate that *cannot* fail, which is what the canary below exists to
disprove.

TWO CHECKS, and the second is the one that cannot be argued with:

  S1  the largest frame in the probe stays within budget.
  S2  NO frame is `dynamic` or `bounded`. GCC's third column says how the size
      was determined: `static` is a fixed frame, `dynamic` means the function
      allocates at runtime (a VLA, or `alloca`). A dynamic frame is an
      UNBOUNDED stack allocation, which docs/SCOPE.md invariant I3 forbids
      outright -- "no unbounded loops on data, no unbounded retry". There is no
      budget to compare against and no size at which it becomes acceptable, so
      this fails regardless of the number.

Usage:
    tools/check_stack.py --su-dir . --cpu cortex-m3
    tools/check_stack.py --su-dir . --cpu cortex-m3 --report
    tools/check_stack.py --self-test
"""

import argparse
import pathlib
import sys

# Budgets in bytes for the LARGEST single frame in the stack probe, per CPU,
# measured by the `invariants` CI job itself.
#
# These started empty on purpose, exactly as check_size.py's did. There is no ARM
# toolchain on the development machine, and local figures have disagreed with CI
# repeatedly -- a clang-tidy count with 3.7x slack, a delta with the wrong sign,
# a header that read clean locally and reported two findings on CI. The job
# prints the measured maxima with --report, and the numbers were set from THAT
# output in a follow-up commit.
#
# A budget goes DOWN with the change that shrinks the frame: a frame under it by
# more than the tolerance fails. It goes UP only when the probe was deliberately
# given more to do, and the commit that raises it has to say what and why --
# never to make a red job green.
#
# Measured by the `invariants` job on PR #79, run 32583708742. All four targets
# agreed on 136 bytes over 39 frames, which is itself worth reading: the deepest
# frame is the probe's OWN `churn_text`, which declares a 64-byte output buffer
# because that is how `try_format_uint` works -- the caller supplies the span.
# METL's own deepest frame is `static_unordered_map::rehash_in_place` at 72
# bytes, and its set counterpart at 40. The reclaim really does rebuild in
# place; rewriting it to build into a local array takes the same measurement to
# 8784.
#
# Lowered to the nightly on e5e2d972b (run 40227568570): 64 bytes on cortex-m0
# and 72 on the others, both in the probe's churn functions. The deepest frame
# fell from 136 with the GCC 13 -> 15 move in #131 and the budget stayed, which
# left room for a frame to double unseen. A frame under the budget by more than
# the tolerance now fails, so the change that shrinks it records the budget.
BUDGETS = {
    "cortex-m0": 64,
    "cortex-m3": 72,
    "cortex-m4": 72,
    "cortex-m7": 72,
}

# Room for a toolchain that spills a little differently. Small on purpose: the
# regression this exists to catch is measured in kilobytes, so a generous
# tolerance costs nothing in detection and a tight one risks false failures on a
# compiler bump. 64 bytes is roughly a dozen spilled registers.
TOLERANCE_BYTES = 64

# Third column of a .su line. `static` is the only acceptable answer.
ACCEPTABLE_QUALIFIERS = {"static"}


def parse_su(path):
    """Yield (function, bytes, qualifier) from one GCC .su file.

    Format is tab-separated: `file:line:column:function<TAB>size<TAB>qualifier`.
    The function field itself contains colons but never a tab. A line in any
    other shape raises ValueError naming it: skipping it would drop a frame,
    and the dropped one could be the deepest.
    """
    entries = []
    lines = pathlib.Path(path).read_text(encoding="utf-8").splitlines()
    for number, line in enumerate(lines, start=1):
        if not line.strip():
            continue
        parts = line.split("\t")
        size = parts[1].strip() if len(parts) == 3 else ""
        if not size.isdigit() or not parts[0].strip() or not parts[2].strip():
            raise ValueError(f"{path}:{number}: not `location<TAB>bytes<TAB>qualifier`: "
                             f"{line!r}")
        entries.append((parts[0].strip(), int(size), parts[2].strip()))
    return entries


def collect(su_dir):
    """Every frame from every .su file under `su_dir`."""
    entries = []
    for path in sorted(pathlib.Path(su_dir).rglob("*.su")):
        entries.extend(parse_su(path))
    return entries


def check(entries, cpu, report):
    """Return (exit_code, lines_to_print)."""
    out = []

    if not entries:
        out.append("::error::no .su files found -- did the compile step drop "
                   "-fstack-usage? A stack check that measured nothing would "
                   "otherwise pass silently.")
        return 1, out

    # Deepest first, so a failure names the function that earned it.
    ranked = sorted(entries, key=lambda e: e[1], reverse=True)
    deepest_name, deepest, _ = ranked[0]

    out.append(f"{cpu}: {len(entries)} frame(s), deepest {deepest} bytes")
    for name, size, qualifier in ranked[:5]:
        out.append(f"    {size:6}  {qualifier:8}  {name}")

    # S2 first: an unbounded frame is a failure with no budget to weigh it
    # against, so there is nothing to be gained by reporting the size problem
    # of a function whose size is not even a fixed number.
    dynamic = [e for e in entries if e[2] not in ACCEPTABLE_QUALIFIERS]
    if dynamic:
        for name, size, qualifier in dynamic:
            out.append(f"::error::{name} has a '{qualifier}' frame ({size} bytes "
                       f"reported). That is a runtime-sized stack allocation -- a "
                       f"VLA or alloca -- and docs/SCOPE.md I3 forbids unbounded "
                       f"allocation outright. There is no size at which this is OK.")
        return 1, out

    if report:
        out.append("  --report: printing only, not enforcing")
        return 0, out

    budget = BUDGETS.get(cpu)
    if budget is None:
        out.append(f"::error::no stack budget recorded for {cpu}. Set "
                   f"BUDGETS['{cpu}'] = {deepest} in tools/check_stack.py from "
                   f"THIS number, measured by this job, not a local one.")
        return 1, out

    ceiling = budget + TOLERANCE_BYTES
    if deepest > ceiling:
        out.append(f"::error::{cpu} deepest frame grew to {deepest} bytes in "
                   f"{deepest_name}, over the {budget}-byte budget "
                   f"(+{TOLERANCE_BYTES} tolerance = {ceiling}).")
        out.append("::error::A jump of this kind usually means an operation "
                   "started building into a local buffer. Check what that "
                   "function does before raising anything; do not pad.")
        return 1, out

    floor = budget - TOLERANCE_BYTES
    if deepest < floor:
        out.append(f"::error::{cpu} deepest frame shrank to {deepest} bytes in "
                   f"{deepest_name}, under the {budget}-byte budget "
                   f"(-{TOLERANCE_BYTES} tolerance = {floor}). Lower "
                   f"BUDGETS['{cpu}'] to {deepest} in this change, or the gap is "
                   f"room for a frame to grow unseen.")
        return 1, out

    out.append(f"  within budget {budget} (+{TOLERANCE_BYTES} tolerance), "
               f"{ceiling - deepest} bytes of room")
    return 0, out


def self_test():
    """A gate that cannot fail is not a gate. Prove both checks bite."""
    import tempfile

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)

        # S1: over budget must be rejected, at budget accepted.
        (root / "over.su").write_text("probe.cpp:1:5:big\t9000\tstatic\n")
        code, _ = check(collect(root), "self-test-cpu", report=False)
        if code == 0:
            failures.append("S1 accepted a 9000-byte frame with no budget set")

        BUDGETS["self-test-cpu"] = 100
        try:
            code, _ = check(collect(root), "self-test-cpu", report=False)
            if code == 0:
                failures.append("S1 accepted 9000 bytes against a 100-byte budget")

            # The deepest frame is the one compared, wherever it sits. A probe
            # has dozens of frames over several files; every fixture above has
            # one, so picking the first or the shallowest frame would pass them.
            (root / "over.su").unlink()
            (root / "a.su").write_text("probe.cpp:1:5:small\t8\tstatic\n"
                                       "probe.cpp:2:5:medium\t40\tstatic\n")
            (root / "b.su").write_text("probe.cpp:3:5:tiny\t4\tstatic\n"
                                       "probe.cpp:4:5:deep\t9000\tstatic\n"
                                       "probe.cpp:5:5:small2\t16\tstatic\n")
            code, out = check(collect(root), "self-test-cpu", report=False)
            if code == 0 or not any("9000 bytes in probe.cpp:4:5:deep" in line for line in out):
                failures.append("S1 did not judge the deepest of several frames in "
                                "several files")
            (root / "a.su").unlink()
            (root / "b.su").unlink()

            (root / "ok.su").write_text("probe.cpp:1:5:small\t100\tstatic\n")
            code, _ = check(collect(root), "self-test-cpu", report=False)
            if code != 0:
                failures.append("S1 rejected a frame exactly at its budget")

            # The tolerance must be a tolerance, not a second budget.
            (root / "ok.su").write_text(
                f"probe.cpp:1:5:edge\t{100 + TOLERANCE_BYTES}\tstatic\n")
            code, _ = check(collect(root), "self-test-cpu", report=False)
            if code != 0:
                failures.append("S1 rejected a frame at exactly budget+tolerance")

            # Under budget-tolerance fails too, naming the number to record;
            # exactly at it passes.
            BUDGETS["self-test-cpu"] = 100 + TOLERANCE_BYTES
            (root / "ok.su").write_text("probe.cpp:1:5:edge\t100\tstatic\n")
            code, _ = check(collect(root), "self-test-cpu", report=False)
            if code != 0:
                failures.append("S1 rejected a frame at exactly budget-tolerance")
            (root / "ok.su").write_text("probe.cpp:1:5:edge\t99\tstatic\n")
            code, out = check(collect(root), "self-test-cpu", report=False)
            if code == 0:
                failures.append("S1 accepted a frame one byte under budget-tolerance")
            if not any("to 99 " in line for line in out):
                failures.append("S1's under-budget failure did not name the number to record")
            code, _ = check(collect(root), "self-test-cpu", report=True)
            if code != 0:
                failures.append("S1 refused an under-budget frame under --report")
            BUDGETS["self-test-cpu"] = 100

            # S2: a dynamic frame fails even when it is tiny and even when it is
            # far inside the budget. This is the check that has no escape hatch.
            (root / "ok.su").write_text("probe.cpp:1:5:vla\t16\tdynamic\n")
            code, out = check(collect(root), "self-test-cpu", report=False)
            if code == 0:
                failures.append("S2 accepted a 'dynamic' (VLA/alloca) frame")
            if not any("I3" in line for line in out):
                failures.append("S2 fired without naming the invariant it enforces")

            # ...and --report does not excuse it, or the two-step measuring
            # commit would quietly ship an unbounded allocation.
            code, _ = check(collect(root), "self-test-cpu", report=True)
            if code == 0:
                failures.append("S2 was skipped under --report")
        finally:
            del BUDGETS["self-test-cpu"]

        # A line that is not `location<TAB>size<TAB>qualifier` is an error,
        # not a frame to leave out: the frame it describes could be the deepest.
        with tempfile.TemporaryDirectory() as odd:
            for label, line in (("a size that is not a number", "probe.cpp:2:5:huge\t9k\tstatic"),
                                ("a missing qualifier", "probe.cpp:2:5:huge\t9000"),
                                ("an extra field", "probe.cpp:2:5:huge\t9000\tstatic\tx")):
                (pathlib.Path(odd) / "odd.su").write_text(
                    f"probe.cpp:1:5:small\t8\tstatic\n{line}\n")
                try:
                    collect(odd)
                    failures.append(f"a .su line with {label} was skipped, not reported")
                except ValueError as error:
                    if "odd.su:2" not in str(error):
                        failures.append(f"a .su line with {label} was reported without "
                                        f"its file and line: {error}")

        # An empty measurement must fail, not pass. This is the failure mode
        # that makes a gate decorative: the flag gets dropped, no .su files are
        # written, and a check over nothing reports success.
        with tempfile.TemporaryDirectory() as empty:
            code, _ = check(collect(empty), "self-test-cpu", report=False)
            if code == 0:
                failures.append("a run that found NO .su files reported success")

    if failures:
        for failure in failures:
            print(f"SELF-TEST FAILED: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: over-budget, under-budget and dynamic frames are rejected, "
          "at-budget and edge frames are accepted, and an empty measurement fails")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--su-dir", help="directory to search for *.su files")
    parser.add_argument("--cpu")
    parser.add_argument("--report", action="store_true",
                        help="print the measured maxima and exit 0 with no budget set")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    if not args.su_dir or not args.cpu:
        print("error: --su-dir and --cpu are required", file=sys.stderr)
        return 2

    try:
        entries = collect(args.su_dir)
    except (OSError, UnicodeDecodeError, ValueError) as error:
        print(f"::error::{error}")
        return 1
    code, lines = check(entries, args.cpu, args.report)
    for line in lines:
        print(line)
    return code


if __name__ == "__main__":
    sys.exit(main())
