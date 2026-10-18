#!/usr/bin/env python3
"""Refuse to release a commit that main's verification has not passed.

release.yml runs one host build. What makes a commit releasable is what ran on
main for it: `ci-gate` (the fan-in of every ci.yml job -- GCC/Clang/MSVC,
sanitizers, ARM size/stack/RAM budgets, QEMU on four Cortex-M cores, Zephyr,
ESP-IDF) and `analyze / c-cpp` (CodeQL). A tag releases a commit only when

  * the commit is on main, and
  * every check in REQUIRED has a successful run on that commit, from a push
    to main in the workflow that produces it. A pull_request run reports on
    its head commit too, but it tested a merge with an older base, and a
    documentation-only pull request's ci-gate passes with every build skipped.

A check still running is waited for (main's CI takes 10-25 minutes, most of it
queueing for runners). A check that finished without success fails the release
at once.

A check that has not appeared yet is waited for while the workflow that
produces it is still running on this commit. `ci-gate` is a fan-in: GitHub
creates its check run only when every job it needs has finished, so for most of
a healthy CI run it does not exist. Only when that workflow is not running here
-- it never started, or finished without producing the check -- and
NO_RUN_MINUTES have passed, does the gate fail: CI never ran on this commit (an
old one, or one from before the check existed), and waiting the full hour would
point at the wrong cause.

Usage (in release.yml, with GH_TOKEN set and `checks: read`):
    tools/release_gate.py --repo owner/name --sha <sha> --ref <tag>
    tools/release_gate.py --self-test
"""

import argparse
import json
import pathlib
import re
import subprocess
import sys
import time

REQUIRED = ("ci-gate", "analyze / c-cpp")
# The workflow that produces each required check.
PRODUCER = {"ci-gate": ".github/workflows/ci.yml", "analyze / c-cpp": ".github/workflows/codeql.yml"}
WAIT_MINUTES = 60
NO_RUN_MINUTES = 10
# Consecutive minutes a GitHub API call may fail (a 502, a rate limit) before
# the gate gives up. One bad answer in an hour of polling is not a verdict.
API_FAILURES = 3


class ApiError(Exception):
    """A command the gate needs failed; the message carries its own output."""


def run(command):
    """stdout of `command`, or ApiError naming it with what it printed."""
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        output = (result.stderr or result.stdout).strip() or "no output"
        raise ApiError(f"`{' '.join(command)}` exited {result.returncode}: {output}")
    return result.stdout


def verdict(runs, minute, producing=False):
    """'pass', 'fail:<why>' or 'wait' for one check's runs at `minute`.

    `producing`: the workflow that creates this check is queued or running on
    the commit, so a check that has not appeared yet is still to come."""
    if any(run.get("conclusion") == "success" for run in runs):
        return "pass"
    if runs and all(run.get("status") == "completed" for run in runs):
        return "fail:finished as " + ",".join(str(run.get("conclusion")) for run in runs)
    if not runs and not producing and minute >= NO_RUN_MINUTES:
        return (f"fail:never reported in {NO_RUN_MINUTES} minutes, and the workflow that "
                f"produces it is not running -- no push to main ran CI on this commit")
    return "wait"


def overall(verdicts):
    """Combine per-check verdicts: any failure fails, all passing passes."""
    failures = {name: v for name, v in verdicts.items() if v.startswith("fail")}
    if failures:
        return "fail", failures
    if all(v == "pass" for v in verdicts.values()):
        return "pass", {}
    return "wait", {name: v for name, v in verdicts.items() if v == "wait"}


def from_main_push(checks, workflow_runs, path):
    """The check runs that a `push` to main ran, in the workflow at `path`."""
    suites = {run.get("check_suite_id") for run in workflow_runs
              if run.get("event") == "push" and run.get("head_branch") == "main"
              and run.get("path") == path}
    return [check for check in checks if (check.get("check_suite") or {}).get("id") in suites]


def check_runs(repo, sha, name):
    runs = json.loads(run(["gh", "api", "-X", "GET", f"repos/{repo}/commits/{sha}/check-runs",
                           "-f", f"check_name={name}"]))["check_runs"]
    return [run for run in runs if (run.get("app") or {}).get("slug") == "github-actions"]


def workflow_runs_on(repo, sha):
    """Every workflow run on `sha`: its path, event, branch, status and check suite."""
    return json.loads(run(["gh", "api", "-X", "GET", f"repos/{repo}/actions/runs",
                           "-f", f"head_sha={sha}", "-f", "per_page=100"]))["workflow_runs"]


def producing_workflows(workflow_runs):
    """Paths of the workflows a push to main is still running on the commit."""
    return {run["path"] for run in workflow_runs if run.get("status") != "completed"
            and run.get("event") == "push" and run.get("head_branch") == "main"}


def on_main(sha):
    run(["git", "fetch", "--no-tags", "--quiet", "origin", "main"])
    return subprocess.run(["git", "merge-base", "--is-ancestor", sha, "origin/main"]).returncode == 0


def self_test():
    ok, done_fail = {"status": "completed", "conclusion": "success"}, {"status": "completed",
                                                                      "conclusion": "failure"}
    running = {"status": "in_progress", "conclusion": None}
    cases = [
        ([ok], 0, "pass"),
        ([done_fail], 0, "fail"),
        ([running], 30, "wait"),
        ([], 3, "wait"),
        ([], NO_RUN_MINUTES, "fail"),
        # A fan-in check does not exist until its workflow's other jobs finish.
        ([], NO_RUN_MINUTES + 15, "wait", True),
        ([done_fail, ok], 0, "pass"),          # a re-run that succeeded counts
    ]
    failures = []
    for runs, minute, want, *producing in cases:
        got = verdict(runs, minute, *producing)
        if not got.startswith(want):
            failures.append(f"verdict({runs}, minute={minute}, producing={producing}) = {got}, "
                            f"want {want}")
    # Only main's own run counts. A pull_request run reports on its head
    # commit but tested a merge with the base of its day, and a docs-only PR's
    # ci-gate passes with every build skipped.
    workflow_runs = [
        {"check_suite_id": 1, "event": "push", "head_branch": "main", "path": PRODUCER["ci-gate"]},
        {"check_suite_id": 2, "event": "pull_request", "head_branch": "topic", "path": PRODUCER["ci-gate"]},
        {"check_suite_id": 3, "event": "push", "head_branch": "topic", "path": PRODUCER["ci-gate"]},
        {"check_suite_id": 4, "event": "push", "head_branch": "main", "path": ".github/workflows/other.yml"},
    ]
    for suite, want in ((1, True), (2, False), (3, False), (4, False)):
        check = dict(ok, check_suite={"id": suite})
        got = bool(from_main_push([check], workflow_runs, PRODUCER["ci-gate"]))
        if got != want:
            failures.append(f"a ci-gate run of suite {workflow_runs[suite - 1]} counted={got}, want {want}")
    combos = [
        ({"ci-gate": "pass", "analyze / c-cpp": "pass"}, "pass"),
        ({"ci-gate": "pass", "analyze / c-cpp": "wait"}, "wait"),
        ({"ci-gate": "pass", "analyze / c-cpp": "fail:finished as failure"}, "fail"),
        ({"ci-gate": "wait", "analyze / c-cpp": "fail:x"}, "fail"),
    ]
    for verdicts, want in combos:
        if overall(verdicts)[0] != want:
            failures.append(f"overall({verdicts}) = {overall(verdicts)[0]}, want {want}")
    if set(PRODUCER) != set(REQUIRED):
        failures.append(f"PRODUCER names {sorted(PRODUCER)}, REQUIRED {sorted(REQUIRED)}")
    # A renamed workflow or job would stop the wait silently, and the gate
    # would again fail a healthy CI run after NO_RUN_MINUTES.
    root = pathlib.Path(__file__).resolve().parent.parent
    for name, path in PRODUCER.items():
        workflow = root / path
        if not workflow.is_file():
            failures.append(f"PRODUCER[{name!r}] = {path}, which does not exist")
        # Job level only: a step's `with: name:` (an artifact) sits deeper.
        elif not re.search(rf"^    name: {re.escape(name)}\s*$", workflow.read_text(), re.MULTILINE):
            failures.append(f"{path} has no job named {name!r}, so it cannot produce that check")
    # A failing command must surface as ApiError with its own output -- not as
    # a traceback that ends a release, after up to an hour of waiting, on one 502.
    try:
        run([sys.executable, "-c", "import sys; sys.stderr.write('HTTP 502'); sys.exit(1)"])
        failures.append("run() returned for a command that exited 1")
    except ApiError as error:
        if "HTTP 502" not in str(error):
            failures.append(f"run()'s error does not carry the command's output: {error}")
    if set(REQUIRED) != {"ci-gate", "analyze / c-cpp"}:
        failures.append(f"REQUIRED changed to {REQUIRED}; update the docstring and this test")
    if failures:
        print("SELF-TEST FAILED:\n  " + "\n  ".join(failures), file=sys.stderr)
        return 1
    print(f"self-test passed: {len(cases)} single-check and {len(combos)} combined verdicts, "
          f"including a re-run that succeeded, a check that never reported, and a fan-in "
          f"check its running workflow has not created yet")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo")
    parser.add_argument("--sha")
    parser.add_argument("--ref", help="the tag, for messages")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    if args.self_test:
        return self_test()
    if not (args.repo and args.sha):
        parser.error("--repo and --sha are required")

    try:
        main_has_it = on_main(args.sha)
    except ApiError as error:
        print(f"::error::could not fetch origin/main to check {args.sha} is on it: {error}")
        return 1
    if not main_has_it:
        print(f"::error::{args.ref} points at {args.sha}, which is not on main. Release only from "
              f"main: merge first, then tag the merge commit.")
        return 1
    failed_calls = 0
    for minute in range(WAIT_MINUTES + 1):
        try:
            workflow_runs = workflow_runs_on(args.repo, args.sha)
            running = producing_workflows(workflow_runs)
            verdicts = {name: verdict(from_main_push(check_runs(args.repo, args.sha, name),
                                                     workflow_runs, PRODUCER[name]),
                                      minute, PRODUCER[name] in running)
                        for name in REQUIRED}
            failed_calls = 0
        except ApiError as error:
            failed_calls += 1
            if failed_calls >= API_FAILURES:
                print(f"::error::the GitHub API failed {failed_calls} minutes running, last with "
                      f"{error}. Re-run this job once it answers.")
                return 1
            print(f"::warning::{error} -- trying again in a minute")
            verdicts = {name: "wait" for name in REQUIRED}
        state, detail = overall(verdicts)
        if state == "pass":
            print(f"main's {' and '.join(REQUIRED)} passed for {args.sha}")
            return 0
        if state == "fail":
            for name, why in detail.items():
                print(f"::error::main's `{name}` for {args.sha}: {why.split(':', 1)[1]}. "
                      f"Fix main, then tag a commit that passes.")
            return 1
        if minute < WAIT_MINUTES:
            print(f"waiting for {', '.join(detail)} on {args.sha} (minute {minute + 1}/{WAIT_MINUTES})")
            time.sleep(60)
    print(f"::error::main's checks did not finish within {WAIT_MINUTES} minutes. Re-run this job "
          f"once they have.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
