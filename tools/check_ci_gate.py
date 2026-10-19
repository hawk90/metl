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
  G6  every workflow sets `defaults: run: shell: bash`, so a pipe whose first
      stage fails fails the step.
  G7  ci-gate's verdict fails when a job it needs did not succeed. G1-G3 prove
      the fan-in is wired; a verdict step that passes regardless (`run: echo
      ok`) would keep all three green and stop nothing. The verdict is the
      inline `python3 - <<'PY'` script in ci-gate's steps, fed
      `RESULTS: ${{ toJSON(needs) }}`; this file runs it against synthetic
      results -- every job succeeded, one failed, one skipped, one cancelled,
      none at all, and the documentation-only shape -- and requires it to pass
      and fail exactly as a gate must.

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
import json
import os
import pathlib
import re
import subprocess
import sys
import textwrap

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

# A job header: two spaces, a name, a colon, and at most a comment. A header
# written `  name:  # why` is still a job; missing it folds the job into the
# one above, where no rule sees it.
_JOB_HEADER = re.compile(r"^  ([A-Za-z0-9_-]+):\s*(?:#.*)?$", re.M)


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


_VERDICT = re.compile(r"python3 - <<'PY'\n(?P<script>.*?)\n[ ]*PY\n", re.S)
_VERDICT_INPUT = re.compile(r"^\s*RESULTS:\s*\$\{\{\s*toJSON\(needs\)\s*\}\}\s*$", re.M)


def _run_verdict(script, results):
    """Exit status of the gate's verdict script, given the needs results."""
    env = dict(os.environ, RESULTS=json.dumps(results))
    return subprocess.run([sys.executable, "-"], input=script, text=True, env=env,
                          capture_output=True).returncode


def _verdict_problems(gate_body, wired):
    """G7: the verdict passes when every job succeeded and fails otherwise."""
    match = _VERDICT.search(gate_body)
    if match is None or not _VERDICT_INPUT.search(gate_body):
        return [f"`{GATE_JOB}` has no verdict step this file can test: it wants a "
                f"`python3 - <<'PY'` script fed `RESULTS: ${{{{ toJSON(needs) }}}}`. "
                f"A fan-in whose verdict is not proven to fail is no gate; if the "
                f"step changed shape on purpose, teach G7 the new one"]
    script = textwrap.dedent(match.group("script"))
    # The real gate also refuses a needs: list far shorter than the workflow,
    # so the probes report on the list as it is wired.
    jobs = list(wired) or ["preflight"]
    ok = {name: {"result": "success", "outputs": {}} for name in jobs}
    if "changes" in ok:
        ok["changes"]["outputs"] = {"code": "true"}
    victim = next((name for name in reversed(jobs) if name != "changes"), jobs[-1])
    probes = [("every job succeeded", ok, True)]
    for result in ("failure", "skipped", "cancelled"):
        probes.append((f"`{victim}` {result} on a code change",
                       {**ok, victim: {"result": result, "outputs": {}}}, False))
    probes.append(("no job reported at all (an emptied needs: list)", {}, False))
    if "changes" in ok:
        docs = {name: {"result": "skipped", "outputs": {}} for name in jobs}
        docs["changes"] = {"result": "success", "outputs": {"code": "false"}}
        probes.append(("a documentation-only change skipped the builds", docs, True))
        broken = dict(docs, changes={"result": "failure", "outputs": {}})
        probes.append(("`changes` failed and the builds were skipped", broken, False))
    problems = []
    for label, results, should_pass in probes:
        passed = _run_verdict(script, results) == 0
        if passed != should_pass:
            problems.append(f"`{GATE_JOB}`'s verdict {'failed' if should_pass else 'PASSED'} "
                            f"when {label} -- " + ("it blocks every merge" if should_pass
                                                   else "a red CI would merge"))
    return problems


def _triggers(text):
    """The event names in a workflow's `on:` block."""
    head = text.split("\njobs:")[0]
    return set(re.findall(r"^  (push|pull_request|schedule|workflow_dispatch|"
                          r"workflow_call|release):", head, re.M))


def _pull_request_filters(text):
    """`paths` / `paths-ignore` under a workflow's pull_request trigger."""
    head = text.split("\njobs:")[0]
    block = re.search(r"^  pull_request:[^\n]*\n((?:(?:    [^\n]*|\s*)\n)*)", head, re.M)
    return re.findall(r"^    (paths|paths-ignore):", block.group(1), re.M) if block else []


def _workflow_files(root):
    """Every workflow GitHub reads: *.yml and *.yaml."""
    directory = pathlib.Path(root) / WORKFLOW_DIR
    return sorted([*directory.glob("*.yml"), *directory.glob("*.yaml")])


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

    # G7: the verdict at the end of the fan-in can fail.
    problems.extend(("G7", message) for message in _verdict_problems(gate_body, wired))

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

    # Where each context could come from: every job of every workflow, as
    # (workflow, job id), with the name it reports under. Two jobs may report
    # the same name, so a context maps to all of them. Matrix jobs report
    # `name (value)` per leg, so a context is matched by its stem too.
    catalogue = {}
    for path in _workflow_files(root):
        text = path.read_text()
        events, filters = _triggers(text), _pull_request_filters(text)
        for job_id, body in _workflow_jobs(text).items():
            catalogue.setdefault(_job_name(body, job_id), []).append(
                (path.name, job_id, body, events, filters))

    for context in contexts:
        entries = catalogue.get(context) or catalogue.get(re.sub(r"\s*\([^)]*\)$", "", context))
        if not entries:
            problems.append(("G4", f"{REQUIRED_LIST} requires `{context}`, which "
                                   f"no job in {WORKFLOW_DIR} reports under. A "
                                   f"required context that never reports leaves "
                                   f"every PR pending forever"))
            continue
        # G4: some job reporting it runs on every pull request.
        if not any("pull_request" in events and not filters for _, _, _, events, filters in entries):
            reasons = "; ".join(
                f"{workflow} is not triggered by `pull_request`" if "pull_request" not in events else
                f"{workflow} filters its `pull_request` trigger with {', '.join(filters)}, so it "
                f"does not run on a PR outside those paths"
                for workflow, _, _, events, filters in entries)
            problems.append(("G4", f"{REQUIRED_LIST} requires `{context}`, but no job reporting "
                                   f"it runs on every pull request: {reasons}"))
        # G5: no job reporting it is skipped on a pull request -- a skipped
        # check reports, and GitHub counts it as passing.
        for workflow, job_id, body, events, _ in entries:
            condition = re.search(r"^    if:\s*(.+?)\s*$", body, re.M)
            # github.ref and github.ref_name are refs/pull/N/merge and N/merge
            # on a pull request, so any test of them is a test a PR fails.
            if "pull_request" in events and condition and re.search(
                    r"github\.event_name|github\.ref\b|github\.ref_name|refs/heads/", condition.group(1)):
                problems.append(("G5", f"{REQUIRED_LIST} requires `{context}`, whose "
                                       f"job `{job_id}` in {workflow} is gated on "
                                       f"`{condition.group(1)}`. It is skipped on "
                                       f"pull requests, and a skipped required check "
                                       f"passes -- requiring it requires nothing"))

    # G6: a pipe whose first stage fails must fail the step. A run step gets
    # `bash -eo pipefail` only through an explicit `shell: bash`; GitHub's
    # implicit default is `bash -e`, under which `gh api ... | sort` that
    # fails yields an empty list and the step carries on with it.
    for path in _workflow_files(root):
        if not re.search(r"^defaults:\n  run:\n    shell: bash\s*$", path.read_text(), re.M):
            problems.append(("G6", f"{WORKFLOW_DIR}/{path.name} does not set "
                                   f"`defaults: run: shell: bash`, so its run "
                                   f"steps lack pipefail and a failing first "
                                   f"stage of a pipe goes unnoticed"))

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

defaults:
  run:
    shell: bash

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
    steps:
      - name: Every job succeeded
        env:
          RESULTS: ${{ toJSON(needs) }}
        run: |
          python3 - <<'PY'
          import json, os, sys

          results = json.loads(os.environ["RESULTS"])
          if not results:
              sys.exit("::error::ci-gate is watching no jobs")
          changes = results.get("changes", {})
          docs_only = (changes.get("result") == "success"
                       and changes.get("outputs", {}).get("code") == "false")
          allowed = {"success", "skipped"} if docs_only else {"success"}
          bad = {name: r["result"] for name, r in results.items()
                 if r["result"] not in allowed}
          if bad:
              sys.exit(f"{len(bad)} job(s) did not succeed")
          PY
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

defaults:
  run:
    shell: bash

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

defaults:
  run:
    shell: bash

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

    # G1: a job header carrying a trailing comment is still a job. YAML allows
    # it, and a header pattern that wants nothing after the colon folds the
    # job into the one above it -- where no rule can see it.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, ci=clean_ci.replace(
            "  ci-gate:\n",
            "  commented:  # added later\n    name: commented\n    needs: preflight\n\n  ci-gate:\n"))
        if "G1" not in fired(root):
            failures.append("G1 did not see a job whose header carries a comment")

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

    # G7: a fan-in whose verdict passes whatever its needs reported. Each of
    # these keeps `needs:` and `if: always()`, so G1-G3 stay quiet.
    steps_at = clean_ci.index("    steps:\n")
    verdict = clean_ci[steps_at:]
    for label, steps in (
            ("a verdict that always passes",
             "    steps:\n      - name: Every job succeeded\n        run: echo ok\n"),
            ("a verdict that reads no results",
             verdict.replace("RESULTS: ${{ toJSON(needs) }}", "RESULTS: '{\"x\": {\"result\": \"success\"}}'")),
            ("a verdict that accepts a skipped job outside the documentation-only path",
             verdict.replace('allowed = {"success", "skipped"} if docs_only else {"success"}',
                             'allowed = {"success", "skipped"}')),
            ("a verdict that passes an emptied needs: list",
             verdict.replace("if not results:", "if False:"))):
        with tempfile.TemporaryDirectory() as stack:
            root = tree(stack, ci=clean_ci[:steps_at] + steps)
            if "G7" not in fired(root):
                failures.append(f"G7 did not fire on {label}")

    # G4: a required context nothing reports under.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, required="ci-gate\nghost-check\n")
        if "G4" not in fired(root):
            failures.append("G4 did not fire on a context no job reports")

    # G4: a required context whose workflow never runs on a PR.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, required="ci-gate\nscorecard\n", extra={
            "scorecard.yml": "name: Scorecard\non:\n  push:\n    branches: [main]\n"
                             "\ndefaults:\n  run:\n    shell: bash\n"
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

    # G5: the same shape spelled with github.ref_name -- skipped on every PR,
    # whose ref is refs/pull/N/merge.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, required="ci-gate\npublish\n", extra={
            "publish.yml": cflite.replace("    name: cflite-pr\n",
                                          "    name: publish\n    if: github.ref_name == 'main'\n")})
        if "G5" not in fired(root):
            failures.append("G5 did not fire on a required job gated on github.ref_name")

    # G4: a required workflow whose pull_request trigger is path-filtered does
    # not run on a PR outside those paths, and its context stays pending.
    for key in ("paths", "paths-ignore"):
        with tempfile.TemporaryDirectory() as stack:
            root = tree(stack, extra={"cflite-pr.yml": cflite.replace(
                "  pull_request:\n    branches: [main]\n",
                f"  pull_request:\n    branches: [main]\n    {key}:\n      - 'fuzz/**'\n")})
            if "G4" not in fired(root):
                failures.append(f"G4 did not fire on a required workflow whose pull_request has {key}:")

    # G5 when two workflows report the same name: a skipped one still reports,
    # and GitHub takes its `skipped` as the context passing. Every job that
    # reports under a required name is checked, not the last one read.
    with tempfile.TemporaryDirectory() as stack:
        gated = cflite.replace("    name: cflite-pr\n",
                               "    name: publish\n    if: github.ref == 'refs/heads/main'\n")
        plain = cflite.replace("    name: cflite-pr\n", "    name: publish\n")
        root = tree(stack, required="ci-gate\npublish\n",
                    extra={"a-gated.yml": gated, "z-plain.yml": plain})
        if "G5" not in fired(root):
            failures.append("G5 missed a gated job because another workflow reports the same name")

    # A .yaml workflow is a workflow: G6 reads it, and a context it reports counts.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, extra={"other.yaml": cflite.replace("defaults:\n  run:\n    shell: bash\n", "")})
        if "G6" not in fired(root):
            failures.append("G6 did not read a .yaml workflow")
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, required="ci-gate\nyaml-check\n", extra={
            "yaml.yaml": cflite.replace("    name: cflite-pr\n", "    name: yaml-check\n")})
        if check(root):
            failures.append(f"a context reported from a .yaml workflow was not found: {check(root)}")

    # G6: a workflow without the bash default, so its pipes lack pipefail.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, extra={"other.yml": cflite.replace("defaults:\n  run:\n    shell: bash\n", "")})
        if "G6" not in fired(root):
            failures.append("G6 did not fire on a workflow without `shell: bash`")

    # And the gate job simply deleted -- the whole file is about that not
    # passing quietly.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, ci=clean_ci.split("  ci-gate:")[0])
        if "G1" not in fired(root):
            failures.append("a ci.yml with no fan-in job at all was not flagged")

    # In CI a problem must reach the run's annotations: `::error::` on stderr,
    # and exit 1.
    with tempfile.TemporaryDirectory() as stack:
        root = tree(stack, ci=clean_ci.replace("    if: always()\n", ""))
        run = subprocess.run([sys.executable, __file__, "--repo-root", str(root)],
                             capture_output=True, text=True)
        if run.returncode != 1 or "::error::G3: " not in run.stderr:
            failures.append(f"a G3 problem was not an ::error:: annotation with exit 1: "
                            f"exit {run.returncode}, stderr {run.stderr[:200]!r}")

    if failures:
        for failure in failures:
            print(f"SELF-TEST FAILED: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: G1-G7 each bite, a job id containing the gate's "
          "name is not mistaken for it, a commented job header is still a job, "
          "and a clean tree is not flagged")
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
        print(f"::error::{rule}: {message}", file=sys.stderr)
    if problems:
        print(f"\n{len(problems)} problem(s).", file=sys.stderr)
        return 1

    root = pathlib.Path(args.repo_root)
    jobs = _workflow_jobs((root / WORKFLOW_DIR / CI_WORKFLOW).read_text())
    wired = len(_needs(jobs[GATE_JOB]))
    # Count only jobs that exist: an EXEMPT entry may name a job this workflow
    # does not have (deploy-docs is kept for the self-test's G5 fixture).
    gated = sum(1 for job_id in jobs if job_id not in EXEMPT)
    contexts = _required_contexts(root)
    print(f"merge boundary OK: `{GATE_JOB}` fans in {wired} of "
          f"{gated} ci.yml jobs with `if: always()` and a verdict proven to fail, and all "
          f"{len(contexts)} required context(s) report on every pull request")
    return 0


if __name__ == "__main__":
    sys.exit(main())
