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
      whose body always reinterpret_casts, or static_casts from a `void*`
      parameter, never can: in C++17 that is ill-formed, no diagnostic
      required, and compilers do not diagnose it. Such a function drops
      `constexpr` -- and a non-template becomes `inline`, which `constexpr`
      implied. A cast inside an `#if` arm or behind an is-constant-evaluated
      test is exempt, since another arm can be constant.

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

METL_ASSERT_CALL = re.compile(r"\bMETL_ASSERT\((.*)\);")
# The shapes of a memory-safety guard (config.hpp, "The rule").
GUARD_SHAPE = re.compile(r"!= *nullptr|\bpos\b|\bfirst\b|\blast\b|Extent|\b[Cc]ount\b|"
                         r"\b[Oo]ffset\b|prev *!= *0|align|aliases_own_storage|\bn *<= *Capacity")

# (header, condition) -> why METL_ASSERT is enough there.
ASSERT_ALLOWLIST = {
    ("arena_allocator.hpp", "object != nullptr"):
        "asserting emplace: stripped, it returns the null try_emplace gave; the caller dereferences",
    ("monotonic_buffer.hpp", "object != nullptr"):
        "asserting emplace: stripped, it returns the null try_emplace gave; the caller dereferences",
    ("object_pool.hpp", "object != nullptr"):
        "asserting emplace: stripped, it returns the null try_emplace gave; the caller dereferences",
    ("static_allocator.hpp", "memory != nullptr"):
        "asserting allocate: stripped, it returns the null try_allocate gave",
    ("static_allocator.hpp", "object != nullptr"):
        "asserting create: stripped, it returns the null try_new gave",
    ("static_allocator.hpp", "location != nullptr"):
        "construct/destroy at a caller-supplied address: the caller's access",
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
}


def strip_comment(line):
    index = line.find("//")
    return line if index < 0 else line[:index]


CONSTEXPR_FUNCTION = re.compile(
    r"\bconstexpr\b(?P<signature>[^;{}]*?\))\s*(?:const\s*)?(?:noexcept(?:\([^)]*\))?)?\s*(?:->[^{;]*)?\{",
    re.S)
NEVER_CONSTANT = re.compile(r"\breinterpret_cast\b")
VOID_POINTER_PARAMETER = re.compile(r"\bvoid\s*\*\s*(?P<name>\w+)\s*[,)]")


def never_constant_functions(text, path):
    """S3: constexpr functions whose body can never be a constant expression."""
    code = re.sub(r"//[^\n]*", "", text)
    # A defaulted argument written `= {}` is not a body; hide its braces.
    code = re.sub(r"=\s*\{\}", "= ()", code)
    violations = []
    for match in CONSTEXPR_FUNCTION.finditer(code):
        signature = match.group("signature")
        if re.search(r"\bif\s*$", code[:match.start()]):
            continue  # `if constexpr (...)` -- not a function
        depth, end = 0, match.end() - 1
        while end < len(code):
            depth += {"{": 1, "}": -1}.get(code[end], 0)
            if depth == 0:
                break
            end += 1
        body = code[match.end() - 1:end + 1]
        if "#if" in body or "constant_evaluated" in body:
            continue
        reasons = []
        if NEVER_CONSTANT.search(body):
            reasons.append("reinterpret_cast")
        for parameter in VOID_POINTER_PARAMETER.finditer(signature):
            if re.search(r"static_cast<[^>]*>\(\s*" + parameter.group("name") + r"\s*\)", body):
                reasons.append(f"static_cast from void* `{parameter.group('name')}`")
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
        if "#define" in code:
            continue
        match = METL_ASSERT_CALL.search(code)
        if match and GUARD_SHAPE.search(match.group(1)):
            key = (header, match.group(1).strip())
            if key in ASSERT_ALLOWLIST:
                if used is not None:
                    used.add(key)
            else:
                violations.append((path, number, "S2",
                                   f"METL_ASSERT({key[1]}) has the shape of a memory-safety guard: "
                                   "make it METL_HARDEN, or list it in ASSERT_ALLOWLIST with the "
                                   "reason stripping it is safe (rule: include/metl/config.hpp)"))
    return violations + never_constant_functions(text, path)


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
    allowed = 'void f(int* p) { METL_ASSERT(pointer != nullptr); }'
    if check_text(allowed, "variant.hpp"):
        failures.append("an ASSERT_ALLOWLIST entry was flagged anyway")
    noise = check_text(CANARY_CLEAN, "<canary-clean>")
    if noise:
        failures.append(f"rule-abiding code was flagged: {noise}")
    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("self-test passed: S1, S2 and S3 bite, the allowlist is honoured, clean code is not flagged")
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
    used = set()
    for header in headers:
        violations += check_text(header.read_text(encoding="utf-8"), str(header), used)
    for header, condition in sorted(set(ASSERT_ALLOWLIST) - used):
        violations.append((f"{root}/{header}", 0, "S2",
                           f"ASSERT_ALLOWLIST names METL_ASSERT({condition}), which no longer "
                           "exists there: remove the entry"))
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
