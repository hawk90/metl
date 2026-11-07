#!/usr/bin/env python3
"""Delete the caches on main that a newer cache of the same kind replaced.

A repository gets 10 GB of Actions cache, and past that GitHub evicts by last
access, whatever the entry is. Two kinds of cache here save a new entry under a
new key every time and are restored by key prefix, newest first, so only the
newest entry of each is ever read again:

  ccache-<key>-<timestamp>       hendrikmuhs/ccache-action (append-timestamp)
  ccache-metl-zephyr-<run>-<try> the zephyr job's ccache
  apt-<set>@<contents hash>      .github/actions/apt-install

Each older one is dead weight. On 2026-11-07 main held 10.0 GB of cache, 3.6 GB
of it older entries of those kinds, and eviction had taken the apt caches of
`big-endian / powerpc64` and `riscv-cross / rv64`: a pull request's run then
downloaded 84.9 MB and 86.7 MB from the Ubuntu mirror that a cache should have
held. Keys of any other shape (`llvm-<version>-<sha>`, `zephyr-sdk-...`,
`pip-zephyr-...`) name one exact entry, are never superseded this way, and are
left alone.

Only main's caches are read: main is the only ref that saves (ci.yml), and a
pull request's entries belong to that pull request.

It also refuses a family whose name is the start of another's followed by `-`
(`ccache-config-matrix-Linux-cxx20` and `...-cxx20-gcc`): restoring the first
matches the second's entries too and takes whichever is newer, so a clang job
starts from a gcc job's objects. That happened in config-matrix and sanitizers
until their keys ended in `@`. The prune still runs; the exit status is 1.

Usage (cache-prune.yml, with GH_TOKEN set and `actions: write`):
    tools/prune_caches.py --repo owner/name [--dry-run]
    tools/prune_caches.py --self-test
"""

import argparse
import re
import subprocess
import sys

REF = "refs/heads/main"

_FAMILIES = (
    # The zephyr job's own key, before the generic timestamp rule.
    re.compile(r"^(ccache-metl-zephyr-)\d+-\d+$"),
    re.compile(r"^(ccache-.+)-\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z$"),
    re.compile(r"^(apt-.+@)[0-9a-f]+$"),
)


def family(key):
    """The prefix a restore matches `key` by, or None if `key` names one entry."""
    for pattern in _FAMILIES:
        match = pattern.match(key)
        if match:
            return match.group(1)
    return None


def superseded(caches):
    """The caches a newer one of the same family replaced, oldest first.

    `caches` are dicts with `id`, `key` and `created_at` (ISO 8601, so the
    strings sort by time)."""
    newest = {}
    for cache in caches:
        name = family(cache["key"])
        if name is None:
            continue
        kept = newest.get(name)
        if kept is None or cache["created_at"] > kept["created_at"]:
            newest[name] = cache
    keep = {id(cache) for cache in newest.values()}
    return sorted((cache for cache in caches
                   if family(cache["key"]) is not None and id(cache) not in keep),
                  key=lambda cache: cache["created_at"])


def collisions(caches):
    """(shorter, longer) family pairs a restore of the shorter one confuses."""
    names = sorted({family(cache["key"]) for cache in caches} - {None})
    return [(a, b) for a in names for b in names
            if a.startswith("ccache-") and b.startswith(a + "-")]


class ApiError(Exception):
    pass


def gh(*args):
    command = ["gh", "api", *args]
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode != 0:
        output = (result.stderr or result.stdout).strip() or "no output"
        raise ApiError(f"`{' '.join(command)}` exited {result.returncode}: {output}")
    return result.stdout


def list_caches(repo):
    out = gh("--paginate", f"repos/{repo}/actions/caches?per_page=100&ref={REF}",
             "--jq", ".actions_caches[] | [.id, .key, .ref, .created_at, .size_in_bytes] | @tsv")
    caches = []
    for line in out.splitlines():
        cache_id, key, ref, created_at, size = line.split("\t")
        # The ref filter is the server's; check it rather than trust it.
        if ref == REF:
            caches.append({"id": int(cache_id), "key": key, "created_at": created_at,
                           "size": int(size)})
    return caches


def delete(repo, cache_id):
    try:
        gh("--method", "DELETE", f"repos/{repo}/actions/caches/{cache_id}")
    except ApiError as error:
        # Another prune got there first.
        if "HTTP 404" not in str(error):
            raise


def self_test():
    def entry(n, key, created):
        return {"id": n, "key": key, "created_at": created}

    old_cc = entry(1, "ccache-lto-Linux-2026-11-06T13:40:31.180Z", "2026-11-06T13:40:32Z")
    new_cc = entry(2, "ccache-lto-Linux-2026-11-06T22:16:36.464Z", "2026-11-06T22:16:37Z")
    old_apt = entry(3, "apt-ubuntu26-X64-build-essential@80ae0000aaaabbbb", "2026-11-06T10:00:00Z")
    new_apt = entry(4, "apt-ubuntu26-X64-build-essential@d4a1297e1c04bf08", "2026-11-06T13:39:39Z")
    # A set whose name extends another's is its own family: `@` ends the prefix.
    other_set = entry(5, "apt-ubuntu26-X64-build-essential-clang@4f991a675cfc754d", "2026-11-05T00:00:00Z")
    old_z = entry(6, "ccache-metl-zephyr-42226783692-1", "2026-11-06T13:42:03Z")
    new_z = entry(7, "ccache-metl-zephyr-42302771438-1", "2026-11-06T22:13:40Z")
    exact = [entry(8, "llvm-23.1.3-14d2f701", "2026-10-13T22:32:27Z"),
             entry(9, "llvm-23.1.4-0123abcd", "2026-11-01T00:00:00Z"),
             entry(10, "zephyr-sdk-1.0.1-linux-x86_64-arm-zephyr-eabi", "2026-10-19T02:37:24Z")]
    shard0 = entry(11, "ccache-mutants-Linux-0@-2026-11-06T22:19:39.456Z", "2026-11-06T22:19:41Z")
    shard1 = entry(12, "ccache-mutants-Linux-1@-2026-11-06T22:21:08.882Z", "2026-11-06T22:21:11Z")

    failures = []

    def expect(what, got, want):
        if got != want:
            failures.append(f"{what}: got {got}, want {want}")

    # The listing order is not the age order.
    caches = [new_cc, old_apt, other_set, old_z, *exact, new_z, old_cc, new_apt, shard0, shard1]
    expect("superseded ids", [c["id"] for c in superseded(caches)], [3, 1, 6])
    expect("a lone entry is kept", superseded([old_cc]), [])
    expect("keys naming one entry are never pruned", superseded(exact), [])
    expect("family of a ccache key", family(new_cc["key"]), "ccache-lto-Linux")
    expect("family of an apt key", family(new_apt["key"]), "apt-ubuntu26-X64-build-essential@")
    expect("family of the zephyr key", family(new_z["key"]), "ccache-metl-zephyr-")
    expect("an exact key has no family", family(exact[0]["key"]), None)
    expect("an apt key without its hash has no family",
           family("apt-ubuntu26-X64-build-essential"), None)

    clang = entry(13, "ccache-config-matrix-Linux-cxx20-2026-11-06T22:17:30.858Z", "2026-11-06T22:17:31Z")
    gcc = entry(14, "ccache-config-matrix-Linux-cxx20-gcc-2026-11-06T22:15:41.086Z", "2026-11-06T22:15:42Z")
    expect("a prefix pair is a collision", collisions([clang, gcc]),
           [("ccache-config-matrix-Linux-cxx20", "ccache-config-matrix-Linux-cxx20-gcc")])
    ended = [dict(clang, key=clang["key"].replace("cxx20-2026", "cxx20@-2026")),
             dict(gcc, key=gcc["key"].replace("gcc-2026", "gcc@-2026"))]
    expect("keys ending in @ do not collide", collisions(ended), [])
    expect("shards ending in @ do not collide", collisions([shard0, shard1]), [])
    expect("apt sets do not collide", collisions([new_apt, other_set]), [])

    for failure in failures:
        print(f"::error::self-test: {failure}")
    if failures:
        return 1
    print("prune_caches self-test passed")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--repo")
    parser.add_argument("--dry-run", action="store_true", help="list, delete nothing")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if not args.repo:
        parser.error("--repo is required")

    caches = list_caches(args.repo)
    stale = superseded(caches)
    total = sum(cache["size"] for cache in caches)
    freed = sum(cache["size"] for cache in stale)
    for cache in stale:
        print(f"{'would delete' if args.dry_run else 'delete'} {cache['key']} "
              f"({cache['size'] / 2**20:.0f} MiB, {cache['created_at']})")
        if not args.dry_run:
            delete(args.repo, cache["id"])
    print(f"main: {len(caches)} caches, {total / 2**30:.2f} GiB; "
          f"{len(stale)} superseded, {freed / 2**30:.2f} GiB"
          f"{' (dry run)' if args.dry_run else ' deleted'}")

    pairs = collisions(caches)
    for shorter, longer in pairs:
        print(f"::error::a restore of `{shorter}` also matches `{longer}`'s entries and takes "
              f"the newer one. End the shorter key with `@` in its workflow.")
    return 1 if pairs else 0


if __name__ == "__main__":
    sys.exit(main())
