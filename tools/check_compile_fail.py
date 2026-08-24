#!/usr/bin/env python3
"""Prove METL's compile-time contracts still fire.

WHY. The public headers carry user-facing `static_assert`s -- "Capacity must be
power of two", "variant_alternative index out of range", "get_if<T> requires
unique alternative type". For a template library those messages ARE the error
handling: they are what a caller meets when they misuse the API, and the only
thing between a misuse and forty lines of instantiation backtrace.

Not one of them was verified to fire.

How many there are is counted, not stated -- see MAX_UNCOVERED below for what
that cost the first time it was written down as prose, and run this script for
the current figures.

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

# ---------------------------------------------------------------------------
# The census, and why the number is here rather than in prose.
#
# This file used to open with "the public headers carry 17 user-facing
# static_asserts". Counted mechanically there are 94 occurrences carrying 72
# distinct messages, so the 8 cases below covered a twelfth of the surface where
# the sentence implied about half. Nobody typed a wrong number on purpose: the
# figure was true when written, it is DERIVED from the headers, and headers kept
# arriving. A denominator that only a human can reproduce is not a denominator,
# which is the lesson docs/TODO.md already records for fuzz coverage -- read the
# denominator, not the percentage.
#
# So it is counted, and the count is RATCHETED on the gap rather than the total:
# UNCOVERED may go down and never up. Adding a static_assert together with a
# case that pins it changes nothing and passes silently; adding one without a
# case fails, which is exactly the defect -- a compile-time contract shipping
# with nothing to prove it fires. Ratcheting the total instead would make every
# new assertion a checker edit, and a gate that cries on correct work gets
# raised until it stops meaning anything.
MAX_UNCOVERED = 29

# A message is user-facing if a caller can trigger it: a `static_assert` in a
# public header, outside `namespace detail`. Those inside are invariants the
# library holds against itself -- a user cannot violate one without reaching
# into detail, and pinning them would test METL's internals rather than its
# contract.
DETAIL_NAMESPACE = "detail"

_SCAN_TOKEN = re.compile(r"namespace\s+([a-zA-Z_]\w*)\s*\{|\{|\}|static_assert\s*\(")
_STRING_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
_MESSAGE_MACRO = re.compile(r"\b(METL_[A-Z0-9_]*MESSAGE)\b")
_DEFINE = re.compile(r"^[ \t]*#[ \t]*define[ \t]+(METL_[A-Z0-9_]*MESSAGE)\b(.*?)(?<!\\)$",
                     re.M | re.S)


def _strip_comments(text):
    return re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", text, flags=re.S))


def message_macros(headers):
    """`#define METL_..._MESSAGE "..."` bodies, joined the way the preprocessor
    would. Without this the one assertion whose message is a macro reads as
    having no message at all, and the case that pins it looks uncovered."""
    macros = {}
    for text in headers.values():
        for match in _DEFINE.finditer(text):
            macros[match.group(1)] = "".join(_STRING_LITERAL.findall(match.group(2)))
    return macros


def static_assert_messages(include_dir):
    """{message: [header, ...]} for every user-facing static_assert."""
    headers = {path: path.read_text(encoding="utf-8")
               for path in sorted(pathlib.Path(include_dir).rglob("*.hpp"))}
    macros = message_macros(headers)

    found = {}
    for path, raw in headers.items():
        text = _strip_comments(raw)
        depth = 0
        namespaces = {}
        position = 0
        while True:
            token = _SCAN_TOKEN.search(text, position)
            if token is None:
                break
            matched = token.group(0)
            if matched.startswith("namespace"):
                depth += 1
                namespaces[depth] = token.group(1)
                position = token.end()
            elif matched == "{":
                depth += 1
                namespaces[depth] = None
                position = token.end()
            elif matched == "}":
                namespaces.pop(depth, None)
                depth = max(0, depth - 1)
                position = token.end()
            else:
                # Walk to the matching close paren; the message is in there.
                cursor = token.end()
                open_parens = 1
                while cursor < len(text) and open_parens:
                    if text[cursor] == "(":
                        open_parens += 1
                    elif text[cursor] == ")":
                        open_parens -= 1
                    cursor += 1
                body = text[token.end():cursor - 1]
                position = cursor
                if any(name == DETAIL_NAMESPACE for name in namespaces.values()):
                    continue
                message = "".join(_STRING_LITERAL.findall(body))
                if not message:
                    macro = _MESSAGE_MACRO.search(body)
                    message = macros.get(macro.group(1), "") if macro else ""
                if message:
                    found.setdefault(message, []).append(path.name)
    return found


def census(include_dir, case_dir):
    """(messages, covered, orphan_expectations).

    `orphan_expectations` are EXPECT-ERROR lines that match no assertion in the
    headers. Such a case still fails to compile and still matches its
    diagnostic, so the suite stays green -- but the message it pins has been
    reworded or moved, and the census would quietly count it as uncovered.
    """
    messages = static_assert_messages(include_dir)
    expectations = [expected_message(case)
                    for case in sorted(pathlib.Path(case_dir).glob("*.cpp"))]
    expectations = [text for text in expectations if text]

    covered = {message for message in messages
               if any(text in message for text in expectations)}
    orphans = [text for text in expectations
               if not any(text in message for message in messages)]
    return messages, covered, orphans


def check_census(include_dir, case_dir, max_uncovered=MAX_UNCOVERED):
    """Failure strings for the coverage ratchet (empty means it held)."""
    messages, covered, orphans = census(include_dir, case_dir)
    failures = []

    if not messages:
        return [f"no user-facing static_assert found under {include_dir}. A "
                f"census over nothing reports full coverage, which is the "
                f"failure this whole file is about."]

    for text in orphans:
        failures.append(
            f"the EXPECT-ERROR text \"{text}\" matches no static_assert in "
            f"{include_dir}. The case still fails to compile, so the suite "
            f"stays green -- but the assertion it pins has been reworded or "
            f"moved, and the census counts it as covering nothing.")

    uncovered = len(messages) - len(covered)
    if uncovered > max_uncovered:
        failures.append(
            f"{uncovered} user-facing static_assert message(s) have no "
            f"compile-failure case, and the ratchet is {max_uncovered}. A "
            f"compile-time contract shipped with nothing proving it fires. Add "
            f"a case under {case_dir} pinning the new message; lower "
            f"MAX_UNCOVERED when you do. Raising it needs a reason in the "
            f"commit, because it means the gap grew on purpose.")
    elif uncovered < max_uncovered:
        failures.append(
            f"{uncovered} user-facing static_assert message(s) are uncovered "
            f"but MAX_UNCOVERED is still {max_uncovered}. Lower it to "
            f"{uncovered}: a ratchet with slack cannot tell the next "
            f"regression from the slack it was left with.")
    return failures


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

    messages, covered, _ = census(include_dir, case_dir)
    say(f"census: {len(covered)} of {len(messages)} distinct user-facing "
        f"static_assert message(s) are pinned by a case "
        f"({len(messages) - len(covered)} uncovered, ratchet {MAX_UNCOVERED})")
    failures.extend(check_census(include_dir, case_dir))

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

        # --- the census ----------------------------------------------------
        # Three public messages, one of them inside `namespace detail`, and one
        # case pinning one of the two that count. So: 2 user-facing, 1 covered,
        # 1 uncovered.
        headers = root / "hdr" / "metl"
        headers.mkdir(parents=True)
        (headers / "pinned.hpp").write_text(
            'template <int N> struct a { static_assert(N > 0, "pinned message"); };\n')
        (headers / "loose.hpp").write_text(
            'template <int N> struct b { static_assert(N > 0, "loose message"); };\n'
            'namespace detail {\n'
            'template <int N> struct c { static_assert(N > 0, "internal invariant"); };\n'
            '}\n')
        cases = root / "cases"
        cases.mkdir()
        (cases / "pins.cpp").write_text("// EXPECT-ERROR: pinned message\n")
        census_dir = str(headers.parent)

        messages, covered, orphans = census(census_dir, cases)
        if len(messages) != 2:
            failures.append(f"the census counted {len(messages)} user-facing "
                            f"message(s), expected 2 -- a static_assert inside "
                            f"`namespace detail` must not count")
        if covered != {"pinned message"}:
            failures.append(f"the census marked {covered} covered, expected "
                            f"exactly the pinned message")
        if orphans:
            failures.append(f"the census reported orphans {orphans} on a "
                            f"fixture whose expectation matches")

        if check_census(census_dir, cases, max_uncovered=0):
            pass  # correct: 1 uncovered against a ratchet of 0
        else:
            failures.append("the census ratchet did not fire on an uncovered "
                            "static_assert -- a contract can ship with nothing "
                            "proving it fires")
        if not check_census(census_dir, cases, max_uncovered=5):
            failures.append("the census accepted a ratchet with 4 slack. A "
                            "ratchet with slack cannot tell the next regression "
                            "from the slack it was left with")
        if check_census(census_dir, cases, max_uncovered=1):
            failures.append("the census rejected an exactly-tight ratchet")

        # An expectation naming a message no header carries.
        (cases / "orphan.cpp").write_text("// EXPECT-ERROR: message that moved\n")
        orphan_failures = check_census(census_dir, cases, max_uncovered=1)
        if not any("matches no static_assert" in text for text in orphan_failures):
            failures.append("an EXPECT-ERROR matching no static_assert in the "
                            "headers was accepted -- the case stays green while "
                            "pinning nothing")
        (cases / "orphan.cpp").unlink()

        # A census over no headers at all must fail rather than report that
        # everything is covered.
        nothing = root / "nothing"
        nothing.mkdir()
        if not check_census(nothing, cases, max_uncovered=0):
            failures.append("a census over an include dir with NO headers "
                            "reported success")

    if failures:
        for failure in failures:
            print(f"SELF-TEST FAILED: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: cases with no EXPECT-ERROR, cases that compile "
          "under the guard, cases that fail for the wrong reason, cases with a "
          "broken control, and an empty directory are all rejected; the census "
          "excludes `namespace detail`, fires on an uncovered assertion, "
          "refuses a slack ratchet, names an expectation that pins nothing, and "
          "does not report success over zero headers")
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
