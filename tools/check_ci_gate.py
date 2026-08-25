#!/usr/bin/env python3
"""Keep the merge boundary a boundary.

Until this file existed, `main`'s branch protection carried no
`required_status_checks` at all: 64 checks ran on every pull request and not one
of them could stop a merge. Every gate this repository built -- the invariant
audit, the size and stack ratchets, the compile-fail census, check_mutants --
was advisory at the only point where being advisory matters.

Requiring the 64 check names directly does not work, for two reasons that are
both failure modes rather than inconveniences:

  * The names are derived from matrices. `host-test / ubuntu-latest / gcc /
    Debug` disappears when a matrix row is edited, and a required check that
    never reports stays pending forever -- the branch is then unmergeable for a
    reason nobody typed.
  * GitHub counts a SKIPPED check as passing. `deploy-docs` is skipped on every
    pull request, so requiring it would be requiring nothing.

So exactly one ci.yml check is required -- `ci-gate`, which `needs:` every other
job and fails unless all of them succeeded. That moves the hole rather than
closing it: a job left out of `needs:` no longer blocks a merge, and nothing
about the workflow file would look wrong. This script closes that hole.

  G1  every job in ci.yml is in ci-gate's `needs:` list, except the ones named
      in EXEMPT with a reason. A new job that is not wired in is a new job that
      does not block a merge.
  G2  every entry in `needs:` is a job that exists. A rename leaves a phantom,
      and a phantom is a job nobody is watching.
  G3  ci-gate declares `if: always()`. Without it the job is SKIPPED when a
      dependency fails -- and a skipped required check passes, so the gate would
      go green exactly when CI is red. This is the sharpest edge in the file.
  G4  every context in .github/required-checks.txt resolves to a job in a
      workflow triggered by `pull_request`. A required context that cannot
      report on a PR blocks that PR forever.
  G5  no required context is a job carrying an `if:` that restricts it to pushes
      or to main. That is the `deploy-docs` shape: it would be skipped on every
      PR and therefore green on every PR.

What this file deliberately does NOT do is read the live branch protection and
compare. Reading it needs an admin token, which CI does not have and should not
have; a checker that silently passes because it could not authenticate is worse
than no checker. The protection settings are applied by hand and recorded in
.github/required-checks.txt, and what this file guarantees is that the recorded
list still describes checks that exist and can report.

Usage:
    tools/check_ci_gate.py
    tools/check_ci_gate.py --self-test   # prove the checker still bites
"""

import argparse
import pathlib
import re
import sys

WORKFLOW_DIR = ".github/workflows"
CI_WORKFLOW = "ci.yml"
GATE_JOB = "ci-gate"
REQUIRED_LIST = ".github/required-checks.txt"

# Jobs that are allowed to be outside ci-gate's `needs:`, each with the reason.
# Anything not in here and not wired in is a failure, so an exemption is an
# edit somebody has to justify rather than an omission nobody notices.
EXEMPT = {
    GATE_JOB: "the fan-in job itself",
    "deploy-docs": "runs only on a push to main, so it cannot report on a PR",
}

# A job header: two spaces, a name, a colon, nothing else on the line.
_JOB_HEADER = re.compile(r"^  ([A-Za-z0-9_-]+):\s*$", re.M)


def _workflow_jobs(text):
    """Job id -> its body, for one workflow file.

    Hand-rolled rather than PyYAML: tools/check_docs.py already parses these
    files this way and CI installs no Python packages for the checkers.
    """
    starts = [(m.group(1), m.start()) for m in _JOB_HEADER.finditer(text)]
    # Everything before `jobs:` is workflow-level; drop headers found there.
    marker = text.find("\njobs:")
    if marker < 0:
        return {}
    starts = [(name, at) for name, at in starts if at > marker]
    jobs = {}
    for index, (name, at) in enumerate(starts):
        end = starts[index + 1][1] if index + 1 < len(starts) else len(text)
        jobs[name] = text[at:end]
    return jobs


def _job_name(body, job_id):
    """The check-run name a job reports under, or the job id if unnamed."""
    match = re.search(r"^    name:\s*(.+?)\s*$", body, re.M)
    return match.group(1) if match else job_id


def _needs(body):
    """The `needs:` list of one job, inline or block form."""
    inline = re.search(r"^    needs:\s*\[([^\]]*)\]", body, re.M | re.S)
    if inline:
        return [item.strip() for item in inline.group(1).split(",") if item.strip()]
    single = re.search(r"^    needs:\s*([A-Za-z0-9_-]+)\s*$", body, re.M)
    if single:
        return [single.group(1)]
    block = re.search(r"^    needs:\s*\n((?:      -\s*[A-Za-z0-9_-]+\s*\n)+)", body, re.M)
    if block:
        return [line.strip().lstrip("-").strip() for line in block.group(1).splitlines()]
    return []


def _triggers(text):
    """The event names in a workflow's `on:` block."""
    head = text.split("\njobs:")[0]
    return set(re.findall(r"^  (push|pull_request|schedule|workflow_dispatch|"
                          r"workflow_call|release):", head, re.M))


def _required_contexts(root):
    path = pathlib.Path(root) / REQUIRED_LIST
    if not path.is_file():
        return None
    contexts = []
    for line in path.read_text().splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            contexts.append(line)
    return contexts


def check(root):
    root = pathlib.Path(root)
    problems = []

    ci_path = root / WORKFLOW_DIR / CI_WORKFLOW
    if not ci_path.is_file():
        return [("G1", f"{WORKFLOW_DIR}/{CI_WORKFLOW} is missing -- "
                       f"the merge boundary cannot be read")]
    ci_text = ci_path.read_text()
    ci_jobs = _workflow_jobs(ci_text)

    if GATE_JOB not in ci_jobs:
        return [("G1", f"{CI_WORKFLOW} has no `{GATE_JOB}` job. It is the only "
                       f"ci.yml check in {REQUIRED_LIST}; without it nothing in "
                       f"this workflow blocks a merge")]

    gate_body = ci_jobs[GATE_JOB]
    wired = _needs(gate_body)

    # G1: nothing escapes the fan-in.
    for job_id in ci_jobs:
        if job_id in EXEMPT or job_id in wired:
            continue
        problems.append(("G1", f"{CI_WORKFLOW} job `{job_id}` is not in "
                               f"`{GATE_JOB}`'s needs -- it runs on every PR and "
                               f"blocks no merge. Wire it in, or add it to "
                               f"EXEMPT in this file with a reason"))

    # G2: no phantoms.
    for name in wired:
        if name not in ci_jobs:
            problems.append(("G2", f"`{GATE_JOB}` needs `{name}`, which is not a "
                                   f"job in {CI_WORKFLOW} -- a rename left a "
                                   f"phantom, and a phantom is watched by nobody"))

    # G3: the skipped-counts-as-passing hole.
    if not re.search(r"^    if:\s*always\(\)\s*$", gate_body, re.M):
        problems.append(("G3", f"`{GATE_JOB}` does not declare `if: always()`. "
                               f"Without it the job is SKIPPED when a dependency "
                               f"fails, and GitHub counts a skipped required "
                               f"check as passing -- the gate would go green "
                               f"exactly when CI is red"))

    # G4/G5: the recorded required list must describe checks that can report.
    contexts = _required_contexts(root)
    if contexts is None:
        problems.append(("G4", f"{REQUIRED_LIST} is missing -- the list of "
                               f"contexts required at the merge boundary is "
                               f"then written down nowhere"))
        return problems
    if not contexts:
        problems.append(("G4", f"{REQUIRED_LIST} names no contexts -- an empty "
                               f"required list is no protection at all"))

    # Where each context could come from: every job of every workflow, keyed by
    # the name it reports under. Matrix jobs report `name (value)` per leg, so a
    # context is matched by its stem too.
    catalogue = {}
    for path in sorted((root / WORKFLOW_DIR).glob("*.yml")):
        text = path.read_text()
        events = _triggers(text)
        for job_id, body in _workflow_jobs(text).items():
            catalogue[_job_name(body, job_id)] = (path.name, job_id, body, events)

    for context in contexts:
        entry = catalogue.get(context)
        if entry is None:
            stem = re.sub(r"\s*\([^)]*\)$", "", context)
            entry = catalogue.get(stem)
        if entry is None:
            problems.append(("G4", f"{REQUIRED_LIST} requires `{context}`, which "
                                   f"no job in {WORKFLOW_DIR} reports under. A "
                                   f"required context that never reports leaves "
                                   f"every PR pending forever"))
            continue
        workflow, job_id, body, events = entry
        if "pull_request" not in events:
            problems.append(("G4", f"{REQUIRED_LIST} requires `{context}`, but "
                                   f"{workflow} is not triggered by "
                                   f"`pull_request` -- it cannot report on a PR"))
        condition = re.search(r"^    if:\s*(.+?)\s*$", body, re.M)
        if condition and ("github.event_name" in condition.group(1)
                          or "refs/heads/main" in condition.group(1)):
            problems.append(("G5", f"{REQUIRED_LIST} requires `{context}`, whose "
                                   f"job `{job_id}` is gated on "
                                   f"`{condition.group(1)}`. It is skipped on "
                                   f"pull requests, and a skipped required check "
                                   f"passes -- requiring it requires nothing"))

    return problems


def self_test():
    import tempfile

    failures = []

    clean_ci = """\
name: CI
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  preflight:
    name: preflight
    runs-on: ubuntu-latest

  host-test:
    name: host-test / ${{ matrix.os }}
    needs: preflight

  deploy-docs:
    name: deploy-docs
    if: github.ref == 'refs/heads/main' && github.event_name == 'push'
    needs: [docs]

  ci-gate:
    name: ci-gate
    needs:
      - preflight
      - host-test
    if: always()
    runs-on: ubuntu-latest
"""
    clean_required = ("# comment\nci-gate\nanalyze / c-cpp\n"
                      "cflite-pr (address)\n")
    codeql = """\
name: CodeQL
on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

jobs:
  analyze:
    name: analyze / c-cpp
    runs-on: ubuntu-latest
"""
    cflite = """\
name: ClusterFuzzLite PR
on:
  pull_request:
    branches: [main]

jobs:
  fuzzing:
    name: cflite-pr
    runs-on: ubuntu-latest
"""

    def tree(stack, ci=clean_ci, required=clean_required, extra=None):
        root = pathlib.Path(stack)
        (root / WORKFLOW_DIR).mkdir(parents=True, exist_ok=True)
        (root / WORKFLOW_DIR / CI_WORKFLOW).write_text(ci)
        (root / WORKFLOW_DIR / "codeql.yml").write_text(codeql)
        (root / WORKFLOW_DIR / "cflite-pr.yml").write_text(cflite)
        for name, text in (extra or {}).items():
            (root / WORKFLOW_DIR / name).write_text(text)
        if required is not None:
            (root / REQUIRED_LIST).write_text(required)
        return root

    def fired(root):
        return {rule for rule, _ in check(root)}

    with tempfile.TemporaryDirectory() as stack:
        # The clean tree must stay clean -- including the block-form `needs:`,
        # the matrix-derived `cflite-pr (address)` context, and `deploy-docs`
        # sitting outside the fan-in on purpose.
        root = tree(stack)
        remaining = check(root)
        if remaining:
            failures.append(f"clean tree was flagged: {remaining}")

    # G1: a job nobody wired in.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, ci=clean_ci.replace(
            "  ci-gate:\n",
            "  brand-new:\n    name: brand-new\n    needs: preflight\n\n  ci-gate:\n"))
        if "G1" not in fired(root):
            failures.append("G1 did not fire on a job missing from the fan-in")

    # G1 near miss: a job whose id merely CONTAINS the gate's name is not the
    # gate and must not be exempt. Otherwise `ci-gate-helper` walks straight out.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, ci=clean_ci.replace(
            "  ci-gate:\n",
            "  ci-gate-helper:\n    name: helper\n    needs: preflight\n\n  ci-gate:\n"))
        if "G1" not in fired(root):
            failures.append("G1 was fooled by a job id containing the gate's name")

    # G2: a phantom left by a rename.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, ci=clean_ci.replace("      - host-test\n",
                                               "      - host-tests\n"))
        if "G2" not in fired(root):
            failures.append("G2 did not fire on a needs entry naming no job")

    # G3: the hole that matters most -- no `if: always()`.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, ci=clean_ci.replace("    if: always()\n", ""))
        if "G3" not in fired(root):
            failures.append("G3 did not fire on a fan-in without `if: always()`")

    # G4: a required context nothing reports under.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, required="ci-gate\nghost-check\n")
        if "G4" not in fired(root):
            failures.append("G4 did not fire on a context no job reports")

    # G4: a required context whose workflow never runs on a PR.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, required="ci-gate\nscorecard\n", extra={
            "scorecard.yml": "name: Scorecard\non:\n  push:\n    branches: [main]\n"
                             "\njobs:\n  analysis:\n    name: scorecard\n"})
        if "G4" not in fired(root):
            failures.append("G4 did not fire on a context that cannot report on a PR")

    # G4: the list itself gone, and the list empty. Silence must not read as
    # agreement in either shape.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, required=None)
        if "G4" not in fired(root):
            failures.append("G4 did not fire when the required list was missing")
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, required="# only a comment\n")
        if "G4" not in fired(root):
            failures.append("G4 did not fire on an empty required list")

    # G5: requiring the `deploy-docs` shape -- skipped on PRs, therefore green.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, required="ci-gate\ndeploy-docs\n")
        if "G5" not in fired(root):
            failures.append("G5 did not fire on a required context that is "
                            "skipped on every pull request")

    # And the gate job simply deleted -- the whole file is about that not
    # passing quietly.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, ci=clean_ci.split("  ci-gate:")[0])
        if "G1" not in fired(root):
            failures.append("a ci.yml with no fan-in job at all was not flagged")

    if failures:
        for failure in failures:
            print(f"SELF-TEST FAILED: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: G1-G5 each bite, a job id containing the gate's "
          "name is not mistaken for it, and a clean tree is not flagged")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    problems = check(args.repo_root)
    for rule, message in problems:
        print(f"{rule}: {message}")
    if problems:
        print(f"\n{len(problems)} problem(s).")
        return 1

    root = pathlib.Path(args.repo_root)
    jobs = _workflow_jobs((root / WORKFLOW_DIR / CI_WORKFLOW).read_text())
    wired = len(_needs(jobs[GATE_JOB]))
    contexts = _required_contexts(root)
    print(f"merge boundary OK: `{GATE_JOB}` fans in {wired} of "
          f"{len(jobs) - len(EXEMPT)} ci.yml jobs with `if: always()`, and all "
          f"{len(contexts)} required context(s) report on every pull request")
    return 0


if __name__ == "__main__":
    sys.exit(main())
