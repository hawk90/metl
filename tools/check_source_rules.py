#!/usr/bin/env python3
"""Spelling rules for library code that handles a user's objects.

  S1  placement new is written `::new`. Unqualified `new (p) T(...)` looks up
      `operator new` in T's class first, so a type that deletes its own
      `operator new` -- the usual way to forbid heap allocation, and exactly the
      kind of type a static pool is for -- does not compile, though nothing
      touches the heap. A class-defined placement `operator new(size_t, void*)`
      would be called instead of the global one.

The companion rule -- take a user object's address with `detail::addressof`,
never `&obj` -- is not checked here. Which `&` applies to a user's type is a
question about types, not text, so a regex would either miss cases or drown in
`this == &other`. tests/core/hostile_types_test.cpp enforces it instead, by
building every container with a type whose `operator&` lies and one that
deletes it.

Usage:
    tools/check_source_rules.py [--include-dir include/metl]
    tools/check_source_rules.py --self-test   # prove the checker still bites
"""

import argparse
import pathlib
import re
import sys

UNQUALIFIED_PLACEMENT_NEW = re.compile(r"(?<![:\w])new\s*\(")


def strip_comment(line):
    index = line.find("//")
    return line if index < 0 else line[:index]


def check_text(text, path):
    violations = []
    for number, line in enumerate(text.splitlines(), start=1):
        code = strip_comment(line)
        if UNQUALIFIED_PLACEMENT_NEW.search(code):
            violations.append((path, number, "S1", "placement new must be written ::new"))
    return violations


CANARY_S1 = """
template <typename T> void make(void* p) { new (p) T(); }
"""

CANARY_CLEAN = """
template <typename T> void make(void* p) { ::new (static_cast<void*>(p)) T(); }
// new (p) T() in a comment is fine
"""


def self_test():
    failures = []
    if {rule for _, _, rule, _ in check_text(CANARY_S1, "<canary-S1>")} != {"S1"}:
        failures.append("S1 canary was NOT reported -- the ::new check is dead")
    noise = check_text(CANARY_CLEAN, "<canary-clean>")
    if noise:
        failures.append(f"rule-abiding code was flagged: {noise}")
    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("self-test passed: S1 bites, and clean code is not flagged")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-dir", default="include/metl")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    root = pathlib.Path(args.include_dir)
    headers = sorted(root.rglob("*.hpp"))
    if not headers:
        print(f"error: no headers under {root}", file=sys.stderr)
        return 2

    violations = []
    for header in headers:
        violations += check_text(header.read_text(encoding="utf-8"), str(header))
    for path, number, rule, message in violations:
        print(f"{path}:{number}: [{rule}] {message}", file=sys.stderr)
    if violations:
        print(f"\n{len(violations)} source-rule violation(s) across {len(headers)} headers.",
              file=sys.stderr)
        return 1
    print(f"source rules hold across {len(headers)} headers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
