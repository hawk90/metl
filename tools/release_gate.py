#!/usr/bin/env python3
"""Refuse to release a commit that main's verification has not passed.

release.yml runs one host build. What makes a commit releasable is what ran on
main for it: `ci-gate` (the fan-in of every ci.yml job -- GCC/Clang/MSVC,
sanitizers, ARM size/stack/RAM budgets, QEMU on four Cortex-M cores, Zephyr,
ESP-IDF) and `analyze / c-cpp` (CodeQL). A tag releases a commit only when

  * the commit is on main, and
  * every check in REQUIRED has a successful run on that commit.

A check still running is waited for (main's CI takes 10-25 minutes, most of it
queueing for runners). A check that finished without success fails the release
at once. A check that has not appeared after NO_RUN_MINUTES fails too: main's
CI starts within a minute or two of a merge, so its absence means CI never ran
on this commit -- an old one, or one from before the check existed -- and
waiting the full hour to report a timeout would point at the wrong cause.

Usage (in release.yml, with GH_TOKEN set and `checks: read`):
    tools/release_gate.py --repo owner/name --sha <sha> --ref <tag>
    tools/release_gate.py --self-test
"""

import argparse
import json
import subprocess
import sys
import time

REQUIRED = ("ci-gate", "analyze / c-cpp")
WAIT_MINUTES = 60
NO_RUN_MINUTES = 10


def verdict(runs, minute):
    """'pass', 'fail:<why>' or 'wait' for one check's runs at `minute`."""
    if any(run.get("conclusion") == "success" for run in runs):
        return "pass"
    if runs and all(run.get("status") == "completed" for run in runs):
        return "fail:finished as " + ",".join(str(run.get("conclusion")) for run in runs)
    if not runs and minute >= NO_RUN_MINUTES:
        return f"fail:never reported in {NO_RUN_MINUTES} minutes -- main's CI did not run on this commit"
    return "wait"


def overall(verdicts):
    """Combine per-check verdicts: any failure fails, all passing passes."""
    failures = {name: v for name, v in verdicts.items() if v.startswith("fail")}
    if failures:
        return "fail", failures
    if all(v == "pass" for v in verdicts.values()):
        return "pass", {}
    return "wait", {name: v for name, v in verdicts.items() if v == "wait"}


def check_runs(repo, sha, name):
    result = subprocess.run(
        ["gh", "api", "-X", "GET", f"repos/{repo}/commits/{sha}/check-runs", "-f", f"check_name={name}"],
        capture_output=True, text=True, check=True)
    runs = json.loads(result.stdout)["check_runs"]
    return [run for run in runs if (run.get("app") or {}).get("slug") == "github-actions"]


def on_main(sha):
    subprocess.run(["git", "fetch", "--no-tags", "--quiet", "origin", "main"], check=True)
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
        ([done_fail, ok], 0, "pass"),          # a re-run that succeeded counts
    ]
    failures = []
    for runs, minute, want in cases:
        got = verdict(runs, minute)
        if not got.startswith(want):
            failures.append(f"verdict({runs}, minute={minute}) = {got}, want {want}")
    combos = [
        ({"ci-gate": "pass", "analyze / c-cpp": "pass"}, "pass"),
        ({"ci-gate": "pass", "analyze / c-cpp": "wait"}, "wait"),
        ({"ci-gate": "pass", "analyze / c-cpp": "fail:finished as failure"}, "fail"),
        ({"ci-gate": "wait", "analyze / c-cpp": "fail:x"}, "fail"),
    ]
    for verdicts, want in combos:
        if overall(verdicts)[0] != want:
            failures.append(f"overall({verdicts}) = {overall(verdicts)[0]}, want {want}")
    if set(REQUIRED) != {"ci-gate", "analyze / c-cpp"}:
        failures.append(f"REQUIRED changed to {REQUIRED}; update the docstring and this test")
    if failures:
        print("SELF-TEST FAILED:\n  " + "\n  ".join(failures), file=sys.stderr)
        return 1
    print(f"self-test passed: {len(cases)} single-check and {len(combos)} combined verdicts, "
          f"including a re-run that succeeded and a check that never reported")
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

    if not on_main(args.sha):
        print(f"::error::{args.ref} points at {args.sha}, which is not on main. Release only from "
              f"main: merge first, then tag the merge commit.")
        return 1
    for minute in range(WAIT_MINUTES + 1):
        verdicts = {name: verdict(check_runs(args.repo, args.sha, name), minute) for name in REQUIRED}
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
