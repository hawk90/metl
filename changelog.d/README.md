# Changelog fragments

Each change that a user of METL could notice adds one file here instead of
editing `CHANGELOG.md`, so two open pull requests never conflict over the same
list:

    changelog.d/<slug>.<type>.md

- `<slug>` is the issue number and a few words: `374-expected-destructor-reentry`.
- `<type>` is the Keep a Changelog section the entry belongs in: `added`,
  `changed`, `deprecated`, `removed`, `fixed` or `security`.
- The file holds one or more `- ` bullets, written as they should read in the
  release notes. A change that breaks or silently alters existing code says so.
- A change under `include/` that no user can notice, such as a refactor, uses
  the type `internal`, with one bullet saying why. It is dropped at release.

A branch that changes code under `include/` must add one. Lines that are only
comments do not count. `tools/check_changelog.py` checks this in CI and in
`tools/pre_push.py`. At release, `tools/check_changelog.py --assemble X.Y.Z`
moves the fragments into `CHANGELOG.md` and deletes them.
