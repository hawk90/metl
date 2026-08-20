#!/usr/bin/env python3
"""Enforce METL's recoverable-API contract (docs/SCOPE.md section 9).

Three rules, two of which are mechanical:

  R2  the `try_` prefix is reserved for the recoverable form of an operation
      that can fail on capacity. No other public function returns a bare
      `bool` meaning "the operation did not happen".
  R3  every `try_X` is METL_NODISCARD. Discarding the result is the exact bug
      the asserting/recoverable pair exists to prevent.

R1 (every capacity-failing operation has both forms) needs a human and stays on
the PR checklist.

Why a script rather than review: this repo already had 22 `try_` entry points
without METL_NODISCARD, split by *when the header was written* rather than by
any principle. `fixed_vector::try_push_back(x);` compiled silently while
`fixed_queue::try_push(x);` warned. Review had looked at all of them.

The allowlist below is the whole of R4 -- functions whose bool answers a
question ("was it there?") rather than reporting a failure. Each entry carries
its reason, so a future reader sees a decision instead of an omission.

Usage:
    tools/check_api_contract.py [--include-dir include/metl]
    tools/check_api_contract.py --self-test   # prove the checker still bites
"""

import argparse
import pathlib
import re
import sys

# R4: a bare `bool` that is an answer, not a failure report. `m.erase(k);` is a
# legitimate idiom; `v.try_push_back(x);` is a dropped overflow.
BOOL_ALLOWLIST = {
    # --- state queries ---
    "empty": "container state query",
    "full": "container state query",
    "contains": "lookup query",
    "has_value": "engaged-state query, mirrors std::optional",
    "valueless_by_exception": "state query, mirrors std::variant",
    "valid": "state query",
    "active": "state query",
    "is_done": "state query",
    "is_error": "state query",
    "is_attached": "state query",
    "holds_alternative": "type query, mirrors std::holds_alternative",
    "can_dispatch": "predicate: would dispatch() fire?",
    "can_append": "predicate: would the characters fit?",
    "has_single_bit": "numeric predicate, mirrors std::has_single_bit",
    "bit_is_constant_evaluated": "predicate: are we in a constant evaluation?",
    "compare_alternative": "private helper predicate: are two alternatives equal?",
    # --- 'was it there?' answers: nothing failed, the key simply was not present ---
    "erase": "answers 'was an element present', mirrors std::map::erase's count",
    "destroy": "answers 'was this a live slot of this pool'",
    "unsubscribe": "answers 'was a matching listener registered'",
    "detach": "answers 'was this task attached'",
    # --- established std vocabulary where the bool is the documented result ---
    "compare_exchange_weak": "mirrors std::atomic; the bool IS the CAS result",
    "compare_exchange_strong": "mirrors std::atomic; the bool IS the CAS result",
    "operator": "comparison operators",
    # --- poll/step protocols: the bool means 'wants to run again' ---
    "dispatch": "answers 'did a transition fire', not 'did it fail'",
    "poll": "cooperative step protocol: 'still wants to run'",
    "stepper_poll": "cooperative step protocol trampoline",
    "protothread_poll": "cooperative step protocol trampoline",
    "run": "cooperative step protocol: 'still wants to run'",
    "run_once": "cooperative step protocol: 'something yielded'",
    "run_until_idle": "cooperative step protocol: 'settled before max_rounds'",
    # --- private helpers whose bool is an internal predicate ---
    "locate_insert_index": "private helper: 'was a slot found'",
}

# A `try_` name preceded by something that is not a return type -- i.e. a call,
# not a declaration.
NOT_A_RETURN_TYPE = {
    "return", "if", "while", "for", "else", "do", "switch", "case",
    "and", "or", "not", "assert", "METL_ASSERT", "METL_HARDEN",
}

DECL_TRY = re.compile(
    r"^\s*(?P<nodiscard>METL_NODISCARD\s+)?"
    r"(?P<prefix>[A-Za-z_][A-Za-z_0-9:<>,&*\s]*\s)?"
    r"(?P<name>try_[A-Za-z_0-9]+)\s*\("
)

DECL_BOOL = re.compile(
    r"^\s*(?:METL_NODISCARD\s+)?"
    r"(?:(?:constexpr|static|inline|friend|explicit)\s+)*"
    r"bool\s+(?P<name>[A-Za-z_][A-Za-z_0-9]*)\s*\("
)

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
LINE_COMMENT = re.compile(r"//[^\n]*")


def strip_comments(text):
    """Blank out comments, preserving line numbering."""
    def blank(match):
        return "".join("\n" if ch == "\n" else " " for ch in match.group(0))

    return LINE_COMMENT.sub(blank, BLOCK_COMMENT.sub(blank, text))


def check_text(text, path="<memory>"):
    """Return a list of (path, line_number, rule, message)."""
    violations = []
    for number, line in enumerate(strip_comments(text).splitlines(), 1):
        match = DECL_TRY.match(line)
        if match:
            prefix = (match.group("prefix") or "").strip()
            first = prefix.split()[0] if prefix else ""
            # A return type must be present, and must not be a statement keyword
            # (`return try_emplace_back(...)` is a call, not a declaration).
            if prefix and first not in NOT_A_RETURN_TYPE and not match.group("nodiscard"):
                violations.append((
                    path, number, "R3",
                    f"`{match.group('name')}` is missing METL_NODISCARD -- "
                    f"dropping its result is the bug the try_/asserting pair prevents",
                ))

        match = DECL_BOOL.match(line)
        if match:
            name = match.group("name")
            if not name.startswith("try_") and name not in BOOL_ALLOWLIST:
                violations.append((
                    path, number, "R2",
                    f"`{name}` returns a bare bool but is not named `try_*`. "
                    f"If the bool reports that the operation did not happen, rename it "
                    f"`try_{name}`; if it answers a question, add it to BOOL_ALLOWLIST "
                    f"in {pathlib.Path(__file__).name} with its reason",
                ))
    return violations


# Fixtures for --self-test. A gate that cannot fail is not a gate: if these stop
# being reported, the regexes have rotted and the sweep is unprotected.
CANARY_R3 = """
class widget {
 public:
  bool try_push_back(int value) { return value != 0; }
};
"""

CANARY_R2 = """
class widget {
 public:
  bool stash(int value) { return value != 0; }
};
"""

CANARY_CLEAN = """
class widget {
 public:
  METL_NODISCARD bool try_push_back(int value) { return value != 0; }
  METL_NODISCARD bool empty() const { return true; }
  METL_NODISCARD bool erase(int key) { return key == 0; }
  /// A doc comment naming try_push_back must not be mistaken for a declaration.
  void push_back(int value) {
    const bool pushed = try_push_back(value);
    (void)pushed;
  }
};
"""


def self_test():
    failures = []

    caught = {rule for _, _, rule, _ in check_text(CANARY_R3, "<canary-R3>")}
    if "R3" not in caught:
        failures.append("R3 canary was NOT reported -- the METL_NODISCARD check is dead")

    caught = {rule for _, _, rule, _ in check_text(CANARY_R2, "<canary-R2>")}
    if "R2" not in caught:
        failures.append("R2 canary was NOT reported -- the naming check is dead")

    noise = check_text(CANARY_CLEAN, "<canary-clean>")
    if noise:
        failures.append(f"contract-abiding code was flagged: {noise}")

    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("self-test passed: both rules bite, and clean code is not flagged")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-dir", default="include/metl")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    root = pathlib.Path(args.include_dir)
    if not root.is_dir():
        print(f"error: {root} is not a directory", file=sys.stderr)
        return 2

    headers = sorted(root.rglob("*.hpp"))
    if not headers:
        print(f"error: no headers under {root}", file=sys.stderr)
        return 2

    violations = []
    for header in headers:
        violations += check_text(header.read_text(), str(header))

    for path, number, rule, message in violations:
        print(f"{path}:{number}: [{rule}] {message}", file=sys.stderr)

    if violations:
        print(
            f"\n{len(violations)} API-contract violation(s) across {len(headers)} headers. "
            f"See docs/SCOPE.md section 9.",
            file=sys.stderr,
        )
        return 1

    print(f"API contract OK: {len(headers)} headers, rules R2 and R3 hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
