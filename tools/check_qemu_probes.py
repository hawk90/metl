#!/usr/bin/env python3
"""Runtime measurements of the Cortex-M build under QEMU: the stack high-water
mark (#404) and guest instruction counts per benchmark region (#338).

WHY. Every other ARM number in this repository is read from a file the compiler
wrote: `.text` and `.bss` from the ELF (check_size.py), the deepest frame from
`.su` (check_stack.py). Nothing measures what the shipped code DOES when it
runs. Two things follow from that, and this tool covers both:

  * Stack. `-fstack-usage` reports single frames, only without LTO, and never
    runs. tests/embedded/qemu_stack_main.cpp links the same operations
    stack_probe.cpp compiles, paints the stack, runs them, and reports how deep
    the stack went -- built twice, without LTO and with -flto, because the LTO
    build is the one the static gate cannot see at all.

  * Instructions. check_instructions.py counts the HOST build with cachegrind,
    which gates the algorithm but not the code a Cortex-M executes. Here QEMU
    runs tests/embedded/qemu_bench_probe.cpp with one guest instruction per
    translation block and `-d exec,nochain`, so each executed instruction is one
    log line with its PC; the instructions between `metl_region_begin` and
    `metl_region_end` are the region's count.

WHY NOT A TCG PLUGIN. The plugin API would count without the log. Ubuntu ships
neither the plugin header nor the contrib plugins (packages.ubuntu.com, suite
resolute = ubuntu-26.04: no package contains qemu-plugin.h or libinsn.so, and
the same for noble and questing), so the CI runner's QEMU cannot load one.
`-d exec` with one instruction per block is in every QEMU since 8.1.

WHAT THESE NUMBERS ARE NOT. Instruction counts are not cycles and not time:
QEMU models no wait states, caches, pipeline or interrupt latency. The stack
mark covers the probe's operations from one call site, not interrupt nesting or
an exception-entry frame.

GRADING. Every way of not measuring is a failure, never a zero: a missing or
unparseable report line, a saturated paint, a program that did not reach the
runner shim's METL_QEMU_EXIT line, a trace with no instructions, a region list
that does not match the regions found in the trace. Before the real stack
probe is graded, a CANARY build (-DMETL_STACK_PROBE_CANARY puts a 1 KiB local
array in the probe's map churn, the defect stack_probe.cpp exists for) must read
at least 1 KiB deeper, or the paint cannot see what the gate is for.

BUDGETS. Empty until CI measures them, as check_size.py's, check_stack.py's and
check_instructions.py's started: a local QEMU and toolchain are a different
measurement. The CI step enforces from its first run, so a missing budget FAILS
and prints the line to paste; there is no green run without a budget.

Usage:
    tools/check_qemu_probes.py --cpu cortex-m3 --machine mps2-an385 \\
        [--record measurements-cortex-m3.json] [--summary] [--report]
    tools/check_qemu_probes.py --self-test
"""

import argparse
import json
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import time

REPO = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import run_qemu_tests  # noqa: E402  (same directory; shares the target flags)

BUILD_DIR = REPO / "build-qemu-probes"
EMBEDDED = REPO / "tests" / "embedded"
STACK_SOURCES = [EMBEDDED / "stack_probe.cpp", EMBEDDED / "qemu_stack_main.cpp"]
BENCH_SOURCE = EMBEDDED / "qemu_bench_probe.cpp"
RUN_TIMEOUT_SECONDS = 30
TRACE_TIMEOUT_SECONDS = 180

# Runtime stack high-water mark in bytes, per CPU, without and with LTO.
# Measured by the `invariants` job and written down in a follow-up commit; a
# budget goes DOWN freely and UP only with the commit saying what grew and why.
STACK_BUDGETS = {}

# Same tolerance as check_stack.py, for the same reason: the regression this
# catches is a local buffer, kilobytes, and 64 bytes is a dozen spilled
# registers.
STACK_TOLERANCE_BYTES = 64

# The canary's 1 KiB array must show up as at least this much more stack.
CANARY_MIN_GROWTH_BYTES = 1024

# Guest instructions per region, per CPU, from the build without LTO.
INSTRUCTION_BUDGETS = {}

# QEMU's count is exact for a given binary; the tolerance is room for a
# toolchain that schedules a little differently, as in check_instructions.py.
INSTRUCTION_TOLERANCE_FRACTION = 0.05

# Regions a core cannot build, and why. Anything else missing is a failure.
ABSENT_REGIONS = {
    "cortex-m0": {"mpmc_queue_push_pop": "mpmc_queue needs a lock-free CAS; ARMv6-M has none"},
}

HWM_LINE = re.compile(r"^METL_STACK_HWM (\S+)\s*$", re.MULTILINE)
REGIONS_LINE = re.compile(r"^METL_BENCH_REGIONS((?: \S+)*)\s*$", re.MULTILINE)
# accel/tcg/cpu-exec.c, log_cpu_exec(): "Trace %d: %p [cs_base/pc/flags/cflags] sym".
TRACE_LINE = re.compile(r"^Trace \d+: \S+ \[[0-9a-fA-F]+/([0-9a-fA-F]+)/")


class ProbeError(Exception):
    """A measurement that was not made. Never graded as a number."""


# --------------------------------------------------------------------------
# Parsing (pure; the self-test covers all of it)


def exit_status(output):
    match = run_qemu_tests.SENTINEL.search(output)
    if match is None:
        raise ProbeError("no METL_QEMU_EXIT line: the program hung, crashed, or never "
                         "reached the end of main")
    return int(match.group(1))


def parse_hwm(output):
    """Bytes from the stack probe's output, or ProbeError."""
    lines = HWM_LINE.findall(output)
    if len(lines) != 1:
        raise ProbeError(f"expected one METL_STACK_HWM line, found {len(lines)}")
    if lines[0] == "saturated":
        raise ProbeError("the stack reached the bottom of the painted region: the "
                         "high-water mark is unknown, only bounded below")
    if not lines[0].isdigit() or int(lines[0]) == 0:
        raise ProbeError(f"unparseable high-water mark {lines[0]!r}")
    status = exit_status(output)
    if status != 0:
        raise ProbeError(f"the stack probe exited {status}")
    return int(lines[0])


def parse_region_names(output):
    match = REGIONS_LINE.findall(output)
    if len(match) != 1:
        raise ProbeError(f"expected one METL_BENCH_REGIONS line, found {len(match)}")
    names = match[0].split()
    if not names:
        raise ProbeError("the benchmark probe listed no regions")
    if len(set(names)) != len(names):
        raise ProbeError(f"duplicate region names: {names}")
    return names


def count_regions(trace_lines, begin, end):
    """Instruction counts between each entry of `begin` and the next of `end`.

    Addresses are compared without the Thumb bit, which a symbol carries and an
    executed PC does not."""
    begin &= ~1
    end &= ~1
    counts, inside, parsed = [], None, 0
    for line in trace_lines:
        match = TRACE_LINE.match(line)
        if match is None:
            continue
        parsed += 1
        pc = int(match.group(1), 16) & ~1
        if pc == begin:
            if inside is not None:
                raise ProbeError("metl_region_begin entered twice without an end")
            inside = 0
        elif pc == end:
            if inside is None:
                raise ProbeError("metl_region_end entered outside a region")
            counts.append(inside)
            inside = None
        elif inside is not None:
            inside += 1
    if parsed == 0:
        raise ProbeError("the trace holds no 'Trace' lines: QEMU did not log execution "
                         "(is -d exec,nochain supported, and did the log reach the file?)")
    if inside is not None:
        raise ProbeError("the trace ends inside a region: the log was cut short")
    return counts


def match_regions(names, counts, cpu):
    """{name: count}, checking the probe's list against the trace and the
    regions this core is expected to lack."""
    if len(names) != len(counts):
        raise ProbeError(f"the probe listed {len(names)} regions but the trace holds "
                         f"{len(counts)}")
    absent = ABSENT_REGIONS.get(cpu, {})
    present_but_absent = sorted(set(names) & set(absent))
    if present_but_absent:
        raise ProbeError(f"{present_but_absent} built on {cpu}, which ABSENT_REGIONS "
                         f"says cannot: the capability gate or the list is wrong")
    return dict(zip(names, counts))


# --------------------------------------------------------------------------
# Grading (pure)


def grade_stack(cpu, marks, canary, report):
    """(exit_code, lines). `marks` is {"no-lto": bytes, "lto": bytes}."""
    out, failures = [], []
    growth = canary - marks["no-lto"]
    out.append(f"{cpu}: runtime stack high-water mark {marks['no-lto']} bytes, "
               f"{marks['lto']} with LTO (canary +{growth})")
    if growth < CANARY_MIN_GROWTH_BYTES:
        # Not excused by --report: a paint that cannot see a 1 KiB local array
        # measures nothing worth printing either.
        return 1, out + [f"::error::the canary's 1 KiB local array raised the mark by only "
                         f"{growth} bytes; the paint cannot see the defect this gate is for"]
    budgets = STACK_BUDGETS.get(cpu)
    for build, used in sorted(marks.items()):
        budget = None if budgets is None else budgets.get(build)
        if budget is None:
            failures.append(f"::error::no runtime stack budget for {cpu} {build}. Set "
                            f"STACK_BUDGETS['{cpu}']['{build}'] = {used} in "
                            f"tools/check_qemu_probes.py from THIS number, measured by this job.")
            continue
        ceiling = budget + STACK_TOLERANCE_BYTES
        if used > ceiling:
            failures.append(f"::error::{cpu} {build}: the stack reached {used} bytes, over the "
                            f"{budget}-byte budget (+{STACK_TOLERANCE_BYTES} = {ceiling}). "
                            f"Find the operation that started using a local buffer; do not pad.")
        else:
            out.append(f"  {build}: within budget {budget} (+{STACK_TOLERANCE_BYTES}), "
                       f"{ceiling - used} bytes of room")
    if report:
        return 0, out + ["  --report: printing only, not enforcing budgets"]
    return (1 if failures else 0), out + failures


def grade_instructions(cpu, counts, report):
    out, failures = [], []
    budgets = INSTRUCTION_BUDGETS.get(cpu, {})
    for name, count in sorted(counts.items()):
        budget = budgets.get(name)
        if budget is None:
            out.append(f"  {count:10,}  {name}   (no budget recorded)")
            failures.append(f"::error::no instruction budget for {cpu} {name}. Set "
                            f"INSTRUCTION_BUDGETS['{cpu}']['{name}'] = {count} in "
                            f"tools/check_qemu_probes.py from THIS number, measured by this job.")
            continue
        ceiling = int(budget * (1.0 + INSTRUCTION_TOLERANCE_FRACTION))
        if count > ceiling:
            failures.append(f"::error::{cpu} {name} executed {count:,} instructions, over the "
                            f"{budget:,} budget (+{int(INSTRUCTION_TOLERANCE_FRACTION * 100)}% = "
                            f"{ceiling:,}). Find out what got more expensive before touching "
                            f"the number.")
            continue
        out.append(f"  {count:10,}  {name}   (budget {budget:,}, {ceiling - count:,} of room)")
        if count < int(budget * (1.0 - INSTRUCTION_TOLERANCE_FRACTION)):
            out.append(f"      NOTE: lower INSTRUCTION_BUDGETS['{cpu}']['{name}'] to {count} "
                       f"so the ratchet keeps its grip.")
    stale = sorted(set(budgets) - set(counts))
    for name in stale:
        failures.append(f"::error::INSTRUCTION_BUDGETS['{cpu}'] names {name}, which the probe "
                        f"no longer runs: a budget nothing measures is a gate that cannot fail")
    if report:
        return 0, out + ["  --report: printing only, not enforcing budgets"]
    return (1 if failures else 0), out + failures


def update_record(path, cpu, marks, counts, versions, machine):
    """Fill the #405 record's runtime keys. The record must already exist: the
    size steps create it, and a record holding only these figures would claim
    a measurement of the rest that was never made."""
    record_path = pathlib.Path(path)
    if not record_path.exists():
        raise ProbeError(f"{path} does not exist; run the size steps first")
    record = json.loads(record_path.read_text())
    if record.get("cpu") != cpu:
        raise ProbeError(f"{path} records {record.get('cpu')}, not {cpu}")
    record["stack"]["runtime_hwm"] = marks["no-lto"]
    record["stack"]["runtime_hwm_lto"] = marks["lto"]
    record["qemu"] = {"machine": machine, **versions}
    record["instructions"] = {"source": "qemu exec trace, no LTO", "regions": counts}
    record_path.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n")


def summary(cpu, marks, counts):
    lines = [f"### QEMU runtime measurements: {cpu}", "",
             "| stack | bytes |", "|---|---:|",
             f"| runtime high-water mark | {marks['no-lto']} |",
             f"| runtime high-water mark, LTO | {marks['lto']} |", "",
             "| region (guest instructions, no LTO) | count |", "|---|---:|"]
    lines += [f"| `{name}` | {count} |" for name, count in sorted(counts.items())]
    return "\n".join(lines) + "\n"


# --------------------------------------------------------------------------
# Building and running (needs the toolchain and QEMU)


def run_tool(command, what):
    result = subprocess.run(command, capture_output=True, text=True, cwd=REPO)
    if result.returncode != 0:
        raise ProbeError(f"{what} failed:\n{(result.stdout + result.stderr).strip()[-2000:]}")
    return result.stdout


def build(cpu, sources, output, extra):
    run_tool(["arm-none-eabi-g++", *run_qemu_tests.cflags(cpu), *extra,
              *[str(s) for s in sources], str(run_qemu_tests.SHIM), "-o", str(output)],
             f"building {output.name}")
    return output


def run_qemu(elf, machine, timeout, extra=()):
    """Run until the shim's exit line, then stop QEMU with SIGTERM so it closes
    its log; SIGKILL would drop the buffered tail of a trace."""
    log = elf.with_suffix(".out")
    with open(log, "w") as out:
        qemu = subprocess.Popen(
            ["qemu-system-arm", "-semihosting-config", "enable=on", "-monitor", "none",
             "-serial", "none", "-nographic", "-machine", machine, *extra,
             "-kernel", str(elf)],
            stdout=out, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if run_qemu_tests.SENTINEL.search(log.read_text(errors="replace")) or \
                    qemu.poll() is not None:
                break
            time.sleep(0.1)
        qemu.terminate()
        try:
            qemu.wait(timeout=10)
        except subprocess.TimeoutExpired:
            qemu.kill()
            qemu.wait()
    return log.read_text(errors="replace")


def symbol(elf, name):
    for line in run_tool(["arm-none-eabi-nm", str(elf)], f"nm {elf.name}").splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == name:
            return int(parts[0], 16)
    raise ProbeError(f"{elf.name} has no symbol {name}")


def versions():
    return {"qemu": run_tool(["qemu-system-arm", "--version"], "qemu --version").splitlines()[0],
            "toolchain": run_tool(["arm-none-eabi-g++", "-dumpfullversion"],
                                  "g++ -dumpfullversion").strip()}


def measure(cpu, machine):
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    marks = {}
    for label, extra in (("no-lto", []), ("lto", ["-flto"])):
        elf = build(cpu, STACK_SOURCES, BUILD_DIR / f"stack-{label}.elf", extra)
        output = run_qemu(elf, machine, RUN_TIMEOUT_SECONDS)
        try:
            marks[label] = parse_hwm(output)
        except ProbeError as error:
            raise ProbeError(f"stack probe ({label}): {error}\n{output[-1500:]}") from None
    canary_elf = build(cpu, STACK_SOURCES, BUILD_DIR / "stack-canary.elf",
                       ["-DMETL_STACK_PROBE_CANARY"])
    output = run_qemu(canary_elf, machine, RUN_TIMEOUT_SECONDS)
    try:
        canary = parse_hwm(output)
    except ProbeError as error:
        raise ProbeError(f"stack probe (canary): {error}\n{output[-1500:]}") from None

    bench = build(cpu, [BENCH_SOURCE], BUILD_DIR / "bench.elf", [])
    trace = BUILD_DIR / "bench.trace"
    output = run_qemu(bench, machine, TRACE_TIMEOUT_SECONDS,
                      ["-accel", "tcg,one-insn-per-tb=on", "-d", "exec,nochain",
                       "-D", str(trace)])
    status = exit_status(output)
    if status != 0:
        raise ProbeError(f"the benchmark probe exited {status}\n{output[-1500:]}")
    names = parse_region_names(output)
    with open(trace, errors="replace") as lines:
        counts = count_regions(lines, symbol(bench, "metl_region_begin"),
                               symbol(bench, "metl_region_end"))
    trace.unlink()  # one log line per executed instruction; only the counts are kept
    return marks, canary, match_regions(names, counts, cpu)


# --------------------------------------------------------------------------


def self_test():
    """A gate that cannot fail is not a gate. Everything that grades, on
    synthetic output, with no toolchain and no QEMU."""
    failures = []

    def expect_error(label, call):
        try:
            call()
            failures.append(f"{label} was accepted")
        except ProbeError:
            pass

    good = "METL_QEMU_BEGIN\nMETL_STACK_HWM 312\nMETL_QEMU_EXIT 0\n"
    if parse_hwm(good) != 312:
        failures.append("a clean report did not parse to 312")
    expect_error("a saturated paint", lambda: parse_hwm(good.replace("312", "saturated")))
    expect_error("a missing METL_STACK_HWM line", lambda: parse_hwm("METL_QEMU_EXIT 0\n"))
    expect_error("two METL_STACK_HWM lines",
                 lambda: parse_hwm("METL_STACK_HWM 1\n" + good))
    expect_error("a zero mark", lambda: parse_hwm(good.replace("312", "0")))
    expect_error("garbage for a mark", lambda: parse_hwm(good.replace("312", "31x")))
    expect_error("a report without the exit line",
                 lambda: parse_hwm("METL_STACK_HWM 312\n"))
    expect_error("a non-zero exit", lambda: parse_hwm(good.replace("EXIT 0", "EXIT 1")))

    def trace(pcs):
        return [f"Trace 0: 0x7f00 [00000000/{pc:016x}/01000000/ff200000] sym" for pc in pcs]

    begin, end = 0x101, 0x201  # symbols carry the Thumb bit; PCs do not
    lines = trace([0x80, 0x100, 0x102, 0x300, 0x302, 0x200, 0x90,
                   0x100, 0x400, 0x200, 0x94])
    if count_regions(lines + ["noise that is not a trace line"], begin, end) != [3, 1]:
        failures.append(f"region counts {count_regions(lines, begin, end)}, expected [3, 1]")
    expect_error("a trace with no Trace lines", lambda: count_regions(["junk"], begin, end))
    expect_error("a region never closed", lambda: count_regions(trace([0x100, 0x102]), begin, end))
    expect_error("a nested begin", lambda: count_regions(trace([0x100, 0x100, 0x200]), begin, end))
    expect_error("an end outside a region", lambda: count_regions(trace([0x200]), begin, end))

    names = parse_region_names("METL_QEMU_BEGIN\nMETL_BENCH_REGIONS a b\nMETL_QEMU_EXIT 0\n")
    if names != ["a", "b"]:
        failures.append(f"region names {names}")
    expect_error("an empty region list", lambda: parse_region_names("METL_BENCH_REGIONS\n"))
    expect_error("duplicate region names", lambda: parse_region_names("METL_BENCH_REGIONS a a\n"))
    expect_error("no region list", lambda: parse_region_names("METL_QEMU_EXIT 0\n"))
    expect_error("more names than regions", lambda: match_regions(["a", "b"], [5], "cortex-m3"))
    expect_error("a region a core cannot build",
                 lambda: match_regions(["mpmc_queue_push_pop"], [5], "cortex-m0"))

    saved = (dict(STACK_BUDGETS), dict(INSTRUCTION_BUDGETS))
    try:
        STACK_BUDGETS.clear()
        INSTRUCTION_BUDGETS.clear()
        marks = {"no-lto": 300, "lto": 260}
        if grade_stack("m", marks, 1400, report=False)[0] == 0:
            failures.append("a stack with no budget passed")
        if grade_stack("m", marks, 1400, report=True)[0] != 0:
            failures.append("--report refused a stack with no budget")
        if grade_stack("m", marks, 900, report=True)[0] == 0:
            failures.append("a canary that grew by 600 bytes passed, even under --report")
        STACK_BUDGETS["m"] = {"no-lto": 300 - STACK_TOLERANCE_BYTES, "lto": 260}
        if grade_stack("m", marks, 1400, report=False)[0] != 0:
            failures.append("a stack exactly at budget + tolerance failed")
        STACK_BUDGETS["m"]["no-lto"] -= 1
        if grade_stack("m", marks, 1400, report=False)[0] == 0:
            failures.append("a stack one byte over budget + tolerance passed")

        counts = {"r": 1000}
        if grade_instructions("m", counts, report=False)[0] == 0:
            failures.append("a region with no instruction budget passed")
        INSTRUCTION_BUDGETS["m"] = {"r": 1000}
        if grade_instructions("m", {"r": 1050}, report=False)[0] != 0:
            failures.append("a count at budget + 5% failed")
        if grade_instructions("m", {"r": 1051}, report=False)[0] == 0:
            failures.append("a count over budget + 5% passed")
        INSTRUCTION_BUDGETS["m"]["gone"] = 10
        if grade_instructions("m", counts, report=False)[0] == 0:
            failures.append("a budget for a region nothing measures passed")
    finally:
        STACK_BUDGETS.clear()
        STACK_BUDGETS.update(saved[0])
        INSTRUCTION_BUDGETS.clear()
        INSTRUCTION_BUDGETS.update(saved[1])

    with tempfile.TemporaryDirectory() as tmp:
        path = pathlib.Path(tmp) / "m.json"
        expect_error("a record that does not exist",
                     lambda: update_record(path, "m", {"no-lto": 1, "lto": 2}, {}, {}, "x"))
        path.write_text(json.dumps({"cpu": "m", "stack": {"static_max_frame": None,
                                                          "runtime_hwm": None,
                                                          "runtime_hwm_lto": None}}))
        update_record(path, "m", {"no-lto": 300, "lto": 260}, {"r": 7},
                      {"qemu": "QEMU 10", "toolchain": "15"}, "mps2-an385")
        record = json.loads(path.read_text())
        got = (record["stack"]["runtime_hwm"], record["stack"]["runtime_hwm_lto"],
               record["instructions"]["regions"], record["qemu"]["machine"])
        if got != (300, 260, {"r": 7}, "mps2-an385"):
            failures.append(f"record fields {got}")
        expect_error("a record for another cpu",
                     lambda: update_record(path, "other", {"no-lto": 1, "lto": 2}, {}, {}, "x"))

    if failures:
        for failure in failures:
            print(f"SELF-TEST FAILED: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: missing, saturated and unparseable stack reports, cut or empty "
          "traces, mismatched region lists, missing and exceeded budgets and a blind canary "
          "are all rejected; at-budget figures pass and the record is filled")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cpu")
    parser.add_argument("--machine")
    parser.add_argument("--record", help="the #405 measurement record to fill")
    parser.add_argument("--summary", action="store_true",
                        help="print a Markdown table for the job summary")
    parser.add_argument("--report", action="store_true", help="print, do not enforce budgets")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)

    if args.self_test:
        return self_test()
    if not args.cpu or not args.machine:
        parser.error("--cpu and --machine are required")
    missing = [t for t in ("arm-none-eabi-g++", "arm-none-eabi-nm", "qemu-system-arm")
               if shutil.which(t) is None]
    if missing:
        print(f"::error::{', '.join(missing)} not found: nothing was measured", file=sys.stderr)
        return 3

    try:
        marks, canary, counts = measure(args.cpu, args.machine)
        found = versions()
        if args.record:
            update_record(args.record, args.cpu, marks, counts, found, args.machine)
    except ProbeError as error:
        print(f"::error::{error}", file=sys.stderr)
        return 1

    print(f"{found['qemu']}; arm-none-eabi-g++ {found['toolchain']}; machine {args.machine}")
    stack_code, stack_lines = grade_stack(args.cpu, marks, canary, args.report)
    insn_code, insn_lines = grade_instructions(args.cpu, counts, args.report)
    print("\n".join(stack_lines))
    print(f"{args.cpu}: guest instructions per region (no LTO)")
    print("\n".join(insn_lines))
    if args.summary:
        summary_path = pathlib.Path(os.environ.get("GITHUB_STEP_SUMMARY", ""))
        text = summary(args.cpu, marks, counts)
        if summary_path.name:
            with open(summary_path, "a") as out:
                out.write(text)
        else:
            print(text)
    return 1 if stack_code or insn_code else 0


if __name__ == "__main__":
    sys.exit(main())
