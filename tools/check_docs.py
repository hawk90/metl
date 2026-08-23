#!/usr/bin/env python3
"""Keep the documentation's checkable claims checked.

docs/COOKBOOK.md opens with "Every snippet below mirrors a compiled, CI-run
example under examples/". That is a claim about CI, and until now nothing in CI
tested it -- the same shape of problem as a benchmark that asserts nothing or a
checklist item nobody measures. This script tests the parts of it that a machine
can settle:

  D1  every `metl::Name` used in the docs exists in a public header.
      Catches an API that was renamed or removed while the prose kept using it.
  D2  every relative link to a file in the repo resolves.
  D3  every example named in the docs is registered in examples/CMakeLists.txt,
      and every examples/*.cpp on disk is registered there too. An example that
      is not in that list is not built and not run, so pointing a reader at it
      is pointing them at something nothing verifies.
  D4  every fuzz/*.cpp harness is registered in BOTH fuzz/CMakeLists.txt and
      .clusterfuzzlite/build.sh, and the two lists agree. SECURITY.md says the
      ClusterFuzzLite workflows "run the OSS-Fuzz toolchain" over the harnesses
      under fuzz/; two hand-maintained lists is how that stops being true. A
      harness missing from build.sh still passes `fuzz-smoke` and is silently
      absent from every nightly run.
  D5  no file outside tools/check_size.py states a per-Cortex-M byte figure.
      Not "the copies agree" -- no copies. See below.

D5 is a different shape from the others and the difference is the point. D1-D4
check that two things agree; D5 forbids the second thing from existing. It was
added after the size budgets were raised in #66 and the figures quoted in
.github/workflows/ci.yml and docs/TODO.md were not, so the repository stated
three answers to "what is the Cortex-M0 budget" and two of them were a
measurement that no longer existed. Nothing failed, because nothing was
checking; the ratchet itself was correct the whole time.

A checker that verified the copies matched would have caught it. It would also
have institutionalised the copies. tools/check_size.py holds BUDGETS, that is
where the number is, and prose points at it -- so there is nothing to drift.

What this deliberately does NOT do is compile the snippets. They are excerpts:
they interleave top-level definitions with statements, call functions the prose
introduces (`read_adc`, `toggle_led`), and elide bodies with `// ...`. Making
them compile would mean rewriting each one into a whole program -- which is what
examples/ already is, and would make the cookbook worse to read for a guarantee
examples/ already provides. So the snippets stay excerpts, D1 catches the drift
that actually bites, and D3 keeps the example they mirror real and running.

Usage:
    tools/check_docs.py
    tools/check_docs.py --self-test   # prove the checker still bites
"""

import argparse
import pathlib
import re
import subprocess
import sys

DOCS = ["README.md", "docs/COOKBOOK.md", "docs/CHOOSING.md", "docs/SCOPE.md",
        "docs/ROADMAP.md", "docs/TODO.md", "docs/AUDIT.md"]

# D1: `metl::` names the docs use on purpose that are not, and must not be,
# symbols. Each carries its reason, so a reader sees a decision.
NOT_A_SYMBOL = {
    "bad_": "prose about the `metl::bad_*_access` exceptions METL does NOT have",
    "exp": "the planned Tier 2 opt-in namespace `metl::exp::`, not a type",
}

# D5: a byte figure attributed to a Cortex-M target -- `cortex-m3 9999`,
# `m0 9999`, `m7: 9999` (deliberately impossible numbers here, so nothing in
# this file can be mistaken for a budget). Narrow on purpose: it wants a target
# name and a 3-to-5 digit number within a few characters of each other, so a
# bare `4236` elsewhere is not a finding and neither is `-mcpu=cortex-m3` on its
# own. Run across the whole tracked tree when it was written, it matched eight
# lines and all eight were the drift it exists to catch.
SIZE_FIGURE = re.compile(r"(?:cortex-)?\bm[0347]\+?\b[^\S\n]{0,4}[=:]?[^\S\n]{0,4}\d{3,5}\b", re.I)

# Files allowed to state a figure, each with the reason it has to.
SIZE_BUDGET_SOURCE = "tools/check_size.py"
SIZE_BUDGET_EXEMPT = {
    SIZE_BUDGET_SOURCE: "holds BUDGETS -- it is the single source",
    "tools/check_docs.py": "must contain the pattern to test for it: the regex "
                           "above and the self-test fixture below",
    # Listed rather than relied upon. Its `"cortex-m0": 136,` happens not to
    # match, because the quote sits where the regex wants whitespace -- which is
    # luck, not a decision, and a comment in that file written any other way
    # would fail a rule it is not the subject of. It holds the STACK budgets,
    # which are its own single source for the same reason.
    "tools/check_stack.py": "holds the stack-depth BUDGETS -- the single source "
                            "for those, as check_size.py is for code size",
}
SIZE_SCANNED_SUFFIXES = (".md", ".yml", ".yaml", ".py", ".txt", ".sh")

QUALIFIED = re.compile(r"\bmetl::([a-zA-Z_][a-zA-Z_0-9]*)")
RELATIVE_LINK = re.compile(r"\]\(((?!https?:|#)[^)\s]+\.(?:cpp|hpp|md|py|yml|txt))[^)]*\)")
EXAMPLE_REF = re.compile(r"examples/([a-zA-Z_0-9]+)\.cpp")


def public_header_text(root):
    return "\n".join(p.read_text(encoding="utf-8")
                     for p in sorted(pathlib.Path(root).rglob("*.hpp")))


def registered_examples(cmake_path):
    """Names in `set(_metl_examples ...)`.

    Comments are stripped BEFORE looking for the closing paren. They have to be:
    one of the entries is commented `# mmio + register_access + bitfield (fake
    peripheral)`, and a scan that respects that paren ends the list eight entries
    early -- which made this checker's first run report fifteen examples as
    unbuilt when every one of them was built. A gate that reads a list wrong is
    worse than no gate.
    """
    text = pathlib.Path(cmake_path).read_text(encoding="utf-8")
    uncommented = "\n".join(line.split("#")[0] for line in text.splitlines())
    match = re.search(r"set\(\s*_metl_examples\b(.*?)\)", uncommented, re.S)
    if not match:
        return None
    return set(match.group(1).split())


def fuzz_targets(repo_root):
    """Return (cmake_list, buildsh_list, on_disk) or None for a list that would not parse."""
    root = pathlib.Path(repo_root)

    cmake_path = root / "fuzz" / "CMakeLists.txt"
    buildsh_path = root / ".clusterfuzzlite" / "build.sh"
    if not cmake_path.exists() or not buildsh_path.exists():
        return None

    cmake_text = "\n".join(
        line.split("#")[0] for line in cmake_path.read_text(encoding="utf-8").splitlines())
    cmake_match = re.search(r"set\(\s*_metl_fuzz_targets\b(.*?)\)", cmake_text, re.S)

    # `FUZZERS="` ... `"` -- a plain shell string, one target per line.
    buildsh_match = re.search(r'FUZZERS="\s*(.*?)"', buildsh_path.read_text(encoding="utf-8"), re.S)

    if cmake_match is None or buildsh_match is None:
        return None

    on_disk = {p.stem for p in (root / "fuzz").glob("fuzz_*.cpp")}
    return set(cmake_match.group(1).split()), set(buildsh_match.group(1).split()), on_disk


def scannable_files(root):
    """Tracked text files, relative to `root`.

    `git ls-files` is asked first so the ~20 untracked build-* trees in a working
    checkout are skipped without naming them. It fails in the self-test, which
    builds its fixture outside any repository, so a plain walk is the fallback --
    correct there because that tree contains only what the fixture put in it.
    """
    try:
        listed = subprocess.run(["git", "ls-files"], cwd=root, capture_output=True,
                                text=True, check=True).stdout.split()
        paths = [pathlib.Path(name) for name in listed]
    except (subprocess.CalledProcessError, FileNotFoundError, OSError):
        paths = [p.relative_to(root) for p in pathlib.Path(root).rglob("*") if p.is_file()]
    return [p for p in paths if p.suffix in SIZE_SCANNED_SUFFIXES]


def check(repo_root="."):
    """Return a list of (rule, message)."""
    root = pathlib.Path(repo_root)
    problems = []

    headers = public_header_text(root / "include" / "metl")
    registered = registered_examples(root / "examples" / "CMakeLists.txt")
    if registered is None:
        problems.append(("D3", "could not find set(_metl_examples ...) in "
                               "examples/CMakeLists.txt -- has it been renamed?"))
        registered = set()

    for doc in DOCS:
        path = root / doc
        if not path.exists():
            problems.append(("D2", f"{doc}: listed in DOCS but does not exist"))
            continue
        text = path.read_text(encoding="utf-8")

        # D1
        for name in sorted(set(QUALIFIED.findall(text))):
            if name in NOT_A_SYMBOL:
                continue
            if not re.search(rf"\b{re.escape(name)}\b", headers):
                problems.append(("D1", f"{doc}: uses `metl::{name}`, which no public "
                                       f"header defines"))

        # D2
        for target in sorted(set(RELATIVE_LINK.findall(text))):
            if not (path.parent / target).resolve().exists():
                problems.append(("D2", f"{doc}: links to `{target}`, which does not exist"))

        # D3
        for name in sorted(set(EXAMPLE_REF.findall(text))):
            if name not in registered:
                problems.append(("D3", f"{doc}: points at examples/{name}.cpp, which is not "
                                       f"in set(_metl_examples) -- CI neither builds nor "
                                       f"runs it"))

    # D4: the two hand-maintained fuzz target lists, and what is actually there.
    targets = fuzz_targets(root)
    if targets is None:
        problems.append(("D4", "could not read both fuzz target lists -- has "
                               "set(_metl_fuzz_targets ...) or FUZZERS=\"...\" been renamed?"))
    else:
        cmake_targets, buildsh_targets, on_disk = targets
        for name in sorted(on_disk - cmake_targets):
            problems.append(("D4", f"fuzz/{name}.cpp is not in set(_metl_fuzz_targets) -- "
                                   f"`fuzz-smoke` does not build it"))
        for name in sorted(on_disk - buildsh_targets):
            problems.append(("D4", f"fuzz/{name}.cpp is not in .clusterfuzzlite/build.sh -- "
                                   f"no ClusterFuzzLite run will ever execute it"))
        for name in sorted(cmake_targets - on_disk):
            problems.append(("D4", f"set(_metl_fuzz_targets) names {name}, which has no "
                                   f"fuzz/{name}.cpp"))
        for name in sorted(buildsh_targets - on_disk):
            problems.append(("D4", f".clusterfuzzlite/build.sh names {name}, which has no "
                                   f"fuzz/{name}.cpp"))

    # D5: the size budgets live in one file. Anywhere else is a copy, and a copy
    # is a number waiting to go stale -- which is exactly what it did.
    for relative in sorted(scannable_files(root)):
        if relative.as_posix() in SIZE_BUDGET_EXEMPT:
            continue
        try:
            text = (root / relative).read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        for number, line in enumerate(text.splitlines(), 1):
            match = SIZE_FIGURE.search(line)
            if match:
                problems.append(("D5", f"{relative.as_posix()}:{number}: states a size figure "
                                       f"`{match.group(0).strip()}` -- the budgets live in "
                                       f"{SIZE_BUDGET_SOURCE}. Point at it; do not restate it."))

    # D3, the other direction: an example on disk that CI does not build.
    examples_dir = root / "examples"
    if examples_dir.is_dir():
        for source in sorted(examples_dir.glob("*.cpp")):
            if source.stem not in registered:
                problems.append(("D3", f"examples/{source.name} exists but is not in "
                                       f"set(_metl_examples) -- it is neither built nor run"))

    # D6: the gate table in SCOPE.md is complete in both directions. A new
    # checker cannot ship without appearing there, and the table cannot name a
    # tool that has been deleted or renamed.
    scope = root / "docs" / "SCOPE.md"
    if not scope.exists():
        problems.append(("D6", "docs/SCOPE.md is missing; the gate table lives there"))
    else:
        scope_text = scope.read_text(encoding="utf-8")
        on_disk = {path.name for path in sorted((root / "tools").glob("check_*.py"))}
        if not on_disk:
            problems.append(("D6", "no tools/check_*.py found at all -- a check over "
                                   "nothing would report success"))
        listed = set(re.findall(r"\.\./tools/(check_[a-z_]+\.py)", scope_text))
        for tool in sorted(on_disk - listed):
            problems.append(("D6", f"tools/{tool} is not in the gate table in "
                                   f"docs/SCOPE.md section 8. A gate nobody lists is "
                                   f"a gate a reader cannot know about, and the list "
                                   f"is how a stale claim about measurement gets "
                                   f"noticed."))
        for tool in sorted(listed - on_disk):
            problems.append(("D6", f"the gate table names tools/{tool}, which does "
                                   f"not exist. Renamed or deleted?"))

    return problems


def self_test():
    """Build a throwaway tree where each rule is violated exactly once."""
    import tempfile

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "include" / "metl").mkdir(parents=True)
        (root / "include" / "metl" / "real.hpp").write_text("class fixed_vector {};\n")
        (root / "examples").mkdir()
        # The comment on the first entry closes a paren on purpose: that is the
        # bug this checker shipped with, and the fixture keeps it from coming back.
        (root / "examples" / "CMakeLists.txt").write_text(
            "set(_metl_examples\n  good   # mmio + bitfield (fake peripheral)\n  also\n)\n")
        (root / "examples" / "good.cpp").write_text("int main(){}\n")
        (root / "examples" / "also.cpp").write_text("int main(){}\n")
        # D4 fixture: one harness registered in both lists, one missing from
        # build.sh -- the drift that silently drops it from every nightly run.
        (root / "fuzz").mkdir()
        (root / "fuzz" / "fuzz_kept.cpp").write_text("int main(){}\n")
        (root / "fuzz" / "fuzz_dropped.cpp").write_text("int main(){}\n")
        (root / "fuzz" / "CMakeLists.txt").write_text(
            "set(_metl_fuzz_targets\n  fuzz_kept   # a comment with a paren )\n  fuzz_dropped\n)\n")
        (root / ".clusterfuzzlite").mkdir()
        (root / ".clusterfuzzlite" / "build.sh").write_text('FUZZERS="\nfuzz_kept\n"\n')
        (root / "examples" / "orphan.cpp").write_text("int main(){}\n")
        # D5 fixture: the drift verbatim -- a workflow comment restating a budget
        # the ratchet has since moved past. `-mcpu=cortex-m3` on the line above is
        # the near miss that must NOT fire, or the rule is too blunt to keep.
        (root / "restated.yml").write_text(
            "# flags: -mcpu=cortex-m3 -mthumb\n#     cortex-m0  2780\n")
        (root / "docs").mkdir()
        (root / "README.md").write_text(
            "`metl::fixed_vector` is fine and `metl::ghost_type` is not.\n"
            "[missing](docs/nope.md)\n"
            "[unbuilt](examples/never.cpp)\n")
        for extra in DOCS[1:]:
            (root / extra).parent.mkdir(parents=True, exist_ok=True)
            (root / extra).write_text("nothing here\n")
        # SCOPE.md carries the gate table D6 reads. It lists one tool that
        # exists, one that does not, and omits one that does.
        (root / "docs" / "SCOPE.md").write_text(
            "| [`check_listed.py`](../tools/check_listed.py) | x | y |\n"
            "| [`check_ghost.py`](../tools/check_ghost.py) | x | y |\n")

        # D6 fixture: one checker on disk that the gate table omits, and one the
        # table names that does not exist. Both directions in one tree.
        (root / "tools").mkdir()
        (root / "tools" / "check_listed.py").write_text("# listed\n")
        (root / "tools" / "check_forgotten.py").write_text("# not in the table\n")

        found = {rule for rule, _ in check(root)}
        for rule in ("D1", "D2", "D3", "D4", "D5", "D6"):
            if rule not in found:
                failures.append(f"{rule} did not fire on a tree that violates it")

        # D5 must have fired on the restated budget and NOT on the -mcpu flag.
        d5 = [message for rule, message in check(root) if rule == "D5"]
        if len(d5) != 1:
            failures.append(f"D5 fired {len(d5)} times, expected exactly 1 (the "
                            f"restated budget, not the -mcpu flag): {d5}")

        # And the clean case must stay clean.
        (root / "README.md").write_text("`metl::fixed_vector` is fine.\n")
        (root / "examples" / "orphan.cpp").unlink()
        # The -mcpu line stays. A clean tree that still contains it is the proof
        # D5 is narrow enough to live with.
        (root / "restated.yml").write_text("# flags: -mcpu=cortex-m3 -mthumb\n")
        (root / ".clusterfuzzlite" / "build.sh").write_text(
            'FUZZERS="\nfuzz_kept\nfuzz_dropped\n"\n')
        # D6's clean case: the table names exactly the tools that exist.
        (root / "docs" / "SCOPE.md").write_text(
            "| [`check_listed.py`](../tools/check_listed.py) | x | y |\n"
            "| [`check_forgotten.py`](../tools/check_forgotten.py) | x | y |\n")
        remaining = check(root)
        if remaining:
            failures.append(f"clean tree was flagged: {remaining}")

    if failures:
        for failure in failures:
            print(f"SELF-TEST FAILED: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: D1-D6 each bite, and a clean tree is not flagged")
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
    print("docs OK: every metl:: name resolves, every relative link exists, "
          "every example is built and run by CI, every fuzz harness is in both "
          "lists, the size budgets are stated in exactly one file, and every\n"
          "      gate is listed in SCOPE.md section 8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
