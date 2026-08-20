#!/usr/bin/env python3
"""Flatten METL's 60 public headers into one self-contained header.

Why
---
METL is header-only and dependency-free, which makes "drop one file into the
tree" a realistic way to adopt it -- especially on the vendor toolchains this
library targets, where adding an include path to a shipped IDE project is more
friction than adding a file. This produces that file from the real headers, so
it cannot drift from them.

How
---
We emit the headers in topological order over the `#include "metl/..."` edges,
dropping the internal includes and each header's `#pragma once`, and hoist
every external `#include <...>` to a single deduplicated block at the top.

Those edges are not quite a DAG, and the exception is deliberate rather than an
accident: `compiler.hpp` re-exports `attributes.hpp` from its *last* line, while
`attributes.hpp` includes `compiler.hpp` from its *first*, so the 43 headers
that include `compiler.hpp` get the attribute macros for free. `#pragma once`
makes that safe for the compiler; for us it is a cycle.

The distinction that resolves it is positional, so the tool derives it rather
than carrying a hardcoded exception: an internal include placed **before** any
of the file's own code is a dependency (it must be emitted first), and one
placed **after** is a re-export (it only has to appear somewhere, and by then
this file has already been emitted). Re-export edges therefore constrain
reachability but not order. Any cycle among genuine dependency edges is still a
hard error, and the resolved re-export edges are reported so they stay visible.

Two things this deliberately does NOT do:

  * It does not strip comments or reformat. The rationale comments in these
    headers are the reason several of the design decisions are legible; an
    amalgamation that drops them is a worse artifact, not a smaller one.
  * It does not rename or wrap anything in an extra namespace. The output has
    to be a drop-in replacement for the include directory, so every name has to
    land exactly where it does today.

Usage:
    tools/amalgamate.py -o metl-single.hpp [--include-dir include]
"""

from __future__ import annotations

import argparse
import datetime
import pathlib
import re
import sys

INTERNAL_INCLUDE = re.compile(r'^\s*#\s*include\s*"(metl/[^"]+)"\s*$')
EXTERNAL_INCLUDE = re.compile(r'^\s*#\s*include\s*<([^>]+)>\s*$')
PRAGMA_ONCE = re.compile(r'^\s*#\s*pragma\s+once\s*$')


def read_header(include_dir: pathlib.Path, rel: str) -> list[str]:
    path = include_dir / rel
    if not path.is_file():
        raise SystemExit(f"error: {rel} is included but does not exist under {include_dir}")
    return path.read_text(encoding="utf-8").splitlines()


def split_includes(lines: list[str]) -> tuple[list[str], list[str]]:
    """Split internal includes into (dependencies, re-exports) by position.

    An include before the file's own code must be emitted first; one after it
    is a convenience re-export. See the module docstring.
    """
    dependencies: list[str] = []
    reexports: list[str] = []
    code_started = False
    for line in lines:
        if m := INTERNAL_INCLUDE.match(line):
            (reexports if code_started else dependencies).append(m.group(1))
            continue
        stripped = line.strip()
        if not stripped or stripped.startswith(("//", "/*", "*", "#")):
            continue
        code_started = True
    return dependencies, reexports


def topological_order(
    include_dir: pathlib.Path, roots: list[str]
) -> tuple[list[str], dict[str, list[str]], list[tuple[str, str]]]:
    """Depth-first post-order, so a header is emitted after everything it needs."""
    order: list[str] = []
    state: dict[str, int] = {}  # 1 = on the stack, 2 = emitted
    bodies: dict[str, list[str]] = {}
    stack_trace: list[str] = []
    resolved_reexports: list[tuple[str, str]] = []

    def visit(rel: str) -> None:
        if state.get(rel) == 2:
            return
        if state.get(rel) == 1:
            cycle = " -> ".join(stack_trace[stack_trace.index(rel):] + [rel])
            raise SystemExit(f"error: include cycle among dependency edges: {cycle}")
        state[rel] = 1
        stack_trace.append(rel)
        bodies[rel] = read_header(include_dir, rel)
        deps, reexports = split_includes(bodies[rel])
        for dep in deps:
            visit(dep)
        stack_trace.pop()
        state[rel] = 2
        order.append(rel)
        # Only now, with this file emitted, is it safe to follow a re-export
        # back into something that includes us.
        for dep in reexports:
            if state.get(dep) != 2:
                resolved_reexports.append((rel, dep))
            visit(dep)

    for root in roots:
        visit(root)
    return order, bodies, resolved_reexports


COND_OPEN = re.compile(r'^\s*#\s*(if|ifdef|ifndef)\b')
COND_CLOSE = re.compile(r'^\s*#\s*endif\b')


def strip_body(lines: list[str]) -> tuple[list[str], list[str]]:
    """Return (hoistable external includes, body with internal includes and pragma once removed).

    Only unconditional `#include <...>` is hoisted. One inside a preprocessor
    conditional is guarded for a reason -- `<intrin.h>` behind `_MSC_VER` is the
    live example -- and hoisting it out of that guard makes the amalgamation
    fail to compile everywhere the guard was protecting. Conditional includes
    stay exactly where they are.
    """
    externals: list[str] = []
    body: list[str] = []
    cond_depth = 0
    for line in lines:
        if PRAGMA_ONCE.match(line) or INTERNAL_INCLUDE.match(line):
            continue
        if COND_OPEN.match(line):
            cond_depth += 1
        elif COND_CLOSE.match(line):
            cond_depth = max(0, cond_depth - 1)
        if cond_depth == 0 and (m := EXTERNAL_INCLUDE.match(line)):
            externals.append(m.group(1))
            continue
        body.append(line)

    # Collapse the run of blank lines left where the include block used to be.
    trimmed: list[str] = []
    for line in body:
        if not line.strip() and trimmed and not trimmed[-1].strip():
            continue
        trimmed.append(line)
    while trimmed and not trimmed[0].strip():
        trimmed.pop(0)
    while trimmed and not trimmed[-1].strip():
        trimmed.pop()
    return externals, trimmed


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-o", "--output", required=True, type=pathlib.Path)
    parser.add_argument("--include-dir", default="include", type=pathlib.Path)
    parser.add_argument("--root", default="metl/metl.hpp",
                        help="umbrella header to start from (default: metl/metl.hpp)")
    parser.add_argument("--version", default="", help="version string to record in the banner")
    parser.add_argument("--date", default="",
                        help="ISO date for the banner; defaults to today. Pass a fixed value "
                             "for a reproducible build.")
    args = parser.parse_args()

    include_dir = args.include_dir.resolve()
    order, bodies, reexports = topological_order(include_dir, [args.root])
    for source, target in reexports:
        print(f"note: {source} re-exports {target} after its own code; "
              f"treated as a reachability edge, not an ordering one")

    externals: list[str] = []
    seen_external: set[str] = set()
    chunks: list[str] = []
    for rel in order:
        header_externals, body = strip_body(bodies[rel])
        for name in header_externals:
            if name not in seen_external:
                seen_external.add(name)
                externals.append(name)
        if body:
            chunks.append(f"// ===== {rel} " + "=" * max(0, 66 - len(rel)) + "\n\n" + "\n".join(body))

    stamp = args.date or datetime.date.today().isoformat()
    version = f" v{args.version}" if args.version else ""

    out: list[str] = [
        "// metl" + version + " -- single-header amalgamation.",
        "//",
        f"// Generated by tools/amalgamate.py on {stamp} from {len(order)} headers.",
        "// DO NOT EDIT: edit include/metl/*.hpp and regenerate. This file is built",
        "// in CI and compiled against the full test suite, so it is not a summary of",
        "// the library -- it is the library.",
        "//",
        "// https://github.com/hawk90/metl",
        "",
        "#pragma once",
        "",
    ]
    out += [f"#include <{name}>" for name in sorted(externals)]
    out += ["", ""]
    out.append("\n\n".join(chunks))
    out.append("")

    args.output.write_text("\n".join(out), encoding="utf-8")
    print(f"{args.output}: {len(order)} headers, {len(externals)} standard includes, "
          f"{sum(len(c.splitlines()) for c in chunks)} lines")
    return 0


if __name__ == "__main__":
    sys.exit(main())
