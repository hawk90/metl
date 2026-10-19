#!/usr/bin/env python3
"""Keep the documentation's checkable claims checked.

docs/COOKBOOK.md opens with "Every section links a compiled, CI-run example
under examples/". That is a claim about CI, and until now nothing in CI
tested it -- the same shape of problem as a benchmark that asserts nothing or a
checklist item nobody measures. This script tests the parts of it that a machine
can settle:

  D1  every `metl::Name` used in the docs exists in a public header.
      Catches an API that was renamed or removed while the prose kept using it.
  D2  every relative link in every tracked Markdown file resolves: inline
      links, reference-style definitions and HTML attributes, to any target.
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
  D6  every tools/check_*.py appears in the gate table in docs/SCOPE.md section
      8, and the table names no checker that has been deleted or renamed.
  D7  README.md states no test-suite size ("N tests"). It states the rule
      instead -- every test except a reasoned deny-list -- which does not move
      when a test is added. A stated figure went stale (71 and 68 where the
      runner produced 76 and 72), and checking it made every PR that added a
      test edit the same README line.
  D8  every tests/**/*_test.cpp is registered in CMakeLists.txt -- in
      set(_metl_tests) or as a metl_cc_test SRCS -- and the list names no file
      that is gone. The QEMU runner and the amalgamation check glob tests/, so
      an unregistered test still runs there and looks covered, while no host,
      sanitizer, MSVC or coverage leg ever builds it.

D5 is a different shape from the others and the difference is the point. D1-D4
check that two things agree; D5 forbids the second thing from existing. It was
added after the size budgets were raised in #66 and the figures quoted in
.github/workflows/ci.yml and a planning note were not, so the repository stated
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
import os
import pathlib
import re
import subprocess
import sys

DOCS = ["README.md", "docs/COOKBOOK.md", "docs/CHOOSING.md", "docs/SCOPE.md",
        "CONTRIBUTING.md", "SECURITY.md"]

# D1: `metl::` names the docs use on purpose that are not, and must not be,
# symbols. Each carries its reason, so a reader sees a decision.
NOT_A_SYMBOL = {
    "bad_": "prose about the `metl::bad_*_access` exceptions METL does NOT have",
    "exp": "the Tier 2 opt-in namespace `metl::exp::`, not a type",
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

# D7: a test-suite size stated in prose. README said "71 tests per core" and
# "68 tests" for the M0 build; the real figures were 76 and 72 and had been for
# some time. Nothing was wrong with the runner -- the number is DERIVED from a
# glob over tests/, so it moves every time a test is added.
#
# The first remedy checked the figure against `run_qemu_tests.py --plan`. That
# kept it true, but every PR that added a test then edited the same README line,
# and each merge put the other open PRs into conflict; a figure the check could
# not see ("106 for an ARMv6-M build") still went stale. So the README states the
# rule the runner follows -- every test except its reasoned deny-list -- and no
# figure at all. The exact count is in the qemu-conformance job's log.
TEST_COUNT_FIGURE = re.compile(r"(?<![-\w.])(\d+)\s+(?:tests?|test files)\b")

QUALIFIED = re.compile(r"\bmetl::([a-zA-Z_][a-zA-Z_0-9]*)")
# A target with a scheme (https:, mailto:) or only an anchor is not a file.
_NOT_A_FILE = r"(?![A-Za-z][\w+.-]*:|#)"
RELATIVE_LINK = re.compile(r"\]\(\s*<?(" + _NOT_A_FILE + r"[^)\s>]+)")
REFERENCE_LINK = re.compile(r"^ {0,3}\[[^\]]+\]:\s*<?(" + _NOT_A_FILE + r"[^\s>]+)", re.M)
# Code is not prose: a lambda's `[](auto x)` is not a link. A code span may
# wrap onto the next line, but not across a paragraph break.
CODE = re.compile(r"^ {0,3}(```|~~~).*?^ {0,3}\1[^\n]*$|`(?:[^`\n]|\n(?![ \t]*\n))*`", re.M | re.S)
# The README header is HTML (a centred logo and badges), so its links are
# `href=` / `src=` / `srcset=` attributes, which the markdown pattern above
# never sees.
HTML_LINK = re.compile(r'(?:href|src|srcset)="((?!https?:|#|mailto:)[^"\s#]+)')
EXAMPLE_REF = re.compile(r"examples/([a-zA-Z_0-9]+)\.cpp")


def public_header_text(root):
    """Every public header, comments removed: a name a comment still mentions
    after its declaration was deleted is exactly the drift D1 exists for."""
    text = "\n".join(p.read_text(encoding="utf-8")
                     for p in sorted(pathlib.Path(root).rglob("*.hpp")))
    return re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", text, flags=re.S))


# Words that end a statement's head rather than a return type: `return
# name(...)` is a call, not a declaration.
_STATEMENT_WORDS = {"return", "co_return", "else", "case", "throw", "new", "delete", "sizeof",
                    "not", "and", "or", "if", "while", "for", "switch"}


def declares(name, headers):
    """True if `headers` declares `name`: a type, namespace, alias, variable or
    function -- not merely a word that contains or mentions it."""
    word = re.escape(name)
    shapes = (
        rf"\b(?:class|struct|union|enum|namespace|concept)\s+(?:class\s+|struct\s+)?{word}\b",
        rf"\busing\s+{word}\s*=",
        rf"\bconstexpr\b[^;=(){{}}]*[\s*&>]{word}\s*[={{\[]",
    )
    if any(re.search(shape, headers) for shape in shapes):
        return True
    # A function: the name, an optional explicit template argument list, `(`,
    # after a return type -- which ends in a word, `>`, `)`, `*` or `&`, possibly
    # on the line above. A call follows `(`, `,`, `=`, `.` or a statement word.
    for match in re.finditer(rf"(?<![\w.>:]){word}\s*(?:<[^;{{}}()]*>)?\s*\(", headers):
        before = headers[max(0, match.start() - 256):match.start()].rstrip()
        if not before or before[-1] not in "_>)*&" and not before[-1].isalnum():
            continue
        last = re.search(r"(\w+)$", before)
        if last and last.group(1) in _STATEMENT_WORDS:
            continue
        return True
    return False


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


def registered_tests(cmake_path):
    """Tests CMakeLists.txt builds, as paths under tests/ without `_test.cpp`."""
    text = pathlib.Path(cmake_path).read_text(encoding="utf-8")
    uncommented = "\n".join(line.split("#")[0] for line in text.splitlines())
    match = re.search(r"set\(\s*_metl_tests\b(.*?)\)", uncommented, re.S)
    if not match:
        return None
    names = set(match.group(1).split())
    # A variable (`tests/${_t}_test.cpp`, the loop over the list) is not a file.
    # Every *_test.cpp in a SRCS list, not only the first: a multi-TU test names
    # its translation units in link order, and the one with main() need not lead.
    for sources in re.findall(r"SRCS((?:\s+[^\s()]+)+)", uncommented):
        names |= set(re.findall(r"tests/([\w/]+)_test\.cpp", sources))
    return names


def check_test_registration(root):
    cmake = root / "CMakeLists.txt"
    tests_dir = root / "tests"
    if not cmake.is_file() or not tests_dir.is_dir():
        return []
    registered = registered_tests(cmake)
    if registered is None:
        return [("D8", "could not find set(_metl_tests ...) in CMakeLists.txt")]
    on_disk = {path.relative_to(tests_dir).as_posix()[:-len("_test.cpp")]
               for path in tests_dir.rglob("*_test.cpp")}
    problems = []
    for name in sorted(on_disk - registered):
        problems.append(("D8", f"tests/{name}_test.cpp is not registered in CMakeLists.txt "
                               f"-- no host, sanitizer, MSVC or coverage leg builds it"))
    for name in sorted(registered - on_disk):
        problems.append(("D8", f"CMakeLists.txt registers tests/{name}_test.cpp, which "
                               f"does not exist"))
    return problems


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

    The GIT_* variables are dropped from git's environment. Inside a git hook
    (tools/pre_push.py runs from the pre-push hook) git exports GIT_DIR, and
    with an absolute GIT_DIR -- every worktree gets one -- `git ls-files` in
    the fixture directory silently lists the real repository instead of
    failing, so the fixture is never scanned and the self-test reports D5 as
    not firing. Discovery from `cwd` is the only behaviour wanted here.
    """
    env = {name: value for name, value in os.environ.items() if not name.startswith("GIT_")}
    try:
        listed = subprocess.run(["git", "ls-files"], cwd=root, capture_output=True,
                                text=True, check=True, env=env).stdout.split()
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
            if not declares(name, headers):
                problems.append(("D1", f"{doc}: uses `metl::{name}`, which no public "
                                       f"header defines"))

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

    # D2: every relative link in every Markdown file resolves.
    scanned = sorted(scannable_files(root))
    for relative in (p for p in scanned if p.suffix == ".md"):
        try:
            prose = CODE.sub("", (root / relative).read_text(encoding="utf-8"))
        except (UnicodeDecodeError, OSError) as error:
            problems.append(("D2", f"{relative.as_posix()}: cannot be read ({error})"))
            continue
        links = (set(RELATIVE_LINK.findall(prose)) | set(REFERENCE_LINK.findall(prose))
                 | set(HTML_LINK.findall(prose)))
        for target in sorted(links):
            file = re.split(r"[#?]", target)[0]
            if file and not (root / relative.parent / file).resolve().exists():
                problems.append(("D2", f"{relative.as_posix()}: links to `{target}`, which does not exist"))

    # D5: the size budgets live in one file. Anywhere else is a copy, and a copy
    # is a number waiting to go stale -- which is exactly what it did.
    for relative in scanned:
        if relative.as_posix() in SIZE_BUDGET_EXEMPT:
            continue
        try:
            text = (root / relative).read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError) as error:
            # A file this rule cannot read is a file it did not check.
            problems.append(("D5", f"{relative.as_posix()}: cannot be read as UTF-8 text, so it "
                                   f"cannot be checked for a size figure ({error})"))
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
        # Section 8 only: a link from elsewhere in SCOPE.md is not a table row.
        table = re.search(r"^## 8\..*?(?=^## |\Z)", scope_text, re.M | re.S)
        if table is None:
            problems.append(("D6", "docs/SCOPE.md has no `## 8.` section; the gate table "
                                   "lives there"))
        listed = set(re.findall(r"\.\./tools/(check_[a-z_]+\.py)", table.group(0) if table else ""))
        for tool in sorted(on_disk - listed):
            problems.append(("D6", f"tools/{tool} is not in the gate table in "
                                   f"docs/SCOPE.md section 8. A gate nobody lists is "
                                   f"a gate a reader cannot know about, and the list "
                                   f"is how a stale claim about measurement gets "
                                   f"noticed."))
        for tool in sorted(listed - on_disk):
            problems.append(("D6", f"the gate table names tools/{tool}, which does "
                                   f"not exist. Renamed or deleted?"))

    # D7: the README states no test-suite size.
    problems.extend(check_test_counts(root))

    # D8: every test on disk is built by CMake.
    problems.extend(check_test_registration(root))

    return problems


def check_test_counts(root):
    readme = pathlib.Path(root) / "README.md"
    if not readme.exists():
        return []
    problems = []
    for number, line in enumerate(readme.read_text(encoding="utf-8").splitlines(), 1):
        for match in TEST_COUNT_FIGURE.finditer(line):
            problems.append(("D7", f"README.md:{number}: states `{match.group(0)}`. A test "
                                   f"count is derived from the tree and goes stale; state "
                                   f"the rule (every test except the deny-list in "
                                   f"tools/run_qemu_tests.py) instead of the figure."))
    return problems


def self_test():
    """Build a throwaway tree where each rule is violated exactly once."""
    import tempfile

    failures = []
    with tempfile.TemporaryDirectory() as tmp:
        root = pathlib.Path(tmp)
        (root / "include" / "metl").mkdir(parents=True)
        # D1 wants a declaration, not the word: `object_pool` here is only a
        # comment and a parameter name, as a deleted type's name lingers in
        # the headers that used to mention it.
        (root / "include" / "metl" / "real.hpp").write_text(
            "class fixed_vector {};\n"
            "// object_pool was removed in favour of fixed_vector.\n"
            "void reserve(int object_pool_size);\n"
            "namespace coro {}\n"
            "enum class hex_case { lower };\n"
            "using byte_view = fixed_vector;\n"
            "inline constexpr hex_case default_case{};\n"
            "template <typename T> constexpr T* parse_it(T* text) noexcept;\n"
            "METL_NODISCARD inline bool\nformat_it(char* out) noexcept;\n")
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
            "`metl::object_pool` is gone; only a comment still names it.\n"
            "[missing](docs/nope.md)\n"
            "<img src=\"docs/nope.svg\" alt=\"\">\n"
            "[unbuilt](examples/never.cpp)\n")
        for extra in DOCS[1:]:
            (root / extra).parent.mkdir(parents=True, exist_ok=True)
            (root / extra).write_text("nothing here\n")
        # SCOPE.md carries the gate table D6 reads. It lists one tool that
        # exists, one that does not, and omits one that does.
        # check_forgotten.py is linked from another section only: the table
        # in section 8 still omits it.
        (root / "docs" / "SCOPE.md").write_text(
            "## 7. Before\n\nSee [`check_forgotten.py`](../tools/check_forgotten.py).\n\n"
            "## 8. The gates\n\n"
            "| [`check_listed.py`](../tools/check_listed.py) | x | y |\n"
            "| [`check_ghost.py`](../tools/check_ghost.py) | x | y |\n\n## 9. After\n")

        # D6 fixture: one checker on disk that the gate table omits, and one the
        # table names that does not exist. Both directions in one tree.
        (root / "tools").mkdir()
        (root / "tools" / "check_listed.py").write_text("# listed\n")
        (root / "tools" / "check_forgotten.py").write_text("# not in the table\n")

        # D7 fixture: the README below states a test count.
        readme_broken = ("`metl::fixed_vector` is fine and `metl::ghost_type` is not.\n"
                         "`metl::object_pool` is gone; only a comment still names it.\n"
                         "[missing](docs/nope.md)\n"
                         "<img src=\"docs/nope.svg\" alt=\"\">\n"
                         "[unbuilt](examples/never.cpp)\n"
                         "runs 9 tests per core\n")
        (root / "README.md").write_text(readme_broken)

        # D8 fixture: one test in the list, one wired through SRCS (as the
        # threaded tests are), one on disk that neither names, one listed but gone.
        (root / "tests" / "a").mkdir(parents=True)
        for name in ("listed", "wired", "orphan"):
            (root / "tests" / "a" / f"{name}_test.cpp").write_text("int main(){}\n")
        (root / "CMakeLists.txt").write_text(
            "set(_metl_tests\n  a/listed  # (a comment with a paren)\n  a/gone\n)\n"
            "metl_cc_test(NAME t SRCS tests/a/wired_test.cpp)\n")

        found = {rule for rule, _ in check(root)}
        d1 = [message for rule, message in check(root) if rule == "D1"]
        if not any("metl::object_pool" in message for message in d1):
            failures.append(f"D1 took a name in a comment for a declaration: {d1}")
        d6 = [message for rule, message in check(root) if rule == "D6"]
        if not any("check_forgotten.py is not in the gate table" in message for message in d6):
            failures.append(f"D6 took a link outside section 8 for a table row: {d6}")
        d8 = sorted(message for rule, message in check(root) if rule == "D8")
        if len(d8) != 2 or "orphan" not in d8[1] or "a/gone" not in d8[0]:
            failures.append(f"D8 should flag exactly the orphan and the vanished test: {d8}")
        for rule in ("D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8"):
            if rule not in found:
                failures.append(f"{rule} did not fire on a tree that violates it")

        # D2 must see HTML attributes too, not just markdown links.
        if not any(rule == "D2" and "nope.svg" in message for rule, message in check(root)):
            failures.append("D2 did not flag a broken HTML <img src=> link")

        # D2 reads every Markdown file, not only DOCS; reference-style links
        # too; and any relative target, not only some extensions. A lambda's
        # `[](auto x)` in code is not a link.
        (root / "docs" / "EXTRA.md").write_text(
            "[gone](missing.md)\n[dir](nodir/)\n\n[ref]: gone.cpp\n"
            "```cpp\nauto f = [](auto x) { return x; };\n```\nand `[](auto y)` inline\n")
        d2 = [message for rule, message in check(root) if rule == "D2"]
        for want in ("docs/EXTRA.md: links to `missing.md`", "docs/EXTRA.md: links to `nodir/`",
                     "docs/EXTRA.md: links to `gone.cpp`"):
            if not any(want in message for message in d2):
                failures.append(f"D2 missed {want!r}: {d2}")
        if any("auto" in message for message in d2):
            failures.append(f"D2 read a link inside code: {d2}")
        (root / "docs" / "EXTRA.md").write_text(
            "[ok](SCOPE.md#8-the-gates)\n\n[ref]: ../README.md\n```cpp\nauto f = [](auto x) {};\n```\n")

        # D5 cannot skip a file it cannot decode: that file is unchecked.
        (root / "binary.yml").write_bytes(b"\xff\xfe cortex-m0 9999\n")
        if not any(rule == "D5" and "binary.yml" in message for rule, message in check(root)):
            failures.append("D5 skipped a file it could not decode")
        (root / "binary.yml").unlink()

        # D5 must have fired on the restated budget and NOT on the -mcpu flag.
        d5 = [message for rule, message in check(root) if rule == "D5"]
        if len(d5) != 1:
            failures.append(f"D5 fired {len(d5)} times, expected exactly 1 (the "
                            f"restated budget, not the -mcpu flag): {d5}")

        # And the clean case must stay clean.
        (root / "README.md").write_text("`metl::fixed_vector` is fine.\n"
                                        "`metl::coro`, `metl::hex_case`, `metl::byte_view`, "
                                        "`metl::default_case`, `metl::parse_it` and "
                                        "`metl::format_it` are declared.\n"
                                        "runs every test except the deny-list\n")
        (root / "examples" / "orphan.cpp").unlink()
        (root / "tests" / "a" / "orphan_test.cpp").unlink()
        (root / "CMakeLists.txt").write_text(
            "set(_metl_tests\n  a/listed\n)\nmetl_cc_test(NAME t SRCS tests/a/wired_test.cpp)\n")
        # The fixture must stay hermetic inside a git hook, where GIT_DIR points
        # at the real repository: scanning has to follow `cwd`, not GIT_DIR, or
        # the restated budget above is never read (the failure seen from a
        # worktree's pre-push hook, 2026-10-06).
        gitdir = subprocess.run(["git", "rev-parse", "--absolute-git-dir"],
                                cwd=pathlib.Path(__file__).resolve().parent,
                                capture_output=True, text=True)
        if gitdir.returncode == 0:
            saved = os.environ.get("GIT_DIR")
            os.environ["GIT_DIR"] = gitdir.stdout.strip()
            try:
                hooked = [message for rule, message in check(root) if rule == "D5"]
            finally:
                if saved is None:
                    del os.environ["GIT_DIR"]
                else:
                    os.environ["GIT_DIR"] = saved
            if len(hooked) != 1:
                failures.append(f"D5 fired {len(hooked)} times with GIT_DIR set, expected "
                                f"exactly 1: the scan followed GIT_DIR instead of the fixture")

        # The -mcpu line stays. A clean tree that still contains it is the proof
        # D5 is narrow enough to live with.
        (root / "restated.yml").write_text("# flags: -mcpu=cortex-m3 -mthumb\n")
        (root / ".clusterfuzzlite" / "build.sh").write_text(
            'FUZZERS="\nfuzz_kept\nfuzz_dropped\n"\n')
        # D6's clean case: the table names exactly the tools that exist.
        (root / "docs" / "SCOPE.md").write_text(
            "## 8. The gates\n\n"
            "| [`check_listed.py`](../tools/check_listed.py) | x | y |\n"
            "| [`check_forgotten.py`](../tools/check_forgotten.py) | x | y |\n")
        remaining = check(root)
        if remaining:
            failures.append(f"clean tree was flagged: {remaining}")

    if failures:
        for failure in failures:
            print(f"SELF-TEST FAILED: {failure}", file=sys.stderr)
        return 1
    print("self-test passed: D1-D8 each bite, and a clean tree is not flagged")
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
          "lists, the size budgets are stated in exactly one file, every\n"
          "      gate is listed in SCOPE.md section 8, the README states no test count, "
          "and every test is registered")
    return 0


if __name__ == "__main__":
    sys.exit(main())
