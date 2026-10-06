#!/usr/bin/env python3
"""Decide whether a pull request is documentation-only.

Documentation-only pull requests skip the builds in ci.yml and the heavy steps
of CodeQL and PR fuzzing (.github/actions/classify-change calls this). A wrong
"documentation-only" therefore skips every verification the repository has, so
the classification is conservative: anything that is not provably Markdown or
LICENSE is code.

    code=false   a pull request whose every changed path is *.md or LICENSE
    code=true    everything else, including any non-PR event and an empty or
                 unreadable diff (a classification failure can only run more)

The diff is HEAD^1..HEAD of the merge commit GitHub builds for the pull request,
so HEAD^1 is the base branch tip and a fetch-depth: 2 checkout is enough.

RENAMES. `git diff --name-only` reports a rename by its NEW path only, so
renaming include/metl/x.hpp to x.md listed just `x.md` -- documentation-only --
while a header disappeared. The diff runs with --no-renames, which reports a
rename as a deletion plus an addition: the deleted .hpp makes it code.

Usage:
    tools/classify_change.py --event pull_request [--repo DIR]
    tools/classify_change.py --self-test

Writes `code=<true|false>` to $GITHUB_OUTPUT when it is set, else to stdout.
"""

import argparse
import os
import re
import subprocess
import sys
import tempfile

DOCUMENTATION = re.compile(r"(\.md|^LICENSE)$")


def changed_paths(repo):
    """Paths changed by the merge commit at HEAD, or None if unreadable."""
    result = subprocess.run(["git", "-C", repo, "diff", "--name-only", "--no-renames",
                             "HEAD^1", "HEAD"], capture_output=True, text=True)
    if result.returncode != 0:
        return None
    return [line for line in result.stdout.splitlines() if line.strip()]


def is_code(event, paths):
    if event != "pull_request":
        return True, "not a pull request: running everything"
    if not paths:
        return True, "no changed paths could be read: running everything"
    code = [path for path in paths if not DOCUMENTATION.search(path)]
    if code:
        return True, f"{len(code)} non-documentation path(s), e.g. {code[0]}"
    return False, "documentation-only change: the heavy steps will be skipped"


def _git(repo, *args):
    subprocess.run(["git", "-C", repo, *args], check=True, capture_output=True,
                   env=dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t",
                            GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@t"))


def _merged_repo(root, change):
    """A repo whose HEAD is a --no-ff merge of a branch that applied `change`."""
    repo = os.path.join(root, change.__name__)
    os.makedirs(os.path.join(repo, "include"))
    _git(repo, "init", "-q", "-b", "main")
    with open(os.path.join(repo, "include", "a.hpp"), "w") as handle:
        handle.write("".join(f"int value_{i};\n" for i in range(20)))
    with open(os.path.join(repo, "README.md"), "w") as handle:
        handle.write("readme\n")
    _git(repo, "add", "-A")
    _git(repo, "commit", "-qm", "base")
    _git(repo, "switch", "-q", "-c", "topic")
    change(repo)
    _git(repo, "add", "-A")
    _git(repo, "commit", "-qm", "change")
    _git(repo, "switch", "-q", "main")
    _git(repo, "merge", "-q", "--no-ff", "--no-edit", "topic")
    return repo


def self_test():
    def docs_and_license(repo):
        with open(os.path.join(repo, "README.md"), "a") as handle:
            handle.write("more\n")
        with open(os.path.join(repo, "LICENSE"), "w") as handle:
            handle.write("license\n")

    def docs_and_header(repo):
        docs_and_license(repo)
        with open(os.path.join(repo, "include", "a.hpp"), "a") as handle:
            handle.write("int more;\n")

    def nested_markdown(repo):
        os.makedirs(os.path.join(repo, "docs"))
        with open(os.path.join(repo, "docs", "guide.md"), "w") as handle:
            handle.write("guide\n")

    def rename_header_to_markdown(repo):
        _git(repo, "mv", "include/a.hpp", "a.md")

    cases = [
        (docs_and_license, "pull_request", False),
        (docs_and_header, "pull_request", True),
        (nested_markdown, "pull_request", False),
        (rename_header_to_markdown, "pull_request", True),
        (docs_and_license, "push", True),
    ]
    failures = []
    with tempfile.TemporaryDirectory() as root:
        for index, (change, event, want) in enumerate(cases):
            repo = _merged_repo(os.path.join(root, str(index)), change)
            got, reason = is_code(event, changed_paths(repo))
            if got != want:
                failures.append(f"{change.__name__} ({event}): code={got}, want {want} -- {reason}")
    if is_code("pull_request", [])[0] is not True:
        failures.append("an empty diff was classified documentation-only")
    if is_code("pull_request", None)[0] is not True:
        failures.append("an unreadable diff was classified documentation-only")
    if failures:
        print("SELF-TEST FAILED:\n  " + "\n  ".join(failures), file=sys.stderr)
        return 1
    print(f"self-test passed: {len(cases)} merge-commit cases classified correctly, including a "
          f"header renamed to .md; empty and unreadable diffs run everything")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--event", help="github.event_name")
    parser.add_argument("--repo", default=".", help="the checkout to classify")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.event:
        parser.error("--event is required")

    paths = changed_paths(args.repo) if args.event == "pull_request" else []
    if paths is not None:
        print("changed files:\n" + "\n".join(paths))
    code, reason = is_code(args.event, paths)
    print(reason)
    line = f"code={'true' if code else 'false'}\n"
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as handle:
            handle.write(line)
    else:
        sys.stdout.write(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
