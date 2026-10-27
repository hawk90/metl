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
import subprocess
import sys
import tempfile

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
    "exp/intrusive_forward_list.hpp": "using-declarations only; the operations are intrusive_forward_list.hpp's",
    "exp/intrusive_list.hpp": "using-declarations only; the operations are intrusive_list.hpp's",
    "exp/once_flag.hpp": "using-declarations only; the operations are once_flag.hpp's",
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


# A word that turns the term after it into its opposite: "not lock-free".
NEGATION = re.compile(r"\b(?:not|never|no|nor|isn't|aren't|non)\b[^.;|]{0,24}$|non-$", re.I)


def guarantee_sections(text):
    """The doc-comment blocks that open with a progress-guarantee heading: the
    heading line and the `///` lines after it, up to the first line that is not
    one. A table under the heading belongs to it; a later comment does not."""
    lines = text.splitlines()
    sections = []
    for index, line in enumerate(lines):
        if HEADING.match(line):
            block = [line]
            for following in lines[index + 1:]:
                if not following.lstrip().startswith("///"):
                    break
                block.append(following)
            sections.append("\n".join(block))
    return sections


def declared_terms(text):
    """Vocabulary terms a guarantee section states, skipping negated ones."""
    terms = []
    for section in guarantee_sections(text):
        for match in VOCABULARY.finditer(section):
            line_start = section.rfind("\n", 0, match.start()) + 1
            if not NEGATION.search(section[line_start:match.start()]):
                terms.append(match.group(0))
    return terms


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
    has_vocabulary = bool(declared_terms(text))

    if not has_heading:
        violations.append((path, "I3-missing",
                           'no "Progress guarantee" section in the header doc comment'))
    elif not has_vocabulary:
        violations.append((path, "I3-vocabulary",
                           'has a "Progress guarantee" section, but the section does not '
                           "declare a guarantee in SCOPE.md section 1's vocabulary "
                           "(wait-free, bounded / lock-free / blocking, bounded) -- a "
                           "negated term (\"not lock-free\") or one outside the section "
                           "does not count"))
    return violations


def states_guarantee(text):
    return HEADING.search(doc_comment_text(text)) is not None and bool(declared_terms(text))


def scan(include_dir, pending=None):
    """Return (violations, stale_pending, counts)."""
    pending = PENDING if pending is None else pending
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
        if rel in pending:
            # A pending header that now states its guarantee must leave the list.
            if states_guarantee(text):
                stale.append(rel)
            continue
        checked += 1
        violations.extend(check_text(text, rel))

    counts = {
        "headers": len(headers),
        "exempt": len(EXEMPT),
        "pending": len(pending),
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
    # The term must be the guarantee declared, not a word near the heading.
    ("negated", "/// Progress guarantee: not lock-free.\nstruct x {};\n", True),
    ("negated-never", "/// Progress guarantee: never wait-free, bounded.\nstruct x {};\n", True),
    ("term-outside-the-section",
     "/// Progress guarantee: see the notes.\nstruct x {};\n\n/// Unlike a lock-free queue, ...\n"
     "struct y {};\n", True),
    ("stated-in-a-table",
     "/// Progress guarantees:\n///\n///   | Operation | Guarantee |\n///   |---|---|\n"
     "///   | `push` | wait-free, bounded |\nstruct x {};\n", False),
    ("stated-in-prose", "/// Progress guarantee: every operation is wait-free and bounded.\n"
     "struct x {};\n", False),
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
    # PENDING is self-cleaning: a listed header that now states its guarantee
    # is reported, and one that still does not is skipped, not failed.
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "done.hpp").write_text(SELF_TEST_CASES[0][1])
        (root / "todo.hpp").write_text("/// A container.\nstruct x {};\n")
        violations, stale, _ = scan(root, pending={"done.hpp", "todo.hpp"})
        if stale != ["done.hpp"] or violations:
            print(f"SELF-TEST FAILED [pending]: stale={stale}, wanted ['done.hpp']; "
                  f"violations={violations}, wanted none", file=sys.stderr)
            return 1
    # PENDING may only shrink; an entry that is also EXEMPT is a contradiction.
    overlap = PENDING & set(EXEMPT)
    if overlap:
        print(f"SELF-TEST FAILED: {sorted(overlap)} is both EXEMPT and PENDING", file=sys.stderr)
        return 1
    # In CI a problem must reach the run's annotations: `::error::`, exit 1.
    with tempfile.TemporaryDirectory() as tmp:
        (pathlib.Path(tmp) / "x.hpp").write_text("/// A container.\nstruct x {};\n")
        run = subprocess.run([sys.executable, __file__, "--include-dir", tmp],
                             capture_output=True, text=True)
        if run.returncode != 1 or "::error::x.hpp: I3-missing: " not in run.stderr:
            print(f"SELF-TEST FAILED [annotation]: exit {run.returncode}, stderr {run.stderr[:200]!r}",
                  file=sys.stderr)
            return 1
    print("self-test passed: a missing, vague, negated or misplaced guarantee is rejected, problems are ::error:: annotations, and a PENDING entry that states one is reported")
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

    # --report is advisory, so its findings are plain lines, not annotations.
    prefix = "" if args.report else "::error::"
    stream = sys.stdout if args.report else sys.stderr
    for path, rule, message in violations:
        print(f"{prefix}{path}: {rule}: {message}", file=stream)
    for path in stale:
        print(f"{prefix}{path}: I3-stale: states a guarantee but is still listed in PENDING; "
              f"delete the entry so the list stays true", file=stream)

    if args.report:
        return 0
    if violations or stale:
        print(f"\n{len(violations) + len(stale)} problem(s). "
              f"See docs/SCOPE.md section 1 for the vocabulary.", file=sys.stderr)
        return 1
    print("all checked headers state a progress guarantee")
    return 0


if __name__ == "__main__":
    sys.exit(main())
