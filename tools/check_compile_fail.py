#!/usr/bin/env python3
"""Prove METL's compile-time contracts still fire.

WHY. The public headers carry 17 user-facing `static_assert`s -- "Capacity must
be power of two", "variant_alternative index out of range", "get_if<T> requires
unique alternative type". For a template library those messages ARE the error
handling: they are what a caller meets when they misuse the API, and the only
thing between a misuse and forty lines of instantiation backtrace.

Not one of them was verified to fire.

A `static_assert` whose condition is accidentally always true is invisible. It
compiles, it never complains, and the contract quietly stops being enforced --
the same shape as a CI step that can only pass, which this repository has spent
a long time removing everywhere else. Every checker under tools/ has a
`--self-test` proving the CHECKER still bites. Nothing proved the CONTRACTS did.
This is that, for the library.

It also removes a real limit. #84 fixed `metl::visit` silently truncating a
visitor's result, and had to test the TRAIT rather than the call, because
instantiating `visit` with a bad visitor is a hard error and there was nowhere
to put a case that expects one.

HOW. Each case is compiled TWICE:

    without METL_COMPILE_FAIL   must SUCCEED
    with    METL_COMPILE_FAIL   must FAIL, and the diagnostic must contain the
                                EXPECT-ERROR text

The first compile is the half a `WILL_FAIL`-style test does not do, and it is
the half that keeps this honest. A case that fails to compile because of a typo,
a stale include or a renamed header satisfies "exited non-zero" perfectly, and
goes on satisfying it forever while testing nothing. Requiring the same file to
build cleanly with the offending construct removed proves the failure comes from
the line under test.

Matching the message is the same argument one level down: an assertion can start
firing for a different reason than the one it was written for -- a different
overload, an earlier assert, a syntax error introduced by an edit -- and a check
that only counts errors would not notice.

Usage:
    tools/check_compile_fail.py --cxx clang++
    tools/check_compile_fail.py --cxx g++ --std c++20
    tools/check_compile_fail.py --self-test
"""

import argparse
import pathlib
import re
import subprocess
import sys
import tempfile

EXPECT = re.compile(r"^//\s*EXPECT-ERROR:\s*(.+?)\s*$", re.M)

GUARD = "METL_COMPILE_FAIL"


def expected_message(path):
    """The EXPECT-ERROR text of one case, or None if it has none."""
    match = EXPECT.search(pathlib.Path(path).read_text(encoding="utf-8"))
    return match.group(1) if match else None


def compile_once(cxx, std, include_dir, case, define_guard):
    """(returncode, combined output) for one -fsyntax-only compile."""
    command = [cxx, f"-std={std}", "-fsyntax-only", "-I", include_dir]
    if define_guard:
        command.append(f"-D{GUARD}")
    command.append(str(case))
    result = subprocess.run(command, capture_output=True, text=True)
    return result.returncode, result.stdout + result.stderr


def check_case(cxx, std, include_dir, case):
    """Return a list of failure strings for one case (empty means it passed)."""
    failures = []
    name = pathlib.Path(case).name

    wanted = expected_message(case)
    if wanted is None:
        return [f"{name}: no `// EXPECT-ERROR:` line. Without one this case "
                f"would assert only that the file fails to compile, which a "
                f"typo satisfies just as well."]

    code, output = compile_once(cxx, std, include_dir, case, define_guard=False)
    if code != 0:
        failures.append(
            f"{name}: the CONTROL compile failed. The case must build cleanly "
            f"without -D{GUARD}, or its failure proves nothing about the "
            f"construct under test.\n{output.strip()[:1500]}")
        # No point testing the guarded build: it would fail for this reason too.
        return failures

    code, output = compile_once(cxx, std, include_dir, case, define_guard=True)
    if code == 0:
        failures.append(
            f"{name}: compiled with -D{GUARD} and was expected NOT to. The "
            f"contract this case pins -- \"{wanted}\" -- is not being enforced.")
        return failures

    if wanted not in output:
        failures.append(
            f"{name}: failed as expected, but for the wrong reason. Wanted a "
            f"diagnostic containing:\n    {wanted}\ngot:\n"
            f"{output.strip()[:1500]}")
    return failures


def run(cxx, std, include_dir, case_dir, quiet=False):
    """`quiet` is for the self-test, whose fixtures are SUPPOSED to fail --
    printing ::error:: for them would put annotations on a green job."""
    def say(message, error=False):
        if quiet:
            return
        print(f"::error::{message}" if error else message,
              file=sys.stderr if error else sys.stdout)

    cases = sorted(pathlib.Path(case_dir).glob("*.cpp"))
    if not cases:
        say(f"no cases found in {case_dir}. A run over nothing reports success, "
            f"which is the failure this whole file is about.", error=True)
        return 1

    failures = []
    for case in cases:
        case_failures = check_case(cxx, std, include_dir, case)
        status = "ok" if not case_failures else "FAILED"
        say(f"  {status:6}  {case.name}  ({expected_message(case) or 'no EXPECT-ERROR'})")
        failures.extend(case_failures)

    say(f"{len(cases)} compile-failure case(s) under {cxx} -std={std}")
    if failures:
        for failure in failures:
            say(failure, error=True)
        return 1
    return 0


def self_test(cxx, std, include_dir):
    """A gate that cannot fail is not a gate. Prove each rejection bites."""
    failures = []

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)

        # A well-formed case must pass, so the fixtures below are compared
        # against something known good rather than against nothing.
        good = root / "good.cpp"
        good.write_text(
            "// EXPECT-ERROR: static assertion\n"
            "int control = 0;\n"
            "#ifdef METL_COMPILE_FAIL\n"
            "static_assert(sizeof(int) == 99, \"static assertion fixture\");\n"
            "#endif\n")
        if check_case(cxx, std, include_dir, good):
            failures.append("a well-formed case was rejected")

        # A case with no EXPECT-ERROR line: the failure would be unattributed.
        no_expect = root / "no_expect.cpp"
        no_expect.write_text("#ifdef METL_COMPILE_FAIL\n#error nope\n#endif\n")
        if not check_case(cxx, std, include_dir, no_expect):
            failures.append("a case with no EXPECT-ERROR line was accepted")

        # A case that compiles under the guard: the contract is not enforced.
        # This is the one that catches a static_assert gone always-true.
        compiles = root / "compiles.cpp"
        compiles.write_text(
            "// EXPECT-ERROR: static assertion\n"
            "int control = 0;\n"
            "#ifdef METL_COMPILE_FAIL\n"
            "int offender = 0;\n"
            "#endif\n")
        if not check_case(cxx, std, include_dir, compiles):
            failures.append("a case that COMPILED under the guard was accepted -- "
                            "this is the always-true static_assert, undetected")

        # A case that fails for the wrong reason.
        wrong = root / "wrong_reason.cpp"
        wrong.write_text(
            "// EXPECT-ERROR: Capacity must be power of two\n"
            "int control = 0;\n"
            "#ifdef METL_COMPILE_FAIL\n"
            "this is not c++;\n"
            "#endif\n")
        if not check_case(cxx, std, include_dir, wrong):
            failures.append("a case that failed for the WRONG reason was accepted")

        # A case whose control does not compile: a typo would otherwise satisfy
        # "exits non-zero" forever.
        broken = root / "broken_control.cpp"
        broken.write_text(
            "// EXPECT-ERROR: static assertion\n"
            "this is not c++ either;\n"
            "#ifdef METL_COMPILE_FAIL\n"
            "static_assert(sizeof(int) == 99, \"static assertion fixture\");\n"
            "#endif\n")
        result = check_case(cxx, std, include_dir, broken)
        if not result:
            failures.append("a case whose CONTROL does not compile was accepted")
        elif "CONTROL" not in result[0]:
            failures.append("a broken control was rejected for the wrong reason")

        # An empty case directory must fail, not pass.
        empty = root / "empty"
        empty.mkdir()
        if run(cxx, std, include_dir, empty, quiet=True) == 0:
            failures.append("a run over an EMPTY case directory reported success")

    if failures:
        for failure in failures:
            print(f"SELF-TEST FAILED: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: cases with no EXPECT-ERROR, cases that compile "
          "under the guard, cases that fail for the wrong reason, cases with a "
          "broken control, and an empty directory are all rejected")
    return 0


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--cxx", default="c++")
    parser.add_argument("--std", default="c++17")
    parser.add_argument("--include-dir", default="include")
    parser.add_argument("--case-dir", default="tests/compile_fail")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test(args.cxx, args.std, args.include_dir)
    return run(args.cxx, args.std, args.include_dir, args.case_dir)


if __name__ == "__main__":
    sys.exit(main())
