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

    symbols = []
    for line in proc.stdout.splitlines():
        match = NM_LINE.match(line)
        if match:
            symbols.append((match.group(1), match.group(2)))
    return symbols


def audit(
    image: str, nm: str, rules: list[tuple[str, re.Pattern[str], str]], allow: set[str]
) -> list[tuple[str, str, str, str]]:
    findings = []
    for sym_type, name in read_symbols(nm, image):
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


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fail if a linked image contains heap/exception/RTTI symbols."
    )
    parser.add_argument("images", nargs="+", help="linked ELF/Mach-O images to audit")
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

    if shutil.which(args.nm) is None and "/" not in args.nm:
        print(f"error: {args.nm} not found on PATH", file=sys.stderr)
        return 2

    rules = compiled(set(args.skip))
    allow = set(args.allow)
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
