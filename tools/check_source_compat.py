#!/usr/bin/env python3
"""Build the previous release's tests against the current headers (#343).

A minor or patch release promises source compatibility: code that compiled
against the last release still compiles, and still means the same thing. The
previous release's own tests and examples are the largest body of such code
there is, so this builds them -- from the last release tag -- with today's
include/ in place of the tag's, and runs them.

A failure is one of two things, and the fix says which:
  * a real break -- a removed overload, a narrowed constraint, a changed
    return type. It needs a major version, or the change needs undoing.
  * an old test that relied on behaviour the library never promised, or that
    asserted a bug since fixed. It goes in EXPECTED_BREAKS with the issue that
    explains it.

The baseline is the newest vX.Y.Z tag reachable from HEAD. EXPECTED_BREAKS is
keyed by that tag, so a new release starts with a clean slate, and an entry for
the current tag that no longer breaks is an error rather than a silent pass.

Usage:
    tools/check_source_compat.py                  # against the last release
    tools/check_source_compat.py --tag v1.2.2     # against another release
    tools/check_source_compat.py --self-test      # prove the check bites
"""

import argparse
import pathlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

REPO = pathlib.Path(__file__).resolve().parent.parent

# {tag: {old test source or ctest name: "why it may break (#issue)"}}
EXPECTED_BREAKS = {}

# make, on a failed object or link step:
#   make[2]: *** [CMakeFiles/metl_x_test.dir/tests/containers/x_test.cpp.o] Error 1
#   make[2]: *** [metl_x_test] Error 1
FAILED_OBJECT = re.compile(r"\*\*\* \[CMakeFiles/[^/\]]+\.dir/(.+?)\.o\] Error")
FAILED_LINK = re.compile(r"\*\*\* \[([A-Za-z0-9_]+)\] Error")
# ctest, after "The following tests FAILED:":
#   	 12 - metl_x_test (Failed)
FAILED_TEST = re.compile(r"^\s*\d+ - (\S+) \((Failed|SEGFAULT|Subprocess aborted|Timeout|Exception|"
                         r"Child aborted|ILLEGAL|BUS error|OTHER_FAULT)\)", re.MULTILINE)


def git(*args):
    return subprocess.run(["git", *args], cwd=REPO, capture_output=True, text=True, check=True).stdout.strip()


def previous_release():
    return git("describe", "--tags", "--abbrev=0", "--match", "v[0-9]*.[0-9]*.[0-9]*", "HEAD")


def stage(tag, root):
    """The tag's tree under `root`, with the working tree's include/ in place of its own."""
    archive = root / "tag.tar"
    subprocess.run(["git", "archive", "--format=tar", "-o", str(archive), tag], cwd=REPO, check=True)
    tree = root / "tree"
    tree.mkdir()
    with tarfile.open(archive) as tar:
        tar.extractall(tree, filter="data")
    archive.unlink()
    shutil.rmtree(tree / "include")
    shutil.copytree(REPO / "include", tree / "include")
    return tree


def build_and_test(tree, build, targets=None, compiler=None):
    """Returns (configure output or None, sorted list of what broke)."""
    configure = ["cmake", "-B", str(build), "-S", str(tree), "-DCMAKE_BUILD_TYPE=Debug", "-DMETL_INSTALL=OFF",
                 "-DMETL_BUILD_EXAMPLES=ON"]
    if compiler:
        configure.append(f"-DCMAKE_CXX_COMPILER={compiler}")
    result = subprocess.run(configure, capture_output=True, text=True)
    if result.returncode != 0:
        return result.stdout + result.stderr, []
    command = ["cmake", "--build", str(build), "-j"]
    for target in targets or []:
        command += ["--target", target]
    # Keep going past the first failure, so one run names every old file that broke.
    command += ["--", "-k"]
    result = subprocess.run(command, capture_output=True, text=True)
    output = result.stdout + result.stderr
    broken = set(FAILED_OBJECT.findall(output))
    broken |= {name for name in FAILED_LINK.findall(output) if name != "all"}
    if targets is None:
        tests = subprocess.run(["ctest", "--output-on-failure", "--no-tests=error", "-j", "4",
                                "--timeout", "300"],
                               cwd=build, capture_output=True, text=True)
        broken |= {name for name, _ in FAILED_TEST.findall(tests.stdout)}
        if tests.returncode != 0 and not broken:
            return tests.stdout[-2000:], []
    elif result.returncode != 0 and not broken:
        return output[-2000:], []
    return None, sorted(broken)


def check(tag, compiler=None, keep=None):
    expected = EXPECTED_BREAKS.get(tag, {})
    root = pathlib.Path(keep) if keep else pathlib.Path(tempfile.mkdtemp(prefix="metl-compat-"))
    try:
        if keep and root.exists():
            shutil.rmtree(root)
        root.mkdir(parents=True, exist_ok=True)
        tree = stage(tag, root)
        print(f"building {tag}'s tests and examples against the current include/")
        error, broken = build_and_test(tree, root / "build", compiler=compiler)
    finally:
        if not keep:
            shutil.rmtree(root, ignore_errors=True)
    if error is not None:
        print(f"::error::could not build or run {tag}'s tree at all:\n{error}", file=sys.stderr)
        return 1
    failures = 0
    for name in broken:
        if name in expected:
            print(f"  expected  {name}: {expected[name]}")
            continue
        print(f"::error::{name} from {tag} no longer builds or passes against the current headers. "
              f"Either the change breaks source compatibility (undo it, or it waits for a major "
              f"version) or the old test relied on something never promised (record it in "
              f"EXPECTED_BREAKS with the issue).", file=sys.stderr)
        failures += 1
    for name in sorted(set(expected) - set(broken)):
        print(f"::error::EXPECTED_BREAKS[{tag!r}] names {name}, which builds and passes: remove the entry.",
              file=sys.stderr)
        failures += 1
    if failures:
        return 1
    print(f"source compatible with {tag}: every old test and example builds and passes"
          + (f" ({len(expected)} recorded exception(s))" if expected else ""))
    return 0


def self_test(tag, compiler=None):
    """A deliberate signature change must fail, naming the old test it broke."""
    root = pathlib.Path(tempfile.mkdtemp(prefix="metl-compat-self-test-"))
    try:
        tree = stage(tag, root)
        header = tree / "include" / "metl" / "fixed_vector.hpp"
        text = header.read_text()
        if "bool try_push_back(" not in text:
            print("::error::self-test anchor `bool try_push_back(` is gone from fixed_vector.hpp; "
                  "pick another signature to change", file=sys.stderr)
            return 1
        header.write_text(text.replace("bool try_push_back(", "bool try_push_back_renamed("))
        error, broken = build_and_test(tree, root / "build", targets=["metl_fixed_vector_test"],
                                       compiler=compiler)
    finally:
        shutil.rmtree(root, ignore_errors=True)
    want = "tests/containers/fixed_vector_test.cpp"
    if error is not None or want not in broken:
        print(f"::error::self-test: renaming fixed_vector::try_push_back should break {want} "
              f"and name it; got {broken or error}", file=sys.stderr)
        return 1
    print(f"self-test passed: a renamed member breaks {tag}'s {want}, and the report names it")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--tag", help="release to check against (default: the newest one reachable from HEAD)")
    parser.add_argument("--compiler", help="CMAKE_CXX_COMPILER for the build")
    parser.add_argument("--keep", help="build here and keep it, instead of a temporary directory")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    try:
        tag = args.tag or previous_release()
    except subprocess.CalledProcessError:
        print("::error::no vX.Y.Z tag is reachable from HEAD. A shallow checkout has no tags: "
              "fetch with `fetch-depth: 0`.", file=sys.stderr)
        return 1
    if args.self_test:
        return self_test(tag, args.compiler)
    return check(tag, args.compiler, args.keep)


if __name__ == "__main__":
    sys.exit(main())
