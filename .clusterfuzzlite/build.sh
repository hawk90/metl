#!/bin/bash -eu
# ClusterFuzzLite / OSS-Fuzz build script for metl.
#
# metl is header-only, so each libFuzzer harness is a single translation unit
# compiled against include/ and linked with the fuzzing engine provided by the
# OSS-Fuzz environment ($LIB_FUZZING_ENGINE). Sanitizers/coverage come in via
# $CXXFLAGS (set by the base-builder for the requested sanitizer). A seed corpus
# is bundled per target as <target>_seed_corpus.zip.

FUZZERS="
fuzz_fixed_string
fuzz_flat_map
fuzz_static_unordered_map
fuzz_allocators
fuzz_crc
"

for fuzzer in ${FUZZERS}; do
  "${CXX}" ${CXXFLAGS} -std=c++17 \
    -I"${SRC}/metl/include" \
    -I"${SRC}/metl/fuzz" \
    "${SRC}/metl/fuzz/${fuzzer}.cpp" \
    ${LIB_FUZZING_ENGINE} \
    -o "${OUT}/${fuzzer}"

  corpus_dir="${SRC}/metl/fuzz/corpus/${fuzzer}"
  if [ -d "${corpus_dir}" ]; then
    # -j: flatten (store files without directory paths).
    zip -j "${OUT}/${fuzzer}_seed_corpus.zip" "${corpus_dir}"/* >/dev/null 2>&1 || true
  fi
done
