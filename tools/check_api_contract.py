#!/usr/bin/env python3
"""Enforce METL's recoverable-API contract (docs/SCOPE.md section 9).

Three rules, two of which are mechanical:

  R2  the `try_` prefix is reserved for the recoverable form of an operation
      that can fail on capacity. No other public function returns a bare
      `bool` meaning "the operation did not happen".
  R3  every `try_X` is METL_NODISCARD. Discarding the result is the exact bug
      the asserting/recoverable pair exists to prevent. So is every function in
      RESULT_ONLY: an asserting form whose return value is its only output.

  R4  a bool that answers "was it there" or "did it fire" for an operation
      the caller runs for its effect -- DISCARDABLE below -- is NOT
      METL_NODISCARD. `m.erase(k);` is a legitimate statement and must not warn.

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
import tempfile

# R4: a bare `bool` that is an answer, not a failure report. `m.erase(k);` is a
# legitimate idiom; `v.try_push_back(x);` is a dropped overflow.
BOOL_ALLOWLIST = {
    # --- state queries ---
    "empty": "container state query",
    "full": "container state query",
    "contains": "lookup query",
    "has_value": "engaged-state query, mirrors std::optional",
    "valueless_by_exception": "state query, mirrors std::variant",
    "holds_value": "expected's private state query; false while a member is switched",
    "holds_error": "expected's private state query; false while a member is switched",
    "settled": "expected's private state query; false while a member is switched",
    "valid": "state query",
    "active": "state query",
    "is_done": "state query",
    "is_error": "state query",
    "is_attached": "state query",
    "is_scheduled": "state query",
    "aliases_own_storage": "precondition query: does an iterator range point into this fixed_vector",
    "points_into_storage": "private precondition query: does an argument point into this variant",
    "holds_alternative": "type query, mirrors std::holds_alternative",
    "can_dispatch": "predicate: would dispatch() fire?",
    "can_append": "predicate: would the characters fit?",
    "has_single_bit": "numeric predicate, mirrors std::has_single_bit",
    "bit_is_constant_evaluated": "predicate: are we in a constant evaluation?",
    "in_constant_evaluation": "predicate: are we in a constant evaluation?",
    "compare_alternative": "private helper predicate: are two alternatives equal?",
    # --- 'was it there?' answers: nothing failed, the key simply was not present ---
    "erase": "answers 'was an element present', mirrors std::map::erase's count",
    "destroy": "answers 'was this a live slot of this pool'",
    "unsubscribe": "answers 'was a matching listener registered'",
    "detach": "answers 'was this task attached'",
    "cancel": "answers 'was this task scheduled'; cancelling nothing is not a failure",
    # --- established std vocabulary where the bool is the documented result ---
    "compare_exchange_weak": "mirrors std::atomic; the bool IS the CAS result",
    "compare_exchange_strong": "mirrors std::atomic; the bool IS the CAS result",
    "operator": "comparison operators",
    # --- poll/step protocols: the bool means 'wants to run again' ---
    "dispatch": "answers 'did a transition fire', not 'did it fail'",
    "poll": "cooperative step protocol: 'still wants to run'",
    "stepper_poll": "cooperative step protocol trampoline",
    "protothread_poll": "cooperative step protocol trampoline",
    # --- private helpers whose bool is an internal predicate ---
    "locate_insert_index": "private helper: 'was a slot found'",
    "is_decimal_digit": "private helper predicate: is this character a digit?",
    "holds": "private query: is this attachment still attached to the scheduler?",
    "in_use": "private query: may this attachment number not be issued now?",
}

# R3 beyond `try_`: asserting forms whose return value is their only output, so
# a call whose result is dropped did nothing anyone can observe. Each carries
# its reason, as BOOL_ALLOWLIST's entries do.
RESULT_ONLY = {
    "parse_uint": "the parsed value and the text consumed; there is no other output",
    "parse_int": "the parsed value and the text consumed; there is no other output",
    "parse_hex": "the parsed value and the text consumed; there is no other output",
    "format_uint": "the span written: the text is not NUL-terminated, so it is the only record of its length",
    "format_int": "the span written: the text is not NUL-terminated, so it is the only record of its length",
    "format_hex": "the span written: the text is not NUL-terminated, so it is the only record of its length",
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

DECL_RESULT_ONLY = re.compile(
    r"^\s*(?P<nodiscard>METL_NODISCARD\s+)?"
    r"(?P<prefix>[A-Za-z_][A-Za-z_0-9:<>,&*\s]*\s)?"
    r"(?P<name>" + "|".join(RESULT_ONLY) + r")\s*\("
)

# R4: the subset of BOOL_ALLOWLIST whose bool reports on an operation the caller
# runs for its effect. Discarding it is the normal call, so these must not be
# METL_NODISCARD. The queries in BOOL_ALLOWLIST (empty, contains, ...) do
# nothing else and stay nodiscard.
DISCARDABLE = {"erase", "destroy", "unsubscribe", "detach", "cancel", "dispatch"}

DECL_BOOL = re.compile(
    r"^\s*(?P<head>(?:template\s*<.*>\s*)?"
    r"(?:(?:METL_NODISCARD|constexpr|static|inline|friend|explicit|virtual)\s+)*)"
    r"bool\s+(?P<name>[A-Za-z_][A-Za-z_0-9]*)\s*\("
)

# A return type alone on a line, as clang-format leaves it when the signature
# is long: `constexpr bool` above `try_push_back(T value) noexcept`.
RETURN_TYPE_LINE = re.compile(r"^\s*[A-Za-z_][A-Za-z_0-9:<>,&*\s]*$")
ALONE = re.compile(r"^\s*[A-Za-z_][A-Za-z_0-9]*\s*\(")


def with_return_type(lines, index):
    """Line `index`, preceded by the return-type lines above it, if it starts
    with a bare name and they read as a declaration's head rather than as a
    statement wrapped onto this line."""
    line = lines[index]
    if not ALONE.match(line):
        return line
    head = []
    for above in reversed(lines[max(0, index - 3):index]):
        text = above.strip()
        if not text or not RETURN_TYPE_LINE.match(above) or text.endswith((",", "&&", "||")):
            break
        head.insert(0, text)
    if not head or head[0].split()[0] in NOT_A_RETURN_TYPE:
        return line
    return " ".join(head) + " " + line.strip()

BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)
LINE_COMMENT = re.compile(r"//[^\n]*")


def strip_comments(text):
    """Blank out comments, preserving line numbering."""
    def blank(match):
        return "".join("\n" if ch == "\n" else " " for ch in match.group(0))

    return LINE_COMMENT.sub(blank, BLOCK_COMMENT.sub(blank, text))


def check_text(text, path="<memory>", declared=None):
    """Return a list of (path, line_number, rule, message). The names of the
    bool functions declared are added to `declared` when it is given."""
    violations = []
    lines = strip_comments(text).splitlines()
    for number, line in enumerate(lines, 1):
        line = with_return_type(lines, number - 1)
        match = DECL_TRY.match(line)
        if match:
            prefix = (match.group("prefix") or "").strip()
            first = prefix.split()[0] if prefix else ""
            nodiscard = match.group("nodiscard") or "METL_NODISCARD" in prefix.split()
            # A return type must be present, and must not be a statement keyword
            # (`return try_emplace_back(...)` is a call, not a declaration).
            if prefix and first not in NOT_A_RETURN_TYPE and not nodiscard:
                violations.append((
                    path, number, "R3",
                    f"`{match.group('name')}` is missing METL_NODISCARD -- "
                    f"dropping its result is the bug the try_/asserting pair prevents",
                ))

        match = DECL_RESULT_ONLY.match(line)
        if match:
            prefix = (match.group("prefix") or "").strip()
            first = prefix.split()[0] if prefix else ""
            nodiscard = match.group("nodiscard") or "METL_NODISCARD" in prefix.split()
            if prefix and first not in NOT_A_RETURN_TYPE and not nodiscard:
                name = match.group("name")
                violations.append((
                    path, number, "R3",
                    f"`{name}` is missing METL_NODISCARD -- its result is "
                    f"{RESULT_ONLY[name]}",
                ))

        match = DECL_BOOL.match(line)
        if match and match.group("name") in DISCARDABLE and "METL_NODISCARD" in match.group("head").split():
            violations.append((
                path, number, "R4",
                f"`{match.group('name')}` is METL_NODISCARD, but its bool "
                f"answers a question about an operation the caller runs for its "
                f"effect ({BOOL_ALLOWLIST[match.group('name')]}). Calling it as "
                f"a statement is correct and must not warn",
            ))
        if match:
            name = match.group("name")
            if declared is not None:
                declared.add(name)
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

CANARY_R3_RESULT_ONLY = """
template <typename T>
constexpr parsed<T> parse_uint(span<const char> text) noexcept { return {}; }
"""

CANARY_R4 = """
class widget {
 public:
  METL_NODISCARD bool erase(int key) { return key == 0; }
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
  bool erase(int key) { return key == 0; }
  METL_NODISCARD constexpr span<char> format_uint(span<char> out, unsigned value) noexcept;
  void log(unsigned value) {
    const auto text = format_uint(buffer, value);
    (void)text;
  }
  /// A doc comment naming try_push_back must not be mistaken for a declaration.
  void push_back(int value) {
    const bool pushed = try_push_back(value);
    (void)pushed;
  }
};
"""


def check_tree(root):
    """Every header under `root`. Returns (violations, headers)."""
    headers = sorted(pathlib.Path(root).rglob("*.hpp"))
    violations = []
    declared = set()
    for header in headers:
        violations += check_text(header.read_text(encoding="utf-8"), str(header), declared)
    for name in sorted(set(BOOL_ALLOWLIST) - declared):
        violations.append((f"{root}", 0, "R2",
                           f"BOOL_ALLOWLIST excuses `{name}`, but no header declares a `bool {name}(`. "
                           f"Remove the entry: a stale reason would excuse the next function given "
                           f"that name"))
    return violations, headers


def self_test():
    failures = []

    caught = {rule for _, _, rule, _ in check_text(CANARY_R3, "<canary-R3>")}
    if "R3" not in caught:
        failures.append("R3 canary was NOT reported -- the METL_NODISCARD check is dead")

    caught = {rule for _, _, rule, _ in check_text(CANARY_R3_RESULT_ONLY, "<canary-R3-result-only>")}
    if "R3" not in caught:
        failures.append("R3 RESULT_ONLY canary was NOT reported -- the parse_/format_ check is dead")

    caught = {rule for _, _, rule, _ in check_text(CANARY_R4, "<canary-R4>")}
    if "R4" not in caught:
        failures.append("R4 canary was NOT reported -- the discardable-answer check is dead")

    caught = {rule for _, _, rule, _ in check_text(CANARY_R2, "<canary-R2>")}
    if "R2" not in caught:
        failures.append("R2 canary was NOT reported -- the naming check is dead")

    # Declarations written any way the headers may write them: a template head
    # on the same line, `virtual`, the specifiers in another order, and a
    # return type clang-format put on the line above the name.
    for text, rule in (("template <typename T> bool stash(T value);", "R2"),
                       ("virtual bool stash(int value);", "R2"),
                       ("constexpr METL_NODISCARD bool erase(int key);", "R4"),
                       ("template <typename T>\nconstexpr bool\ntry_push_back(T value) noexcept;", "R3")):
        if rule not in {r for _, _, r, _ in check_text(text, "<canary-shape>")}:
            failures.append(f"{rule} missed {text!r}")
    # The line above a `try_` call is not its return type.
    for text in ("const bool pushed = ok &&\n    try_push_back(value);",
                 "consume(first,\n        try_push_back(value));",
                 "return\n    try_push_back(value);",
                 "METL_NODISCARD constexpr bool\ntry_push_back(int value) noexcept;"):
        if check_text(text, "<canary-call>"):
            failures.append(f"a wrapped call or a nodiscard declaration was flagged: {text!r}")

    # An allowlist entry no header declares is an error: the list must not
    # outlive the code it excuses, or a new `bool run_once()` that reports a
    # failure would inherit the reason written for one that no longer exists.
    with tempfile.TemporaryDirectory() as tmp:
        (pathlib.Path(tmp) / "a.hpp").write_text("METL_NODISCARD bool empty() const;\n")
        stale = [message for _, _, rule, message in check_tree(tmp)[0] if "BOOL_ALLOWLIST" in message]
        if len(stale) != len(BOOL_ALLOWLIST) - 1 or any("`empty`" in m for m in stale):
            failures.append(f"stale BOOL_ALLOWLIST entries were not each reported: {len(stale)} of "
                            f"{len(BOOL_ALLOWLIST) - 1}")

    noise = check_text(CANARY_CLEAN, "<canary-clean>")
    if noise:
        failures.append(f"contract-abiding code was flagged: {noise}")

    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("self-test passed: R2, R3 and R4 bite on every declaration shape, a stale allowlist entry is reported, and clean code and wrapped calls are not flagged")
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

    violations, headers = check_tree(root)
    if not headers:
        print(f"error: no headers under {root}", file=sys.stderr)
        return 2

    for path, number, rule, message in violations:
        print(f"{path}:{number}: [{rule}] {message}", file=sys.stderr)

    if violations:
        print(
            f"\n{len(violations)} API-contract violation(s) across {len(headers)} headers. "
            f"See docs/SCOPE.md section 9.",
            file=sys.stderr,
        )
        return 1

    print(f"API contract OK: {len(headers)} headers, rules R2, R3 and R4 hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
