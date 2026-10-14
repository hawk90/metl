#!/usr/bin/env python3
"""Changelog fragments: one file per change, assembled at release (#272).

Every fix PR used to add a bullet to the same `## [Unreleased]` list in
CHANGELOG.md, so any two open fix PRs conflicted there. Each merge forced the
next PR to merge main, resolve the list, rerun pre_push.py and rerun CI. Now
each change adds its own file, and two PRs never touch the same one:

    changelog.d/<slug>.<type>.md

<slug> is the issue or a short name (`374-expected-destructor-reentry`), and
<type> is a Keep a Changelog section: added, changed, deprecated, removed,
fixed or security. The file holds one or more `- ` bullets, written as they
will read in the release notes. A change under include/ that no user can
notice (a refactor) uses the type `internal`: its bullet says why, and it is
dropped at release.

Checks (the default):
  C1  every file in changelog.d/ is named <slug>.<type>.md with a known type,
      and holds at least one `- ` bullet.
  C2  `## [Unreleased]` in CHANGELOG.md is empty. A bullet written there is the
      shared list this replaces.
  C3  with --base: a branch that changes code under include/ adds a fragment.
      Lines that are only comments do not count, so a documentation fix in a
      header does not need one.

At release, `--assemble X.Y.Z` writes the fragments under `## [X.Y.Z] - <date>`
in Keep a Changelog order and deletes them. The highlights and upgrade notes
are still written by hand, above the sections.

Usage:
    tools/check_changelog.py                       # C1, C2
    tools/check_changelog.py --base origin/main    # C1-C3
    tools/check_changelog.py --assemble 1.3.0      # at release
    tools/check_changelog.py --self-test           # prove the checker bites
"""

import argparse
import datetime
import os
import pathlib
import re
import subprocess
import sys
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent
FRAGMENTS = "changelog.d"
CHANGELOG = "CHANGELOG.md"
# Keep a Changelog's sections, in its order.
TYPES = ["added", "changed", "deprecated", "removed", "fixed", "security"]
# A change to include/ that no user can notice -- a refactor, a test hook --
# says so in a fragment of this type. It satisfies C3 and is dropped at release.
INTERNAL = "internal"
FRAGMENT_NAME = re.compile(r"^[a-z0-9][a-z0-9-]*\.(" + "|".join(TYPES + [INTERNAL]) + r")\.md$")
UNRELEASED = "## [Unreleased]"


def fragments(root):
    directory = root / FRAGMENTS
    return sorted(p for p in directory.iterdir() if p.name != "README.md") if directory.is_dir() else []


def unreleased_body(root):
    text = (root / CHANGELOG).read_text(encoding="utf-8")
    start = text.find(UNRELEASED)
    if start < 0:
        return None
    start += len(UNRELEASED)
    end = text.find("\n## ", start)
    return text[start:end if end >= 0 else len(text)]


def is_comment_or_blank(line):
    """A blank line, a `//` line, or a line of a `/* */` block.

    A block's inner lines start with `*` and a space or end there; a line that
    starts with `*` and an expression (`*out = value;`) is code.
    """
    content = line.strip()
    return (not content or content.startswith(("//", "/*", "*/"))
            or content == "*" or content.startswith("* "))


def code_lines_changed(root, base):
    """The added or removed lines under include/ that are not only comments."""
    merge_base = subprocess.run(["git", "merge-base", base, "HEAD"], cwd=root,
                                capture_output=True, text=True, check=True).stdout.strip()
    diff = subprocess.run(["git", "diff", "-U0", merge_base, "--", "include/"], cwd=root,
                          capture_output=True, text=True, check=True).stdout
    changed = []
    for line in diff.splitlines():
        if line.startswith(("+++", "---")) or not line.startswith(("+", "-")):
            continue
        if not is_comment_or_blank(line[1:]):
            changed.append(line)
    return changed, merge_base


def fragments_added(root, merge_base):
    diff = subprocess.run(["git", "diff", "--name-only", "--diff-filter=A", merge_base, "--", FRAGMENTS],
                          cwd=root, capture_output=True, text=True, check=True).stdout
    untracked = subprocess.run(["git", "ls-files", "--others", "--exclude-standard", "--", FRAGMENTS],
                               cwd=root, capture_output=True, text=True, check=True).stdout
    return [name for name in (diff + untracked).split() if name.endswith(".md")]


def check(root, base=None):
    problems = []
    for path in fragments(root):
        if not FRAGMENT_NAME.match(path.name):
            problems.append(("C1", f"{FRAGMENTS}/{path.name}: name it <slug>.<type>.md, "
                                   f"with <type> one of {', '.join(TYPES + [INTERNAL])}"))
        elif not any(line.startswith("- ") for line in path.read_text(encoding="utf-8").splitlines()):
            problems.append(("C1", f"{FRAGMENTS}/{path.name}: holds no `- ` bullet"))
    body = unreleased_body(root)
    if body is None:
        problems.append(("C2", f"{CHANGELOG} has no `{UNRELEASED}` heading"))
    elif body.strip():
        problems.append(("C2", f"`{UNRELEASED}` in {CHANGELOG} is not empty. Move each entry to "
                               f"its own file in {FRAGMENTS}/ (see tools/check_changelog.py)"))
    if base:
        changed, merge_base = code_lines_changed(root, base)
        if changed and not fragments_added(root, merge_base):
            problems.append(("C3", f"this branch changes {len(changed)} line(s) of code under include/ "
                                   f"and adds no {FRAGMENTS}/<slug>.<type>.md. First: {changed[0][:100]}"))
    return problems


def assemble(root, version, date):
    by_type = {kind: [] for kind in TYPES}
    for path in fragments(root):
        match = FRAGMENT_NAME.match(path.name)
        if match and match.group(1) != INTERNAL:
            by_type[match.group(1)].append(path.read_text(encoding="utf-8").strip())
    sections = [f"### {kind.capitalize()}\n\n" + "\n\n".join(entries)
                for kind, entries in by_type.items() if entries]
    if not sections:
        raise SystemExit(f"::error::{FRAGMENTS}/ holds no fragments; nothing to release")
    text = (root / CHANGELOG).read_text(encoding="utf-8")
    heading = f"## [{version}] - {date}"
    if f"## [{version}]" in text:
        raise SystemExit(f"::error::{CHANGELOG} already has a `## [{version}]` section")
    text = text.replace(UNRELEASED + "\n", f"{UNRELEASED}\n\n{heading}\n\n" + "\n\n".join(sections) + "\n", 1)
    (root / CHANGELOG).write_text(re.sub(r"\n{3,}", "\n\n", text), encoding="utf-8")
    for path in fragments(root):
        if FRAGMENT_NAME.match(path.name):
            path.unlink()
    return sum(len(entries) for entries in by_type.values())


def self_test():
    failures = []

    def git(root, *args):
        # A scratch repository: no hooks, no signing, an identity of its own,
        # and none of the GIT_* variables a pre-push hook exports.
        env = {name: value for name, value in os.environ.items() if not name.startswith("GIT_")}
        env.update(GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@t")
        subprocess.run(["git", "-c", "core.hooksPath=/dev/null", "-c", "commit.gpgsign=false", *args],
                       cwd=root, check=True, capture_output=True, env=env)

    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / FRAGMENTS).mkdir()
        (root / "include").mkdir()
        (root / "include" / "a.hpp").write_text("int f();\n")
        (root / CHANGELOG).write_text(f"# Changelog\n\n{UNRELEASED}\n\n## [1.0.0] - 2026-01-01\n\n- old\n")
        (root / FRAGMENTS / "README.md").write_text("How to write a fragment.\n")
        git(root, "init", "-q", "-b", "main")
        git(root, "add", "-A")
        git(root, "commit", "-q", "-m", "base")

        if check(root, base="main"):
            failures.append(f"a clean tree should pass: {check(root, base='main')}")

        # C1: a bad name, and a fragment with no bullet.
        (root / FRAGMENTS / "x.fix.md").write_text("- typo in the type\n")
        (root / FRAGMENTS / "y.fixed.md").write_text("no bullet\n")
        rules = sorted(rule for rule, _ in check(root))
        if rules != ["C1", "C1"]:
            failures.append(f"C1 should flag the bad name and the empty fragment: {rules}")
        (root / FRAGMENTS / "x.fix.md").unlink()
        (root / FRAGMENTS / "y.fixed.md").unlink()

        # C2: an entry written into the shared list.
        changelog = (root / CHANGELOG).read_text()
        (root / CHANGELOG).write_text(changelog.replace(UNRELEASED + "\n", UNRELEASED + "\n\n- direct entry\n"))
        if [rule for rule, _ in check(root)] != ["C2"]:
            failures.append(f"C2 should flag an entry under Unreleased: {check(root)}")
        (root / CHANGELOG).write_text(changelog)

        # C3: a comment-only header change needs no fragment; a code change does,
        # and a fragment satisfies it.
        git(root, "checkout", "-q", "-b", "topic")
        (root / "include" / "a.hpp").write_text("// Returns one.\nint f();\n")
        if check(root, base="main"):
            failures.append(f"a comment-only change should need no fragment: {check(root, base='main')}")
        (root / "include" / "a.hpp").write_text("// Returns one.\n/* A block\n * comment.\n */\nint f();\n")
        if check(root, base="main"):
            failures.append(f"a block comment should need no fragment: {check(root, base='main')}")
        # A dereference starts with `*` too, and is code.
        (root / "include" / "a.hpp").write_text("int f();\n*out = value;\n")
        if [rule for rule, _ in check(root, base="main")] != ["C3"]:
            failures.append(f"C3 should count `*out = value;` as code: {check(root, base='main')}")
        (root / "include" / "a.hpp").write_text("// Returns one.\nlong f();\n")
        if [rule for rule, _ in check(root, base="main")] != ["C3"]:
            failures.append(f"C3 should flag a code change without a fragment: {check(root, base='main')}")
        (root / FRAGMENTS / "1-f-returns-long.changed.md").write_text("- `f` returns `long`.\n")
        if check(root, base="main"):
            failures.append(f"a fragment should satisfy C3: {check(root, base='main')}")

        # --assemble: sections in Keep a Changelog order, fragments gone, the
        # README kept, Unreleased empty again.
        (root / FRAGMENTS / "1-f-returns-long.changed.md").unlink()
        (root / FRAGMENTS / "1-f-refactor.internal.md").write_text("- Only the declaration moved.\n")
        if check(root, base="main"):
            failures.append(f"an internal fragment should satisfy C3: {check(root, base='main')}")
        (root / FRAGMENTS / "1-f-returns-long.changed.md").write_text("- `f` returns `long`.\n")
        (root / FRAGMENTS / "2-g.fixed.md").write_text("- `g` no longer crashes.\n")
        (root / FRAGMENTS / "3-h.added.md").write_text("- `h`.\n")
        count = assemble(root, "1.1.0", "2026-02-02")
        text = (root / CHANGELOG).read_text()
        order = [text.find(s) for s in ("## [1.1.0] - 2026-02-02", "### Added", "### Changed", "### Fixed",
                                        "## [1.0.0]")]
        if count != 3 or -1 in order or order != sorted(order):
            failures.append(f"--assemble should write Added, Changed, Fixed under 1.1.0 in that order:\n{text}")
        if [p.name for p in (root / FRAGMENTS).iterdir()] != ["README.md"]:
            failures.append("--assemble should delete the fragments it used, and only those")
        if "Only the declaration moved" in text or "### Internal" in text:
            failures.append("--assemble should drop internal fragments from the notes")
        if check(root):
            failures.append(f"the assembled changelog should pass: {check(root)}")

    if failures:
        for failure in failures:
            print(f"::error::self-test: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: bad fragment names, empty fragments, entries under Unreleased and a code "
          "change without a fragment are rejected; comments need none; an internal fragment satisfies C3 and is dropped by --assemble, which orders and cleans up")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--base", help="also require a fragment if this branch changes code under include/")
    parser.add_argument("--assemble", metavar="VERSION", help="write the fragments into CHANGELOG.md")
    parser.add_argument("--date", default=datetime.date.today().isoformat(), help="release date for --assemble")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.assemble:
        count = assemble(REPO, args.assemble, args.date)
        print(f"wrote {count} fragment(s) under ## [{args.assemble}] - {args.date}; add the highlights and "
              f"upgrade notes above the sections by hand")
        return 0
    problems = check(REPO, args.base)
    for rule, message in problems:
        print(f"::error::{rule}: {message}", file=sys.stderr)
    if problems:
        return 1
    print(f"changelog OK: every fragment in {FRAGMENTS}/ is well formed, {UNRELEASED} is empty"
          + (", and a code change under include/ comes with a fragment" if args.base else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
