#!/usr/bin/env python3
"""Hold commit subjects and pull-request titles to Conventional Commits.

WHY. CONTRIBUTING.md has asked for Conventional Commits since the start, and
nothing checked it. Of the first 197 commits on main, 191 followed it -- but
they used fifteen different types, seven of them invented on the spot
(`deps`, `fuzz`, `harden`, `audit`, `security`, `github`, `release`), and the
type used most, `ci`, was not on CONTRIBUTING's list at all. A convention that
drifts like that cannot drive a changelog or a release note, which is the
point of having one.

THE RULE.

    <type>[(<scope>)][!]: <description>

  * type is one of TYPES below -- the Conventional Commits / Angular set.
    Dependency updates are `build(deps)` or `chore(deps)`, a release is
    `chore(release)`, a fuzz harness is `test(fuzz)`: the scope says where,
    the type says what kind of change.
  * scope is lowercase: letters, digits, `.`, `_`, `-`, `/`.
  * `!` marks a breaking change (after 1.0, a new major version).
  * the subject line is at most MAX_LENGTH characters and does not end in a
    period.

WHERE IT RUNS.

  * CI checks the pull-request TITLE (.github/workflows/pr-title.yml). The
    repository squash-merges with the PR title as the commit subject, so the
    title is what lands on main.
  * `pre-commit install` adds a commit-msg hook that checks each local commit,
    so a branch's own history follows the rule before it is pushed. Git's own
    `Merge ...`, `Revert "..."` and `fixup!`/`squash!`/`amend!` subjects pass
    there; they never reach main as-is.

Usage:
    tools/check_commit_message.py --title "fix(spsc_queue): correct memory order"
    tools/check_commit_message.py --file .git/COMMIT_EDITMSG    # commit-msg hook
    tools/check_commit_message.py --self-test
"""

import argparse
import re
import sys

TYPES = ("build", "chore", "ci", "docs", "feat", "fix", "perf", "refactor", "revert", "style",
         "test")
MAX_LENGTH = 72
SUBJECT = re.compile(r"^(?P<type>[a-z]+)(?:\((?P<scope>[^)]*)\))?(?P<breaking>!)?: (?P<text>.*)$")
SCOPE = re.compile(r"^[a-z0-9][a-z0-9._/-]*$")
# Subjects git writes itself. Allowed in local history (the commit-msg hook),
# never as a pull-request title.
GIT_GENERATED = re.compile(r"^(Merge |Revert \"|fixup! |squash! |amend! )")


def problems(subject, allow_git_generated=False):
    """Every way `subject` breaks the rule; empty when it complies."""
    if allow_git_generated and GIT_GENERATED.match(subject):
        return []
    found = []
    match = SUBJECT.match(subject)
    if match is None:
        return [f"`{subject}` is not `<type>[(<scope>)][!]: <description>`"]
    if match.group("type") not in TYPES:
        found.append(f"type `{match.group('type')}` is not one of: {', '.join(TYPES)}")
    scope = match.group("scope")
    if scope is not None and not SCOPE.match(scope):
        found.append(f"scope `{scope}` must be lowercase letters, digits, `.`, `_`, `-`, `/`")
    text = match.group("text")
    if not text.strip() or text != text.lstrip():
        found.append("the description is empty or starts with a space")
    if text.endswith("."):
        found.append("the subject ends with a period")
    if len(subject) > MAX_LENGTH:
        found.append(f"the subject is {len(subject)} characters; the limit is {MAX_LENGTH}")
    return found


def subject_of(message):
    """The first line of a commit message that is not a git comment."""
    for line in message.splitlines():
        if line.strip() and not line.startswith("#"):
            return line.rstrip()
    return ""


def self_test():
    good = [
        "feat(fixed_vector): add insert_range overload",
        "fix(spsc_queue): correct memory order on pop",
        "docs: document CMake integration",
        "ci(release): gate releases on main's CI",
        "chore(deps): update github/codeql-action action to v4.38.2",
        "refactor(detail/ring_core)!: drop the legacy index type",
        "build: require CMake 3.20",
    ]
    bad = {
        "Trim compile-time cost: drop <memory>": "is not `<type>",
        "github: issue forms": "type `github`",
        "release: 1.0.0": "type `release`",
        "fix(Spsc): wrong scope case": "scope `Spsc`",
        "fix: trailing period.": "period",
        "fix:no space after colon": "is not `<type>",
        "fix: " + "x" * 80: "characters",
        "docs+test: two types": "is not `<type>",
        "fix(): empty scope": "scope ``",
    }
    failures = []
    for subject in good:
        if problems(subject):
            failures.append(f"rejected a valid subject: {subject!r} -> {problems(subject)}")
    for subject, expected in bad.items():
        found = problems(subject)
        if not any(expected in problem for problem in found):
            failures.append(f"did not reject {subject!r} for {expected!r}: {found}")
    # Git-generated subjects: allowed for local commits, refused as PR titles.
    for subject in ("Merge branch 'main' into topic", 'Revert "fix: x"', "fixup! fix: x"):
        if problems(subject, allow_git_generated=True):
            failures.append(f"the commit-msg hook rejected git's own {subject!r}")
        if not problems(subject):
            failures.append(f"accepted git's own {subject!r} as a pull-request title")
    if subject_of("# comment\n\nfix: x\n\nbody\n") != "fix: x":
        failures.append("subject_of did not skip git's comment lines")
    if failures:
        print("SELF-TEST FAILED:\n  " + "\n  ".join(failures), file=sys.stderr)
        return 1
    print(f"self-test passed: {len(good)} valid subjects accepted, {len(bad)} invalid ones each "
          f"rejected for the right reason, git's own subjects pass only as local commits")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--title", help="a pull-request title or commit subject")
    mode.add_argument("--file", help="a commit message file (the commit-msg hook)")
    mode.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if args.file:
        with open(args.file, encoding="utf-8") as handle:
            subject = subject_of(handle.read())
        found = problems(subject, allow_git_generated=True)
    else:
        subject = args.title
        found = problems(subject)
    if found:
        print(f"::error::{subject!r} does not follow Conventional Commits:", file=sys.stderr)
        for problem in found:
            print(f"  - {problem}", file=sys.stderr)
        print("  See CONTRIBUTING.md, \"Commit messages\".", file=sys.stderr)
        return 1
    print(f"ok: {subject}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
