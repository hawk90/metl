#!/usr/bin/env python3
"""Prove the ABI tag turns a configuration mismatch into a link error (#344).

WHY. METL is header-only, but its types cross binary boundaries: a vendor SDK
built with one METL_HARDENING level, an application built with another. Before
2.0 that linked without a word. Each translation unit's inline functions were
emitted under one name whatever the configuration, the linker kept whichever
copy it saw first, and the 2.0 re-review measured the result: a NONE TU and a
FAST TU, each using fixed_vector<int, 4>::operator[] only internally, gave a
FAST read past size() that aborted when the FAST object was linked first and
returned garbage when the NONE object was.

Every METL declaration now sits in `metl::v<major>::<config>`, an inline
namespace whose name spells out every setting that changes the code of an
inline function (config.hpp, "ABI tag"). So:

    internal use, mixed levels   each TU keeps its own checks, in either
                                 link order: the two copies have two names
    a METL type across the       the program does not link: the library
    boundary, mixed settings     defines api(metl::v2::h0_...::fixed_vector&),
                                 the application calls the h1 one
    the same settings            links and runs

This script builds those programs with the compiler it is given and fails if
any of them behaves otherwise.

Usage:
    tools/check_abi_tag.py --cxx clang++
    tools/check_abi_tag.py --cxx g++ --std c++20
    tools/check_abi_tag.py --self-test
"""

import argparse
import pathlib
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent
ABI = REPO / "tests" / "abi"

# (name, library flags, application flags, whether the program must link).
# Each pair differs in exactly one setting the tag encodes, except the controls.
API_CASES = [
    ("same configuration", ["-DMETL_HARDENING=1"], ["-DMETL_HARDENING=1"], True),
    ("METL_HARDENING 0 against 1", ["-DMETL_HARDENING=0"], ["-DMETL_HARDENING=1"], False),
    ("METL_HARDENING 1 against 2", ["-DMETL_HARDENING=1"], ["-DMETL_HARDENING=2"], False),
    ("exceptions against -fno-exceptions", [], ["-fno-exceptions"], False),
    # Not in the tag: METL_CRC_TABLE and METL_ASSERT_STRINGS change no result.
    ("METL_ASSERT_STRINGS 0 against 1 (not tagged)", ["-DMETL_ASSERT_STRINGS=0"],
     ["-DMETL_ASSERT_STRINGS=1"], True),
]


def run(command, cwd):
    return subprocess.run(command, cwd=cwd, capture_output=True, text=True)


def compile_object(cxx, std, source, flags, output, cwd, optimize="-O2"):
    result = run([cxx, f"-std={std}", optimize, f"-I{REPO / 'include'}", *flags, "-c", str(source),
                  "-o", str(output)], cwd)
    if result.returncode != 0:
        raise RuntimeError(f"{cxx} could not compile {source.name} with {flags}:\n{result.stderr}")
    return output


def link(cxx, objects, output, cwd):
    """True if the objects link into `output`."""
    return run([cxx, *map(str, objects), "-o", str(output)], cwd).returncode == 0


def judge_api(name, linked, expected):
    """The finding for one API case, or None."""
    if linked == expected:
        return None
    if expected:
        return f"{name}: the program should link and does not"
    return (f"{name}: the program links. A library and an application built with different "
            f"settings share a METL type without a link error, so the ABI tag does not "
            f"encode this setting")


def judge_mix(order, unchecked_code, checked_code):
    """The findings for one link order of the mixed-level program."""
    findings = []
    if unchecked_code != 0:
        findings.append(f"link order {order}: the METL_HARDENING_NONE reader stopped (exit "
                        f"{unchecked_code}); it ran the FAST copy of operator[]")
    if checked_code == 0:
        findings.append(f"link order {order}: the FAST reader read past size() without stopping; "
                        f"it ran the NONE copy of operator[]")
    return findings


def check(cxx, std):
    findings = []
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        for name, lib_flags, app_flags, expected in API_CASES:
            lib = compile_object(cxx, std, ABI / "config_api_lib.cpp", lib_flags, work / "lib.o", work)
            app = compile_object(cxx, std, ABI / "config_api_main.cpp", app_flags, work / "app.o", work)
            exe = work / "api"
            linked = link(cxx, [lib, app], exe, work)
            if linked and run([str(exe)], work).returncode != 0:
                findings.append(f"{name}: the program links but returns the wrong value")
            finding = judge_api(name, linked, expected)
            if finding:
                findings.append(finding)
            print(f"  {'ok  ' if finding is None else 'FAIL'}  {name}: "
                  f"{'links' if linked else 'does not link'}")

        # -O0: an inlined operator[] would hide the problem. Out of line, both
        # objects define it, and without the tag the linker keeps one copy.
        unchecked = compile_object(cxx, std, ABI / "config_mix_reader.cpp",
                                   ["-DMETL_HARDENING=0", "-DMETL_ABI_READER=read_unchecked"],
                                   work / "unchecked.o", work, "-O0")
        checked = compile_object(cxx, std, ABI / "config_mix_reader.cpp",
                                 ["-DMETL_HARDENING=1", "-DMETL_ABI_READER=read_checked"],
                                 work / "checked.o", work, "-O0")
        main = compile_object(cxx, std, ABI / "config_mix_main.cpp", [], work / "main.o", work)
        for order, objects in (("NONE first", [unchecked, checked, main]),
                               ("FAST first", [checked, unchecked, main])):
            exe = work / "mix"
            if not link(cxx, objects, exe, work):
                findings.append(f"link order {order}: the mixed-level program does not link")
                continue
            codes = [run([str(exe), arg], work).returncode for arg in ("unchecked", "checked")]
            found = judge_mix(order, *codes)
            findings.extend(found)
            print(f"  {'ok  ' if not found else 'FAIL'}  mixed levels, internal use, {order}: "
                  f"unchecked exit {codes[0]}, checked exit {codes[1]}")
    return findings


def self_test(cxx):
    failures = []
    if judge_api("x", True, True) or judge_api("x", False, False):
        failures.append("an API case that behaved as expected was reported")
    if not judge_api("x", True, False):
        failures.append("a mismatch that linked was not reported")
    if not judge_api("x", False, True):
        failures.append("a matching configuration that failed to link was not reported")
    if judge_mix("o", 0, -6):
        failures.append("a mixed program that kept both copies was reported")
    if len(judge_mix("o", -6, 0)) != 2:
        failures.append("a mixed program that ran the wrong copy each way was not reported twice")

    # The link step must notice a real undefined symbol, or every "does not
    # link" above could be a broken command line instead.
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        (work / "lib.cpp").write_text("int api(long value) { return static_cast<int>(value); }\n")
        (work / "app.cpp").write_text("int api(int value);\nint main() { return api(0); }\n")
        (work / "good.cpp").write_text("int api(int value) { return value; }\n")
        objects = {}
        for stem in ("lib", "app", "good"):
            result = run([cxx, "-c", f"{stem}.cpp", "-o", f"{stem}.o"], work)
            if result.returncode != 0:
                failures.append(f"{cxx} could not compile the self-test's {stem}.cpp:\n{result.stderr}")
            objects[stem] = work / f"{stem}.o"
        if not failures:
            if link(cxx, [objects["lib"], objects["app"]], work / "bad", work):
                failures.append("two objects with mismatched signatures linked: the link step "
                                "cannot see a missing symbol")
            if not link(cxx, [objects["good"], objects["app"]], work / "good", work):
                failures.append("two matching objects did not link: the link step is broken")

    for failure in failures:
        print(f"self-test FAIL: {failure}", file=sys.stderr)
    if not failures:
        print("self-test passed: expected and unexpected link results and copy choices are told "
              "apart, a real undefined symbol fails to link, and a matching pair links")
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--std", default="c++17")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test(args.cxx)
    print(f"ABI tag under {args.cxx} -std={args.std}:")
    try:
        findings = check(args.cxx, args.std)
    except RuntimeError as error:
        print(f"::error::{error}", file=sys.stderr)
        return 1
    for finding in findings:
        print(f"::error::{finding}", file=sys.stderr)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
