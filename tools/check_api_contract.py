#!/usr/bin/env python3
"""Enforce METL's recoverable-API contract (docs/SCOPE.md section 9).

Five rules, four of which are mechanical:

  R2  the `try_` prefix is reserved for the recoverable form of an operation
      that can fail on capacity. No other public function returns a bare
      `bool` meaning "the operation did not happen". The same holds for a
      pointer, `optional` or handle: VALUE_ALLOWLIST names those whose empty
      result is an answer, or that assert instead of returning one.
  R3  every `try_X` is METL_NODISCARD. Discarding the result is the exact bug
      the asserting/recoverable pair exists to prevent. So is every function in
      RESULT_ONLY: an asserting form whose return value is its only output.

  R4  a bool that answers "was it there" or "did it fire" for an operation
      the caller runs for its effect -- DISCARDABLE below -- is NOT
      METL_NODISCARD. `m.erase(k);` is a legitimate statement and must not warn.
      Every other BOOL_ALLOWLIST name is a query that does nothing else, and IS
      METL_NODISCARD, unless QUERY_EXEMPT names its header with a reason.
  R6  every container observer in OBSERVERS (`empty()`, `size()`, `front()`,
      `begin()`, ...) is METL_NODISCARD: calling one as a statement does
      nothing. The intrusive lists shipped without it in 1.7.0 and no rule
      noticed; adding it after a type is frozen warns at existing call sites.

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
    "is_linked": "state query: is this hook's element in a list",
    "aliases_own_storage": "precondition query: does an iterator range point into this fixed_vector",
    "points_into_storage": "private precondition query: does an argument point into this variant",
    "holds_alternative": "type query, mirrors std::holds_alternative",
    "can_dispatch": "predicate: would dispatch() fire?",
    "can_append": "predicate: would the characters fit?",
    "has_single_bit": "numeric predicate, mirrors std::has_single_bit",
    "test": "bit query, mirrors std::bitset::test",
    "any": "bit query, mirrors std::bitset::any",
    "none": "bit query, mirrors std::bitset::none",
    "all": "bit query, mirrors std::bitset::all",
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
    "in_this_list": "private precondition query: is this position in this intrusive list",
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

# R6: observers whose only effect is their result.
OBSERVERS = ("empty", "size", "capacity", "max_size", "front", "back", "data",
             "begin", "end", "cbegin", "cend", "before_begin", "cbefore_begin", "is_linked")

# R6 exemptions, by header file name, each with its reason.
OBSERVER_EXEMPT = {
    "span.hpp": ("begin/end/cbegin/cend shipped without METL_NODISCARD in 1.0; "
                 "adding it in 1.x warns at existing call sites, so it waits for 2.0.0"),
}

# R6 also covers a const subscript: unlike `map[key]` it cannot insert, so its
# result is its only effect. OBSERVER_EXEMPT does not excuse it.
DECL_CONST_SUBSCRIPT = re.compile(r"\boperator\[\]\([^)]*\)\s*const\b")

DECL_OBSERVER = re.compile(
    r"^\s*(?P<head>(?:(?:METL_NODISCARD|constexpr|static|inline|virtual)\s+)*)"
    r"(?P<type>[A-Za-z_][A-Za-z_0-9:<>,&*]*(?:\s+[A-Za-z_][A-Za-z_0-9:<>,&*]*)*)\s+"
    r"(?P<name>" + "|".join(OBSERVERS) + r")\(\)\s*(?:const\s*)?(?:noexcept|\{|;)"
)

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
DISCARDABLE = {"erase", "destroy", "unsubscribe", "detach", "cancel", "dispatch",
               "poll", "stepper_poll", "protothread_poll"}

# R4 exemptions from "a query is METL_NODISCARD", by (header file name, function
# name), each with its reason. An entry no header still needs is reported.
QUERY_EXEMPT = {
    ("atomic_ref.hpp", "compare_exchange_strong"): (
        "shipped without METL_NODISCARD in 1.x, as std::atomic_ref's is; adding it "
        "warns at existing call sites, so it waits for 2.0.0"),
    ("atomic_ref.hpp", "compare_exchange_weak"): (
        "shipped without METL_NODISCARD in 1.x, as std::atomic_ref's is; adding it "
        "warns at existing call sites, so it waits for 2.0.0"),
}

# R2 for the other ways to report "did not happen": a plain-named public function
# returning a pointer, an optional or a handle. Each name has its reason.
_OBSERVER_VALUE = "observes storage or an element; there is no operation to fail"
_LOOKUP_VALUE = "a lookup: an empty result answers 'is it there'"
_ASSERTING_VALUE = "the asserting form: running out of capacity aborts, it returns no empty value"
_PRIVATE_VALUE = "a private helper, not part of the public API"
VALUE_ALLOWLIST = {
    "addr": _PRIVATE_VALUE,
    "allocate_impl": _PRIVATE_VALUE,
    "atomic_ref_cast": _PRIVATE_VALUE,
    "c_str": _OBSERVER_VALUE,
    "copies_in_place": _PRIVATE_VALUE,
    "copy_assign_ops": _PRIVATE_VALUE,
    "copy_ops": _PRIVATE_VALUE,
    "create": _ASSERTING_VALUE,
    "data": _OBSERVER_VALUE,
    "destroy_ops": _PRIVATE_VALUE,
    "detach": "intrusive_ptr gives up its pointer; nothing can fail",
    "emplace": _ASSERTING_VALUE,
    "error_ptr": _PRIVATE_VALUE,
    "exchange": "atomic_handle returns the previous value; nothing can fail",
    "find": _LOOKUP_VALUE,
    "find_transition": _LOOKUP_VALUE,
    "get": _LOOKUP_VALUE,
    "get_if": _LOOKUP_VALUE,
    "load": "atomic_handle returns the stored value; nothing can fail",
    "make_optional": "builds an engaged optional; nothing can fail",
    "move_assign_ops": _PRIVATE_VALUE,
    "move_ops": _PRIVATE_VALUE,
    "moves_in_place": _PRIVATE_VALUE,
    "next_deadline": "an empty result answers 'is anything scheduled'",
    "ptr": _PRIVATE_VALUE,
    "raw_addr": _PRIVATE_VALUE,
    "slot_": _PRIVATE_VALUE,
    "slot_ptr": _PRIVATE_VALUE,
    "slot_value": _PRIVATE_VALUE,
    "storage_ptr": _PRIVATE_VALUE,
    "storage_ptr_const": _PRIVATE_VALUE,
    "value_ptr": _PRIVATE_VALUE,
}

# A name that means different things in different headers, by (header file name,
# function name). `allocate` asserts here and returns null in the two below.
VALUE_ALLOWLIST_BY_HEADER = {
    ("static_allocator.hpp", "allocate"): _ASSERTING_VALUE,
}

# Known R2 violations, each named with the issue that fixes it. They break user
# code to change, so they wait for 2.0.0. An entry no header still needs is
# reported.
R2_EXEMPT = {
    ("arena_allocator.hpp", "allocate"): "returns null when full; #165 makes it try_allocate in 2.0.0",
    ("monotonic_buffer.hpp", "allocate"): "returns null when full; #165 makes it try_allocate in 2.0.0",
    ("event_dispatcher.hpp", "subscribe"): "returns an empty optional when full; #155 makes it assert in 2.0.0",
}

DECL_VALUE = re.compile(
    r"^\s*(?:template\s*<.*>\s*)?"
    r"(?:(?:METL_NODISCARD|METL_CONSTEXPR20|constexpr|static|inline|friend|explicit|virtual)\s+)*"
    r"(?:const\s+)?"
    r"(?:[A-Za-z_][\w:]*(?:<[^()]*>)?\s*(?:const\s*)?\*+(?:\s*const)?"
    r"|(?:metl::)?optional<.*>|handle_type|Handle|handle|pointer|const_pointer)"
    r"\s+(?P<name>[a-z_]\w*)\s*\("
)

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


def check_value_return(line, path, number, used):
    """R2 for a pointer, optional or handle return, as a list of 0 or 1
    violations. What excused the declaration is added to `used`."""
    match = DECL_VALUE.match(line)
    if not match or "detail" in pathlib.Path(path).parts:
        return []
    name = match.group("name")
    if name.startswith("try_"):
        return []
    key = (pathlib.Path(path).name, name)
    for excused in (key if key in VALUE_ALLOWLIST_BY_HEADER or key in R2_EXEMPT else None,
                    name if name in VALUE_ALLOWLIST else None):
        if excused is not None:
            if used is not None:
                used.add(excused)
            return []
    return [(path, number, "R2",
             f"`{name}` returns a pointer, optional or handle but is not named `try_*`. "
             f"If an empty result reports that the operation did not happen, rename it "
             f"`try_{name}`; if it answers a question or the function asserts, add it to "
             f"VALUE_ALLOWLIST in {pathlib.Path(__file__).name} with its reason")]


def check_text(text, path="<memory>", declared=None, exempted=None, values=None):
    """Return a list of (path, line_number, rule, message). The names of the
    bool functions declared are added to `declared`, the QUERY_EXEMPT keys that
    excused a declaration to `exempted`, and what excused a pointer, optional or
    handle return to `values`, when they are given."""
    violations = []
    lines = strip_comments(text).splitlines()
    for number, line in enumerate(lines, 1):
        line = with_return_type(lines, number - 1)
        violations += check_value_return(line, path, number, values)
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

        match = DECL_OBSERVER.match(line)
        if (match and "METL_NODISCARD" not in match.group("head").split()
                and match.group("type").split()[0] not in NOT_A_RETURN_TYPE
                and pathlib.Path(path).name not in OBSERVER_EXEMPT):
            violations.append((
                path, number, "R6",
                f"`{match.group('name')}()` is missing METL_NODISCARD -- an observer "
                f"called as a statement does nothing",
            ))

        if DECL_CONST_SUBSCRIPT.search(line) and "METL_NODISCARD" not in line.split():
            violations.append((
                path, number, "R6",
                "const `operator[]` is missing METL_NODISCARD -- a subscript "
                "called as a statement does nothing",
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
        if (match and match.group("name") in BOOL_ALLOWLIST
                and match.group("name") not in DISCARDABLE
                and "METL_NODISCARD" not in match.group("head").split()):
            key = (pathlib.Path(path).name, match.group("name"))
            if key in QUERY_EXEMPT:
                if exempted is not None:
                    exempted.add(key)
            else:
                violations.append((
                    path, number, "R4",
                    f"`{match.group('name')}` is missing METL_NODISCARD -- it is a "
                    f"query ({BOOL_ALLOWLIST[match.group('name')]}), and calling it as "
                    f"a statement does nothing",
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

CANARY_R4_QUERY = """
class widget {
 public:
  constexpr bool contains(int key) const noexcept { return key == 0; }
};
"""

CANARY_R6 = """
class widget {
 public:
  constexpr size_type size() const noexcept { return 0; }
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
    declared, exempted, values = set(), set(), set()
    for header in headers:
        violations += check_text(header.read_text(encoding="utf-8"), str(header), declared, exempted, values)
    for key in sorted(set(VALUE_ALLOWLIST) | set(VALUE_ALLOWLIST_BY_HEADER) | set(R2_EXEMPT), key=str):
        if key not in values:
            violations.append((f"{root}", 0, "R2",
                               f"{key} excuses a pointer, optional or handle return that no header "
                               f"declares. Remove the entry"))
    for file_name, name in sorted(set(QUERY_EXEMPT) - exempted):
        violations.append((f"{root}", 0, "R4",
                           f"QUERY_EXEMPT excuses `{name}` in {file_name}, but no declaration "
                           f"there lacks METL_NODISCARD. Remove the entry"))
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

    caught = {rule for _, _, rule, _ in check_text(CANARY_R4_QUERY, "<canary-R4-query>")}
    if "R4" not in caught:
        failures.append("R4 query canary was NOT reported -- the query METL_NODISCARD check is dead")
    # An exemption covers its own header and name only.
    cas = "bool compare_exchange_weak(T& expected, T desired) noexcept;"
    if check_text(cas, "include/metl/atomic_ref.hpp"):
        failures.append("R4 flagged a declaration QUERY_EXEMPT excuses")
    if "R4" not in {r for _, _, r, _ in check_text(cas, "include/metl/atomic_handle.hpp")}:
        failures.append("R4 let QUERY_EXEMPT excuse a header it does not name")
    # A discardable answer may go without METL_NODISCARD: that is R4's other half.
    if check_text("bool poll() noexcept;", "<canary-discardable>"):
        failures.append("R4 required METL_NODISCARD on a DISCARDABLE answer")

    caught = {rule for _, _, rule, _ in check_text(CANARY_R6, "<canary-R6>")}
    if "R6" not in caught:
        failures.append("R6 canary was NOT reported -- the observer METL_NODISCARD check is dead")
    subscript = "constexpr reference operator[](size_type i) const noexcept { return p[i]; }"
    if "R6" not in {rule for _, _, rule, _ in check_text(subscript, "include/metl/span.hpp")}:
        failures.append("R6 canary was NOT reported -- the const operator[] check is dead")
    if check_text(CANARY_R6, "include/metl/span.hpp"):
        failures.append("R6 flagged a header OBSERVER_EXEMPT excuses")
    for text in ("constexpr size_type\nsize() const noexcept { return 0; }",
                 "METL_CONSTEXPR20 T* data() noexcept { return p; }",
                 "reference front() { return *p; }",
                 "const_iterator cend() const;",
                 "template <bool C = true>\nstatic constexpr bool empty() noexcept { return C; }"):
        if "R6" not in {r for _, _, r, _ in check_text(text, "<canary-observer-shape>")}:
            failures.append(f"R6 missed {text!r}")
    for text in ("return size() == 0;", "return size();", "iterator it = begin();", "if (empty()) {",
                 "METL_NODISCARD constexpr size_type\nsize() const noexcept;",
                 "(void)v.front();", "for (auto it = begin(); it != end(); ++it) {"):
        if check_text(text, "<canary-observer-call>"):
            failures.append(f"R6 flagged a call: {text!r}")

    caught = {rule for _, _, rule, _ in check_text(CANARY_R2, "<canary-R2>")}
    if "R2" not in caught:
        failures.append("R2 canary was NOT reported -- the naming check is dead")

    # R2 beyond bool: every shape of return, and each kind of excuse in its place.
    for text in ("METL_NODISCARD void* stash(int size) noexcept;",
                 "template <typename T>\nMETL_NODISCARD T* stash() noexcept;",
                 "const value_type* stash() const noexcept;",
                 "METL_NODISCARD optional<int> stash() noexcept;",
                 "handle_type stash();",
                 "pointer stash();"):
        if "R2" not in {r for _, _, r, _ in check_text(text, "<canary-value>")}:
            failures.append(f"R2 missed {text!r}")
    for text in ("METL_NODISCARD T* try_stash() noexcept;",
                 "METL_NODISCARD pointer find(int key) noexcept;",
                 "pointer memory = try_allocate(count);",
                 "return try_allocate(count);"):
        if check_text(text, "<canary-value-clean>"):
            failures.append(f"R2 flagged {text!r}")
    allocate = "METL_NODISCARD void* allocate(size_type bytes) noexcept;"
    if check_text(allocate, "include/metl/arena_allocator.hpp"):
        failures.append("R2 flagged a declaration R2_EXEMPT excuses")
    if check_text(allocate, "include/metl/static_allocator.hpp"):
        failures.append("R2 flagged a declaration VALUE_ALLOWLIST_BY_HEADER excuses")
    if "R2" not in {r for _, _, r, _ in check_text(allocate, "include/metl/other_allocator.hpp")}:
        failures.append("R2 let a per-header excuse apply to a header it does not name")
    if check_text("void* stash();", "include/metl/detail/stash.hpp"):
        failures.append("R2 checked a value return under detail/")

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
        stale = [message for _, _, _, message in check_tree(tmp)[0] if "no header declares. Remove" in message]
        wanted = len(VALUE_ALLOWLIST) + len(VALUE_ALLOWLIST_BY_HEADER) + len(R2_EXEMPT)
        if len(stale) != wanted:
            failures.append(f"stale value-return entries were not each reported: {len(stale)} of {wanted}")
        stale = [message for _, _, _, message in check_tree(tmp)[0] if "QUERY_EXEMPT" in message]
        if len(stale) != len(QUERY_EXEMPT):
            failures.append(f"stale QUERY_EXEMPT entries were not each reported: {len(stale)} of "
                            f"{len(QUERY_EXEMPT)}")

    noise = check_text(CANARY_CLEAN, "<canary-clean>")
    if noise:
        failures.append(f"contract-abiding code was flagged: {noise}")

    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("self-test passed: R2, R3, R4 (both halves) and R6 bite on every declaration shape, a stale allowlist or exemption entry is reported, and clean code and wrapped calls are not flagged")
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

    print(f"API contract OK: {len(headers)} headers, rules R2, R3, R4 and R6 hold")
    return 0


if __name__ == "__main__":
    sys.exit(main())
