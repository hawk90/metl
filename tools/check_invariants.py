#!/usr/bin/env python3
"""Audit a linked image's symbol table for METL invariant violations.

Proves three of the five invariants in docs/SCOPE.md at once:

  I1  no heap        -- malloc/free/sbrk/operator new must not be in the image
  I2a no exceptions  -- __cxa_throw / _Unwind_* / personality routines
  I2b no RTTI        -- __dynamic_cast / __cxa_bad_cast / typeinfo objects

Why a symbol audit and not linker poisoning: the classic trick (define
`operator new` so it references an undefined symbol) depends on --gc-sections
behaviour and answers differently under GNU ld, lld and IAR. Reading the final
symbol table is linker-agnostic, yields a readable error, and covers every path
linked into the image -- not just the paths a test happened to execute.

The image is never executed, so no emulator and no semihosting are involved.
See docs/SCOPE.md section 7 for why --specs=rdimon.specs must not be used.

Usage:
    check_invariants.py IMAGE [IMAGE...] [--nm arm-none-eabi-nm]
                        [--allow SYMBOL]... [--skip CATEGORY]...

Exit status: 0 clean, 1 violations found, 2 usage/tooling error.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys

# --- Forbidden symbol patterns ----------------------------------------------
#
# Matched against the symbol name with at most one leading underscore stripped,
# so the same table works for ELF (`malloc`) and Mach-O (`_malloc`).
#
# Each entry is (compiled regex, human-readable reason).

CATEGORIES: dict[str, list[tuple[str, str]]] = {
    "heap": [
        (r"^(m|c|re)alloc$", "C heap allocation"),
        (r"^free$", "C heap deallocation"),
        (r"^aligned_alloc$|^posix_memalign$|^memalign$", "aligned heap allocation"),
        (r"^_(m|c|re)alloc_r$|^_free_r$", "newlib reentrant heap"),
        (r"^__libc_(m|c|re)alloc$|^__libc_free$", "glibc heap"),
        # _sbrk under nosys.specs is only pulled in when something references
        # it -- libnosys is an archive. Its presence is a true positive and
        # must never be allowlisted (docs/SCOPE.md section 7).
        (r"^_?sbrk$|^_?brk$", "heap break -- something requested heap growth"),
        # operator new/delete, all mangled spellings incl. sized/aligned/nothrow
        (r"^_Zn[wa][jmy]", "operator new"),
        (r"^_Zd[la]", "operator delete"),
    ],
    "exceptions": [
        (r"^__cxa_(throw|rethrow|allocate_exception|free_exception)$", "throw path"),
        (r"^__cxa_(begin|end)_catch$", "catch path"),
        (r"^_Unwind_", "unwinder"),
        (r"^__gxx_personality", "personality routine"),
    ],
    "rtti": [
        (r"^__dynamic_cast$", "dynamic_cast"),
        (r"^__cxa_bad_(cast|typeid)$", "RTTI failure path"),
        (r"^_ZTI", "typeinfo object"),
        (r"^_ZTS", "typeinfo name"),
    ],
}

# nm symbol types that mean "this symbol is part of the image or is required by
# it". 'U' (undefined) is included on purpose: in a fully linked image an
# undefined heap symbol is just as fatal, and catching it produces a better
# message than a raw linker error.
RELEVANT_TYPES = set("TtDdBbRrWwVvSsGgi") | {"U"}

NM_LINE = re.compile(r"^\s*(?:[0-9a-fA-F]+)?\s*([A-Za-z])\s+(\S+)\s*$")


def compiled(skip: set[str]) -> list[tuple[str, re.Pattern[str], str]]:
    out = []
    for category, entries in CATEGORIES.items():
        if category in skip:
            continue
        for pattern, reason in entries:
            out.append((category, re.compile(pattern), reason))
    return out


def normalize(name: str) -> list[str]:
    """Candidate spellings: as-is, and with one leading underscore removed.

    Mach-O prefixes every C symbol with '_', so `_malloc` and `__Znwm` there
    correspond to `malloc` and `_Znwm` in ELF.
    """
    if name.startswith("_"):
        return [name, name[1:]]
    return [name]


def read_symbols(nm: str, image: str) -> list[tuple[str, str]]:
    try:
        proc = subprocess.run(
            [nm, image], capture_output=True, text=True, check=False
        )
    except OSError as exc:  # nm missing / not executable
        print(f"error: cannot run {nm!r}: {exc}", file=sys.stderr)
        raise SystemExit(2) from exc
    if proc.returncode != 0:
        print(f"error: {nm} {image} failed:\n{proc.stderr.strip()}", file=sys.stderr)
        raise SystemExit(2)

    return parse_symbols(proc.stdout)


def parse_symbols(text: str) -> list[tuple[str, str]]:
    """(type, name) pairs from nm's output. Split out from read_symbols so the
    self-test can feed the audit a symbol table without a toolchain."""
    symbols = []
    for line in text.splitlines():
        match = NM_LINE.match(line)
        if match:
            symbols.append((match.group(1), match.group(2)))
    return symbols


def audit_symbols(
    symbols: list[tuple[str, str]],
    rules: list[tuple[str, re.Pattern[str], str]],
    allow: set[str],
) -> list[tuple[str, str, str, str]]:
    findings = []
    for sym_type, name in symbols:
        if sym_type not in RELEVANT_TYPES:
            continue
        candidates = normalize(name)
        if any(c in allow for c in candidates):
            continue
        for category, pattern, reason in rules:
            if any(pattern.search(c) for c in candidates):
                findings.append((category, name, sym_type, reason))
                break
    return findings


def audit(
    image: str, nm: str, rules: list[tuple[str, re.Pattern[str], str]], allow: set[str]
) -> list[tuple[str, str, str, str]]:
    return audit_symbols(read_symbols(nm, image), rules, allow)


# --- Self-test fixtures -----------------------------------------------------
#
# WHY THIS EXISTS. The `invariants` job already links a canary that deliberately
# violates the invariants and requires the audit to reject it. That canary calls
# `operator new` and `malloc` -- and nothing else, because the probe and the
# canary must share link flags and those flags are `-fno-exceptions -fno-rtti`.
# A canary that cannot throw cannot exercise the throw path.
#
# So the canary proves the audit CAN fail. It does not prove the `exceptions` or
# `rtti` rules can: a typo in one of those regexes, or a category emptied by an
# edit, leaves the canary failing for its heap reason, every clean image passing,
# and two thirds of this gate silently dead. That is this file's own sentence --
# a gate that cannot fail is not a gate -- pointed back at itself.
#
# One representative symbol per PATTERN, not per category, so a pattern that
# stops matching is a failure rather than a rule the others cover for. Editing a
# pattern means editing its fixture, which is the point: both move deliberately.
SELF_TEST_HITS = {
    "heap": [
        "malloc", "calloc", "realloc",
        "free",
        "posix_memalign", "aligned_alloc", "memalign",
        "_malloc_r", "_free_r",
        "__libc_malloc", "__libc_free",
        "_sbrk", "sbrk", "brk",
        "_Znwm", "_Znaj", "_ZnwmSt11align_val_t",
        "_ZdlPv", "_ZdaPv",
    ],
    "exceptions": [
        "__cxa_throw", "__cxa_rethrow", "__cxa_allocate_exception",
        "__cxa_begin_catch", "__cxa_end_catch",
        "_Unwind_RaiseException", "_Unwind_Resume",
        "__gxx_personality_v0",
    ],
    "rtti": [
        "__dynamic_cast",
        "__cxa_bad_cast", "__cxa_bad_typeid",
        "_ZTI7payload",
        "_ZTS7payload",
    ],
}

# Near misses that must NOT fire. Without these the rules could be widened to
# `.*alloc.*` and every fixture above would still pass, which is how a check
# stops distinguishing anything. Same argument as check_docs.py D5 keeping
# `-mcpu=cortex-m3` in its clean tree.
SELF_TEST_MISSES = [
    "mallocate",          # ^(m|c|re)alloc$ is anchored
    "free_list_init",     # ^free$ likewise
    "my_memalign",        # ^memalign$ likewise
    "sbrk_bytes_used",    # ^_?sbrk$ likewise -- a counter, not the syscall
    "_ZN4metl11fixed_arrayE",  # a normal mangled name: _ZN, not _Znw
    "_ZdvSomething",      # _Zd[la] wants delete, not divide
    "_ZTVN4metlE",        # a vtable is not a typeinfo (_ZTV, not _ZTI/_ZTS)
    "metl_free_slots",    # substring of a rule, matching none of them
]


def _table(symbols, sym_type="T"):
    return "\n".join(f"00000000 {sym_type} {name}" for name in symbols)


def self_test() -> int:
    """Prove every rule still bites, including the two the canary cannot reach."""
    failures = []
    rules = compiled(set())

    # 1. Every pattern in every category fires on its representative symbol.
    for category, symbols in SELF_TEST_HITS.items():
        for symbol in symbols:
            findings = audit_symbols(parse_symbols(_table([symbol])), rules, set())
            if not findings:
                failures.append(
                    f"{category}: {symbol!r} was NOT reported. The rule that "
                    f"catches it has stopped matching, and no linked image in "
                    f"CI can tell you -- the canary only violates `heap`."
                )
            elif findings[0][0] != category:
                failures.append(
                    f"{symbol!r} was reported as {findings[0][0]!r}, expected "
                    f"{category!r} -- the categories overlap, so a skipped "
                    f"category would suppress the wrong rule"
                )

    # 2. Near misses stay clean.
    findings = audit_symbols(parse_symbols(_table(SELF_TEST_MISSES)), rules, set())
    if findings:
        failures.append(
            f"the rules are too blunt: {[name for _, name, _, _ in findings]} "
            f"matched, and none of them is a violation"
        )

    # 3. Symbol types outside RELEVANT_TYPES are ignored -- an absolute symbol
    #    is not code in the image.
    if audit_symbols(parse_symbols(_table(["malloc"], sym_type="a")), rules, set()):
        failures.append("an irrelevant nm symbol type was audited anyway")

    # 4. An undefined symbol IS audited: in a fully linked image an undefined
    #    malloc is just as fatal, and this is the case a naive filter drops.
    if not audit_symbols(parse_symbols(_table(["malloc"], sym_type="U")), rules, set()):
        failures.append("an UNDEFINED heap symbol was not reported")

    # 5. --allow suppresses exactly what it names, and --skip a whole category.
    table = parse_symbols(_table(["malloc", "__cxa_throw"]))
    if audit_symbols(table, rules, {"malloc"}) == []:
        failures.append("--allow suppressed a symbol it does not name")
    if any(f[1] == "malloc" for f in audit_symbols(table, rules, {"malloc"})):
        failures.append("--allow did not suppress the symbol it names")
    skipped = audit_symbols(table, compiled({"heap"}), set())
    if any(f[0] == "heap" for f in skipped):
        failures.append("--skip heap still reported a heap finding")
    if not any(f[0] == "exceptions" for f in skipped):
        failures.append("--skip heap also suppressed the exceptions category")

    # 6. A clean table is clean, and an EMPTY rule set is not mistaken for one.
    if audit_symbols(parse_symbols(_table(["main", "memcpy", "__aeabi_uidiv"])),
                     rules, set()):
        failures.append("a clean symbol table was flagged")
    if not rules:
        failures.append("compiled() produced no rules at all -- an audit over "
                        "nothing passes every image")

    if failures:
        for failure in failures:
            print(f"SELF-TEST FAILED: {failure}", file=sys.stderr)
        return 1
    print(
        f"self-test passed: {sum(len(v) for v in SELF_TEST_HITS.values())} "
        f"violation symbols across {len(SELF_TEST_HITS)} categories are caught, "
        f"{len(SELF_TEST_MISSES)} near misses are not, undefined symbols count, "
        f"irrelevant symbol types do not, and --allow/--skip scope correctly"
    )
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fail if a linked image contains heap/exception/RTTI symbols."
    )
    parser.add_argument(
        "images", nargs="*", help="linked ELF/Mach-O images to audit"
    )
    parser.add_argument(
        "--self-test",
        action="store_true",
        help="prove every rule still bites, without a toolchain or an image",
    )
    parser.add_argument(
        "--nm", default="nm", help="nm binary to use (e.g. arm-none-eabi-nm)"
    )
    parser.add_argument(
        "--allow",
        action="append",
        default=[],
        metavar="SYMBOL",
        help="permit one exact symbol name; requires a comment justifying it "
        "at the call site. Never valid for _sbrk (see docs/SCOPE.md).",
    )
    parser.add_argument(
        "--skip",
        action="append",
        default=[],
        choices=sorted(CATEGORIES),
        metavar="CATEGORY",
        help=f"skip a whole category ({', '.join(sorted(CATEGORIES))})",
    )
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if not args.images:
        parser.error("at least one image is required (or pass --self-test)")

    if shutil.which(args.nm) is None and "/" not in args.nm:
        print(f"error: {args.nm} not found on PATH", file=sys.stderr)
        return 2

    rules = compiled(set(args.skip))
    allow = set(args.allow)

    # docs/SCOPE.md says _sbrk must never be allowlisted, because under
    # nosys.specs libnosys is an archive: _sbrk is only pulled in when something
    # actually references it, which makes it a true positive every time.
    # A rule stated only in prose is a rule that erodes, so enforce it here.
    forbidden_allows = {name for name in allow if name.lstrip("_") in {"sbrk", "brk"}}
    if forbidden_allows:
        print(
            f"error: refusing to allowlist {', '.join(sorted(forbidden_allows))} — "
            "its presence is always a real heap reference (docs/SCOPE.md §7). "
            "Find what pulled it in instead.",
            file=sys.stderr,
        )
        return 2
    checked = ", ".join(c for c in sorted(CATEGORIES) if c not in set(args.skip))

    total = 0
    for image in args.images:
        findings = audit(image, args.nm, rules, allow)
        if not findings:
            print(f"PASS  {image}  [{checked}]")
            continue
        total += len(findings)
        print(f"FAIL  {image}  ({len(findings)} violation(s))")
        width = max(len(name) for _, name, _, _ in findings)
        for category, name, sym_type, reason in sorted(findings):
            print(f"        {category:<10} {name:<{width}}  [{sym_type}]  {reason}")

    if total:
        sys.stdout.flush()  # keep the findings above the summary in CI logs
        print(
            "\nMETL invariant violated -- see docs/SCOPE.md.\n"
            "The image must contain no heap, exception or RTTI symbols.\n"
            "Common causes: a printf (newlib's vfprintf allocates), a "
            "std::function,\nan std:: container, or --specs=rdimon.specs "
            "(its crt0 sets up the heap).",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
