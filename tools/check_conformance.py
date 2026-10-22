#!/usr/bin/env python3
"""Keep the conformance suite's registry complete (#320).

tests/conformance/ checks each property once, against every type in its
registry. That only helps if the registry keeps up with the library: the
cross-type tests it replaces each had a hand-picked type list, and the arena was
missing from the re-entrancy one when #312 shipped. So the default for a public
header is "registered", and anything else has to say why.

Every public header under include/metl (detail/ excluded) must be in exactly one
of three places:

  registered  tagged `// conformance-header: <header>` above an adapter in
              tests/conformance/conformance.hpp, and that adapter listed in
              `registry`
  EXEMPT      owns no user object, so no property applies. Each entry says why.
  PENDING     owns user objects and has no adapter yet. This list may only
              SHRINK, and an entry that gets registered must leave it.

Tier 2 headers under exp/ follow the same rule, with no blanket exemption: an
experimental type that holds user objects is registered like any other, so a
bug class the suite checks cannot come back through the experimental door.

It also checks each known gap (`gap_<property> = "#NNN"` in an adapter): it must
name an issue, and the property must exist in a test. The suite itself fails
when a gap starts passing, so the gaps cannot go stale silently.

With --base, PENDING is compared with the merge base's: an entry the base
does not have fails, which is what "may only SHRINK" needs to mean anything.

Usage:
    tools/check_conformance.py [--base origin/main]
    tools/check_conformance.py --self-test   # prove the checker still bites
"""

import argparse
import ast
import pathlib
import re
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
INCLUDE = REPO / "include" / "metl"
SUITE = REPO / "tests" / "conformance"
REGISTRY = SUITE / "conformance.hpp"

# Headers that own no user object: macros, traits, value types, non-owning
# views and references, and byte-level utilities. Each entry says why.
EXEMPT = {
    "assert.hpp": "assertion macros",
    "attributes.hpp": "attribute macros",
    "compiler.hpp": "compiler detection macros",
    "config.hpp": "configuration macros",
    "in_place.hpp": "empty tag types",
    "metl.hpp": "umbrella header; its types are registered under their own headers",
    "optimization.hpp": "hint macros",
    "type_traits.hpp": "compile-time traits",
    "version.hpp": "version constants",
    "bit.hpp": "functions on integers",
    "bitfield.hpp": "functions on integers",
    "crc8.hpp": "functions on bytes",
    "crc16.hpp": "functions on bytes",
    "crc32.hpp": "functions on bytes",
    "endian.hpp": "functions on integers",
    "hash.hpp": "hash functions; hold no object",
    "format.hpp": "functions writing characters to a caller's buffer",
    "parse.hpp": "functions reading characters from a caller's buffer",
    "mmio.hpp": "register accessors over addresses the caller owns",
    "register_access.hpp": "volatile access functions and barriers",
    "wait.hpp": "CPU hint functions",
    "span.hpp": "non-owning view",
    "function_ref.hpp": "non-owning reference to a callable",
    "delegate.hpp": "non-owning function and object pointer",
    "atomic_ref.hpp": "non-owning reference to a caller's object",
    "atomic_handle.hpp": "holds a handle value, not an object",
    "versioned_handle.hpp": "a value type",
    "tick_extender.hpp": "holds integers only",
    "spsc_byte_ring.hpp": "holds bytes, not objects",
    "intrusive_ptr.hpp": "a reference count on the caller's object; the object is not stored",
    "event_dispatcher.hpp": "holds delegates, which do not own their targets",
    "fsm.hpp": "holds a transition table of function pointers",
    "coro/protothread.hpp": "a base class for a task the caller owns",
    "coro/stepper.hpp": "a base class for a task the caller owns",
    "coro/scheduler.hpp": "holds pointers to tasks the caller owns",
    "coro/deadline_scheduler.hpp": "holds pointers to tasks the caller owns",
    "fixed_string.hpp": "holds characters, not objects",
    "lookup_table.hpp": "built once from its entries; nothing is added or removed",
    "monotonic_buffer.hpp": "builds objects but never destroys them; their lifetime is the caller's",
    "static_allocator.hpp": "hands out storage; constructs nothing",
    "scope_exit.hpp": "holds one callable for its whole lifetime; nothing is added or removed",
    "lock.hpp": "guarded<T> holds one T for its whole lifetime; nothing is added or removed",
    "exp/bitset.hpp": "holds bits, not objects",
    "exp/intrusive_forward_list.hpp": ("links objects the caller owns; stores, constructs and destroys none. "
                    "std parity: agrees_with_std_forward_list in its own test"),
    "exp/intrusive_list.hpp": ("links objects the caller owns; stores, constructs and destroys none. "
                    "std parity: agrees_with_std_list in its own test"),
}

# Own user objects, no adapter yet. MAY ONLY SHRINK: --base fails an entry the
# merge base did not have.
PENDING = set()

TAG = re.compile(r"^// conformance-header:\s*(\S+)\s*\n(?://.*\n)*struct (\w+)", re.M)
REGISTRY_LIST = re.compile(r"using registry = std::tuple<([^>]*)>;", re.S)
GAP = re.compile(r"static constexpr const char\* gap_(\w+) = \"([^\"]*)\";")
DECLARED = re.compile(r"METL_CONFORMANCE_DECLARE_GAP\((\w+)\)")
ISSUE = re.compile(r"^#\d+$")
INVALIDATION = re.compile(r"^/// @par (Iterator|Reference) invalidation$", re.M)


def pending_in(text):
    """The PENDING set a version of this file assigns. Raises ValueError if it
    assigns none: a base whose PENDING cannot be read must not read as empty,
    which would let any entry count as growth -- or, compared the other way,
    let any growth pass."""
    for node in ast.parse(text).body:
        if isinstance(node, ast.Assign) and [getattr(t, "id", None) for t in node.targets] == ["PENDING"]:
            value = node.value
            if isinstance(value, ast.Call) and getattr(value.func, "id", None) == "set" and not value.args:
                return set()
            return set(ast.literal_eval(value))
    raise ValueError("no `PENDING = ...` assignment")


def pending_growth(pending, base_pending):
    """Entries in `pending` the base did not have, sorted."""
    return sorted(set(pending) - set(base_pending))


def base_pending(base):
    """PENDING as this file assigned it at the merge base of `base` and HEAD."""
    merge_base = subprocess.run(["git", "merge-base", base, "HEAD"], cwd=REPO,
                                capture_output=True, text=True, check=True).stdout.strip()
    relative = pathlib.Path(__file__).resolve().relative_to(REPO).as_posix()
    text = subprocess.run(["git", "show", f"{merge_base}:{relative}"], cwd=REPO,
                          capture_output=True, text=True, check=True).stdout
    return pending_in(text)


def public_headers(include_dir):
    return sorted(p.relative_to(include_dir).as_posix()
                  for p in include_dir.rglob("*.hpp") if "detail" not in p.parts)


def check(headers, registry_text, test_texts, exempt, pending, header_texts=None):
    """Return a list of problems (strings). `header_texts` maps a header to its
    text; when given, every registered header must state its invalidation rule."""
    problems = []

    tagged = {}
    for header, adapter in TAG.findall(registry_text):
        if header in tagged:
            problems.append(f"{header}: tagged twice in {REGISTRY.name}")
        tagged[header] = adapter

    listed = REGISTRY_LIST.search(registry_text)
    in_registry = set()
    if listed is None:
        problems.append(f"{REGISTRY.name}: no `using registry = std::tuple<...>;`")
    else:
        in_registry = {name.strip() for name in listed.group(1).split(",") if name.strip()}
    for header, adapter in sorted(tagged.items()):
        if adapter not in in_registry:
            problems.append(f"{header}: adapter {adapter} is not in `registry`, so no property runs on it")

    for header in headers:
        places = [name for name, present in (("registered", header in tagged),
                                             ("EXEMPT", header in exempt),
                                             ("PENDING", header in pending)) if present]
        if not places:
            problems.append(f"{header}: not registered, EXEMPT or PENDING. Write an adapter in "
                            f"{REGISTRY.relative_to(REPO)}, or say in EXEMPT why no property applies")
        elif len(places) > 1:
            problems.append(f"{header}: listed as {' and '.join(places)}; it must be exactly one "
                            "(a registered header leaves PENDING)")
    for name in sorted((set(exempt) | pending | set(tagged)) - set(headers)):
        problems.append(f"{name}: listed, but no such public header")

    declared = set()
    for text in test_texts:
        declared.update(DECLARED.findall(text))
    for prop, issue in GAP.findall(registry_text):
        if not ISSUE.match(issue):
            problems.append(f"gap_{prop} = \"{issue}\": a known gap must name its issue, like \"#312\"")
        if prop not in declared:
            problems.append(f"gap_{prop}: no test declares the property `{prop}`")

    # The stability properties check what a header says about invalidation, so
    # a registered type has to say it (#340).
    for header in sorted(tagged):
        text = (header_texts or {}).get(header)
        if text is not None and not INVALIDATION.search(text):
            problems.append(f"{header}: registered, but its class documentation has no "
                            "`@par Iterator invalidation` or `@par Reference invalidation` paragraph")
    return problems


SELF_TEST_REGISTRY = """
// conformance-header: a.hpp
struct a_adapter {
  static constexpr const char* gap_p = "#1";
};
using registry = std::tuple<a_adapter>;
"""


def self_test():
    tests = ["METL_CONFORMANCE_DECLARE_GAP(p)"]
    cases = [
        ("clean", ["a.hpp", "b.hpp"], SELF_TEST_REGISTRY, {"b.hpp": "why"}, set(), False),
        ("unlisted header", ["a.hpp", "b.hpp"], SELF_TEST_REGISTRY, {}, set(), True),
        ("registered and pending", ["a.hpp"], SELF_TEST_REGISTRY, {}, {"a.hpp"}, True),
        ("adapter not in registry", ["a.hpp"],
         SELF_TEST_REGISTRY.replace("std::tuple<a_adapter>", "std::tuple<>"), {}, set(), True),
        ("gap without issue", ["a.hpp"], SELF_TEST_REGISTRY.replace('"#1"', '"later"'), {}, set(), True),
        ("gap for unknown property", ["a.hpp"], SELF_TEST_REGISTRY.replace("gap_p", "gap_q"), {}, set(), True),
        ("listed but gone", ["a.hpp"], SELF_TEST_REGISTRY, {"gone.hpp": "why"}, set(), True),
        ("registered and exempt", ["a.hpp"], SELF_TEST_REGISTRY, {"a.hpp": "why"}, set(), True),
        ("tagged twice", ["a.hpp"],
         SELF_TEST_REGISTRY.replace("using registry",
                                    "// conformance-header: a.hpp\nstruct a2_adapter {};\nusing registry")
         .replace("std::tuple<a_adapter>", "std::tuple<a_adapter, a2_adapter>"), {}, set(), True),
        ("no registry list", ["b.hpp"], "", {"b.hpp": "why"}, set(), True),
    ]
    for name, headers, registry_text, exempt, pending, expect in cases:
        got = bool(check(headers, registry_text, tests, exempt, pending))
        if got != expect:
            print(f"SELF-TEST FAILED [{name}]: expected a problem={expect}, got {got}", file=sys.stderr)
            return 1
    stated = {"a.hpp": "/// @par Reference invalidation\n/// Nothing moves.\n"}
    unstated = {"a.hpp": "/// Holds things.\n"}
    for name, texts, expect in (("rule stated", stated, False), ("rule missing", unstated, True)):
        got = bool(check(["a.hpp"], SELF_TEST_REGISTRY, tests, {}, set(), texts))
        if got != expect:
            print(f"SELF-TEST FAILED [{name}]: expected a problem={expect}, got {got}", file=sys.stderr)
            return 1
    # PENDING may only shrink: an entry the base does not have is growth.
    base_text = 'PENDING = {"old.hpp", "kept.hpp"}\n'
    if pending_growth({"kept.hpp"}, pending_in(base_text)):
        print("SELF-TEST FAILED: PENDING shrinking was reported as growth", file=sys.stderr)
        return 1
    if pending_growth({"kept.hpp", "new.hpp"}, pending_in(base_text)) != ["new.hpp"]:
        print("SELF-TEST FAILED: an entry added to PENDING was accepted -- the list may only shrink",
              file=sys.stderr)
        return 1
    if pending_in("PENDING = set()\n") != set():
        print("SELF-TEST FAILED: an empty PENDING was not read as empty", file=sys.stderr)
        return 1
    try:
        pending_in("OTHER = 1\n")
        print("SELF-TEST FAILED: a base with no PENDING was read as an empty one", file=sys.stderr)
        return 1
    except ValueError:
        pass
    overlap = PENDING & set(EXEMPT)
    if overlap:
        print(f"SELF-TEST FAILED: {sorted(overlap)} is both EXEMPT and PENDING", file=sys.stderr)
        return 1
    print("self-test passed: an unlisted header, a stray adapter, an unnamed gap, an unstated "
          "invalidation rule or a PENDING that grew is rejected")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--base", help="git ref whose merge base's PENDING this one may not exceed")
    args = parser.parse_args()
    if args.self_test:
        return self_test()

    headers = public_headers(INCLUDE)
    registry_text = REGISTRY.read_text(encoding="utf-8")
    test_texts = [p.read_text(encoding="utf-8") for p in sorted(SUITE.glob("*_test.cpp"))]
    header_texts = {h: (INCLUDE / h).read_text(encoding="utf-8") for h in headers}
    problems = check(headers, registry_text, test_texts, EXEMPT, PENDING, header_texts)
    if args.base:
        try:
            grown = pending_growth(PENDING, base_pending(args.base))
        except (subprocess.CalledProcessError, ValueError) as error:
            detail = error.stderr.strip() if isinstance(error, subprocess.CalledProcessError) else error
            problems.append(f"PENDING could not be read at the merge base of {args.base}: {detail}")
        else:
            problems += [f"{header}: added to PENDING, which may only shrink. Write its adapter, "
                         f"or say in EXEMPT why no property applies" for header in grown]

    registered = len(TAG.findall(registry_text))
    print(f"conformance registry: {registered} registered, {len(EXEMPT)} exempt, "
          f"{len(PENDING)} pending, of {len(headers)} public headers")
    for problem in problems:
        print(f"::error::{problem}", file=sys.stderr)
    if problems:
        print(f"\n{len(problems)} problem(s). See tests/conformance/conformance.hpp.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
