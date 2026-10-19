#!/usr/bin/env python3
"""Show that a commit on main was already verified, or say that it was not.

A pull request merges only when its ci.yml run passed `ci-gate` on a branch that
already contains main (strict up-to-date protection), and the squash merge then
puts that run's tree on main. Running all of ci.yml again on the push re-tests
the same tree while the next pull request waits for runners (#431). So main.yml
runs this first, and calls ci.yml in full only when this cannot show the commit
was verified.

The commit counts as verified ("reused") when

  * exactly one pull request was merged as this commit, from a branch of this
    repository (a fork's run is not taken on trust);
  * a successful pull_request run of ci.yml on that pull request's head
    recorded, after its verdict passed, the tree it tested (the
    `tested-tree` artifact ci-gate uploads), and that tree is this commit's.
    The tree is compared, not inferred from branch protection, so an admin
    merge or a protection change cannot make an untested tree pass;
  * that run built and tested the tree (code=true). A documentation-only run
    skipped every build, so for it the commit must also differ from its parent
    only in documentation, by classify_change.py's rule on this commit's own
    diff, and the parent must have passed main.yml. The chain then ends at a
    commit whose tree was built.

Anything else -- no pull request, a fork, no matching run, an API error -- is
"not reused", and main.yml runs ci.yml in full. A doubt can only run more.

What reuse gives up is stated on #431: the tests ran on the pull request's run,
not on a run of this commit, so a change in the runner images between the two
is not seen here. ci.yml's nightly run on main covers that.

--gate is main-gate's verdict over main.yml's jobs: the subject check and the
provenance check succeeded, and exactly one of the two paths ran and passed --
the measurement records after a reuse, or ci.yml in full. A skipped job counts
only where the path explains it.

Usage:
    tools/check_provenance.py --repo owner/name --sha <commit>   # in main.yml
    RESULTS='<toJSON(needs)>' tools/check_provenance.py --gate
    tools/check_provenance.py --self-test

Writes `reused=<true|false>` and `source=<run url>` to $GITHUB_OUTPUT when it is
set, else to stdout, and a short report to $GITHUB_STEP_SUMMARY.
"""

import argparse
import json
import os
import pathlib
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import classify_change  # noqa: E402

CI_WORKFLOW = "ci.yml"
MAIN_WORKFLOW = "main.yml"
TESTED_TREE = "tested-tree"


class ApiError(Exception):
    """A command this check needs failed; the message carries its output."""


def run(command):
    """stdout of `command`, or ApiError naming it with what it printed."""
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        output = (result.stderr or result.stdout).strip() or "no output"
        raise ApiError(f"`{' '.join(command)}` exited {result.returncode}: {output}")
    return result.stdout


def merged_pull(pulls, repo, sha):
    """(the pull request merged as `sha`, None) or (None, why there is none)."""
    merged = [p for p in pulls if p.get("merged_at") and p.get("merge_commit_sha") == sha
              and (p.get("base") or {}).get("ref") == "main"]
    if len(merged) != 1:
        return None, f"{len(merged)} pull requests were merged as {sha}, not one"
    pull = merged[0]
    head_repo = ((pull.get("head") or {}).get("repo") or {}).get("full_name")
    if head_repo != repo:
        return None, (f"#{pull.get('number')} came from {head_repo or 'a deleted repository'}, "
                      f"not {repo}; a fork's run is not taken on trust")
    return pull, None


def decide(tree, records, docs_only_commit, parent_passed):
    """(reused, source, reason) for a commit whose tree is `tree`.

    `records`: (run url, tested-tree record) for each successful pull_request
    run of ci.yml on the merged pull request's head, newest first; a record is
    None when the run left none. `docs_only_commit`: this commit differs from
    its parent only in documentation. `parent_passed`: main.yml succeeded on
    the parent."""
    seen = []
    for url, record in records:
        if record is None:
            seen.append(f"{url} recorded no tested tree")
            continue
        if record.get("tree") != tree:
            seen.append(f"{url} tested tree {record.get('tree')}, this commit's is {tree}")
            continue
        if record.get("code") == "true":
            return True, url, f"{url} passed ci-gate on this exact tree ({tree})"
        if record.get("code") != "false":
            seen.append(f"{url} recorded code={record.get('code')!r}")
            continue
        if not docs_only_commit:
            seen.append(f"{url} skipped the builds, but this commit changes more than documentation")
            continue
        if not parent_passed:
            seen.append(f"{url} skipped the builds, and the parent has not passed {MAIN_WORKFLOW}")
            continue
        return True, url, (f"{url} passed ci-gate on this exact tree as a documentation-only "
                           f"change, and the parent passed {MAIN_WORKFLOW}")
    if not records:
        seen.append(f"no successful pull_request run of {CI_WORKFLOW} on the pull request's head")
    return False, "", "; ".join(seen)


def gate_verdict(results):
    """main-gate: [] when main.yml's jobs add up to a verified commit, else the problems."""
    expected = ("subject", "provenance", "invariants", "full")
    missing = [name for name in expected if name not in results]
    if missing:
        return [f"main-gate does not see {', '.join(missing)}: was a needs: entry deleted?"]
    # Every job but the two alternatives must succeed, including any added later.
    problems = [f"{name}: {result.get('result')}" for name, result in sorted(results.items())
                if name not in ("invariants", "full") and result.get("result") != "success"]
    reused = (results["provenance"].get("outputs") or {}).get("reused") == "true"
    ran, skipped = ("invariants", "full") if reused else ("full", "invariants")
    if results[ran].get("result") != "success":
        problems.append(f"{ran}: {results[ran].get('result')} (reused={str(reused).lower()})")
    if results[skipped].get("result") != "skipped":
        problems.append(f"{skipped}: {results[skipped].get('result')}, expected skipped "
                        f"(reused={str(reused).lower()})")
    return problems


def gh_json(*args):
    return json.loads(run(["gh", "api", "-X", "GET", *args]))


def tested_tree(repo, run_id, scratch):
    """The tested-tree record a run uploaded, or None if it has none."""
    target = pathlib.Path(scratch) / str(run_id)
    result = subprocess.run(["gh", "run", "download", str(run_id), "--repo", repo,
                             "--name", TESTED_TREE, "--dir", str(target)],
                            capture_output=True, text=True)
    if result.returncode != 0:
        # gh's wording for a run that has no artifact of that name.
        if "no artifact matches" in (result.stderr + result.stdout).lower():
            return None
        raise ApiError(f"downloading {TESTED_TREE} from run {run_id}: "
                       f"{(result.stderr or result.stdout).strip()}")
    return json.loads((target / "tested-tree.json").read_text())


def investigate(repo, sha, checkout):
    """(reused, source, reason) for `sha`, from the GitHub API and the checkout."""
    tree = run(["git", "-C", checkout, "rev-parse", f"{sha}^{{tree}}"]).strip()
    pull, why = merged_pull(gh_json(f"repos/{repo}/commits/{sha}/pulls"), repo, sha)
    if pull is None:
        return False, "", why
    head = pull["head"]["sha"]
    runs = gh_json(f"repos/{repo}/actions/workflows/{CI_WORKFLOW}/runs", "-f", "event=pull_request",
                   "-f", f"head_sha={head}", "-f", "status=success", "-f", "per_page=20")
    runs = sorted(runs.get("workflow_runs", []), key=lambda r: r.get("created_at", ""), reverse=True)
    with tempfile.TemporaryDirectory() as scratch:
        records = [(r["html_url"], tested_tree(repo, r["id"], scratch)) for r in runs]
    docs_only = parent_passed = False
    if any(record and record.get("code") == "false" for _, record in records):
        paths = classify_change.changed_paths(checkout)
        docs_only = classify_change.is_code("pull_request", paths)[0] is False
        parent = run(["git", "-C", checkout, "rev-parse", f"{sha}^1"]).strip()
        passed = gh_json(f"repos/{repo}/actions/workflows/{MAIN_WORKFLOW}/runs", "-f", "event=push",
                         "-f", f"head_sha={parent}", "-f", "status=success")
        parent_passed = passed.get("total_count", 0) > 0
    reused, source, reason = decide(tree, records, docs_only, parent_passed)
    return reused, source, f"#{pull['number']}: {reason}"


def write_outputs(reused, source, reason):
    lines = f"reused={'true' if reused else 'false'}\nsource={source}\n"
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as handle:
            handle.write(lines)
    else:
        sys.stdout.write(lines)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as handle:
            verdict = ("verified by its pull request; ci.yml is not run again" if reused
                       else "not shown to be verified; ci.yml runs in full")
            handle.write(f"### Provenance\n\nThis commit is {verdict}.\n\n{reason}\n")


def self_test():
    failures = []
    repo, sha, tree = "o/r", "c" * 40, "t" * 40

    def pull(**over):
        base = {"number": 7, "merged_at": "2026-10-19T00:00:00Z", "merge_commit_sha": sha,
                "base": {"ref": "main"}, "head": {"sha": "h" * 40, "repo": {"full_name": repo}}}
        base.update(over)
        return base

    for label, pulls, want in (
            ("one merged pull request from this repository", [pull()], True),
            ("no pull request (a direct push)", [], False),
            ("a pull request that is open, not merged", [pull(merged_at=None)], False),
            ("a pull request merged as another commit", [pull(merge_commit_sha="d" * 40)], False),
            ("a pull request into another branch", [pull(base={"ref": "release"})], False),
            ("a fork's pull request",
             [pull(head={"sha": "h" * 40, "repo": {"full_name": "fork/r"}})], False),
            ("a pull request whose head repository was deleted",
             [pull(head={"sha": "h" * 40, "repo": None})], False),
            ("two pull requests claiming the same merge", [pull(), pull(number=8)], False)):
        got = merged_pull(pulls, repo, sha)[0] is not None
        if got != want:
            failures.append(f"merged_pull: {label} -> {got}, want {want}")

    url = "https://example/run/1"
    built = {"tree": tree, "code": "true"}
    docs = {"tree": tree, "code": "false"}
    for label, records, docs_only, parent_ok, want in (
            ("a run that built this exact tree", [(url, built)], False, False, True),
            ("a run that tested another tree (an older base)",
             [(url, dict(built, tree="x" * 40))], False, False, False),
            ("a run that left no record", [(url, None)], False, False, False),
            ("no successful run at all", [], False, False, False),
            ("a newer run on another tree, an older one on this tree",
             [(url, dict(built, tree="x" * 40)), ("u2", built)], False, False, True),
            ("a record with an unreadable code flag", [(url, dict(built, code=""))], False, False, False),
            ("an unreadable code flag on a documentation-only commit whose parent passed",
             [(url, dict(built, code=""))], True, True, False),
            ("a documentation-only run, a documentation-only commit, parent passed",
             [(url, docs)], True, True, True),
            ("a documentation-only run, but the commit changes code",
             [(url, docs)], False, True, False),
            ("a documentation-only run, but the parent has not passed",
             [(url, docs)], True, False, False)):
        got = decide(tree, records, docs_only, parent_ok)[0]
        if got != want:
            failures.append(f"decide: {label} -> {got}, want {want}")

    def needs(subject="success", provenance="success", reused="true", invariants="success",
              full="skipped"):
        return {"subject": {"result": subject, "outputs": {}},
                "provenance": {"result": provenance, "outputs": {"reused": reused}},
                "invariants": {"result": invariants, "outputs": {}},
                "full": {"result": full, "outputs": {}}}

    for label, results, want in (
            ("reused, records measured", needs(), True),
            ("not reused, full run passed", needs(reused="false", invariants="skipped",
                                                  full="success"), True),
            ("reused, but the records failed", needs(invariants="failure"), False),
            ("reused, and the records were skipped", needs(invariants="skipped"), False),
            ("not reused, and the full run failed", needs(reused="false", invariants="skipped",
                                                          full="failure"), False),
            ("not reused, and the full run was skipped", needs(reused="false", invariants="skipped"),
             False),
            ("not reused, and only the records ran", needs(reused="false"), False),
            ("both paths ran (the workflow's conditions are broken)", needs(full="success"), False),
            ("the subject check failed", needs(subject="failure"), False),
            ("the provenance check crashed and nothing ran",
             needs(provenance="failure", reused="", invariants="skipped"), False),
            ("the provenance check crashed and the full run passed",
             needs(provenance="failure", reused="", invariants="skipped", full="success"), False),
            ("a run cancelled midway", needs(invariants="cancelled"), False),
            ("an emptied needs: list", {}, False),
            ("a job added later failed", {**needs(), "later": {"result": "failure"}}, False),
            ("a job added later was skipped", {**needs(), "later": {"result": "skipped"}}, False),
            ("a job added later passed", {**needs(), "later": {"result": "success"}}, True)):
        got = not gate_verdict(results)
        if got != want:
            failures.append(f"gate_verdict: {label} -> {got}, want {want}")

    if failures:
        print("SELF-TEST FAILED:\n  " + "\n  ".join(failures), file=sys.stderr)
        return 1
    print("self-test passed: merged-pull selection, tree matching (including the "
          "documentation-only chain) and main-gate's verdict each fail where they must")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo")
    parser.add_argument("--sha")
    parser.add_argument("--checkout", default=".", help="a checkout of --sha, fetch-depth 2")
    parser.add_argument("--gate", action="store_true", help="main-gate's verdict over $RESULTS")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.gate:
        problems = gate_verdict(json.loads(os.environ.get("RESULTS") or "{}"))
        for problem in problems:
            print(f"::error::{problem}")
        if problems:
            return 1
        print("main-gate: this commit is verified and measured")
        return 0
    if not (args.repo and args.sha):
        parser.error("--repo and --sha are required")
    try:
        reused, source, reason = investigate(args.repo, args.sha, args.checkout)
    except (ApiError, KeyError, ValueError) as error:
        # Not knowing is not a failure here: main.yml then runs ci.yml in full.
        reused, source, reason = False, "", f"could not establish provenance: {error}"
    print(reason)
    write_outputs(reused, source, reason)
    return 0


if __name__ == "__main__":
    sys.exit(main())
