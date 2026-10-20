#!/usr/bin/env python3
"""Source-based coverage for the host test suite (Clang / llvm-cov).

Reports on include/metl only. The whole-run total that llvm-cov prints by
default also counts the test sources, which drags the number down and measures
the wrong thing: how well the tests cover themselves is not interesting.

WHAT THIS CANNOT SEE, and why the number is not the whole story:

  * `#if` arms that this configuration does not compile. Coverage describes the
    code the build produced, so a branch nobody builds does not appear as
    uncovered -- it does not appear at all. METL_CRC_TABLE=0 has its own CI job
    for that reason; see config-matrix.
  * `#if` arms of the other configurations, unless --configurations is given.
    Then each of CONFIGURATIONS is built and run too, and every line that only
    that configuration compiles must have run: the -fno-exceptions arms, the
    C++20 comparisons, the bitwise CRC loops. Their figures are not merged
    into this one, so they are a separate gate (#387).
  * Everything gated on being an MCU. irq_lock's PRIMASK path, the ARMv6-M
    capability rejections, the -fno-exceptions arms of expected.hpp -- none of
    that code exists in a host build. Its evidence is the qemu-conformance job,
    not this one.
  * Anything evaluated at COMPILE time. A constexpr function exercised only by
    static_assert never runs, so its lines read as uncovered: detail/crc.hpp
    reports 58% lines with 100% branches for exactly that reason. Those lines
    are recorded as compile-time in tools/coverage_records.txt, not test gaps
    -- do not "fix" them by adding runtime tests that duplicate a
    static_assert.
  * A constant condition reads as a MISSED branch. llvm-cov's lcov export
    writes the folded side of a branch -- the `while (false)` of every
    METL_ASSERT, METL_HARDEN, METL_DASSERT and METL_PANIC -- as a branch never
    taken. `llvm-cov report` and the record's own BRF/BRH leave it out
    (llvm/llvm-project#231521, #363). So the gated branch figure, and
    Codecov's partial lines, read low: on 106de55e7, 77.5% branches where an
    export that skips folded sides gives 85.7%. The floors are set against the
    figure as exported. Do not correct it by matching source text; wait for
    the fixed export and reset the floors from CI.

So: this measures host-visible RUNTIME branch depth. It is a floor on quality,
not a summary of it.

EVERY MISSED OR PARTIAL LINE IS EITHER TESTED OR RECORDED (#418). A floor lets
a new gap hide behind the margin, so each include/metl line that never ran, or
ran with a branch that never went one of its ways, is covered by a test or
recorded in tools/coverage_records.txt with the reason no test can reach it.
The record stays out of the headers, which users include: a header states its
contract, not how this repository measures it.

    include/metl/variant.hpp
        unreachable: <why, in the present tense>
            <the line's code as the header has it, without the indentation>

A line is named by its code, not its number, so an edit elsewhere in the
header does not invalidate the record; a header with the same code on several
gap lines lists it once for each. The kinds are RECORD_KINDS. A gap without a
record fails, and so does a recorded line that is no longer a gap: a record
that no longer describes a line is deleted, not kept as history. Two kinds of
gap need no record, because the export makes them and the code does not:
lines that hold no run-time code (a lone brace, a comment, a preprocessor
directive), and the constant condition of an assertion macro's
`while (false)` or of a `} while (0)` line (#363; see folded_first). Any other
untaken branch on such a line -- an assertion no test makes fail -- still
needs a test or a record.

MC/DC IS REPORTED, NOT GATED (#342). Branch coverage can reach 100% on
`if (a && b)` without ever showing that `b` alone changes the outcome. So one
more build is measured with -fcoverage-mcdc (Clang 18+), through the CMake
option METL_ENABLE_COVERAGE_MCDC. The report prints the share of conditions
shown to affect their decision independently, the weakest files, and the
decisions with conditions missing. It has no floor, by decision (2026-10-20):
the missed conditions are read and classified, not ratcheted. Read it with
these limits:

  * Clang caps the conditions in one decision and does not instrument a
    decision over the cap; it says so in a -Wpgo-coverage warning. Those
    decisions are absent from the figure, not counted as missed, so every one
    the build skipped is listed.
  * It is its own build because -fcoverage-mcdc moves the line and branch
    figures (+1 point of lines on Apple Clang 21, mostly constexpr headers),
    and those must stay the ones the floors were set against.
  * LLVM 23.1's llvm-cov crashes (SIGILL) on the MC/DC records of
    variant.hpp; Apple Clang 21 does not. A crash fails this run rather than
    printing no figure.

ONE BINARY AT A TIME. Every test is its own program, and the programs share
function names: each has a `main`, and every header function exists in every
binary that includes it, used or not. Neither llvm tool keys on the program:

  * llvm-profdata keys a record by (name, structural hash). Two tests' mains
    with the same hash and different counters failed to merge ("counter
    mismatch") and were dropped.
  * llvm-cov, given several -object binaries, keeps the FIRST record of each
    function name and skips the rest. A header function that the first binary
    only includes, never calls, comes as an empty record, so the run of every
    later binary was discarded: coro::stepper::poll() ran 20 times and read 0,
    and lines compiled only by later binaries were not reported at all. The
    result depended on the order of the binaries.

So each binary writes its profile under its own name (CMakeLists.txt), and is
exported on its own with only that profile. The include/metl records are then
merged here, per source line and per lcov branch, taking the highest count. A
function's record from a binary that includes it but never calls it is left
out when another binary uses it (see unused_records).
`llvm-cov export` numbers a line's branches by their position on the line,
across instantiations, so the same branch has the same key in every binary.
A test that compiles a line with its own macros can give it other branches;
those lines are printed, and their branches merged as lcov_merge() says.

ONE FILE, ONE FORMULA (#308). The gated figures are computed from the same
include/metl lcov export that Codecov receives, with Codecov's formula, so the
badge and the gate report the same number for the same commit:

  lines     hits / (hits + partials + misses), where a partial is a line that
            ran but has a branch that never went one of its ways. llvm-cov's
            own line figure counts a partial line as covered, which is why it
            read 91.5% while Codecov showed 87%; it is printed for reference.
  branches  branches taken / branches found, from the same lcov records.

THE NUMBERS ARE COMPILER-VERSION DEPENDENT. Different Clang releases lower the
same source to different numbers of instrumented branches -- this repo measured
1258 branches under Apple Clang and 1122 under Ubuntu's, giving 75.1% and 72.3%
for identical code. So a local run will not match CI, and the CI number is the
one the floor is set against. Do not chase a local figure.

Usage:
    tools/coverage.py [--min-lines N] [--min-branches N] [--lcov PATH] [--configurations]
    tools/coverage.py --check-records LCOV
    tools/coverage.py --self-test

--lcov writes the include/metl lcov file it measured, for Codecov to upload.
The floors here stay the gate; Codecov only displays.
"""

import argparse
import functools
import json
from concurrent.futures import ThreadPoolExecutor
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO / "build-coverage"

# The configurations whose `#if` arms the default build does not compile, each
# with the flags that select it. Each is measured on its own (#387), and each
# must compile at least one line the default build does not: one that compiles
# none has nothing for this gate to check.
#
# METL_HARDENING=0 is not here, for exactly that reason. It only REMOVES checks,
# so it compiles no line of its own; it sat here reporting "0 lines, 0 of them
# ran" on every run. The suite at that level runs in the sanitizers job
# (asan-ubsan-hardening-none).
CONFIGURATIONS = {
    "cxx20": "-std=c++20",
    "no-exceptions": "-fno-exceptions -fno-rtti",
    "crc-table-off": "-DMETL_CRC_TABLE=0",
}


# Clang's -Wpgo-coverage warning for a decision it leaves out of MC/DC: over the
# condition cap (or, before Clang 19, a nested boolean expression).
MCDC_SKIPPED = re.compile(r"^(?P<path>[^:\s][^:]*):(?P<line>\d+):(?P<col>\d+): warning: "
                          r"unsupported MC/DC boolean expression; (?P<why>.*?)(?: \[-W[\w-]+\])?$")

# The kind llvm-cov gives a region the preprocessor or a comment left out.
SKIPPED_REGION = 2

# How many of the weakest decisions the report lists.
MCDC_WEAKEST = 25

# Why a missed or partial line can have no test, for its record in RECORDS_FILE.
RECORD_KINDS = {
    "unreachable": "no input reaches it: the callers' preconditions or an earlier check exclude it",
    "compile-time": "it runs only during constant evaluation, or Clang folds the call out of the "
                    "profile while a test checks its value",
    "unmeasurable": "it runs, but the process ends without writing its profile (std::abort)",
    "artifact": "llvm-cov reports it wrong: a test runs it, and the record says which",
}
RECORDS_FILE = REPO / "tools" / "coverage_records.txt"

# A folded `while (false)` (#363): an assertion macro's invocation, or the
# `} while (0)` closing a statement macro. It is the line's first lcov branch
# when nothing before it on the line branches (see folded_first).
ASSERTION_MACRO = re.compile(r"\bMETL_(ASSERT|DASSERT|HARDEN|PANIC)\s*\(")
MACRO_LOOP_END = re.compile(r"^\s*\}\s*while\s*\((0|false)\)")
BRANCHING = re.compile(r"\b(if|while|for|switch|case)\b|\?|&&|\|\|")
# A member function's ref-qualifier, `f() &&` or `f() const&`: not a branch.
REF_QUALIFIER = re.compile(r"\)(\s*const)?\s*&&?(?=\s*(noexcept\b|\{|->))")


def find_tool(name):
    found = shutil.which(name)
    if found is None and shutil.which("xcrun"):
        result = subprocess.run(["xcrun", "--find", name], capture_output=True, text=True)
        found = result.stdout.strip() or None
    return found


def pct(total, missed):
    return 100.0 * (total - missed) / total if total else 0.0


def lcov_filtered(text, marker):
    """The lcov records whose source file path contains `marker`, paths made
    repo-relative so Codecov matches them to the tree without a path fix."""
    out, record, keep = [], [], False
    for line in text.splitlines():
        if line.startswith("SF:"):
            path = line[3:]
            keep = marker in path
            if keep and path.startswith(str(REPO) + os.sep):
                line = "SF:" + os.path.relpath(path, REPO)
        record.append(line)
        if line == "end_of_record":
            if keep:
                out.extend(record)
            record, keep = [], False
    return "\n".join(out) + "\n" if out else ""


def lcov_totals(text):
    """Line and branch counts from lcov records, as Codecov counts them.

    Returns (hits, partials, misses, branches_hit, branches_found). A line is a
    partial when it ran and one of its branches never went one of its ways
    (BRDA taken count 0 or '-')."""
    hits = partials = misses = branches_hit = branches_found = 0
    line_hits, line_has_untaken = {}, set()

    def flush():
        nonlocal hits, partials, misses
        for line, count in line_hits.items():
            if count == 0:
                misses += 1
            elif line in line_has_untaken:
                partials += 1
            else:
                hits += 1
        line_hits.clear()
        line_has_untaken.clear()

    for raw in text.splitlines():
        if raw.startswith("DA:"):
            fields = raw[3:].split(",")
            line, count = int(fields[0]), int(fields[1])
            line_hits[line] = max(line_hits.get(line, 0), count)
        elif raw.startswith("BRDA:"):
            line, _block, _branch, taken = raw[5:].split(",")
            branches_found += 1
            if taken not in ("-", "0"):
                branches_hit += 1
            else:
                line_has_untaken.add(int(line))
        elif raw == "end_of_record":
            flush()
    flush()
    return hits, partials, misses, branches_hit, branches_found


def line_counts(text):
    """{(repo-relative file, line): execution count} from lcov records."""
    counts, path = {}, None
    for raw in text.splitlines():
        if raw.startswith("SF:"):
            path = raw[3:]
        elif raw.startswith("DA:"):
            fields = raw[3:].split(",")
            key = (path, int(fields[0]))
            counts[key] = max(counts.get(key, 0), int(fields[1]))
    return counts


def carries_code(text):
    """Whether a source line holds code: not blank, a comment or a lone brace.
    Region boundaries differ between builds, so a configuration can report a
    blank line the default build does not."""
    stripped = text.strip()
    return bool(stripped) and not stripped.startswith(("//", "/*", "*")) and stripped not in ("{", "}", "};")


def configuration_only_unexecuted(default_lcov, configuration_lcov, source_line):
    """The code lines only `configuration_lcov` reports, and of those the ones
    that never ran. `source_line(path, number)` returns the line's text."""
    default = line_counts(default_lcov)
    only = {key: count for key, count in line_counts(configuration_lcov).items()
            if key not in default and carries_code(source_line(*key))}
    return only, sorted(key for key, count in only.items() if count == 0)


def repo_relative(path):
    return os.path.relpath(path, REPO) if path.startswith(str(REPO) + os.sep) else path


def mcdc_skipped(build_output, marker):
    """{(file, line, col): reason} for each decision under `marker` that Clang
    did not instrument. A header's decision is reported once per translation
    unit, so they are deduplicated."""
    skipped = {}
    for raw in build_output.splitlines():
        found = MCDC_SKIPPED.match(raw.strip())
        if found and marker in found["path"]:
            key = (repo_relative(found["path"]), int(found["line"]), int(found["col"]))
            skipped[key] = found["why"]
    return skipped


def mcdc_shown(export_json, marker):
    """{(file, line, col, conditions): (shown, vectors)} from one binary's
    `llvm-cov export -format=text`, for the files under `marker`: whether some
    record shows each condition, and the test vectors the records executed,
    each a (conditions, result) with None for a condition not evaluated. The
    vectors are None when this llvm-cov does not export them (Apple's 21 does
    not; LLVM 23 does).

    A record starts with its line and column. Its first list is one flag per
    condition; a later list, where the LLVM release writes one, holds the test
    vectors. The fields between them changed across releases and are not
    read."""
    shown = {}
    for data in json.loads(export_json)["data"]:
        for entry in data["files"]:
            path = repo_relative(entry["filename"])
            if marker not in path:
                continue
            for record in entry.get("mcdc_records", []):
                lists = [field for field in record if isinstance(field, list)]
                flags = lists[0]
                key = (path, record[0], record[1], len(flags))
                before, vectors = shown.get(key, ([False] * len(flags), set()))
                if len(lists) > 1 and vectors is not None:
                    vectors = vectors | {(tuple(vector["conditions"]), vector["result"])
                                         for vector in lists[1]
                                         if isinstance(vector, dict) and vector.get("executed")}
                else:
                    vectors = None
                shown[key] = ([a or b for a, b in zip(before, flags)], vectors)
    return shown


def independent_conditions(vectors, count):
    """Which of `count` conditions a pair of `vectors` shows independent: the
    results differ, and the condition is the only one both vectors evaluated
    and gave different values (llvm-cov's own rule)."""
    shown = [False] * count
    for conditions, result in vectors:
        for other, other_result in vectors:
            if result == other_result:
                continue
            differ = [index for index in range(count)
                      if conditions[index] is not None and other[index] is not None
                      and conditions[index] != other[index]]
            if len(differ) == 1:
                shown[differ[0]] = True
    return shown


def mcdc_summary(per_binary):
    """Merges mcdc_shown() of every binary. Returns (covered, count, files,
    decisions, pooled): condition totals, [(file, covered, count)] per file,
    [(file, line, col, uncovered, conditions)] per decision with a condition no
    test showed, and whether every record carried its test vectors.

    MC/DC is judged per source decision, as it is written: a template's
    decision has one record per instantiation and per binary, and the test
    vectors of all of them are pooled before the independence pairs are
    sought. Without the vectors, a condition counts when one record shows it,
    which reads lower for a decision whose instantiations each saw only one
    outcome. A decision written in a macro body is recorded where the macro is
    defined, so its uses share one location; there the vectors of different
    uses are not pooled, and a condition counts when some record shows it.
    llvm-cov's own totals are not used: they come from one binary per
    function (see ONE BINARY AT A TIME)."""
    shown, pooled = {}, {}
    for binary in per_binary:
        for key, (flags, vectors) in binary.items():
            before = shown.get(key, [False] * len(flags))
            shown[key] = [a or b for a, b in zip(before, flags)]
            pooled[key] = None if vectors is None or pooled.get(key, set()) is None \
                else pooled.get(key, set()) | vectors
    per_file, decisions = {}, []
    for key, flags in sorted(shown.items()):
        path, line, col, total = key
        if pooled[key] is not None and \
                not read_source_line(path, line).lstrip().startswith("#define"):
            flags = [a or b for a, b in zip(flags, independent_conditions(pooled[key], total))]
        hit, count = per_file.get(path, (0, 0))
        per_file[path] = (hit + sum(flags), count + total)
        if not all(flags):
            decisions.append((path, line, col, flags.count(False), total))
    files = [(path, hit, count) for path, (hit, count) in sorted(per_file.items())]
    return (sum(hit for _, hit, _ in files), sum(count for _, _, count in files), files,
            decisions, all(vectors is not None for vectors in pooled.values()))


def lcov_merge(texts):
    """Merges lcov exports of the same sources from different binaries into one.

    Per file, a line's count and a branch's count are the highest any binary
    reports ('-', never evaluated, is below 0); a function's FNDA likewise.

    A line's branches are merged only between binaries that give that line
    the same branch keys. Where they differ, a test compiled the line with its
    own macros -- METL_ASSERT is a `?:` with one branch pair, not an `if`
    with two, under the METL_HARDENING 0 that harden_floor_memory_test sets
    -- and merging by position would credit one expansion's branch to the
    other. The keys most binaries give are kept, the larger set on a tie.
    Returns (the merged lcov, [(file, line)] whose keys differ)."""
    parsed = []
    for text in texts:
        records, record = [], None
        for raw in text.splitlines():
            if raw.startswith("SF:"):
                record = {"path": raw[3:], "fn": {}, "fnda": {}, "da": {}, "brda": {}}
                records.append(record)
            elif raw.startswith("FN:"):
                line, name = raw[3:].split(",", 1)
                record["fn"].setdefault(name, int(line))
            elif raw.startswith("FNDA:"):
                count, name = raw[5:].split(",", 1)
                record["fnda"][name] = max(record["fnda"].get(name, 0), int(count))
            elif raw.startswith("DA:"):
                line, count = (int(field) for field in raw[3:].split(",")[:2])
                record["da"][line] = max(record["da"].get(line, 0), count)
            elif raw.startswith("BRDA:"):
                line, block, branch, taken = raw[5:].split(",")
                record["brda"].setdefault(int(line), {})[(int(block), int(branch))] = \
                    -1 if taken == "-" else int(taken)
        parsed.append(records)

    shapes = {}
    for records in parsed:
        for record in records:
            for line, branches in record["brda"].items():
                key = frozenset(branches)
                counts = shapes.setdefault((record["path"], line), {})
                counts[key] = counts.get(key, 0) + 1
    kept = {where: max(counts, key=lambda shape: (counts[shape], len(shape), sorted(shape)))
            for where, counts in shapes.items()}

    files = {}
    for records in parsed:
        for record in records:
            merged = files.setdefault(record["path"], {"fn": {}, "fnda": {}, "da": {},
                                                       "brda": {}})
            for name, line in record["fn"].items():
                merged["fn"].setdefault(name, line)
            for name, count in record["fnda"].items():
                merged["fnda"][name] = max(merged["fnda"].get(name, 0), count)
            for line, count in record["da"].items():
                merged["da"][line] = max(merged["da"].get(line, 0), count)
            for line, branches in record["brda"].items():
                if frozenset(branches) != kept[(record["path"], line)]:
                    continue
                for (block, branch), taken in branches.items():
                    key = (line, block, branch)
                    merged["brda"][key] = max(merged["brda"].get(key, -1), taken)
    differing = sorted(where for where, counts in shapes.items() if len(counts) > 1)
    out = []
    for path, record in sorted(files.items()):
        out.append(f"SF:{path}")
        for name, line in sorted(record["fn"].items(), key=lambda item: (item[1], item[0])):
            out.append(f"FN:{line},{name}")
        for name, line in sorted(record["fn"].items(), key=lambda item: (item[1], item[0])):
            out.append(f"FNDA:{record['fnda'].get(name, 0)},{name}")
        out.append(f"FNF:{len(record['fn'])}")
        out.append(f"FNH:{sum(1 for name in record['fn'] if record['fnda'].get(name, 0))}")
        for line, count in sorted(record["da"].items()):
            out.append(f"DA:{line},{count}")
        for (line, block, branch), taken in sorted(record["brda"].items()):
            out.append(f"BRDA:{line},{block},{branch},{'-' if taken < 0 else taken}")
        out.append(f"BRF:{len(record['brda'])}")
        out.append(f"BRH:{sum(1 for taken in record['brda'].values() if taken > 0)}")
        out.append(f"LF:{len(record['da'])}")
        out.append(f"LH:{sum(1 for count in record['da'].values() if count)}")
        out.append("end_of_record")
    return ("\n".join(out) + "\n" if out else ""), differing


def lcov_per_file(text):
    """{file: lcov_totals()} for each record of `text`."""
    totals, record = {}, []
    for raw in text.splitlines():
        record.append(raw)
        if raw == "end_of_record":
            totals[record[0][3:]] = lcov_totals("\n".join(record))
            record = []
    return totals


def print_mcdc(covered, count, files, decisions, pooled, skipped):
    """Prints the MC/DC report. Returns False when the export holds no MC/DC
    record at all, which means the measurement did not happen."""
    print("\n=== include/metl MC/DC (reported, not gated; #342) ===")
    if not count:
        print("FAIL: the MC/DC build exported no MC/DC record for include/metl")
        return False
    print(f"  conditions {pct(count, count - covered):6.2f}%  ({covered}/{count} shown to "
          f"affect their decision independently)")
    if not pooled:
        print("  this llvm-cov exports no test vectors, so a condition counts only when one "
              "instantiation shows it; a template whose instantiations each saw one outcome "
              "reads lower than its tests together show")
    print(f"  decisions not instrumented (left out of the figure): {len(skipped)}")
    for (path, line, col), why in sorted(skipped.items()):
        print(f"    {path}:{line}:{col}  {why}")
    print("  weakest files:")
    for path, hit, total in sorted(files, key=lambda f: (f[1] / f[2], f[0]))[:10]:
        print(f"    {pct(total, total - hit):6.2f}%  ({hit}/{total})  {path}")
    print(f"  decisions with conditions no instantiation showed independent "
          f"({len(decisions)} in all, most missing first, {min(len(decisions), MCDC_WEAKEST)} "
          f"shown):")
    for path, line, col, uncovered, total in sorted(
            decisions, key=lambda d: (-d[3], d[0], d[1], d[2]))[:MCDC_WEAKEST]:
        print(f"    {uncovered}/{total} missing  {path}:{line}:{col}  "
              f"{read_source_line(path, line).strip()[:70]}")
    return True


def configuration_failures(name, only, unexecuted, source_line):
    """FAIL lines for one configuration: each line only it compiles that no
    test ran -- and, if it compiles no line of its own, that, because then
    nothing was checked."""
    if not only:
        return [f"FAIL: the {name} configuration compiled no line the default build does not, so "
                f"there is nothing for it to check: its flags did not take effect, or it does not "
                f"belong in CONFIGURATIONS"]
    return [f"FAIL: {path}:{number} is compiled only in {name} and no test ran it: "
            f"{source_line(path, number).strip()}" for path, number in unexecuted]


def parse_records(text, name=RECORDS_FILE.name):
    """The records in RECORDS_FILE's `text`: ({path: [(kind, why, [code])]},
    [error]). A path starts at column 0, its `kind: why` lines are indented
    four spaces and their code lines eight; blank lines and lines starting
    with # are comments."""
    records, errors, path, group = {}, [], None, None
    for number, raw in enumerate(text.splitlines(), start=1):
        if not raw.strip() or raw.startswith("#"):
            continue
        indent = len(raw) - len(raw.lstrip(" "))
        body = raw.strip()
        if indent == 0:
            path, group = body, None
            if not path.startswith("include/metl/"):
                errors.append(f"{name}:{number}: {path!r} is not an include/metl header")
        elif indent == 4 and path is not None:
            kind, _, why = body.partition(":")
            if kind not in RECORD_KINDS:
                errors.append(f"{name}:{number}: unknown kind {kind!r}; the kinds are "
                              f"{', '.join(sorted(RECORD_KINDS))}")
            if not why.strip():
                errors.append(f"{name}:{number}: a record must say why after the kind")
            group = (kind, why.strip(), [], number)
            records.setdefault(path, []).append(group)
        elif indent == 8 and group is not None:
            group[2].append(body)
        else:
            errors.append(f"{name}:{number}: expected a path at column 0, `kind: why` at 4 or a "
                          f"code line at 8, under the ones before it")
    for path, groups in records.items():
        for kind, _, code, number in groups:
            if not code:
                errors.append(f"{name}:{number}: the {kind} record of {path} lists no line")
    return ({path: [group[:3] for group in groups] for path, groups in records.items()},
            errors)


def folded_first(text):
    """Whether a source line's first lcov branch is a folded `while (false)`:
    the line closes a statement macro, or invokes an assertion macro with no
    branch before it on the line (`void f() { METL_ASSERT(x); }` too)."""
    if MACRO_LOOP_END.match(text):
        return True
    found = ASSERTION_MACRO.search(text)
    return bool(found) and not BRANCHING.search(REF_QUALIFIER.sub(")", text[:found.start()]))


def lcov_untaken(text):
    """{(file, line): {(block, branch)}}: the lcov branches never taken, a
    count of 0 or '-' (never evaluated)."""
    untaken, path = {}, None
    for raw in text.splitlines():
        if raw.startswith("SF:"):
            path = raw[3:]
        elif raw.startswith("BRDA:"):
            line, block, branch, taken = raw[5:].split(",")
            if taken in ("-", "0"):
                untaken.setdefault((path, int(line)), set()).add((int(block), int(branch)))
    return untaken


def line_gap(count, untaken, text):
    """'missed', 'partial' or None for one source line `text` that ran
    `count` times with the `untaken` lcov branches, less what the export alone
    causes (see EVERY MISSED OR PARTIAL LINE)."""
    if not carries_code(text) or text.lstrip().startswith("#"):
        return None
    if folded_first(text):
        # The constant condition decides nothing, on either side; whether the
        # assertion ever passed is the next pair's second branch.
        untaken = {key for key in untaken if key[0] != 0}
    if count == 0:
        return "missed"
    return "partial" if untaken else None


def lcov_gaps(text, source_lines):
    """{(file, line): 'missed' | 'partial'} for the lines of `text`, by
    line_gap(). `source_lines(path)` returns a file's lines."""
    untaken, gaps = lcov_untaken(text), {}
    for (path, line), count in line_counts(text).items():
        lines = source_lines(path)
        gap = line_gap(count, untaken.get((path, line), set()),
                       lines[line - 1] if line <= len(lines) else "")
        if gap:
            gaps[(path, line)] = gap
    return gaps


def match_records(gaps, records, source_lines):
    """Matches parse_records()' `records` to lcov_gaps() by code text. Returns
    (stale, unrecorded): [(file, kind, code)] recorded but no longer a gap of
    its file, and [(file, line, code)] gaps no record names, in line order. A
    code line on several gap lines is matched once for each time it is
    recorded."""
    open_lines = {}
    for path, line in sorted(gaps):
        open_lines.setdefault((path, source_lines(path)[line - 1].strip()), []).append(line)
    stale = []
    for path, groups in records.items():
        for kind, _, code in groups:
            for text in code:
                if open_lines.get((path, text)):
                    open_lines[(path, text)].pop(0)
                else:
                    stale.append((path, kind, text))
    unrecorded = sorted((path, line, text) for (path, text), lines in open_lines.items()
                        for line in lines)
    return stale, unrecorded


def record_failures(gaps, records, source_lines):
    """FAIL lines for match_records(): each stale record, then each unrecorded
    gap."""
    stale, unrecorded = match_records(gaps, records, source_lines)
    return ([f"FAIL: {path}: `{text}` is recorded as {kind} but is no longer a missed or "
             f"partial line; delete the record" for path, kind, text in stale]
            + [f"FAIL: {path}:{line} is {gaps[(path, line)]} and has no record in "
               f"{RECORDS_FILE.relative_to(REPO)}: {text[:70]}" for path, line, text in unrecorded])


def recorded_counts(records):
    """{kind: number of code lines} over parse_records()' `records`."""
    counts = {}
    for groups in records.values():
        for kind, _, code in groups:
            counts[kind] = counts.get(kind, 0) + len(code)
    return counts


def records_checked(lcov, records_text):
    """Checks `records_text`, RECORDS_FILE's content, against `lcov` and
    prints the result. Returns whether every gap is recorded and every record
    still names one."""
    records, errors = parse_records(records_text)
    gaps = lcov_gaps(lcov, source_lines)
    failures = [f"FAIL: {error}" for error in errors] + record_failures(gaps, records, source_lines)
    print("\n=== missed and partial lines: each is tested or recorded (#418) ===")
    print(f"  missed or partial code lines: {len(gaps)}; recorded: "
          + (", ".join(f"{count} {kind}" for kind, count in sorted(recorded_counts(records).items()))
             or "none"))
    for failure in failures:
        print(failure)
    return not failures


@functools.lru_cache(maxsize=None)
def source_lines(path):
    """A repository file's lines, read once; none when it cannot be read."""
    try:
        return tuple((REPO / path).read_text().splitlines())
    except OSError:
        return ()


def read_source_line(path, number):
    lines = source_lines(path)
    return lines[number - 1] if 0 < number <= len(lines) else ""


SELF_TEST_LCOV = """SF:include/metl/a.hpp
DA:1,5
DA:2,0
DA:3,2
DA:4,1
BRDA:3,0,0,2
BRDA:3,0,1,0
BRDA:4,0,0,1
BRDA:4,0,1,-
BRDA:5,0,0,3
end_of_record
SF:include/metl/b.hpp
DA:1,1
DA:1,0
end_of_record
"""


def self_test():
    # a.hpp: line 1 hit, 2 missed, 3 and 4 partial (a branch never taken);
    # b.hpp: a line listed twice counts once, as hit. 5 branches, 3 taken.
    got = lcov_totals(SELF_TEST_LCOV)
    want = (2, 2, 1, 3, 5)
    if got != want:
        print(f"SELF-TEST FAILED: lcov_totals gave {got}, expected {want}", file=sys.stderr)
        return 1
    # A configuration adds lines 7-9 to a.hpp: 7 ran, 8 did not, 9 is blank.
    default = "SF:include/metl/a.hpp\nDA:1,1\nDA:2,0\nend_of_record\n"
    other = "SF:include/metl/a.hpp\nDA:1,0\nDA:7,3\nDA:8,0\nDA:9,0\nend_of_record\n"
    source = {1: "int f();", 7: "  throw_or_abort();", 8: "  std::abort();", 9: ""}
    only, unexecuted = configuration_only_unexecuted(default, other, lambda _path, n: source.get(n, ""))
    if sorted(only) != [("include/metl/a.hpp", 7), ("include/metl/a.hpp", 8)] or \
            unexecuted != [("include/metl/a.hpp", 8)]:
        print(f"SELF-TEST FAILED: configuration-only lines {sorted(only)}, unexecuted {unexecuted}",
              file=sys.stderr)
        return 1
    # MC/DC: the LLVM 18 record layout and a later, wider one; a test file left
    # out. Line 3 has two instantiations that each miss a condition, line 5 two
    # that together show both; line 7 misses two in one binary, and a second
    # binary shows one of them. On line 9 neither instantiation shows a
    # condition, but their test vectors together show both: one only ever
    # found the key, the other only missed it.
    first = json.dumps({"data": [{"files": [
        {"filename": str(REPO / "include/metl/a.hpp"),
         "mcdc_records": [[3, 9, 3, 20, 0, 5, [True, False], []],
                          [3, 9, 3, 20, 0, 5, [True, False], []],
                          [5, 9, 5, 20, 0, 5, [True, False], []],
                          [5, 9, 5, 20, 0, 5, [False, True], []],
                          [7, 5, 7, 30, 1, 1, 0, 0, 5, [False, False, True], [{"result": True}]],
                          [9, 9, 9, 40, 1, 0, 0, 0, 5, [False, False],
                           [{"conditions": [True, True], "executed": True, "result": True}]],
                          [9, 9, 9, 40, 2, 0, 0, 0, 5, [False, False],
                           [{"conditions": [False, None], "executed": True, "result": False},
                            {"conditions": [True, False], "executed": True, "result": False},
                            {"conditions": [True, True], "executed": False, "result": True}]]]},
        {"filename": str(REPO / "include/metl/b.hpp"), "mcdc_records": []},
        {"filename": str(REPO / "tests/t.cpp"),
         "mcdc_records": [[1, 1, 1, 9, 0, 5, [False, False]]]},
    ]}]})
    second = json.dumps({"data": [{"files": [
        {"filename": str(REPO / "include/metl/a.hpp"),
         "mcdc_records": [[7, 5, 7, 30, 1, 1, 0, 0, 5, [True, False, False], []]]},
    ]}]})
    got = mcdc_summary([mcdc_shown(first, "include/metl/"), mcdc_shown(second, "include/metl/")])
    want = (7, 9, [("include/metl/a.hpp", 7, 9)],
            [("include/metl/a.hpp", 3, 9, 1, 2), ("include/metl/a.hpp", 7, 5, 1, 3)], True)
    if got != want:
        print(f"SELF-TEST FAILED: mcdc_summary gave {got}, expected {want}", file=sys.stderr)
        return 1
    # An llvm-cov that writes no test vectors: nothing is pooled, and it says so.
    bare = json.dumps({"data": [{"files": [
        {"filename": str(REPO / "include/metl/a.hpp"),
         "mcdc_records": [[9, 9, 9, 40, 1, 0, 0, 0, 5, [False, False]],
                          [9, 9, 9, 40, 0, 2, 0, 0, 5, [False, False]]]}]}]})
    got = mcdc_summary([mcdc_shown(bare, "include/metl/"), mcdc_shown(first, "include/metl/")])
    if got[4] or ("include/metl/a.hpp", 9, 9, 2, 2) not in got[3]:
        print(f"SELF-TEST FAILED: mcdc_summary without vectors gave {got}", file=sys.stderr)
        return 1
    # Two binaries' exports of a.hpp. Line 2 ran only in the second (the first
    # has the function as an unused, empty record); line 4 only the first
    # compiles; the branch on line 2 went one way in each binary, and line 6
    # has a different branch shape in each. b.hpp is only in the second.
    one = ("SF:include/metl/a.hpp\nFN:1,f\nFNDA:0,f\nFNF:1\nFNH:0\nDA:2,0\nDA:4,1\n"
           "BRDA:2,0,0,0\nBRDA:2,0,1,3\nBRDA:6,0,0,1\nBRF:3\nBRH:2\nLF:2\nLH:1\nend_of_record\n")
    two = ("SF:include/metl/a.hpp\nFN:1,f\nFNDA:5,f\nFNF:1\nFNH:1\nDA:2,5\n"
           "BRDA:2,0,0,2\nBRDA:2,0,1,-\nBRDA:6,0,0,1\nBRDA:6,1,1,1\nBRF:4\nBRH:3\nLF:1\nLH:1\n"
           "end_of_record\nSF:include/metl/b.hpp\nDA:1,0\nend_of_record\n")
    # A third binary compiles line 6 as the first does, so their one branch
    # is kept and the second's two are not merged into it.
    three = "SF:include/metl/a.hpp\nDA:6,7\nBRDA:6,0,0,0\nend_of_record\n"
    merged, differing = lcov_merge([one, two])
    want = ("SF:include/metl/a.hpp\nFN:1,f\nFNDA:5,f\nFNF:1\nFNH:1\nDA:2,5\nDA:4,1\n"
            "BRDA:2,0,0,2\nBRDA:2,0,1,3\nBRDA:6,0,0,1\nBRDA:6,1,1,1\nBRF:4\nBRH:4\nLF:2\nLH:2\n"
            "end_of_record\nSF:include/metl/b.hpp\nFNF:0\nFNH:0\nDA:1,0\nBRF:0\nBRH:0\nLF:1\n"
            "LH:0\nend_of_record\n")
    if merged != want or differing != [("include/metl/a.hpp", 6)]:
        print(f"SELF-TEST FAILED: lcov_merge gave\n{merged}differing {differing}", file=sys.stderr)
        return 1
    merged, _ = lcov_merge([one, two, three])
    if "BRDA:6,0,0,1\nBRF:3\n" not in merged or "DA:6,7" not in merged:
        print(f"SELF-TEST FAILED: lcov_merge with three binaries gave\n{merged}", file=sys.stderr)
        return 1
    warning = ("unsupported MC/DC boolean expression; number of conditions (7) exceeds max (6). "
               "Expression will not be covered [-Wpgo-coverage]")
    build = (f"{REPO}/include/metl/a.hpp:12:20: warning: {warning}\n"
             f"{REPO}/include/metl/a.hpp:12:20: warning: {warning}\n"
             f"{REPO}/tests/t.cpp:4:1: warning: {warning}\n"
             f"{REPO}/include/metl/b.hpp:3:3: warning: unused variable 'x' [-Wunused-variable]\n")
    got = mcdc_skipped(build, "include/metl/")
    want = {("include/metl/a.hpp", 12, 20):
            "number of conditions (7) exceeds max (6). Expression will not be covered"}
    if got != want:
        print(f"SELF-TEST FAILED: mcdc_skipped gave {got}, expected {want}", file=sys.stderr)
        return 1
    # Binary 1 uses f (lines 1-3, line 2 skipped) and only includes g and h.
    # Binary 2 uses g, whose own record skips line 5, and includes h, whose
    # record there skips the comment on line 11. g's unused record in binary 1
    # is left out except line 3, which f covers; h, used nowhere, keeps the
    # lines both its records list.
    export = json.dumps({"data": [{"functions": [
        {"name": "f", "filenames": [str(REPO / "include/metl/a.hpp")],
         "regions": [[1, 1, 3, 2, 1, 0, 0, 0], [2, 1, 2, 9, 0, 0, 0, SKIPPED_REGION]]},
        {"name": "g", "filenames": [str(REPO / "include/metl/a.hpp")],
         "regions": [[3, 1, 6, 2, 0, 0, 0, 0]]},
        {"name": "h", "filenames": [str(REPO / "include/metl/a.hpp")],
         "regions": [[10, 1, 12, 2, 0, 0, 0, 0]]},
        {"name": "t", "filenames": [str(REPO / "tests/t.cpp")],
         "regions": [[1, 1, 9, 2, 1, 0, 0, 0]]},
    ]}]})
    first = function_lines(export, {"f", "t"}, "include/metl/")
    if first["t"] != (True, set(), set()) or first["g"][0] or \
            first["f"][2] != {("include/metl/a.hpp", 2)}:
        print(f"SELF-TEST FAILED: function_lines gave {first}", file=sys.stderr)
        return 1
    # Binary 2 also includes k, used nowhere, over lines 20-22, and uses m,
    # whose record skips line 21.
    second = {"g": (True, {("include/metl/a.hpp", line) for line in (3, 4, 6)}, set()),
              "h": (False, {("include/metl/a.hpp", line) for line in (10, 12)}, set()),
              "k": (False, {("include/metl/a.hpp", line) for line in (20, 21, 22)}, set()),
              "m": (True, {("include/metl/a.hpp", line) for line in (20, 22)},
                    {("include/metl/a.hpp", 21)})}
    got = unused_records([first, second])
    want = [({("include/metl/a.hpp", line) for line in (4, 5, 6, 11)}, {"g"}),
            ({("include/metl/a.hpp", 21)}, set())]
    if got != want:
        print(f"SELF-TEST FAILED: unused_records gave {got}, expected {want}", file=sys.stderr)
        return 1
    kept = lcov_without("SF:include/metl/a.hpp\nFN:1,f\nFN:3,g\nFNDA:1,f\nFNDA:0,g\nDA:1,1\n"
                        "DA:4,0\nBRDA:4,0,0,-\nBRDA:1,0,0,1\nend_of_record\n", want[0][0], {"g"})
    if kept != ("SF:include/metl/a.hpp\nFN:1,f\nFNDA:1,f\nDA:1,1\nBRDA:1,0,0,1\n"
                "end_of_record\n"):
        print(f"SELF-TEST FAILED: lcov_without gave\n{kept}", file=sys.stderr)
        return 1
    if configuration_failures("fixture", only, unexecuted, lambda _path, n: source.get(n, "")) != [
            "FAIL: include/metl/a.hpp:8 is compiled only in fixture and no test ran it: std::abort();"]:
        print("SELF-TEST FAILED: a configuration-only line that never ran was not reported",
              file=sys.stderr)
        return 1
    # A configuration that compiles nothing of its own checks nothing.
    nothing, _ = configuration_only_unexecuted(default, default, lambda _path, n: source.get(n, ""))
    if not configuration_failures("fixture", nothing, [], lambda _path, _n: ""):
        print("SELF-TEST FAILED: a configuration with no lines of its own passed", file=sys.stderr)
        return 1
    # Only include/metl records are kept, with the repository prefix removed so
    # Codecov matches them to the tree; a test source's record is dropped.
    exported = (f"SF:{REPO}/include/metl/a.hpp\nDA:1,1\nend_of_record\n"
                f"SF:{REPO}/tests/a_test.cpp\nDA:1,0\nend_of_record\n")
    if lcov_filtered(exported, "include/metl/") != "SF:include/metl/a.hpp\nDA:1,1\nend_of_record\n":
        print(f"SELF-TEST FAILED: lcov_filtered kept {lcov_filtered(exported, 'include/metl/')!r}",
              file=sys.stderr)
        return 1
    if lcov_filtered(exported, "no-such-dir/"):
        print("SELF-TEST FAILED: lcov_filtered kept a record outside the marker", file=sys.stderr)
        return 1
    # Records. Line 2 is partial and recorded; 3 missed with no record; 4 a
    # brace; 5 an assertion whose only untaken branch is its constant
    # condition; 6 one whose trap side never ran; 7 and 8 the same code, both
    # missed, recorded once; 9 a record over a line that ran; 10 asserts in a
    # one-line body; on 11 an `if` comes first, so its untaken branch is a
    # gap; 12 closes a statement macro after a return, so its constant
    # condition never ran; 13 is a directive; on 14 the `&&` is an rvalue
    # ref-qualifier, not a branch.
    header = ["int f(int x) {",
              "  if (x == 3) {",
              "    return 0;",
              "  }",
              "  METL_ASSERT(x > 0);",
              "  METL_HARDEN(x < 9);",
              "  int t = g();",
              "  int t = g();",
              "  return x + t;",
              "  auto v = [] { METL_ASSERT(true); };",
              "  if (x) { METL_HARDEN(x != 0); }",
              "  } while (0)",
              "#endif",
              "  void value() && noexcept { METL_ASSERT(x); }",
              "}"]
    measured = ("SF:include/metl/a.hpp\nDA:1,2\nDA:2,2\nBRDA:2,0,0,2\nBRDA:2,0,1,0\nDA:3,0\nDA:4,0\n"
                "DA:5,2\nBRDA:5,0,0,0\nBRDA:5,0,1,2\nBRDA:5,1,2,1\nBRDA:5,1,3,2\n"
                "DA:6,2\nBRDA:6,0,0,0\nBRDA:6,0,1,2\nBRDA:6,1,2,-\nBRDA:6,1,3,2\n"
                "DA:7,0\nDA:8,0\nDA:9,2\nDA:10,2\nBRDA:10,0,0,0\nBRDA:10,0,1,2\nBRDA:10,1,2,1\n"
                "BRDA:10,1,3,2\nDA:11,2\nBRDA:11,0,0,0\nBRDA:11,0,1,2\nDA:12,2\nBRDA:12,0,0,-\n"
                "BRDA:12,0,1,-\nDA:13,0\nDA:14,2\nBRDA:14,0,0,0\nBRDA:14,0,1,1\nBRDA:14,1,2,1\n"
                "BRDA:14,1,3,1\nDA:15,2\nend_of_record\n")
    records, errors = parse_records("# a comment\n"
                                    "include/metl/a.hpp\n"
                                    "    unreachable: no caller passes 3\n"
                                    "        if (x == 3) {\n"
                                    "\n"
                                    "    compile-time: only a constexpr initializer calls g\n"
                                    "        int t = g();\n"
                                    "    artifact: a stale record\n"
                                    "        return x + t;\n")
    if errors or records != {"include/metl/a.hpp": [
            ("unreachable", "no caller passes 3", ["if (x == 3) {"]),
            ("compile-time", "only a constexpr initializer calls g", ["int t = g();"]),
            ("artifact", "a stale record", ["return x + t;"])]}:
        print(f"SELF-TEST FAILED: parse_records gave {records}, errors {errors}", file=sys.stderr)
        return 1
    gaps = lcov_gaps(measured, lambda _path: header)
    if gaps != {("include/metl/a.hpp", 2): "partial", ("include/metl/a.hpp", 3): "missed",
                ("include/metl/a.hpp", 6): "partial", ("include/metl/a.hpp", 7): "missed",
                ("include/metl/a.hpp", 8): "missed", ("include/metl/a.hpp", 11): "partial"}:
        print(f"SELF-TEST FAILED: lcov_gaps gave {gaps}", file=sys.stderr)
        return 1
    got = record_failures(gaps, records, lambda _path: header)
    want = ["FAIL: include/metl/a.hpp: `return x + t;` is recorded as artifact but is no longer a "
            "missed or partial line; delete the record",
            "FAIL: include/metl/a.hpp:3 is missed and has no record in tools/coverage_records.txt: "
            "return 0;",
            "FAIL: include/metl/a.hpp:6 is partial and has no record in tools/coverage_records.txt: "
            "METL_HARDEN(x < 9);",
            "FAIL: include/metl/a.hpp:8 is missed and has no record in tools/coverage_records.txt: "
            "int t = g();",
            "FAIL: include/metl/a.hpp:11 is partial and has no record in tools/coverage_records.txt: "
            "if (x) { METL_HARDEN(x != 0); }"]
    if got != want:
        print("SELF-TEST FAILED: record_failures gave\n  " + "\n  ".join(got), file=sys.stderr)
        return 1
    _, errors = parse_records("    unreachable: before any path\n"
                              "include/metl/a.hpp\n"
                              "    guess: x\n"
                              "        int a;\n"
                              "    unreachable:\n"
                              "        int b;\n"
                              "      odd indent\n"
                              "tests/x.cpp\n"
                              "    unreachable: why\n", name="r")
    if errors != ["r:1: expected a path at column 0, `kind: why` at 4 or a code line at 8, under "
                  "the ones before it",
                  "r:3: unknown kind 'guess'; the kinds are artifact, compile-time, unmeasurable, "
                  "unreachable",
                  "r:5: a record must say why after the kind",
                  "r:7: expected a path at column 0, `kind: why` at 4 or a code line at 8, under "
                  "the ones before it",
                  "r:8: 'tests/x.cpp' is not an include/metl header",
                  "r:9: the unreachable record of tests/x.cpp lists no line"]:
        print("SELF-TEST FAILED: malformed records gave\n  " + "\n  ".join(errors), file=sys.stderr)
        return 1
    print("self-test passed: partial lines and untaken branches are counted as Codecov counts them, "
          "only include/metl records are measured, a configuration's own code lines are found "
          "and checked (one with none fails), binaries are merged per line and branch without "
          "their unused records, MC/DC conditions and skipped decisions are read, and every "
          "missed or partial line needs a record that still names it")
    return 0


def test_binaries(build_dir):
    """The instrumented executables: those that carry a coverage mapping. A
    header check such as metl_header_odr_link is linked without the test
    options and has none."""
    return [binary for binary in sorted(build_dir.glob("metl_*"))
            if binary.is_file() and os.access(binary, os.X_OK)
            and b"__llvm_covmap" in binary.read_bytes()]


def function_lines(export_json, profiled, marker):
    """{name: (used, {(file, line)}, {(file, line)})} for the functions of one
    binary's `llvm-cov export -format=text`: the lines under `marker` their
    regions cover, and those their skipped regions cover (comments, `#if`
    arms not compiled, discarded `if constexpr` branches). `used` says whether
    the name is in the binary's own profile, `profiled`."""
    functions = {}
    for data in json.loads(export_json)["data"]:
        for function in data["functions"]:
            lines, skipped = set(), set()
            for line_start, _, line_end, _, _, file_id, _, kind in function["regions"]:
                path = repo_relative(function["filenames"][file_id])
                if marker in path:
                    (skipped if kind == SKIPPED_REGION else lines).update(
                        (path, line) for line in range(line_start, line_end + 1))
            name = function["name"]
            used, before, before_skipped = functions.get(name, (False, set(), set()))
            functions[name] = (used or name in profiled, before | lines, before_skipped | skipped)
    return functions


def unused_records(per_binary):
    """What to leave out of each binary's export: [(lines, names)], given
    function_lines() of every binary.

    A header function that a translation unit includes but never calls still
    gets a coverage record, with no profile behind it. Its line set is not
    the one the compiled function has: depending on the translation unit it
    also lists comments and the lines of `#if` arms that were not compiled
    (the MSVC and ARM paths of wait.hpp read as 42 missed lines). So:

      * a function some binary uses is reported only from the binaries that
        use it: its unused records, and their lines, are left out;
      * a function no binary uses is reported as never run, on the lines
        every one of its records lists, less the lines a used function's
        record skips. bit_ceil<unsigned char>, evaluated only in static_assert,
        is such a function, and bit_ceil<unsigned long>, which runs, marks its
        comments as skipped. A function no used record overlaps keeps every
        line its records share.

    A line a binary's own used function also covers is never left out."""
    used_names = {name for functions in per_binary
                  for name, (used, _, _) in functions.items() if used}
    skipped_when_used = set().union(*(skipped for functions in per_binary
                                      for used, _, skipped in functions.values() if used))
    everywhere = {}
    for functions in per_binary:
        for name, (used, lines, _) in functions.items():
            if not used and name not in used_names:
                everywhere[name] = everywhere[name] & lines if name in everywhere else set(lines)
    result = []
    for functions in per_binary:
        own = set().union(*(lines for used, lines, _ in functions.values() if used))
        drop, names = set(), set()
        for name, (used, lines, _) in functions.items():
            if used:
                continue
            if name in used_names:
                drop |= lines
                names.add(name)
            else:
                drop |= (lines - everywhere[name]) | (lines & skipped_when_used)
        result.append((drop - own, names))
    return result


def lcov_without(text, lines, names):
    """`text` without the DA and BRDA records of `lines` and the FN and FNDA
    records of the functions `names`. The per-record totals are left as they
    are: lcov_merge() recomputes them."""
    out, path = [], None
    for raw in text.splitlines():
        if raw.startswith("SF:"):
            path = raw[3:]
        elif raw.startswith(("DA:", "BRDA:")):
            if (path, int(raw.split(":", 1)[1].split(",", 1)[0])) in lines:
                continue
        elif raw.startswith(("FN:", "FNDA:")):
            if raw.split(",", 1)[1] in names:
                continue
        out.append(raw)
    return "\n".join(out) + "\n" if out else ""


def export_each(build_dir, binaries, profdata, cov, mcdc):
    """Per binary, with only its own profile: the include/metl lcov and, with
    `mcdc`, mcdc_shown(), both without its unused records (unused_records).
    Returns [(lcov, mcdc or None)], or None after saying why."""
    def run(command):
        done = subprocess.run(command, capture_output=True, text=True)
        # A warning such as "N functions have mismatched data" means records
        # were skipped, so it fails the run as an error does.
        if done.returncode or done.stderr.strip():
            raise RuntimeError(f"{Path(command[0]).name} {command[1]} (exit {done.returncode}): "
                               f"{done.stderr.strip()}")
        return done.stdout

    def one(binary):
        profile = build_dir / f"{binary.name}.profdata"
        raws = sorted((build_dir / "profraw").glob(f"{binary.name}-*.profraw"))
        if not raws:
            raise RuntimeError("no test ran it, so it wrote no profile")
        run([profdata, "merge", "-sparse", *map(str, raws), "-o", str(profile)])
        profiled = {line[2:-1] for line in run([profdata, "show", "--all-functions",
                                                str(profile)]).splitlines()
                    if line.startswith("  ") and not line.startswith("   ") and line.endswith(":")}
        as_lcov = run([cov, "export", "-format=lcov", str(binary), f"-instr-profile={profile}"])
        as_json = run([cov, "export", "-format=text", "-skip-expansions", str(binary),
                       f"-instr-profile={profile}"])
        return (lcov_filtered(as_lcov, "include/metl/"),
                function_lines(as_json, profiled, "include/metl/"),
                mcdc_shown(as_json, "include/metl/") if mcdc else None)

    def guarded(binary):
        try:
            return None, one(binary)
        except RuntimeError as error:
            return f"error: {binary.name}: {error}", None

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        results = list(pool.map(guarded, binaries))
    errors = [error for error, _ in results if error]
    for error in errors:
        print(error, file=sys.stderr)
    if errors:
        return None
    exports = [result for _, result in results]
    leave_out = unused_records([functions for _, functions, _ in exports])
    return [(lcov_without(lcov, lines, names),
             None if shown is None else
             {key: flags for key, flags in shown.items() if key[:2] not in lines})
            for (lcov, _, shown), (lines, names) in zip(exports, leave_out)]


def measure(build_dir, cxx_flags, profdata, cov, mcdc=False):
    """Builds `build_dir` instrumented with `cxx_flags`, runs every test and
    returns (the merged include/metl lcov, the lines whose branch keys differ
    between binaries, the per-binary MC/DC or None, the build's output), or
    None after saying why. `mcdc` adds -fcoverage-mcdc and reads its records."""
    prof_dir = build_dir / "profraw"
    shutil.rmtree(build_dir, ignore_errors=True)
    prof_dir.mkdir(parents=True)

    quiet = {"stdout": subprocess.DEVNULL}
    configure = ["cmake", "-B", str(build_dir), "-S", str(REPO), "-DCMAKE_BUILD_TYPE=Debug",
                 "-DMETL_ENABLE_COVERAGE=ON", "-DMETL_INSTALL=OFF"]
    if cxx_flags:
        configure.append(f"-DCMAKE_CXX_FLAGS={cxx_flags}")
    if mcdc:
        configure.append("-DMETL_ENABLE_COVERAGE_MCDC=ON")
    if subprocess.run(configure, **quiet).returncode:
        print(f"error: configuring {build_dir.name} failed", file=sys.stderr)
        return None
    built = subprocess.run(["cmake", "--build", str(build_dir), "-j"], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, text=True)
    if built.returncode:
        print(built.stdout)
        print(f"error: building {build_dir.name} failed", file=sys.stderr)
        return None

    # The binaries name their own profiles (CMakeLists.txt); an inherited
    # LLVM_PROFILE_FILE would send them all to one name again.
    env = {key: value for key, value in os.environ.items() if key != "LLVM_PROFILE_FILE"}
    ctest = subprocess.run(["ctest", "--no-tests=error", "--test-dir", str(build_dir), "-j4",
                            "--output-on-failure"], env=env, capture_output=True, text=True)
    (build_dir / "ctest.log").write_text(ctest.stdout + ctest.stderr)
    if ctest.returncode != 0:
        # The whole log: the failing test's own output is what explains it.
        print(ctest.stdout + ctest.stderr)
        print("error: tests failed under instrumentation; coverage numbers would be meaningless",
              file=sys.stderr)
        return None
    print("\n".join((ctest.stdout + ctest.stderr).splitlines()[-3:]))

    exported = export_each(build_dir, test_binaries(build_dir), profdata, cov, mcdc)
    if exported is None:
        return None
    lcov, differing = lcov_merge([lcov for lcov, _ in exported])
    return lcov, differing, [shown for _, shown in exported] if mcdc else None, built.stdout


def print_differing(differing):
    """Lists the lines whose branch keys differ between binaries; lcov_merge()
    kept the keys most binaries give."""
    if differing:
        print(f"  lines compiled with different branches by different binaries (the "
              f"branches most compile are kept): {len(differing)}")
        for path, line in differing:
            print(f"    {path}:{line}  {read_source_line(path, line).strip()[:70]}")


def configurations_ran(default_lcov, profdata, cov):
    """Measures each of CONFIGURATIONS and checks that every line only it
    compiles ran. Returns whether all did."""
    print("\n=== configurations (#387): the lines only each one compiles must run ===")
    all_ran = True
    for name, flags in CONFIGURATIONS.items():
        print(f"--- {name} ({flags})")
        measured = measure(REPO / f"build-coverage-{name}", flags, profdata, cov)
        if measured is None:
            print(f"FAIL: the {name} configuration did not build and run")
            all_ran = False
            continue
        lcov = measured[0]
        print_differing(measured[1])
        hits, partials, misses, br_hit, br_found = lcov_totals(lcov)
        only, unexecuted = configuration_only_unexecuted(default_lcov, lcov, read_source_line)
        lines = hits + partials + misses
        print(f"  lines {100.0 * hits / lines if lines else 0.0:6.2f}%   branches "
              f"{100.0 * br_hit / br_found if br_found else 0.0:6.2f}%   (for reference)")
        print(f"  lines only this configuration compiles: {len(only)}, "
              f"{len(only) - len(unexecuted)} of them ran")
        for failure in configuration_failures(name, only, unexecuted, read_source_line):
            print(failure)
            all_ran = False
    return all_ran


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--min-lines", type=float, default=92.1)
    parser.add_argument("--min-branches", type=float, default=86.0)
    parser.add_argument("--lcov", type=Path,
                        help="also write the include/metl lcov file it measured here")
    parser.add_argument("--configurations", action="store_true",
                        help="also build each of CONFIGURATIONS and require every line only it "
                             "compiles to have run")
    parser.add_argument("--check-records", type=Path, metavar="LCOV",
                        help="only check the coverage records against this include/metl lcov "
                             "file (one --lcov wrote), without building")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.check_records is not None:
        return 0 if records_checked(args.check_records.read_text(), RECORDS_FILE.read_text()) else 1
    sys.stdout.reconfigure(line_buffering=True)

    profdata, cov = find_tool("llvm-profdata"), find_tool("llvm-cov")
    if profdata is None or cov is None:
        print("error: llvm-profdata / llvm-cov not found on PATH", file=sys.stderr)
        return 2

    measured = measure(BUILD_DIR, "", profdata, cov)
    if measured is None:
        return 1
    lcov, differing = measured[0], measured[1]
    per_file = lcov_per_file(lcov)
    if not per_file:
        print("error: the lcov export has no include/metl record", file=sys.stderr)
        return 1
    (BUILD_DIR / "metl.lcov").write_text(lcov)
    if args.lcov is not None:
        args.lcov.write_text(lcov)
        print(f"lcov: {len(per_file)} include/metl file(s) written to {args.lcov}")
    print_differing(differing)

    table = ["=== per-file, weakest branch coverage first ==="]
    for path, (hit, partial, miss, br_hit, br_found) in sorted(
            ((path, totals) for path, totals in per_file.items() if totals[4]),
            key=lambda item: (item[1][3] / item[1][4], item[0])):
        table.append(f"  branches {100.0 * br_hit / br_found:7.2f}%   lines "
                     f"{100.0 * hit / (hit + partial + miss):6.2f}%   {path}")
    print("\n" + "\n".join(table))
    (BUILD_DIR / "report.txt").write_text("\n".join(table) + "\n")

    hits, partials, misses, br_hit, br_found = lcov_totals(lcov)
    functions = [int(line[4:]) for line in lcov.splitlines() if line.startswith(("FNF:", "FNH:"))]
    fn_found, fn_hit = sum(functions[0::2]), sum(functions[1::2])
    lines_pct = 100.0 * hits / (hits + partials + misses) if hits + partials + misses else 0.0
    branch_pct = 100.0 * br_hit / br_found if br_found else 0.0
    print("\n=== include/metl totals (gated, from the lcov file Codecov receives) ===")
    print(f"  lines     {lines_pct:6.2f}%  ({hits} hit, {partials} partial, {misses} missed)"
          f"   floor {args.min_lines:g}%")
    print(f"  branches  {branch_pct:6.2f}%  ({br_hit}/{br_found})   floor {args.min_branches:g}%")
    print("=== for reference ===")
    print(f"  lines     {pct(hits + partials + misses, misses):6.2f}%  (a partial line counted "
          f"as covered, as llvm-cov counts it)")
    print(f"  functions {pct(fn_found, fn_found - fn_hit):6.2f}%  ({fn_hit}/{fn_found})")

    failed = not records_checked(lcov, RECORDS_FILE.read_text())
    print("\n--- MC/DC build (-fcoverage-mcdc)")
    measured = measure(REPO / "build-coverage-mcdc", "", profdata, cov, mcdc=True)
    if measured is None:
        print("FAIL: the MC/DC build did not build, run and export")
        failed = True
    elif not print_mcdc(*mcdc_summary(measured[2]), mcdc_skipped(measured[3], "include/metl/")):
        failed = True
    if args.configurations and not configurations_ran(lcov, profdata, cov):
        failed = True
    if lines_pct < args.min_lines:
        print(f"\nFAIL: line coverage {lines_pct:.2f}% is below the {args.min_lines:g}% floor")
        failed = True
    if branch_pct < args.min_branches:
        print(f"\nFAIL: branch coverage {branch_pct:.2f}% is below the "
              f"{args.min_branches:g}% floor")
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
