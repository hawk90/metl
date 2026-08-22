# ClusterFuzzLite corpus storage

This branch is **not part of METL**. It holds the fuzzing corpus that the
`ClusterFuzzLite Batch` workflow grows each night, so that per-PR fuzzing starts
from a warm corpus instead of from the seed files in `fuzz/corpus/`.

It exists because ClusterFuzzLite cannot create it: its uploader runs
`git checkout --orphan <branch>` against a branch it has already created
locally, which fails, and the failure was only visible in the job log. Before
this branch existed, every nightly run fuzzed for ten minutes per target per
sanitizer and then threw the result away.

Nothing here is reviewed, and nothing here is a source of truth. Delete it and
the next nightly run recreates the contents; the branch itself has to stay.

Source: `.github/workflows/cflite-batch.yml` on `main`.