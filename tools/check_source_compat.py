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

# Where a tree states its own version: (file, pattern whose group 2 is the version).
VERSION_STAMPS = [
    ("CMakeLists.txt", re.compile(r"(\n  VERSION )([0-9]+\.[0-9]+\.[0-9]+)(\n)")),
    ("components/metl/idf_component.yml", re.compile(r"(\nversion: \")([0-9.]+)(\"\n)")),
]

# The target a failed build step names. make: GNU make 4 puts the makefile and
# line before it, make 3.81 (macOS) does not. Ninja: newer releases add the
# exit code. PARSE_CASES has one line of each.
FAILED_MAKE = re.compile(r"\*\*\* \[(?:[^\]]*?:\d+: )?([^\]]+)\] Error")
FAILED_NINJA = re.compile(r"^FAILED: (?:\[code=\d+\] )?(.+?)\s*$", re.MULTILINE)
# An object, at the top level or in a subdirectory: <dir/>CMakeFiles/<t>.dir/<source>.o,
# where <source> is relative to <dir>.
OBJECT = re.compile(r"^((?:.*/)?)CMakeFiles/[^/]+\.dir/(.+?)\.(?:o|obj)$")
# ctest, after "The following tests FAILED:":
#   	 12 - metl_x_test (Failed)
FAILED_TEST = re.compile(r"^\s*\d+ - (\S+) \((Failed|SEGFAULT|Subprocess aborted|Timeout|Exception|"
                         r"Child aborted|ILLEGAL|BUS error|OTHER_FAULT)\)", re.MULTILINE)


def build_failures(output):
    """The old sources and link targets a build's output says failed."""
    broken = set()
    for target in FAILED_MAKE.findall(output) + [name for line in FAILED_NINJA.findall(output)
                                                 for name in line.split()]:
        obj = OBJECT.match(target)
        if obj:
            broken.add(obj.group(1) + obj.group(2))
        elif "CMakeFiles/" not in target and pathlib.PurePosixPath(target).name != "all":
            broken.add(pathlib.PurePosixPath(target).name)  # a link step: the target's name
    return broken


def keep_going(generator):
    """The native build tool's keep-going flag for a CMake generator."""
    if "Ninja" in generator:
        return ["-k", "0"]  # `ninja -k` takes a count; 0 means no limit
    if "Makefiles" in generator:
        return ["-k"]
    return []


# (build output, what it must name), in the forms make 3.81, make 4 and Ninja
# print, for targets at the top level and in a subdirectory.
PARSE_CASES = [
    ("make[2]: *** [CMakeFiles/metl_x_test.dir/build.make:79: CMakeFiles/metl_x_test.dir/tests/x_test.cpp.o] Error 1",
     {"tests/x_test.cpp"}),
    ("make[2]: *** [CMakeFiles/metl_x_test.dir/tests/x_test.cpp.o] Error 1", {"tests/x_test.cpp"}),
    ("make[2]: *** [CMakeFiles/metl_x_test.dir/build.make:120: metl_x_test] Error 1", {"metl_x_test"}),
    ("make[2]: *** [tests/CMakeFiles/metl_x_test.dir/build.make:76: "
     "tests/CMakeFiles/metl_x_test.dir/containers/x_test.cpp.o] Error 1", {"tests/containers/x_test.cpp"}),
    ("make[2]: *** [tests/CMakeFiles/metl_x_test.dir/build.make:120: tests/metl_x_test] Error 1", {"metl_x_test"}),
    ("make[1]: *** [CMakeFiles/Makefile2:1234: tests/CMakeFiles/metl_x_test.dir/all] Error 2\n"
     "make: *** [Makefile:146: all] Error 2", set()),
    ("FAILED: CMakeFiles/metl_x_test.dir/tests/x_test.cpp.o \n/usr/bin/c++ -c x", {"tests/x_test.cpp"}),
    ("FAILED: [code=1] tests/CMakeFiles/metl_x_test.dir/containers/x_test.cpp.o", {"tests/containers/x_test.cpp"}),
    ("FAILED: [code=1] tests/metl_x_test \n: && /usr/bin/c++ ...", {"metl_x_test"}),
]


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
    # `git archive` honours export-ignore, which would drop old tests without a
    # word; every file the tag holds must be here.
    missing = [name for name in git("ls-tree", "-r", "--name-only", tag).splitlines()
               if not (tree / name).exists() and not (tree / name).is_symlink()]
    if missing:
        raise SystemExit(f"::error::git archive {tag} left out {len(missing)} file(s), "
                         f"such as {missing[0]}; export-ignore in .gitattributes?")
    shutil.rmtree(tree / "include")
    shutil.copytree(REPO / "include", tree / "include")
    restamp(tree)
    return tree


def restamp(tree):
    """Give the staged tree the version of the headers it now holds (#403).

    tests/core/version_test.cpp checks that metl::version matches the
    CMakeLists project(VERSION), and the CMakeLists checks the ESP-IDF manifest
    against it. Left at the tag's version, the old test fails on every release
    PR, though the version stamps are no API promise.
    """
    for name, stamp in VERSION_STAMPS:
        current = stamp.search((REPO / name).read_text(encoding="utf-8"))
        path = tree / name
        text = path.read_text(encoding="utf-8")
        if current is None or not stamp.search(text):
            raise SystemExit(f"::error::no version stamp in {name}; update VERSION_STAMPS in "
                             f"{pathlib.Path(__file__).name}")
        path.write_text(stamp.sub(lambda match: match.group(1) + current.group(2) + match.group(3), text, count=1),
                        encoding="utf-8")


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
    # Keep going past the first failure, so one run names every old file that
    # broke. The flag is the native tool's: `-- -k` alone stops Ninja at once.
    cache = (pathlib.Path(build) / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
    generator = re.search(r"^CMAKE_GENERATOR:INTERNAL=(.*)$", cache, re.MULTILINE)
    flags = keep_going(generator.group(1) if generator else "")
    if flags:
        command += ["--", *flags]
    result = subprocess.run(command, capture_output=True, text=True)
    output = result.stdout + result.stderr
    broken = build_failures(output)
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
    # The parsing first: it needs no build, and a form it misses would leave
    # the build below reporting "could not build" instead of the file's name.
    for output, names in PARSE_CASES:
        if build_failures(output) != names:
            print(f"::error::self-test: build_failures({output!r}) = {build_failures(output)}, want {names}",
                  file=sys.stderr)
            return 1
    for generator, flags in (("Unix Makefiles", ["-k"]), ("Ninja", ["-k", "0"]),
                             ("Ninja Multi-Config", ["-k", "0"]), ("Xcode", [])):
        if keep_going(generator) != flags:
            print(f"::error::self-test: keep_going({generator!r}) = {keep_going(generator)}, want {flags}",
                  file=sys.stderr)
            return 1
    root = pathlib.Path(tempfile.mkdtemp(prefix="metl-compat-self-test-"))
    try:
        tree = stage(tag, root)
        for name, stamp in VERSION_STAMPS:
            staged = stamp.search((tree / name).read_text(encoding="utf-8")).group(2)
            current = stamp.search((REPO / name).read_text(encoding="utf-8")).group(2)
            if staged != current:
                print(f"::error::self-test: the staged {name} says {staged}, but the headers it builds "
                      f"are {current}", file=sys.stderr)
                return 1
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
