#!/usr/bin/env python3
"""Fail on a type-layout change in a minor or patch release (#539).

tests/embedded/layout_probe.cpp holds one row per public type: sizeof, alignof, and
the offsets of the members a user names. The invariants job compiles it for each
Cortex-M, so the rows match the headers. This compares them with the previous
release's rows. A changed or removed row fails unless ALLOWED_CHANGES lists it for
that release with a reason. A new major version may change any row.

The rows only cover the type arguments they name, not every instantiation.

Usage:
    tools/check_layout.py
    tools/check_layout.py --self-test
"""

import argparse
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
PROBE = "tests/embedded/layout_probe.cpp"
CONFIG = "include/metl/config.hpp"

# {previous release tag: {row key: "why it may change (#issue)"}}
ALLOWED_CHANGES: dict[str, dict[str, str]] = {}

# A name that proves the header's type is recorded, where it is not the stem.
NAME_IN_HEADER = {
    "crc8.hpp": "crc8_params",
    "crc16.hpp": "crc16_params",
    "crc32.hpp": "crc32_params",
    "lock.hpp": "guarded",
    "mmio.hpp": "mmio_ptr",
    "parse.hpp": "parsed",
    "scope_exit.hpp": "make_scope_exit",
}

NOT_RECORDED = {
    "assert.hpp": "functions and macros: no type with a layout",
    "attributes.hpp": "macros only",
    "bit.hpp": "functions only",
    "bitfield.hpp": "static members only: no object has a layout",
    "compiler.hpp": "macros and constants only",
    "config.hpp": "macros and constants only",
    "endian.hpp": "functions only",
    "format.hpp": "functions only",
    "hash.hpp": "stateless function objects",
    "in_place.hpp": "empty tag types",
    "metl.hpp": "the umbrella: it only includes the others",
    "optimization.hpp": "macros only",
    "register_access.hpp": "functions only",
    "type_traits.hpp": "traits, and storage_for, whose size is its T's",
    "version.hpp": "constants only",
    "wait.hpp": "functions only",
}

ROW = re.compile(r"^(LAYOUT|OFFSET)\((\d+), ([^,]+), (.+)\);$", re.M)


def rows(text: str) -> dict[str, str]:
    """{key: value}. A LAYOUT's key is its type; an OFFSET's is its type and member."""
    found = {}
    for kind, first, second, third in ROW.findall(text):
        if kind == "LAYOUT":
            found[third] = f"size {first}, alignment {second}"
        else:
            found[f"{third}::{second}"] = f"offset {first}"
    return found


def major_of(config_text: str) -> int:
    match = re.search(r"version_major = (\d+);", config_text)
    if match is None:
        raise ValueError(f"no version_major in {CONFIG}")
    return int(match.group(1))


def compare(old: dict[str, str], new: dict[str, str], allowed: dict[str, str]) -> list[str]:
    errors = []
    touched = set()
    for key, value in old.items():
        current = new.get(key)
        if current == value:
            continue
        touched.add(key)
        if key not in allowed:
            what = "removed" if current is None else f"now {current}"
            errors.append(f"{key}: was {value}, {what}. List it in ALLOWED_CHANGES with a reason, or undo it")
    for key in sorted(set(allowed) - touched):
        errors.append(f"{key}: in ALLOWED_CHANGES, but its layout did not change")
    return errors


def public_headers(root: pathlib.Path) -> list[str]:
    include = root / "include" / "metl"
    return sorted(
        path.relative_to(include).as_posix()
        for path in include.rglob("*.hpp")
        if path.relative_to(include).parts[0] not in ("detail", "exp")
    )


def coverage(headers: list[str], text: str, names: dict[str, str], not_recorded: dict[str, str]) -> list[str]:
    errors = []
    for header in headers:
        name = names.get(header, pathlib.PurePosixPath(header).stem)
        recorded = f"<metl/{header}>" in text and re.search(rf"\bmetl::(?:coro::)?{re.escape(name)}\b", text)
        if recorded and header in not_recorded:
            errors.append(f"{header}: recorded, so remove it from NOT_RECORDED")
        elif not recorded and header not in not_recorded:
            errors.append(f"{header}: no row. Add one to {PROBE}, or list it in NOT_RECORDED with a reason")
    for header in sorted(set(not_recorded) - set(headers)):
        errors.append(f"{header}: in NOT_RECORDED, but there is no such public header")
    return errors


def git(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], cwd=REPO, capture_output=True, text=True)


def previous_release() -> str:
    on_head = git("tag", "--points-at", "HEAD").stdout.split()
    done = git("describe", "--tags", "--abbrev=0", "--match", "v[0-9]*.[0-9]*.[0-9]*",
               *(f"--exclude={tag}" for tag in on_head), "HEAD")
    if done.returncode != 0:
        raise RuntimeError(f"no previous release tag: {done.stderr.strip()}")
    return done.stdout.strip()


def self_test() -> int:
    text = "LAYOUT(8, 4, metl::a<int, 2>);\nOFFSET(4, member, alias);\n"
    failures = []
    parsed = rows(text)
    if parsed != {"metl::a<int, 2>": "size 8, alignment 4", "alias::member": "offset 4"}:
        failures.append(f"rows parsed wrongly: {parsed}")
    same = {"t": "size 8, alignment 4"}
    cases = (
        ("no change", same, same, {}, 0),
        ("same size, new alignment", same, {"t": "size 8, alignment 8"}, {}, 1),
        ("new size, same alignment", same, {"t": "size 4, alignment 4"}, {}, 1),
        ("a removed row", same, {}, {}, 1),
        ("a new row", same, {**same, "u": "size 1, alignment 1"}, {}, 0),
        ("a listed change", same, {"t": "size 4, alignment 4"}, {"t": "why"}, 0),
        ("a stale listing", same, same, {"t": "why"}, 1),
    )
    for name, old, new, allowed, want in cases:
        got = len(compare(old, new, allowed))
        if got != want:
            failures.append(f"{name}: {got} errors, wanted {want}")
    probe = "#include <metl/a.hpp>\n#include <metl/b.hpp>\nLAYOUT(1, 1, metl::a);\n"
    got = coverage(["a.hpp", "b.hpp", "c.hpp"], probe, {}, {"c.hpp": "why"})
    if len(got) != 1 or not got[0].startswith("b.hpp"):
        failures.append(f"an included header with no row must be the one error, got {got}")
    if major_of("inline constexpr int version_major = 2;") != 2:
        failures.append("version_major parsed wrongly")
    for failure in failures:
        print(f"self-test: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("check_layout self-test OK")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--self-test", action="store_true")
    if parser.parse_args().self_test:
        return self_test()

    text = (REPO / PROBE).read_text()
    new = rows(text)
    if not new:
        print(f"error: no rows in {PROBE}", file=sys.stderr)
        return 2
    errors = coverage(public_headers(REPO), text, NAME_IN_HEADER, NOT_RECORDED)

    tag = previous_release()
    old_file = git("show", f"{tag}:{PROBE}")
    if old_file.returncode != 0:
        print(f"{tag} has no {PROBE}: nothing to compare with yet")
    else:
        old_config = git("show", f"{tag}:{CONFIG}")
        if old_config.returncode != 0:
            print(f"error: cannot read {CONFIG} at {tag}: {old_config.stderr.strip()}", file=sys.stderr)
            return 2
        if major_of((REPO / CONFIG).read_text()) > major_of(old_config.stdout):
            print(f"a new major version since {tag}: layouts may change")
        else:
            errors += compare(rows(old_file.stdout), new, ALLOWED_CHANGES.get(tag, {}))
    for error in errors:
        print(f"error: {error}", file=sys.stderr)
    if errors:
        return 1
    print(f"Layouts OK: {len(new)} rows against {tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
