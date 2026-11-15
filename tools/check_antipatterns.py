#!/usr/bin/env python3
"""Keep the anti-patterns in tools/antipatterns.yaml from coming back.

Each entry records a pattern that caused a METL bug, and names how it is
stopped:

  A1  the catalogue is well formed: unique kebab-case ids; a title, symptom,
      cause and fix; at least one `#N` reference; and a `check`, a
      `caught_by`, or both.
  A2  a `check` regex reports every line of the tree that matches it, in the
      files its `paths` globs select. Comments and #define lines are ignored.
      A hit that is right where it is must be listed in `allow` with a `why`.
      An `allow` entry must match exactly `count` (default 1) hits in its file,
      so the list cannot outlive the code it excuses.
  A3  every `bad` example matches the regex, and no `good` example does.
      Otherwise the regex is dead or too wide.
  A4  every `caught_by` names something that exists: a `job` that is a job id
      in a workflow under .github/workflows, or a `file` in the repository.

Usage:
    tools/check_antipatterns.py [--root .]
    tools/check_antipatterns.py --self-test   # prove the checker still bites
"""

import argparse
import pathlib
import re
import sys
import tempfile

try:
    import yaml
except ModuleNotFoundError:
    sys.exit("check_antipatterns.py needs PyYAML: `python3 -m pip install pyyaml`, "
             "or the python3-yaml package (see CONTRIBUTING.md, \"Before you push\")")

CATALOGUE = "tools/antipatterns.yaml"
ID_SHAPE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
REF_SHAPE = re.compile(r"^#[0-9]+$")
TEXT_FIELDS = ("title", "symptom", "cause", "fix")


def strip_comment(line):
    index = line.find("//")
    return line if index < 0 else line[:index]


def code_lines(text):
    """(line number, code) for each line, with comments and #define lines blanked."""
    for number, line in enumerate(text.splitlines(), 1):
        code = strip_comment(line)
        yield number, "" if "#define" in code else code


def workflow_jobs(root):
    jobs = set()
    for workflow in sorted((root / ".github" / "workflows").glob("*.yml")):
        document = yaml.safe_load(workflow.read_text(encoding="utf-8")) or {}
        jobs.update((document.get("jobs") or {}).keys())
    return jobs


def check_entry_shape(entry, seen):
    """A1 for one entry: the messages, without the entry's id."""
    problems = []
    pattern_id = entry.get("id")
    if not isinstance(pattern_id, str) or not ID_SHAPE.match(pattern_id):
        problems.append(f"id {pattern_id!r} is not kebab-case")
    elif pattern_id in seen:
        problems.append("id is used twice")
    for field in TEXT_FIELDS:
        if not isinstance(entry.get(field), str) or not entry[field].strip():
            problems.append(f"has no {field}")
    refs = entry.get("refs")
    if not refs or not all(isinstance(ref, str) and REF_SHAPE.match(ref) for ref in refs):
        problems.append(f"refs {refs!r} must be a non-empty list of '#N'")
    if "check" not in entry and "caught_by" not in entry:
        problems.append("has neither a check nor a caught_by: nothing stops it coming back")
    unknown = set(entry) - {"id", *TEXT_FIELDS, "refs", "check", "caught_by"}
    if unknown:
        problems.append(f"unknown fields {sorted(unknown)}")
    return problems


def compile_check(check):
    """The check's compiled regex, or a message saying why there is none."""
    for field in ("paths", "regex", "bad", "good"):
        if not check.get(field):
            return None, f"check has no {field}"
    try:
        return re.compile(check["regex"]), None
    except re.error as error:
        return None, f"check regex does not compile: {error}"


def check_examples(regex, check):
    """A3: the messages for examples on the wrong side of the regex."""
    problems = []
    for line in check["bad"]:
        if not regex.search(line):
            problems.append(f"bad example is not matched: {line!r}")
    for line in check["good"]:
        if regex.search(line):
            problems.append(f"good example is matched: {line!r}")
    return problems


def scan(root, regex, check):
    """A2: the hits not allowed, and the allow entries that are stale."""
    files = set()
    for glob in check["paths"]:
        files.update(path for path in root.glob(glob) if path.is_file())
    hits = []
    for path in sorted(files):
        relative = path.relative_to(root).as_posix()
        for number, code in code_lines(path.read_text(encoding="utf-8")):
            if regex.search(code):
                hits.append((relative, number, code.strip()))
    problems = []
    allowed = set()
    for allow in check.get("allow") or []:
        if not allow.get("why"):
            problems.append(f"allow entry for {allow.get('file')} has no why")
        matched = [hit for hit in hits if hit[0] == allow.get("file") and allow.get("match", "") in hit[2]]
        want = allow.get("count", 1)
        if len(matched) != want:
            problems.append(f"allow entry for {allow.get('file')} ({allow.get('match')!r}) matches "
                            f"{len(matched)} hits, not {want}: update or remove it")
        allowed.update(matched)
    unexpected = [f"{file}:{number}: {code}" for file, number, code in hits if (file, number, code) not in allowed]
    return unexpected, problems


def check_catalogue(root, catalogue):
    """Every violation, as 'id: message' strings."""
    violations = []
    patterns = (catalogue or {}).get("patterns")
    if (catalogue or {}).get("version") != 1 or not isinstance(patterns, list) or not patterns:
        return ["catalogue: needs `version: 1` and a non-empty `patterns` list"]
    jobs = workflow_jobs(root)
    seen = set()
    for entry in patterns:
        name = entry.get("id", "<no id>")
        violations += [f"{name}: {problem}" for problem in check_entry_shape(entry, seen)]
        seen.add(name)
        if "check" in entry:
            regex, problem = compile_check(entry["check"])
            if problem:
                violations.append(f"{name}: {problem}")
            else:
                violations += [f"{name}: {p}" for p in check_examples(regex, entry["check"])]
                unexpected, problems = scan(root, regex, entry["check"])
                violations += [f"{name}: {p}" for p in problems]
                violations += [f"{name}: {hit} -- {entry['title']}; fix: {entry['fix']}" for hit in unexpected]
        for target in entry.get("caught_by") or []:
            if "job" in target and target["job"] not in jobs:
                violations.append(f"{name}: caught_by job {target['job']!r} is not a job in .github/workflows")
            elif "file" in target and not (root / target["file"]).is_file():
                violations.append(f"{name}: caught_by file {target['file']!r} does not exist")
            elif not ({"job", "file"} & set(target)):
                violations.append(f"{name}: caught_by entry {target!r} names neither a job nor a file")
    return violations


def run(root):
    catalogue = yaml.safe_load((root / CATALOGUE).read_text(encoding="utf-8"))
    return check_catalogue(root, catalogue)


SELF_TEST_ENTRY = {
    "id": "canary",
    "title": "a canary",
    "symptom": "s",
    "cause": "c",
    "fix": "f",
    "refs": ["#1"],
    "check": {
        "paths": ["include/**/*.hpp"],
        "regex": r"\bbad_call\(\)",
        "bad": ["bad_call();"],
        "good": ["good_call();"],
    },
    "caught_by": [{"job": "build"}, {"file": "tests/a_test.cpp"}],
}


def self_test():
    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / ".github" / "workflows").mkdir(parents=True)
        (root / ".github" / "workflows" / "ci.yml").write_text("jobs:\n  build:\n    runs-on: x\n")
        (root / "include" / "metl").mkdir(parents=True)
        (root / "tests").mkdir()
        (root / "tests" / "a_test.cpp").write_text("int main() {}\n")
        header = root / "include" / "metl" / "x.hpp"

        def entry(**changes):
            result = {key: (dict(value) if isinstance(value, dict) else value)
                      for key, value in SELF_TEST_ENTRY.items()}
            for key, value in changes.items():
                if value is None:
                    result.pop(key, None)
                else:
                    result[key] = value
            return result

        def violations(*entries):
            return check_catalogue(root, {"version": 1, "patterns": list(entries)})

        header.write_text("good_call();\n// bad_call(); in a comment\n#define X bad_call()\n")
        if violations(entry()):
            failures.append(f"a clean tree was reported: {violations(entry())}")

        header.write_text("good_call();\n  bad_call();\n")
        found = violations(entry())
        if not any("include/metl/x.hpp:2:" in v for v in found):
            failures.append(f"A2: a matching line was NOT reported -- the scan is dead: {found}")

        allowed = entry(check={**SELF_TEST_ENTRY["check"],
                               "allow": [{"file": "include/metl/x.hpp", "match": "bad_call", "why": "w"}]})
        if violations(allowed):
            failures.append(f"A2: an allowed hit was reported: {violations(allowed)}")
        header.write_text("good_call();\n")
        if not any("matches 0 hits" in v for v in violations(allowed)):
            failures.append("A2: a stale allow entry was NOT reported")
        no_why = entry(check={**SELF_TEST_ENTRY["check"],
                              "allow": [{"file": "include/metl/x.hpp", "match": "bad_call"}]})
        header.write_text("bad_call();\n")
        if not any("has no why" in v for v in violations(no_why)):
            failures.append("A2: an allow entry without a why was NOT reported")
        header.write_text("good_call();\n")

        dead = entry(check={**SELF_TEST_ENTRY["check"], "bad": ["something_else();"]})
        if not any("bad example is not matched" in v for v in violations(dead)):
            failures.append("A3: a regex that misses its bad example was NOT reported")
        wide = entry(check={**SELF_TEST_ENTRY["check"], "good": ["bad_call();"]})
        if not any("good example is matched" in v for v in violations(wide)):
            failures.append("A3: a regex that matches its good example was NOT reported")

        shapes = {
            "neither a check nor a caught_by": entry(check=None, caught_by=None),
            "not kebab-case": entry(id="Not_Kebab"),
            "has no cause": entry(cause=None),
            "refs": entry(refs=["1"]),
            "unknown fields": entry(extra="x"),
        }
        for message, bad_entry in shapes.items():
            if not any(message in v for v in violations(bad_entry)):
                failures.append(f"A1: an entry that {message} was NOT reported")
        if not any("used twice" in v for v in violations(entry(), entry())):
            failures.append("A1: a duplicate id was NOT reported")

        if not any("is not a job" in v for v in violations(entry(caught_by=[{"job": "gone"}]))):
            failures.append("A4: a caught_by job that does not exist was NOT reported")
        if not any("does not exist" in v for v in violations(entry(caught_by=[{"file": "tests/gone.cpp"}]))):
            failures.append("A4: a caught_by file that does not exist was NOT reported")

    if failures:
        for failure in failures:
            print(f"self-test FAILED: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: a matching line, a stale or reasonless allow entry, a dead or too-wide regex, "
          "a malformed entry and a missing caught_by target are each reported; comments, #define lines "
          "and allowed hits are not")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", default=pathlib.Path(__file__).resolve().parent.parent, type=pathlib.Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    violations = run(args.root)
    for violation in violations:
        print(f"error: {violation}", file=sys.stderr)
    if violations:
        print(f"{len(violations)} anti-pattern violation(s); see {CATALOGUE}", file=sys.stderr)
        return 1
    print(f"anti-patterns: none of the {CATALOGUE} checks found a new hit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
