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

The pre-release is part of the name too (#717): a tree cut as 2.0.0-alpha.3
opens `metl::v2::alpha3_<config>`, and a release opens `metl::v2::<config>`.
Pre-releases change return types, which are not in a mangled name, so without
the prefix an alpha.2 object and an alpha.3 object link and run each other's
code. --release-tag checks that the prefix in <metl/detail/abi.hpp> is the one
the tag being released calls for; release.yml runs it.

This script builds those programs with the compiler it is given and fails if
any of them behaves otherwise.

Usage:
    tools/check_abi_tag.py --cxx clang++
    tools/check_abi_tag.py --cxx g++ --std c++20
    tools/check_abi_tag.py --release-tag v2.0.0-alpha.3
    tools/check_abi_tag.py --self-test
"""

import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent
ABI = REPO / "tests" / "abi"
ABI_HEADER = REPO / "include" / "metl" / "detail" / "abi.hpp"

# (name, library flags, application flags, whether the program must link).
# Each pair differs in exactly one setting the tag encodes, except the controls.
API_CASES = [
    ("same configuration", ["-DMETL_HARDENING=1"], ["-DMETL_HARDENING=1"], True),
    ("METL_HARDENING 0 against 1", ["-DMETL_HARDENING=0"], ["-DMETL_HARDENING=1"], False),
    ("METL_HARDENING 1 against 2", ["-DMETL_HARDENING=1"], ["-DMETL_HARDENING=2"], False),
    ("exceptions against -fno-exceptions", [], ["-fno-exceptions"], False),
    # Two pre-releases, and a pre-release against the release. Spelled out, so
    # the cases do not depend on what this tree is cut as.
    ("pre-release alpha8 against alpha9", ["-DMETL_DETAIL_ABI_PRERELEASE=alpha8_"],
     ["-DMETL_DETAIL_ABI_PRERELEASE=alpha9_"], False),
    ("release against pre-release alpha9", ["-DMETL_DETAIL_ABI_PRERELEASE="],
     ["-DMETL_DETAIL_ABI_PRERELEASE=alpha9_"], False),
    # Not in the tag: METL_CRC_TABLE and METL_ASSERT_STRINGS change no result.
    ("METL_ASSERT_STRINGS 0 against 1 (not tagged)", ["-DMETL_ASSERT_STRINGS=0"],
     ["-DMETL_ASSERT_STRINGS=1"], True),
]


PRERELEASE_DEFINE = re.compile(r"^#define METL_DETAIL_ABI_PRERELEASE[ \t]*(\S*)[ \t]*$", re.M)


def prefix_for_tag(tag):
    """The namespace prefix a release tag calls for: v2.0.0-alpha.3 -> alpha3_."""
    match = re.fullmatch(r"v?\d+\.\d+\.\d+(?:-([0-9A-Za-z.]+))?", tag)
    if match is None:
        raise ValueError(f"{tag!r} is not a version tag such as v2.0.0 or v2.0.0-alpha.3")
    if match.group(1) is None:
        return ""
    return match.group(1).replace(".", "").lower() + "_"


def header_prefix(text):
    """The prefix <metl/detail/abi.hpp> defines, or None if it defines none or several."""
    found = PRERELEASE_DEFINE.findall(text)
    return found[0] if len(found) == 1 else None


def judge_release_tag(tag, text):
    """The finding for releasing `tag` from a tree whose abi.hpp reads `text`, or None."""
    want = prefix_for_tag(tag)
    have = header_prefix(text)
    if have is None:
        return "include/metl/detail/abi.hpp must define METL_DETAIL_ABI_PRERELEASE exactly once"
    if have == want:
        return None
    return (f"{tag} needs METL_DETAIL_ABI_PRERELEASE to be {want or '(empty)'}, but "
            f"include/metl/detail/abi.hpp defines {have or '(empty)'}. Set it in the release "
            f"commit, so this release's objects do not link with another pre-release's")


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

    for tag, want in (("v2.0.0-alpha.3", "alpha3_"), ("v2.0.0", ""), ("2.1.0-rc.1", "rc1_"),
                      ("v3.0.0-Beta.2", "beta2_")):
        if prefix_for_tag(tag) != want:
            failures.append(f"{tag} gave prefix {prefix_for_tag(tag)!r}, not {want!r}")
    for bad in ("v2.0", "2.0.0-", "v2.0.0-alpha_3", "main"):
        try:
            prefix_for_tag(bad)
            failures.append(f"{bad!r} was taken for a version tag")
        except ValueError:
            pass
    pre = "#ifndef METL_DETAIL_ABI_PRERELEASE\n#define METL_DETAIL_ABI_PRERELEASE {}\n#endif\n"
    if judge_release_tag("v2.0.0-alpha.3", pre.format("alpha3_")):
        failures.append("a matching pre-release prefix was reported")
    if judge_release_tag("v2.0.0", pre.format("")):
        failures.append("an empty prefix for a release was reported")
    if not judge_release_tag("v2.0.0", pre.format("alpha3_")):
        failures.append("a release cut with a pre-release prefix was not reported")
    if not judge_release_tag("v2.0.0-alpha.4", pre.format("alpha3_")):
        failures.append("alpha.4 cut with alpha.3's prefix was not reported")
    if not judge_release_tag("v2.0.0-alpha.3", pre.format("")):
        failures.append("a pre-release cut with the release's empty prefix was not reported")
    if not judge_release_tag("v2.0.0", "// nothing defined here\n"):
        failures.append("a header without the define was not reported")
    if header_prefix(ABI_HEADER.read_text()) is None:
        failures.append("the real abi.hpp defines METL_DETAIL_ABI_PRERELEASE other than once")

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
    parser.add_argument("--release-tag", metavar="TAG",
                        help="check the pre-release prefix in abi.hpp against TAG, and build nothing")
    args = parser.parse_args()
    if args.self_test:
        return self_test(args.cxx)
    if args.release_tag:
        try:
            finding = judge_release_tag(args.release_tag, ABI_HEADER.read_text())
        except ValueError as error:
            finding = str(error)
        if finding:
            print(f"::error::{finding}", file=sys.stderr)
            return 1
        print(f"ABI prefix matches {args.release_tag}: "
              f"{prefix_for_tag(args.release_tag) or '(empty, a release)'}")
        return 0
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
