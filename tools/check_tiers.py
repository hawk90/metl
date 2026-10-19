#!/usr/bin/env python3
"""Keep Tier 2 (include/metl/exp/) opt-in and labelled (docs/SCOPE.md section 3).

A Tier 2 type is outside the umbrella header and outside the Semantic
Versioning promise. That only holds if a user can always tell one apart and
never gets one without asking:

  T1  every header under exp/ carries the notice "experimental: may change or
      be removed in a minor release", so a reader of the header sees it.
  T2  every header under exp/ names its tracking issue ("Tracking issue: #N").
      SCOPE.md section 3 requires that issue to state, before the type lands,
      when it is promoted and when it is removed.
  T3  every header under exp/ declares into `metl::exp`, and no header outside
      exp/ opens a namespace `exp`: the namespace is how code that uses an
      experimental type says so.
  T4  no header outside exp/ includes an exp/ header. A Tier 0 type that
      depended on one would ship the experiment to every user; one that needs
      the implementation includes it from detail/, as handle_pool would use
      detail/bitset.hpp.

That the umbrella header includes no exp/ header is checked with the rest of
the umbrella rule, in cmake/CheckUmbrella.cmake.

Usage:
    tools/check_tiers.py [--include-dir include/metl]
    tools/check_tiers.py --self-test   # prove the checker still bites
"""

import argparse
import pathlib
import re
import sys

NOTICE = "experimental: may change or be removed in a minor release"
TRACKING = re.compile(r"Tracking issue: #\d+")
EXP_NAMESPACE = re.compile(r"\bnamespace\s+(?:metl\s*::\s*)?exp\s*\{")
EXP_INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]metl/exp/[^>"]+[>"]', re.M)
LINE_COMMENT = re.compile(r"//[^\n]*")
BLOCK_COMMENT = re.compile(r"/\*.*?\*/", re.S)


def code_only(text):
    return LINE_COMMENT.sub("", BLOCK_COMMENT.sub("", text))


def check(headers):
    """`headers` maps a path relative to include/metl (posix) to its text.
    Returns problems as (path, rule, message)."""
    problems = []
    for path, text in sorted(headers.items()):
        tier2 = path.startswith("exp/")
        code = code_only(text)
        if tier2:
            if NOTICE not in text:
                problems.append((path, "T1", f'missing the notice "{NOTICE}"'))
            if not TRACKING.search(text):
                problems.append((path, "T2", 'names no tracking issue ("Tracking issue: #N")'))
            if not EXP_NAMESPACE.search(code):
                problems.append((path, "T3", "declares nothing in namespace metl::exp"))
        else:
            if EXP_NAMESPACE.search(code):
                problems.append((path, "T3", "opens namespace exp outside include/metl/exp/"))
            for include in EXP_INCLUDE.findall(code):
                problems.append((path, "T4", f"includes a Tier 2 header: {include.strip()}"))
    return problems


GOOD_EXP = """#pragma once
/// experimental: may change or be removed in a minor release.
/// Tracking issue: #336
namespace metl {
namespace exp {
struct thing {};
}  // namespace exp
}  // namespace metl
"""

GOOD_CORE = """#pragma once
// a comment may mention metl/exp/ and namespace exp { freely
#include "metl/detail/bitset.hpp"
namespace metl { struct core {}; }
"""


def self_test():
    failures = []

    def rules(headers):
        return sorted(rule for _, rule, _ in check(headers))

    clean = {"exp/thing.hpp": GOOD_EXP, "core.hpp": GOOD_CORE}
    if check(clean):
        failures.append(f"rule-abiding headers were flagged: {check(clean)}")
    nested = {"exp/thing.hpp": GOOD_EXP.replace("namespace metl {\nnamespace exp {",
                                                "namespace metl::exp {")}
    if check(nested):
        failures.append("`namespace metl::exp {` was not accepted")

    cases = [
        ("T1", {"exp/thing.hpp": GOOD_EXP.replace(NOTICE, "experimental")}),
        ("T2", {"exp/thing.hpp": GOOD_EXP.replace("Tracking issue: #336", "")}),
        ("T3", {"exp/thing.hpp": GOOD_EXP.replace("namespace exp {", "namespace other {")}),
        ("T3", {"core.hpp": GOOD_CORE + "namespace metl { namespace exp { int x; } }\n"}),
        ("T4", {"core.hpp": GOOD_CORE + '#include <metl/exp/thing.hpp>\n'}),
        ("T4", {"detail/x.hpp": '#include "metl/exp/thing.hpp"\n'}),
    ]
    for rule, headers in cases:
        if rules(headers) != [rule]:
            failures.append(f"{rule} fixture reported {rules(headers)}, wanted [{rule!r}]")

    for failure in failures:
        print(f"self-test FAILED: {failure}", file=sys.stderr)
    if failures:
        return 1
    print("self-test passed: T1, T2, T3 and T4 bite, and rule-abiding headers are not flagged")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--include-dir", default="include/metl")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    root = pathlib.Path(args.include_dir)
    paths = sorted(root.rglob("*.hpp"))
    if not paths:
        print(f"::error::no headers under {root}", file=sys.stderr)
        return 2
    headers = {p.relative_to(root).as_posix(): p.read_text(encoding="utf-8") for p in paths}
    problems = check(headers)
    for path, rule, message in problems:
        print(f"::error file={root / path}::[{rule}] {message}", file=sys.stderr)
    if problems:
        print(f"{len(problems)} tier violation(s) across {len(headers)} headers. "
              f"See docs/SCOPE.md section 3.", file=sys.stderr)
        return 1
    tier2 = sum(1 for path in headers if path.startswith("exp/"))
    print(f"tiers hold: {tier2} Tier 2 header(s) under exp/ are labelled and opt-in, "
          f"across {len(headers)} headers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
