#!/usr/bin/env python3
"""Enforce I3's documentation half (docs/SCOPE.md section 1).

I3 says every public operation has a bounded worst-case execution time. Four of
METL's five invariants are machine-checked; I3 is the one SCOPE.md says needs a
human. This script does not check the *bound* -- it cannot. It checks that a
human wrote one down, in the vocabulary SCOPE.md defines:

    wait-free, bounded | lock-free | blocking, bounded

Why a script rather than the PR checklist: the checklist has carried
"[ ] I3 progress guarantee stated in the header doc comment" since the repo
started, and when it was finally measured, 10 of 51 headers had one. A checklist
item honoured 20% of the time is not a checklist item. The same thing happened
with METL_NODISCARD on `try_` (see check_api_contract.py) and with the code-size
step that could only pass (see check_size.py): a claim with no gate decays to
whatever the last author felt like.

Two lists carry the state, and both name their entries:

  EXEMPT   headers with no public runtime operation to bound -- macros, tag
           types, compile-time traits. Each entry says why.
  PENDING  headers that predate the gate and have not been written up yet. This
           list may only SHRINK. Adding to it is a deliberate, visible act; a
           new header cannot be grandfathered by accident, because the default
           for anything not in either list is "must state a guarantee".

PENDING is self-cleaning: once a header states its guarantee, the script fails
until the entry is deleted. Otherwise the list would still say 41 long after the
work was done, and the next reader would trust it.

Usage:
    tools/check_progress_guarantee.py [--include-dir include/metl]
    tools/check_progress_guarantee.py --report     # print, never enforce
    tools/check_progress_guarantee.py --self-test  # prove the checker still bites
"""

import argparse
import pathlib
import re
import sys

# No public runtime operation whose execution time could be unbounded: these are
# preprocessor macros, empty tag types, or traits evaluated by the compiler.
EXEMPT = {
    "assert.hpp": "assertion macros; expands to the host's assert or a trap",
    "attributes.hpp": "attribute macros only, no operations",
    "compiler.hpp": "compiler/architecture detection macros, no operations",
    "config.hpp": "configuration macros, no operations",
    "in_place.hpp": "empty tag types, no operations",
    "metl.hpp": "umbrella header; every operation belongs to an included header",
    "optimization.hpp": "branch/inline hint macros, no operations",
    "type_traits.hpp": "compile-time traits; nothing executes at run time",
    "version.hpp": "version macros and constants, no operations",
}

# Headers that predate the gate. MAY ONLY SHRINK -- see the module docstring.
PENDING = set()

# The vocabulary from docs/SCOPE.md section 1. A guarantee must use one of these
# terms; "O(1)" is not one of them, on purpose, because SCOPE.md says complexity
# alone is not a progress guarantee.
VOCABULARY = re.compile(r"wait-free|lock-free|blocking, bounded", re.I)

# The heading that makes the statement findable by a reader, not just by grep.
HEADING = re.compile(r"^\s*///.*\bprogress guarantees?\b", re.I | re.M)

# Declared non-conformance. SCOPE.md section 1 lists this as "Not acceptable in
# METL", so a header claiming it is a bug in the header, not in this script.
FORBIDDEN = re.compile(r"blocking,\s*unbounded", re.I)


def doc_comment_text(text):
    """Only `///` lines. A guarantee buried in an implementation comment does not
    reach the reader who is choosing a type."""
    return "\n".join(line for line in text.splitlines() if line.lstrip().startswith("///"))


def check_text(text, path="<memory>"):
    """Return a list of (path, rule, message)."""
    violations = []
    doc = doc_comment_text(text)

    forbidden = FORBIDDEN.search(doc)
    if forbidden:
        violations.append((path, "I3-forbidden",
                           'declares "blocking, unbounded", which docs/SCOPE.md section 1 '
                           "lists as not acceptable in METL"))

    has_heading = HEADING.search(doc) is not None
    has_vocabulary = VOCABULARY.search(doc) is not None

    if not has_heading:
        violations.append((path, "I3-missing",
                           'no "Progress guarantee" section in the header doc comment'))
    elif not has_vocabulary:
        violations.append((path, "I3-vocabulary",
                           'has a "Progress guarantee" section but does not use SCOPE.md '
                           "section 1's vocabulary (wait-free, bounded / lock-free / "
                           "blocking, bounded)"))
    return violations


def states_guarantee(text):
    return HEADING.search(doc_comment_text(text)) is not None and \
        VOCABULARY.search(doc_comment_text(text)) is not None


def scan(include_dir):
    """Return (violations, stale_pending, counts)."""
    root = pathlib.Path(include_dir)
    headers = sorted(p for p in root.rglob("*.hpp") if "detail" not in p.parts)
    if not headers:
        print(f"error: no headers found under {include_dir}", file=sys.stderr)
        sys.exit(2)

    violations, stale, checked = [], [], 0
    for header in headers:
        rel = header.relative_to(root).as_posix()
        if rel in EXEMPT:
            continue
        text = header.read_text(encoding="utf-8")
        if rel in PENDING:
            # A pending header that now states its guarantee must leave the list.
            if states_guarantee(text):
                stale.append(rel)
            continue
        checked += 1
        violations.extend(check_text(text, rel))

    counts = {
        "headers": len(headers),
        "exempt": len(EXEMPT),
        "pending": len(PENDING),
        "checked": checked,
    }
    return violations, stale, counts


SELF_TEST_CASES = [
    # (name, text, expect_violation)
    ("stated", "/// Progress guarantee: wait-free, bounded.\nstruct x {};\n", False),
    ("stated-plural", "/// Progress guarantees:\n///   push -- lock-free\nstruct x {};\n", False),
    ("missing", "/// A container.\nstruct x {};\n", True),
    ("heading-without-vocabulary",
     "/// Progress guarantee: O(1) amortized.\nstruct x {};\n", True),
    # SCOPE.md section 1: complexity alone is not a progress guarantee.
    ("vocabulary-in-code-comment-only",
     "// wait-free, bounded\n/// A container.\nstruct x {};\n", True),
    ("forbidden",
     "/// Progress guarantee: blocking, unbounded.\nstruct x {};\n", True),
]


def self_test():
    for name, text, expect in SELF_TEST_CASES:
        got = bool(check_text(text))
        if got != expect:
            print(f"SELF-TEST FAILED [{name}]: expected violation={expect}, got {got}",
                  file=sys.stderr)
            for violation in check_text(text):
                print(f"  {violation}", file=sys.stderr)
            return 1
    # PENDING may only shrink; an entry that is also EXEMPT is a contradiction.
    overlap = PENDING & set(EXEMPT)
    if overlap:
        print(f"SELF-TEST FAILED: {sorted(overlap)} is both EXEMPT and PENDING", file=sys.stderr)
        return 1
    print("self-test passed: a missing, vague, or forbidden guarantee is rejected")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--include-dir", default="include/metl")
    parser.add_argument("--report", action="store_true",
                        help="print findings and exit 0, never enforce")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    violations, stale, counts = scan(args.include_dir)

    print(f"I3 progress guarantees: {counts['checked']} header(s) checked, "
          f"{counts['exempt']} exempt, {counts['pending']} pending")

    for path, rule, message in violations:
        print(f"{path}: {rule}: {message}")
    for path in stale:
        print(f"{path}: I3-stale: states a guarantee but is still listed in PENDING; "
              f"delete the entry so the list stays true")

    if args.report:
        return 0
    if violations or stale:
        print(f"\n{len(violations) + len(stale)} problem(s). "
              f"See docs/SCOPE.md section 1 for the vocabulary.")
        return 1
    print("all checked headers state a progress guarantee")
    return 0


if __name__ == "__main__":
    sys.exit(main())
