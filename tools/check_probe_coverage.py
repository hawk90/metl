#!/usr/bin/env python3
"""Fail when a public header is outside the I1/I2 symbol audit (docs/SCOPE.md section 7).

The audit proves only what its probe images link. Every public header must be
either used by one of PROBES or named in NOT_LINKED with a reason. A header used
means both an #include and a name from it: an #include alone instantiates nothing.

An entry in NOT_LINKED that a probe now uses, or whose header is gone, is an error.

Usage:
    tools/check_probe_coverage.py
    tools/check_probe_coverage.py --self-test
"""

import argparse
import pathlib
import re
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent

PROBES = ("tests/embedded/invariant_probe.cpp", "tests/embedded/invariant_api_probe.cpp")

# A name that proves the header is used, where it is not the header's own stem.
NAME_IN_HEADER = {
    "bit.hpp": "popcount",
    "endian.hpp": "byteswap",
    "format.hpp": "format_uint",
    "hash.hpp": "fnv1a",
    "lock.hpp": "guarded",
    "mmio.hpp": "mmio_ptr",
    "parse.hpp": "try_parse_uint",
    "register_access.hpp": "write_once",
    "scope_exit.hpp": "make_scope_exit",
    "wait.hpp": "cpu_relax",
}

NOT_LINKED = {
    "assert.hpp": "macros and handler setters; every container in the probes expands the macros",
    "attributes.hpp": "attribute macros only: no code of its own",
    "compiler.hpp": "detection macros and constants: no code of its own",
    "config.hpp": "configuration constants: no code of its own",
    "in_place.hpp": "empty tag types: no code of its own",
    "metl.hpp": "the umbrella: it only includes the others",
    "optimization.hpp": "optimization hint macros: no heap or exception path to find",
    "type_traits.hpp": "compile-time traits and storage_for, used by the containers in the probes",
    "version.hpp": "version constants: no code of its own",
}


def public_headers(root: pathlib.Path) -> list[str]:
    include = root / "include" / "metl"
    return sorted(
        path.relative_to(include).as_posix()
        for path in include.rglob("*.hpp")
        if path.relative_to(include).parts[0] not in ("detail", "exp")
    )


def used(header: str, probe_text: str, names: dict[str, str]) -> bool:
    if f"<metl/{header}>" not in probe_text:
        return False
    name = names.get(header, pathlib.PurePosixPath(header).stem)
    return re.search(rf"\bmetl::(?:coro::)?{re.escape(name)}\b", probe_text) is not None


def check(headers: list[str], probe_text: str, names: dict[str, str], not_linked: dict[str, str]) -> list[str]:
    errors = []
    for header in headers:
        if used(header, probe_text, names):
            if header in not_linked:
                errors.append(f"{header}: a probe uses it, so remove it from NOT_LINKED")
        elif header not in not_linked:
            errors.append(f"{header}: no probe includes and uses it. Add it to a probe, or to NOT_LINKED with a reason")
    for table_name, table in (("NOT_LINKED", not_linked), ("NAME_IN_HEADER", names)):
        for header in sorted(set(table) - set(headers)):
            errors.append(f"{header}: named in {table_name}, but there is no such public header")
    return errors


def self_test() -> int:
    headers = ["good.hpp", "coro/task.hpp", "unused.hpp", "listed.hpp"]
    probe = "#include <metl/good.hpp>\n#include <metl/coro/task.hpp>\n#include <metl/unused.hpp>\nmetl::good g; metl::coro::task t;\n"
    errors = check(headers, probe, {}, {"listed.hpp": "reason"})
    failures = []
    if len(errors) != 1 or not errors[0].startswith("unused.hpp"):
        failures.append(f"an included but unused header must be the one error, got {errors}")
    errors = check(["a.hpp"], "metl::a x;", {}, {})
    if len(errors) != 1:
        failures.append(f"a name without its #include must fail, got {errors}")
    errors = check(["good.hpp"], "#include <metl/good.hpp>\nmetl::good g;\n", {}, {"good.hpp": "reason"})
    if len(errors) != 1 or "remove it" not in errors[0]:
        failures.append(f"a stale NOT_LINKED entry must fail, got {errors}")
    errors = check(["good.hpp"], "#include <metl/good.hpp>\nmetl::good g;\n", {}, {"gone.hpp": "reason"})
    if len(errors) != 1 or "no such" not in errors[0]:
        failures.append(f"an entry for a missing header must fail, got {errors}")
    errors = check(["bit.hpp"], "#include <metl/bit.hpp>\nmetl::popcount(1);\n", {"bit.hpp": "popcount"}, {})
    if errors:
        failures.append(f"a NAME_IN_HEADER name must count, got {errors}")
    for failure in failures:
        print(f"self-test: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("check_probe_coverage self-test OK")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--self-test", action="store_true")
    if parser.parse_args().self_test:
        return self_test()

    headers = public_headers(REPO)
    if not headers:
        print("error: no public headers found", file=sys.stderr)
        return 2
    probe_text = "".join((REPO / probe).read_text() for probe in PROBES)
    errors = check(headers, probe_text, NAME_IN_HEADER, NOT_LINKED)
    for error in errors:
        print(f"error: {error}", file=sys.stderr)
    if errors:
        return 1
    print(f"Probe coverage OK: {len(headers) - len(NOT_LINKED)} of {len(headers)} headers linked, {len(NOT_LINKED)} listed with a reason")
    return 0


if __name__ == "__main__":
    sys.exit(main())
