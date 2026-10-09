#!/usr/bin/env python3
"""Cross-compile METL's host test suite for Cortex-M and run each test under
qemu-system-arm.

Why this exists: METL's differentiator is that it is provably usable on bare
metal, but until now the only thing executed on an MCU was one smoke program
covering seven headers. Everything else was compile-and-link only. This runs
the real tests on the real (emulated) target.

DENY-LIST, NOT ALLOW-LIST. A curated list of "embedded-appropriate checks"
would silently miss every type added afterwards -- the same failure mode
docs/SCOPE.md section 8 records for the invariant probe. Here every test runs
unless it is explicitly excluded below with a reason, so a new test gets target
execution by default and skipping one requires saying why.

Grading is on the METL_QEMU_EXIT line printed by the --wrap=main shim, not on
QEMU's exit status: semihosting SYS_EXIT does not bring this machine down, so
the emulator lingers after the program has finished and printed its result.
(The first run of this job proved it by "timing out" on every test, including
trivial ones, purely because the grading checked the timeout before the
sentinel.) The sentinel therefore decides, and QEMU is killed as soon as it
appears.

That is still strict, which is the part that matters: a test that genuinely
hangs never prints the line, and a crash never reaches it. No sentinel is a
failure -- a conformance gate that cannot tell a hang from a pass is not a
gate.

Before the suite, tests/embedded/qemu_canary.cpp -- one failing CHECK -- must
read as METL_QEMU_EXIT 1, or nothing runs: a shim or grading bug that turned
every exit into 0 would otherwise pass the whole suite. And a test named in
--expect-build-fail must fail on a capability static_assert (its text names
ARMv6-M), not on just any error.

--plan prints what this script WOULD run and exits, without a toolchain, a
QEMU or a build. It exists so the count in README.md can be checked instead of
retyped: that figure is derived from a glob, it went stale by five without
anything noticing, and tools/check_docs.py D7 now reads it from here. The
discovery and deny-list logic is the same code path in both modes, so --plan
cannot drift from what a real run does.

Usage:
    tools/run_qemu_tests.py [--cpu cortex-m3] [--machine mps2-an385]
                            [--expect-build-fail a.cpp,b.cpp] [--plan]
"""

import argparse
import re
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO / "build-qemu-conformance"
SHIM = REPO / "tests" / "embedded" / "qemu_runner_shim.cpp"
CANARY = REPO / "tests" / "embedded" / "qemu_canary.cpp"
# Every capability static_assert names the core it is about. A test expected not
# to build must fail with this text -- any other error (a typo, a missing
# include) is a broken test, not the gate firing.
CAPABILITY_GATE_TEXT = "ARMv6-M (Cortex-M0/M0+)"
LINKER_SCRIPT = REPO / "tests" / "embedded" / "mps2-an385.ld"
TIMEOUT_SECONDS = 20
SENTINEL = re.compile(r"^METL_QEMU_EXIT (\d+)$", re.MULTILINE)
# metl_test::skip_code in tests/metl_check.hpp.
SKIP_CODE = 77

# Deny-list: tests that cannot run freestanding, each with the reason.
DENIED = {
    "sync/spsc_queue_threaded_test.cpp": "needs <thread>; no OS scheduler on bare metal",
    "sync/atomic_handle_threaded_test.cpp": "needs <thread>",
    "sync/mpmc_queue_threaded_test.cpp": "needs <thread>",
    "sync/spsc_byte_ring_threaded_test.cpp": "needs <thread>",
    "sync/size_hint_observer_threaded_test.cpp": "needs <thread>",
    "containers/fixed_vector_asan_test.cpp": "forked death test; also assumes ASan",
    "core/assert_test.cpp": "setjmp/longjmp around an abort path; host-runtime specific",
    "memory/arena_throwing_ctor_test.cpp": "throws; needs -fexceptions",
    "vocab/expected_regression_test.cpp": "throws; needs -fexceptions",
    "vocab/variant_regression_test.cpp": "throws; needs -fexceptions",
    "containers/throwing_element_test.cpp": "throws; needs -fexceptions",
    "sync/atomic_ref_test.cpp": "atomic_ref<8-byte> needs libatomic on ARMv7-M; see atomic_ref.hpp",
}

# Tests allowed to report a skip (metl_test::skip_code), and the cores where
# they may. A skip anywhere else is a FAILURE: the interrupt tests are the only
# evidence irq_lock masks anything, and a core that should run them but skips
# them would otherwise turn that evidence into a quiet no-op.
EXPECTED_SELF_SKIP = {
    "sync/irq_masking_test.cpp": {"cortex-m0"},        # ARMv6-M has no VTOR
    "sync/tick_extender_irq_test.cpp": {"cortex-m0"},  # same
}

# The `throws; needs -fexceptions` entries above deliberately `throw` to check
# what METL does when a USER's type throws. The freestanding build is
# -fno-exceptions (METL is a no-exception library), and enabling exceptions here
# would need unwind tables this target's linker script does not provide.
# Verifying exception-safety on a target that cannot do exceptions is not a
# meaningful check; the host build covers it.
#
# atomic_ref_test is a FINDING, not a workaround: metl::atomic_ref<T> for an
# 8-byte T lowers to __atomic_load_8/__atomic_store_8 on ARMv7-M, and bare-metal
# toolchains ship no libatomic, so it does not link. atomic_ref.hpp documents
# this. The test stays deny-listed until the header either constrains the size on
# such targets or the runner links a libatomic.


def cflags(cpu):
    return [
        "--specs=picolibc.specs", "--oslib=semihost",
        "-std=c++17", f"-mcpu={cpu}", "-mthumb", "-Os",
        "-ffunction-sections", "-fdata-sections", "-fno-exceptions", "-fno-rtti",
        "-Wl,--gc-sections", "-Wl,--wrap=main",
        # libstdc++ is prebuilt WITH exceptions, so a few of its members
        # reference the ARM unwinder even though nothing here can throw
        # (-fno-exceptions). The unwinder then wants __exidx_start/__exidx_end,
        # which this linker script does not define. Defining them as an empty
        # range lets that unreachable code link; it can never run, because
        # unwinding requires a throw.
        #
        # Worth recording rather than hiding: the library itself does not drag
        # the unwinder in -- the `invariants` job fails the build on any
        # _Unwind_* symbol in the image. This is two TESTS pulling a libstdc++
        # facility that does.
        "-Wl,--defsym=__exidx_start=0", "-Wl,--defsym=__exidx_end=0",
        "-T", str(LINKER_SCRIPT),
        f"-I{REPO / 'include'}", f"-I{REPO / 'tests'}",
    ]


def discover():
    tests = REPO / "tests"
    return sorted(p.relative_to(tests).as_posix() for p in tests.rglob("*_test.cpp")
                  if not p.relative_to(tests).as_posix().startswith("embedded/"))


def indented(text, limit):
    return "\n".join("      | " + line for line in text.splitlines()[:limit])


def run_one(elf, machine, log):
    """Run detached and poll for the sentinel rather than waiting for QEMU to exit.

    On this machine + libc combination semihosting SYS_EXIT does not bring QEMU
    down, so the sentinel decides and the timeout only matters when it never
    arrives. Returns the exit code the test reported, or None for no sentinel."""
    run_log = log.with_suffix(".log.run")
    with open(run_log, "w") as out:
        qemu = subprocess.Popen(
            ["qemu-system-arm", "-semihosting-config", "enable=on",
             "-monitor", "none", "-serial", "none", "-nographic",
             "-machine", f"{machine},accel=tcg", "-kernel", str(elf)],
            stdout=out, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            if SENTINEL.search(run_log.read_text(errors="replace")) or qemu.poll() is not None:
                break
            time.sleep(0.1)
        qemu.kill()
        qemu.wait()
    output = run_log.read_text(errors="replace")
    with open(log, "a") as full:
        full.write(output)
    match = SENTINEL.search(output)
    return (int(match.group(1)) if match else None), output


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cpu", default="cortex-m3")
    parser.add_argument("--machine", default="mps2-an385")
    parser.add_argument("--stop-on-first-failure", action="store_true")
    # Comma-separated tests that MUST fail to compile on this target, because a
    # capability gate should reject them. Asserted, not tolerated: if one of
    # them builds, the gate has silently stopped working and that is a failure.
    parser.add_argument("--expect-build-fail", default="")
    parser.add_argument("--plan", action="store_true")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)

    sources = discover()
    expect_fail = {name for name in args.expect_build_fail.split(",") if name}

    # Every --expect-build-fail entry must name a test that discovery actually
    # found. A typo there is invisible otherwise: the misspelled entry matches
    # nothing, the real test builds and passes, and the job stays green while
    # the capability gate it was meant to assert goes unchecked.
    #
    # The deny-list is checked the same way, for the same reason from the other
    # direction: an entry left behind after a test is renamed or deleted
    # excludes nothing, and the reason it records stops being true without
    # saying so.
    stale = False
    for want in sorted(expect_fail - set(sources)):
        print(f"::error::--expect-build-fail names '{want}', which discovery did not find",
              file=sys.stderr)
        stale = True
    for path in sorted(set(DENIED) - set(sources)):
        print(f"::error::the deny-list names '{path}', which discovery did not find",
              file=sys.stderr)
        stale = True
    if stale:
        return 2

    if args.plan:
        denied = sum(1 for s in sources if s in DENIED)
        xfail = sum(1 for s in sources if s not in DENIED and s in expect_fail)
        print(f"cpu:          {args.cpu}")
        print(f"discovered:   {len(sources)}")
        print(f"deny-listed:  {denied}")
        print(f"xfail-build:  {xfail}")
        print(f"would-run:    {len(sources) - denied - xfail}")
        return 0

    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    flags = cflags(args.cpu)

    # Negative control first: a test whose CHECK fails must be read as exit 1.
    canary = BUILD_DIR / "canary.elf"
    build = subprocess.run(["arm-none-eabi-g++", *flags, str(CANARY), str(SHIM), "-o", str(canary)],
                           capture_output=True, text=True, cwd=REPO)
    if build.returncode != 0:
        print("::error::the runner's canary did not build:")
        print(indented(build.stdout + build.stderr, 20))
        return 1
    code, output = run_one(canary, args.machine, canary.with_suffix(".log"))
    if code != 1:
        print(f"::error::the canary's CHECK fails, so it must report METL_QEMU_EXIT 1; "
              f"the runner read {code}. Nothing below would mean anything.")
        print(indented(output, 15))
        return 1
    print("canary: a failing CHECK reads as exit 1, as it must")
    print()
    passed = skipped = runtime_skipped = xfailed = 0
    failures, build_failures = [], []

    print(f"{'test':<52} result")
    print(f"{'----':<52} ------")
    for rel in sources:
        if rel in DENIED:
            print(f"{rel:<52} SKIP  ({DENIED[rel]})")
            skipped += 1
            continue

        elf = BUILD_DIR / (rel.replace("/", "_")[:-len(".cpp")] + ".elf")
        log = elf.with_suffix(".log")
        build = subprocess.run(["arm-none-eabi-g++", *flags, f"tests/{rel}", str(SHIM),
                                "-o", str(elf)], capture_output=True, text=True, cwd=REPO)
        log.write_text(build.stdout + build.stderr)

        if build.returncode != 0:
            if rel in expect_fail and CAPABILITY_GATE_TEXT in build.stdout + build.stderr:
                # The capability gate fired, which is the point of the gate.
                print(f"{rel:<52} XFAIL-BUILD  (capability gate fired, as required)")
                xfailed += 1
                continue
            if rel in expect_fail:
                print(f"{rel:<52} BUILD-FAIL  (expected the capability gate, "
                      f"got another error)")
            else:
                print(f"{rel:<52} BUILD-FAIL")
            # Print the diagnostic inline. Hiding it in an artifact means a red
            # build tells you only that something broke, not what.
            print(indented(build.stdout + build.stderr, 20))
            build_failures.append(rel)
            if args.stop_on_first_failure:
                break
            continue

        if rel in expect_fail:
            # It built on a target where the capability is absent. Same reasoning
            # as the invariant canary: a gate that has stopped rejecting is not a
            # gate.
            print(f"{rel:<52} FAIL  (built, but the capability gate should have rejected it)")
            failures.append(f"{rel} (capability gate did not fire)")
            if args.stop_on_first_failure:
                break
            continue

        code, output = run_one(elf, args.machine, log)
        if code == 0:
            print(f"{rel:<52} PASS")
            passed += 1
        elif code == SKIP_CODE and args.cpu in EXPECTED_SELF_SKIP.get(rel, set()):
            # The test found nothing it can check on this core (no VTOR on
            # ARMv6-M). Counted apart from passes so it cannot read as evidence.
            print(f"{rel:<52} SKIP  (reported by the test, expected on {args.cpu})")
            runtime_skipped += 1
        elif code == SKIP_CODE:
            print(f"{rel:<52} FAIL  (skipped itself; not expected to on {args.cpu})")
            print(indented(output, 15))
            failures.append(f"{rel} (unexpected self-skip on {args.cpu})")
        elif code is not None:
            print(f"{rel:<52} FAIL  (exit {code})")
            print(indented(output, 15))
            failures.append(f"{rel} (exit {code})")
        else:
            # No sentinel: hung inside the test, crashed, or never started.
            print(f"{rel:<52} FAIL  (no METL_QEMU_EXIT line)")
            print(indented(output, 15))
            failures.append(f"{rel} (no exit line — hung or crashed before main returned)")
        if args.stop_on_first_failure and failures:
            break

    print()
    print("=" * 62)
    print(f"  passed:       {passed}")
    print(f"  failed:       {len(failures)}")
    print(f"  build-failed: {len(build_failures)}")
    print(f"  xfail-build:  {xfailed} (capability gate fired, as required)")
    print(f"  skipped:      {skipped} (deny-listed, see the table above)")
    print(f"  self-skipped: {runtime_skipped} (the test reported nothing to check here)")
    print("=" * 62)
    if build_failures:
        print(f"\nBuild failures (compiler output is in {BUILD_DIR}/*.log):")
        for name in build_failures:
            print(f"  - {name}")
    if failures:
        print("\nRun failures:")
        for name in failures:
            print(f"  - {name}")
    return 1 if failures or build_failures else 0


if __name__ == "__main__":
    sys.exit(main())
