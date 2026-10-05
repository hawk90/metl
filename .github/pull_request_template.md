<!-- What does this change, and why? Link the issue: "Closes #NNN". -->

## Checklist

See [CONTRIBUTING.md](https://github.com/hawk90/metl/blob/main/CONTRIBUTING.md) for the details of each item.

- [ ] `tools/pre_push.py` passes (installed as the pre-push hook by `pre-commit install`)
- [ ] A bug fix comes with a regression test that failed before the fix
- [ ] `CHANGELOG.md` updated under `[Unreleased]`; a breaking change says so
- [ ] Public API changes are documented in the headers (and README/docs where relevant)
- [ ] A new public header states its progress guarantee and is in `metl.hpp`
