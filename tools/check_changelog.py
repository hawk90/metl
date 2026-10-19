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
      header does not need one. Nor does the release's bump of the version
      constants in include/metl/config.hpp, when the same branch adds the
      CHANGELOG section of that version (#401).

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
# The release bumps these; see C3.
VERSION_FILE = "include/metl/config.hpp"
VERSION_LINE = re.compile(r"^inline constexpr int version_(major|minor|patch) = (\d+);$")


def git(root, *args):
    """stdout of git in `root`. GIT_* is dropped: a pre-push hook exports
    GIT_DIR, and with it git answers about the hook's repository, not `root`."""
    env = {name: value for name, value in os.environ.items() if not name.startswith("GIT_")}
    return subprocess.run(["git", *args], cwd=root, capture_output=True, text=True, check=True,
                          env=env).stdout


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


def releases(root, merge_base):
    """True if this branch adds the CHANGELOG section of the version VERSION_FILE names."""
    path = root / VERSION_FILE
    if not path.is_file():
        return False
    parts = dict(match.groups() for match in map(VERSION_LINE.match, path.read_text(encoding="utf-8").splitlines())
                 if match)
    if set(parts) != {"major", "minor", "patch"}:
        return False
    heading = f"+## [{parts['major']}.{parts['minor']}.{parts['patch']}]"
    diff = git(root, "diff", "-U0", merge_base, "--", CHANGELOG)
    return any(line.startswith(heading) for line in diff.splitlines())


def code_lines_changed(root, base):
    """The added or removed lines under include/ that are not only comments.

    A release PR's bump of the version constants is not counted, so it needs no
    fragment, when the same branch adds that version's CHANGELOG section.
    """
    merge_base = git(root, "merge-base", base, "HEAD").strip()
    # --no-renames: a moved header is a deletion plus an addition. With rename
    # detection a pure move has no +/- lines and would need no fragment,
    # though it breaks every user's #include.
    diff = git(root, "diff", "-U0", "--no-renames", merge_base, "--", "include/")
    released = releases(root, merge_base)
    changed = []
    path = None
    for line in diff.splitlines():
        if line.startswith("+++ "):
            path = line[len("+++ b/"):] if line.startswith("+++ b/") else path
            continue
        if line.startswith("--- "):
            path = line[len("--- a/"):] if line.startswith("--- a/") else None
            continue
        if not line.startswith(("+", "-")):
            continue
        if released and path == VERSION_FILE and VERSION_LINE.match(line[1:]):
            continue
        if not is_comment_or_blank(line[1:]):
            changed.append(line)
    return changed, merge_base


def fragments_added(root, merge_base):
    diff = git(root, "diff", "--name-only", "--no-renames", "--diff-filter=A", merge_base, "--", FRAGMENTS)
    untracked = git(root, "ls-files", "--others", "--exclude-standard", "--", FRAGMENTS)
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
        try:
            changed, merge_base = code_lines_changed(root, base)
        except subprocess.CalledProcessError as error:
            problems.append(("C3", f"cannot diff against --base {base}: `{' '.join(error.cmd)}` failed: "
                                   f"{(error.stderr or '').strip() or error.returncode}. Fetch it "
                                   f"(CI checks out with fetch-depth: 0)"))
            return problems
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
    sections = [f"### {kind.capitalize()}\n\n" + "\n".join(entries)
                for kind, entries in by_type.items() if entries]
    if not sections:
        raise SystemExit(f"::error::{FRAGMENTS}/ holds no fragments; nothing to release")
    text = (root / CHANGELOG).read_text(encoding="utf-8")
    heading = f"## [{version}] - {date}"
    if f"## [{version}]" in text:
        raise SystemExit(f"::error::{CHANGELOG} already has a `## [{version}]` section")
    if UNRELEASED + "\n" not in text:
        # Without it the replace below writes nothing, and the fragments
        # deleted after it would be lost.
        raise SystemExit(f"::error::{CHANGELOG} has no `{UNRELEASED}` line to write the release under")
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
        (root / FRAGMENTS / "4-k.fixed.md").write_text("- `k` no longer leaks.\n")
        count = assemble(root, "1.1.0", "2026-02-02")
        text = (root / CHANGELOG).read_text()
        order = [text.find(s) for s in ("## [1.1.0] - 2026-02-02", "### Added", "### Changed", "### Fixed",
                                        "## [1.0.0]")]
        if count != 4 or -1 in order or order != sorted(order):
            failures.append(f"--assemble should write Added, Changed, Fixed under 1.1.0 in that order:\n{text}")
        if "- `g` no longer crashes.\n- `k` no longer leaks.\n" not in text:
            failures.append(f"--assemble should keep one section's entries a tight list, one per line (#400):\n{text}")
        if [p.name for p in (root / FRAGMENTS).iterdir()] != ["README.md"]:
            failures.append("--assemble should delete the fragments it used, and only those")
        if "Only the declaration moved" in text or "### Internal" in text:
            failures.append("--assemble should drop internal fragments from the notes")
        if check(root):
            failures.append(f"the assembled changelog should pass: {check(root)}")

    # C3 and a release (#401): the release PR bumps the version constants and
    # deletes the fragments. The bump needs no fragment when the same branch
    # adds the section of the version it names; other code still does.
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / FRAGMENTS).mkdir()
        (root / "include" / "metl").mkdir(parents=True)
        config = root / VERSION_FILE
        version = "inline constexpr int version_major = 1;\ninline constexpr int version_minor = {};\n" \
                  "inline constexpr int version_patch = 0;\n"
        config.write_text(version.format(0))
        (root / CHANGELOG).write_text(f"# Changelog\n\n{UNRELEASED}\n\n## [1.0.0] - 2026-01-01\n\n- old\n")
        (root / FRAGMENTS / "1-g.fixed.md").write_text("- `g` no longer crashes.\n")
        git(root, "init", "-q", "-b", "main")
        git(root, "add", "-A")
        git(root, "commit", "-q", "-m", "base")
        git(root, "checkout", "-q", "-b", "release")
        config.write_text(version.format(1))
        if [rule for rule, _ in check(root, base="main")] != ["C3"]:
            failures.append(f"a version bump without its section should still need a fragment: "
                            f"{check(root, base='main')}")
        assemble(root, "1.0.1", "2026-02-02")
        if [rule for rule, _ in check(root, base="main")] != ["C3"]:
            failures.append(f"a section for another version should not excuse the bump: {check(root, base='main')}")
        (root / CHANGELOG).write_text((root / CHANGELOG).read_text().replace("## [1.0.1]", "## [1.1.0]"))
        if check(root, base="main"):
            failures.append(f"a version bump with its section should need no fragment: {check(root, base='main')}")
        config.write_text(version.format(1) + "int f();\n")
        if [rule for rule, _ in check(root, base="main")] != ["C3"]:
            failures.append(f"a release should still need a fragment for other code: {check(root, base='main')}")

    # C3 and a moved header: renaming include/a.hpp changes every user's
    # #include line. git diff's rename detection reports it with no +/- lines
    # at all, so the diff must be read without it.
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / FRAGMENTS).mkdir()
        (root / "include").mkdir()
        (root / "include" / "a.hpp").write_text("int f();\nint g();\n")
        (root / CHANGELOG).write_text(f"# Changelog\n\n{UNRELEASED}\n\n## [1.0.0] - 2026-01-01\n\n- old\n")
        git(root, "init", "-q", "-b", "main")
        git(root, "add", "-A")
        git(root, "commit", "-q", "-m", "base")
        git(root, "checkout", "-q", "-b", "move")
        git(root, "mv", "include/a.hpp", "include/b.hpp")
        git(root, "commit", "-q", "-m", "move")
        if [rule for rule, _ in check(root, base="main")] != ["C3"]:
            failures.append(f"C3 should flag a header renamed without a fragment: {check(root, base='main')}")

        # Inside a git hook GIT_DIR names the hook's repository; the checks
        # must still read the tree they were given.
        with tempfile.TemporaryDirectory() as other:
            git(pathlib.Path(other), "init", "-q", "-b", "main")
            saved = os.environ.get("GIT_DIR")
            os.environ["GIT_DIR"] = str(pathlib.Path(other) / ".git")
            try:
                hooked = [rule for rule, _ in check(root, base="main")]
            except subprocess.CalledProcessError as error:
                hooked = [f"git failed: {error}"]
            finally:
                if saved is None:
                    del os.environ["GIT_DIR"]
                else:
                    os.environ["GIT_DIR"] = saved
            if hooked != ["C3"]:
                failures.append(f"with GIT_DIR set, C3 read another repository: {hooked}")

        # A base that does not resolve is a finding with a reason, not a traceback.
        try:
            missing = [message for rule, message in check(root, base="no-such-ref") if rule == "C3"]
        except subprocess.CalledProcessError as error:
            missing = [f"traceback: {error}"]
        if len(missing) != 1 or "no-such-ref" not in missing[0] or "traceback" in missing[0]:
            failures.append(f"an unknown --base should be one C3 problem naming it: {missing}")

        # --assemble with no `## [Unreleased]` heading has nowhere to write the
        # notes; it must refuse, and keep the fragments it could not place.
        (root / FRAGMENTS / "5-m.fixed.md").write_text("- `m` no longer hangs.\n")
        (root / CHANGELOG).write_text("# Changelog\n\n## [1.0.0] - 2026-01-01\n\n- old\n")
        try:
            assemble(root, "1.1.0", "2026-02-02")
            failures.append("--assemble without an Unreleased heading should refuse")
        except SystemExit:
            pass
        if not (root / FRAGMENTS / "5-m.fixed.md").exists():
            failures.append("--assemble deleted a fragment it never wrote into CHANGELOG.md")

    if failures:
        for failure in failures:
            print(f"::error::self-test: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: bad fragment names, empty fragments, entries under Unreleased and a code "
          "change without a fragment are rejected; comments need none; an internal fragment satisfies C3 and is dropped by --assemble, which orders and cleans up; "
          "a version bump needs no fragment only beside its own release section; a moved header needs one; "
          "GIT_DIR does not redirect the diff; an unknown base is a finding; --assemble keeps what it cannot place")
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
