#!/usr/bin/env python3
"""Spelling rules for library code that handles a user's objects.

  S1  placement new is written `::new`. Unqualified `new (p) T(...)` looks up
      `operator new` in T's class first, so a type that deletes its own
      `operator new` -- the usual way to forbid heap allocation, and exactly the
      kind of type a static pool is for -- does not compile, though nothing
      touches the heap. A class-defined placement `operator new(size_t, void*)`
      would be called instead of the global one.
  S2  a check that guards the library's own memory safety is METL_HARDEN, not
      METL_ASSERT. METL_ASSERT is stripped at METL_HARDENING_NONE; the rule for
      which checks must survive it is in include/metl/config.hpp. A
      METL_ASSERT whose condition has the shape of such a guard -- a position or
      count against the bounds, a null pointer, an alignment, a reference
      count, an own-range alias -- must be listed in ASSERT_ALLOWLIST with the
      reason stripping it is safe, or be METL_HARDEN. An allowlist entry that
      no longer matches a METL_ASSERT is an error too, so the list cannot
      outlive the code it excuses.
  S3  a `constexpr` function can be constant-evaluated for some argument. One
      whose body always reinterpret_casts, static_casts from a `void*`
      parameter, or calls a function the same header defines only without
      `constexpr`, never can: in C++17 that is ill-formed, no diagnostic
      required, and compilers do not diagnose it. Such a function drops
      `constexpr` -- and a non-template becomes `inline`, which `constexpr`
      implied. "Always" means no `return` statement comes first. A cast inside
      an `#if` arm, or inside the `if` statement of an is-constant-evaluated
      test, is exempt, since another arm can be constant; the rest of the body
      is still checked. METL_DETAIL_CONSTEXPR20 (and its old spelling
      METL_CONSTEXPR20) counts as `constexpr`.

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
import tempfile

UNQUALIFIED_PLACEMENT_NEW = re.compile(r"(?<![:\w])new\s*\(")

# The shapes of a memory-safety guard (config.hpp, "The rule").
GUARD_SHAPE = re.compile(r"!= *nullptr|\bpos\b|\bfirst\b|\blast\b|Extent|\b[Cc]ount\b|"
                         r"\b[Oo]ffset\b|prev *!= *0|align|aliases_own_storage|\bn *<= *Capacity|"
                         # The container's own size against its capacity: #579's guard.
                         r"\bsize_ *< *Capacity\b|\.size\(\) *<= *Capacity\b")

# (header, condition) -> why METL_ASSERT is enough there.
ASSERT_ALLOWLIST = {
    ("arena_allocator.hpp", "object != nullptr"):
        "asserting emplace: stripped, it returns the null try_emplace gave; the caller dereferences",
    ("monotonic_buffer.hpp", "object != nullptr"):
        "asserting emplace: stripped, it returns the null try_emplace gave; the caller dereferences",
    ("object_pool.hpp", "object != nullptr"):
        "asserting emplace: stripped, it returns the null try_emplace gave; the caller dereferences",
    ("arena_allocator.hpp", "memory != nullptr"):
        "asserting allocate: stripped, it returns the null try_allocate gave",
    ("monotonic_buffer.hpp", "memory != nullptr"):
        "asserting allocate: stripped, it returns the null try_allocate gave",
    ("arena_allocator.hpp", "target.offset <= offset_"):
        "rewind to a stale mark: stripped, the unwinding loop does nothing",
    ("fixed_vector.hpp", "n <= Capacity - size_"):
        "insert(pos, n, v): every emplace it makes is position-checked by METL_HARDEN",
    ("fixed_vector.hpp", "n <= Capacity"):
        "assign(n, v): a full emplace_back returns the last element, in bounds",
    ("fixed_vector.hpp", "!aliases_own_storage(first, last)"):
        "insert of an own range shifts before it reads: wrong values, in bounds",
    ("intrusive_ptr.hpp", "ptr != nullptr"):
        "add_ref/release are called by intrusive_ptr only with a non-null pointer",
    ("intrusive_ptr.hpp", "ptr_ != nullptr"):
        "operator* / operator->: the caller's access",
    ("variant.hpp", "pointer != nullptr"):
        "get<>: the caller's access, as std::get without exceptions",
    ("fixed_vector.hpp", "size_ < Capacity"):
        "assign(first, last): a full emplace_back returns the last element, in bounds",
    ("fixed_vector.hpp", "il.size() <= Capacity"):
        "initializer list: a full emplace_back returns the last element, in bounds",
}


def strip_comment(line):
    index = line.find("//")
    return line if index < 0 else line[:index]


# What may stand between a function's parameter list and its body: cv- and
# ref-qualifiers, noexcept, a trailing return type and a constructor's member
# initializers (#588). One level of nested parentheses or braces is enough for
# the headers' noexcept(...) conditions and `storage_{}`-style initializers.
_PARENS = r"\((?:[^()]|\([^()]*\))*\)"
_QUALIFIERS = r"(?:\s*(?:const\b|volatile\b|&&|&|noexcept(?:\s*" + _PARENS + r")?|override\b|final\b))*"
_INITIALIZER = r"[\w:]+(?:<[^<>;{}]*>)?\s*(?:" + _PARENS + r"|\{[^{}]*\})"
_INITIALIZERS = r"(?:\s*:\s*" + _INITIALIZER + r"(?:\s*,\s*" + _INITIALIZER + r")*)?"
_TAIL = _QUALIFIERS + r"\s*(?:->[^{;]*?)?" + _INITIALIZERS + r"\s*\{"
CONSTEXPR_FUNCTION = re.compile(
    r"\b(?:constexpr|METL_(?:DETAIL_)?CONSTEXPR20)\b(?P<signature>[^;{}]*?\))" + _TAIL, re.S)
# Any function definition: its name, and the declaration text before the name.
FUNCTION_DEFINITION = re.compile(
    r"(?P<prefix>[^;{}]*?)(?<![\w.>:])(?P<name>~?[A-Za-z_]\w*)\s*\((?P<params>[^;{}]*?)\)" + _TAIL, re.S)
NOT_A_FUNCTION = {"if", "for", "while", "switch", "catch", "return", "sizeof", "alignof",
                  "decltype", "noexcept", "static_assert", "defined", "do", "else", "constexpr"}
CONSTANT_EVALUATION_TEST = re.compile(r"constant_evaluated|in_constant_evaluation")
NEVER_CONSTANT = re.compile(r"\breinterpret_cast\b")
VOID_POINTER_PARAMETER = re.compile(r"\bvoid\s*\*\s*(?P<name>\w+)\s*[,)]")


def unconditional_lines(body):
    """`body` without the lines inside a preprocessor conditional."""
    kept, depth = [], 0
    for line in body.splitlines():
        directive = line.strip()
        if re.match(r"#\s*if", directive):
            depth += 1
        elif re.match(r"#\s*endif", directive):
            depth = max(0, depth - 1)
        elif depth == 0:
            kept.append(line)
    return "\n".join(kept)


def assert_calls(text):
    """(line number, condition) of every METL_ASSERT, however it is wrapped.

    Comments and #define lines are blanked first, keeping line numbers; the
    condition runs to the matching parenthesis and its whitespace is
    collapsed, so a condition clang-format wrapped reads as written on one line.
    """
    code = "\n".join("" if "#define" in strip_comment(line) else strip_comment(line)
                     for line in text.splitlines())
    for match in re.finditer(r"\bMETL_ASSERT\(", code):
        depth, end = 1, match.end()
        while end < len(code) and depth:
            depth += {"(": 1, ")": -1}.get(code[end], 0)
            end += 1
        yield code.count("\n", 0, match.start()) + 1, " ".join(code[match.end():end - 1].split())


def matching(code, start, open_char, close_char):
    """Index just past the bracket that closes the one at `start`."""
    depth, end = 0, start
    while end < len(code):
        depth += {open_char: 1, close_char: -1}.get(code[end], 0)
        end += 1
        if depth == 0:
            break
    return end


def without_constant_evaluation_branches(body):
    """`body` without each `if` whose condition tests for constant evaluation.

    S3 exempts a cast behind such a test, and only that cast (#588): the whole
    statement, condition and both branches, is cut, so the rest of the body is
    still checked. A line that tests and casts in one expression (a ternary) is
    cut too.
    """
    out, position = [], 0
    for match in re.finditer(r"\bif\s*(?:constexpr\s*)?\(", body):
        if match.start() < position:
            continue
        condition_end = matching(body, match.end() - 1, "(", ")")
        if not CONSTANT_EVALUATION_TEST.search(body[match.end():condition_end]):
            continue
        end = condition_end
        while True:
            rest = body[end:]
            stripped = len(rest) - len(rest.lstrip())
            if rest.lstrip().startswith("{"):
                end = matching(body, end + stripped, "{", "}")
            else:
                end = body.find(";", end) + 1 or len(body)
            following = re.match(r"\s*else\b\s*(?:if\s*\()?", body[end:])
            if not following:
                break
            end += following.end()
            if following.group(0).rstrip().endswith("("):
                end = matching(body, end - 1, "(", ")")
        out.append(body[position:match.start()])
        position = end
    out.append(body[position:])
    return "\n".join(line for line in "".join(out).splitlines()
                     if not CONSTANT_EVALUATION_TEST.search(line))


def checked_body(code, open_brace):
    """The part of a function body S3 holds to its rule."""
    body = code[open_brace:matching(code, open_brace, "{", "}")]
    # A cast inside an #if arm is exempt -- another arm can be constant --
    # but only that cast: an unrelated #if elsewhere in the body exempts
    # nothing else.
    return without_constant_evaluation_branches(unconditional_lines(body))


def non_constexpr_functions(code):
    """Names this header defines only without `constexpr`.

    A constexpr function that calls one of them can never be constant-evaluated
    either (#588: fixed_vector::begin called data()). Only names never defined
    constexpr in the same header count, so an overload set that is constexpr
    somewhere is left alone.
    """
    plain, constant = set(), set()
    for match in FUNCTION_DEFINITION.finditer(code):
        name = match.group("name")
        prefix = match.group("prefix")
        if name in NOT_A_FUNCTION or name.startswith("~") or re.search(r"=\s*$|\breturn\s*$", prefix):
            continue
        if re.search(r"\b(?:constexpr|METL_(?:DETAIL_)?CONSTEXPR20)\b", prefix):
            constant.add(name)
        elif not re.search(r"\bconstexpr\s*$", prefix):
            plain.add(name)
    return plain - constant


def never_constant_functions(text, path):
    """S3: constexpr functions whose body can never be a constant expression."""
    code = re.sub(r"//[^\n]*", "", text)
    # A defaulted argument written `= {}` is not a body; hide its braces.
    code = re.sub(r"=\s*\{\}", "= ()", code)
    plain = non_constexpr_functions(code)
    violations = []
    for match in CONSTEXPR_FUNCTION.finditer(code):
        signature = match.group("signature")
        if re.search(r"\bif\s*$", code[:match.start()]):
            continue  # `if constexpr (...)` -- not a function
        body = checked_body(code, match.end() - 1)

        def on_every_path(pattern):
            # Reached on every path: no `return` comes before it. One that does
            # leaves a path that can be constant, as get_if's null check is.
            found = re.search(pattern, body)
            return found is not None and not re.search(r"\breturn\b[^;]*;", body[:found.start()])

        reasons = []
        if on_every_path(NEVER_CONSTANT.pattern):
            reasons.append("reinterpret_cast")
        for parameter in VOID_POINTER_PARAMETER.finditer(signature):
            if on_every_path(r"static_cast<[^>]*>\(\s*" + parameter.group("name") + r"\s*\)"):
                reasons.append(f"static_cast from void* `{parameter.group('name')}`")
        for name in sorted(plain):
            if on_every_path(r"(?<![\w:])(?<!std::)" + re.escape(name) + r"\s*\("):
                reasons.append(f"call to `{name}`, which this header defines without constexpr")
        if reasons:
            line = code[:match.start()].count("\n") + 1
            violations.append((path, line, "S3",
                               f"constexpr function can never be constant-evaluated ({', '.join(reasons)}): "
                               "in C++17 that is ill-formed, no diagnostic required. Drop constexpr, and "
                               "make a non-template inline (constexpr implied it; without it every TU "
                               "defines the function, which metl_header_odr_link catches)"))
    return violations


def check_text(text, path, used=None):
    header = pathlib.PurePath(path).name
    violations = []
    for number, line in enumerate(text.splitlines(), start=1):
        code = strip_comment(line)
        if UNQUALIFIED_PLACEMENT_NEW.search(code):
            violations.append((path, number, "S1", "placement new must be written ::new"))
    for number, condition in assert_calls(text):
        if GUARD_SHAPE.search(condition):
            key = (header, condition)
            if key in ASSERT_ALLOWLIST:
                if used is not None:
                    used.add(key)
            else:
                violations.append((path, number, "S2",
                                   f"METL_ASSERT({key[1]}) has the shape of a memory-safety guard: "
                                   "make it METL_HARDEN, or list it in ASSERT_ALLOWLIST with the "
                                   "reason stripping it is safe (rule: include/metl/config.hpp)"))
    return violations + never_constant_functions(text, path)


def check_tree(root):
    """Every header under `root`, plus the ASSERT_ALLOWLIST entries none of them used."""
    headers = sorted(pathlib.Path(root).rglob("*.hpp"))
    violations = []
    used = set()
    for header in headers:
        violations += check_text(header.read_text(encoding="utf-8"), str(header), used)
    for header, condition in sorted(set(ASSERT_ALLOWLIST) - used):
        violations.append((f"{root}/{header}", 0, "S2",
                           f"ASSERT_ALLOWLIST names METL_ASSERT({condition}), which no longer "
                           "exists there: remove the entry"))
    return violations, headers


CANARY_S1 = """
template <typename T> void make(void* p) { new (p) T(); }
"""

CANARY_S2 = """
void at(int* first, int* pos, int* last) { METL_ASSERT(pos >= first && pos <= last); }
"""

CANARY_S3 = """
constexpr unsigned char first(const void* data) noexcept {
  return *static_cast<const unsigned char*>(data);
}
template <typename T>
constexpr unsigned long bytes(const T* data, int seed = {}) noexcept {
  return *reinterpret_cast<const unsigned char*>(data);
}
"""

CANARY_CLEAN = """
template <typename T> void make(void* p) { ::new (static_cast<void*>(p)) T(); }
// new (p) T() in a comment is fine
void at(int* first, int* pos, int* last) { METL_HARDEN(pos >= first && pos <= last); }
int& front(int* data, unsigned size) { METL_ASSERT(size > 0); return data[0]; }
template <typename T> constexpr T* address(T& object) noexcept {
#if defined(__GNUC__)
  return __builtin_addressof(object);
#else
  return reinterpret_cast<T*>(&reinterpret_cast<char&>(object));
#endif
}
unsigned char first(const void* data) noexcept { return *static_cast<const unsigned char*>(data); }
struct buffer {
  constexpr buffer() noexcept : storage_{} {}
  void* allocate(unsigned n) noexcept { return reinterpret_cast<char*>(storage_) + n; }
  char storage_[8];
};
template <typename T> void store(T* p) { if constexpr (sizeof(T) > 1) { *reinterpret_cast<char*>(p) = 0; } }
"""


def self_test():
    failures = []
    if {rule for _, _, rule, _ in check_text(CANARY_S1, "<canary-S1>")} != {"S1"}:
        failures.append("S1 canary was NOT reported -- the ::new check is dead")
    if {rule for _, _, rule, _ in check_text(CANARY_S2, "canary.hpp")} != {"S2"}:
        failures.append("S2 canary was NOT reported -- the METL_HARDEN check is dead")
    caught = [rule for _, _, rule, _ in check_text(CANARY_S3, "<canary-S3>")]
    if caught != ["S3", "S3"]:
        failures.append(f"S3 canary reported {caught}, wanted two S3 -- the constexpr check is dead")
    # S2 across lines: clang-format wraps a long condition, and the guard is no
    # less a guard for it.
    wrapped = "void at(int* first, int* pos) {\n  METL_ASSERT(pos >= first &&\n              pos != nullptr);\n}\n"
    if [(line, rule) for _, line, rule, _ in check_text(wrapped, "canary.hpp")] != [(2, "S2")]:
        failures.append(f"S2 missed a METL_ASSERT wrapped over two lines: {check_text(wrapped, 'canary.hpp')}")
    # S3 is exempt only for a cast INSIDE an #if arm. An unrelated #if block in
    # the same body -- a hardening check, say -- exempts nothing else.
    guarded = ("template <typename T> constexpr T* get(void* p) noexcept {\n#if METL_HARDENING\n"
               "  check(p);\n#endif\n  return reinterpret_cast<T*>(p);\n}\n")
    behind_test = """
template <typename T>
constexpr bool aligned(T* p) noexcept {
  if (!detail::in_constant_evaluation()) {
    return reinterpret_cast<std::uintptr_t>(p) % alignof(T) == 0;
  }
  return true;
}
"""
    if check_text(behind_test, "<canary-S3-exempt>"):
        failures.append("S3 flagged a cast behind METL's own in_constant_evaluation() test")
    if [rule for _, _, rule, _ in check_text(guarded, "<canary-S3-if>")] != ["S3"]:
        failures.append("S3 let an unconditional reinterpret_cast through because the body has an #if")
    # #588: shapes S3 used to miss, one canary each.
    missed_shapes = {
        "a ref-qualified member":
            "struct s { void* p_; constexpr int& get() & noexcept { return *reinterpret_cast<int*>(p_); } };",
        "a METL_CONSTEXPR20 function":
            "METL_CONSTEXPR20 int* get(char* p) noexcept { return reinterpret_cast<int*>(p); }",
        "a constructor with member initializers":
            "struct s { constexpr s(char* p) noexcept : p_(p), q_{} { touch(reinterpret_cast<int*>(p)); } "
            "char* p_; int q_; };",
        "a call to a non-constexpr function of the same header":
            "struct v { int* data() noexcept { return ptr(); } constexpr int* begin() noexcept { return data(); } };",
        "a cast outside the constant-evaluation branch":
            "template <typename T> constexpr T* get(char* p) noexcept {\n"
            "  if (!detail::in_constant_evaluation()) {\n    check(p);\n  }\n"
            "  return reinterpret_cast<T*>(p);\n}\n",
    }
    for shape, text in missed_shapes.items():
        if [rule for _, _, rule, _ in check_text(text, "<canary-S3-shape>")] != ["S3"]:
            failures.append(f"S3 missed {shape}: {check_text(text, '<canary-S3-shape>')}")
    # A path that returns before the cast or call can be constant (get_if's
    # null check), so S3 does not apply; nor does a call into an overload set
    # the header also defines constexpr.
    for text in ("template <typename T> constexpr T* get_if(void* p) noexcept {\n"
                 "  if (p == nullptr) {\n    return nullptr;\n  }\n  return static_cast<T*>(p);\n}\n",
                 "int* data(int) noexcept; constexpr int* data() noexcept { return nullptr; }\n"
                 "inline int* data(long) noexcept { return nullptr; }\n"
                 "constexpr int* first() noexcept { return data(); }\n"):
        if check_text(text, "<canary-S3-clean>"):
            failures.append(f"S3 flagged a function with a constant path: {check_text(text, '<canary-S3-clean>')}")
    # #588: #579's guard, a container's size against its capacity.
    if [rule for _, _, rule, _ in check_text("void f() { METL_ASSERT(size_ < Capacity); }", "x.hpp")] != ["S2"]:
        failures.append("S2 missed METL_ASSERT(size_ < Capacity)")
    allowed ='void f(int* p) { METL_ASSERT(pointer != nullptr); }'
    if check_text(allowed, "variant.hpp"):
        failures.append("an ASSERT_ALLOWLIST entry was flagged anyway")
    # An allowlist entry whose METL_ASSERT is gone is an error, so the list
    # cannot outlive the code it excuses.
    with tempfile.TemporaryDirectory() as tmp:
        (pathlib.Path(tmp) / "variant.hpp").write_text(allowed + "\n")
        stale = [message for _, _, rule, message in check_tree(pathlib.Path(tmp))[0] if rule == "S2"]
        if len(stale) != len(ASSERT_ALLOWLIST) - 1 or any("pointer != nullptr" in m for m in stale):
            failures.append(f"stale ASSERT_ALLOWLIST entries were not each reported: {stale}")
    noise = check_text(CANARY_CLEAN, "<canary-clean>")
    if noise:
        failures.append(f"rule-abiding code was flagged: {noise}")
    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("self-test passed: S1, S2 (one line or wrapped) and S3 bite, an #if elsewhere exempts no cast, the allowlist is honoured and a stale entry reported, clean code is not flagged")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--include-dir", default="include/metl")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    root = pathlib.Path(args.include_dir)
    violations, headers = check_tree(root)
    if not headers:
        print(f"error: no headers under {root}", file=sys.stderr)
        return 2
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
