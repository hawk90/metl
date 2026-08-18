#!/usr/bin/env bash
#
# Source-based coverage for the host test suite (Clang / llvm-cov).
#
# Reports on include/metl only. The whole-run total that llvm-cov prints by
# default also counts the test sources, which drags the number down and measures
# the wrong thing: how well the tests cover themselves is not interesting.
#
# WHAT THIS CANNOT SEE, and why the number is not the whole story:
#
#   * `#if` arms that this configuration does not compile. Coverage describes the
#     code the build produced, so a branch nobody builds does not appear as
#     uncovered — it does not appear at all. METL_CRC_TABLE=0 has its own CI job
#     for that reason; see config-matrix.
#   * Everything gated on being an MCU. irq_lock's PRIMASK path, the ARMv6-M
#     capability rejections, the -fno-exceptions arms of expected.hpp — none of
#     that code exists in a host build. Its evidence is the qemu-conformance job,
#     not this one.
#   * Anything evaluated at COMPILE time. A constexpr function exercised only by
#     static_assert never runs, so its lines read as uncovered: bit.hpp reports
#     ~29% lines with 100% branches for exactly that reason, and so do
#     lookup_table.hpp and detail/crc.hpp. Those are measurement artifacts, not
#     test gaps — do not "fix" them by adding runtime tests that duplicate a
#     static_assert.
#
# So: this measures host-visible RUNTIME branch depth. It is a floor on quality,
# not a summary of it.
#
# Usage: tools/coverage.sh [--min-lines 85] [--min-branches 70]

set -uo pipefail

MIN_LINES=85
MIN_BRANCHES=70

while [ $# -gt 0 ]; do
  case "$1" in
    --min-lines) MIN_LINES="$2"; shift 2 ;;
    --min-branches) MIN_BRANCHES="$2"; shift 2 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build-coverage"
PROF_DIR="${BUILD_DIR}/profraw"

PROFDATA="$(command -v llvm-profdata || xcrun --find llvm-profdata 2>/dev/null)"
COV="$(command -v llvm-cov || xcrun --find llvm-cov 2>/dev/null)"
if [ -z "${PROFDATA}" ] || [ -z "${COV}" ]; then
  echo "error: llvm-profdata / llvm-cov not found on PATH" >&2
  exit 2
fi

rm -rf "${BUILD_DIR}"
mkdir -p "${PROF_DIR}"

cmake -B "${BUILD_DIR}" -S "${REPO_ROOT}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DMETL_ENABLE_COVERAGE=ON \
  -DMETL_INSTALL=OFF > /dev/null || exit 1
cmake --build "${BUILD_DIR}" -j > /dev/null || exit 1

LLVM_PROFILE_FILE="${PROF_DIR}/%p.profraw" ctest --test-dir "${BUILD_DIR}" -j4 --output-on-failure > "${BUILD_DIR}/ctest.log" 2>&1
ctest_status=$?
tail -3 "${BUILD_DIR}/ctest.log"
if [ "${ctest_status}" -ne 0 ]; then
  echo "error: tests failed under instrumentation; coverage numbers would be meaningless" >&2
  exit 1
fi

"${PROFDATA}" merge -sparse "${PROF_DIR}"/*.profraw -o "${BUILD_DIR}/coverage.profdata" || exit 1

# Every test is its own binary, so every one has to be handed to llvm-cov or the
# functions only that binary exercised are reported as unexecuted.
OBJECTS=()
while IFS= read -r binary; do
  OBJECTS+=(-object "${binary}")
done < <(find "${BUILD_DIR}" -maxdepth 1 -type f -perm -u+x -name 'metl_*')

"${COV}" report "${OBJECTS[@]}" -instr-profile="${BUILD_DIR}/coverage.profdata" \
  > "${BUILD_DIR}/report.txt" 2>/dev/null

echo
echo "=== per-file, weakest branch coverage first ==="
grep "include/metl" "${BUILD_DIR}/report.txt" \
  | awk '$11+0>0 {gsub("%","",$13); printf "%7.2f %s %s\n", $13, $1, $10}' \
  | sort -n \
  | awk '{printf "  branches %7s%%   lines %-9s %s\n", $1, $3, $2}'

echo
echo "=== include/metl totals ==="
grep "include/metl" "${BUILD_DIR}/report.txt" | awk -v min_l="${MIN_LINES}" -v min_b="${MIN_BRANCHES}" '
{ reg+=$2; regmiss+=$3; fn+=$5; fnmiss+=$6; ln+=$8; lnmiss+=$9; br+=$11; brmiss+=$12 }
END {
  lines_pct = 100*(ln-lnmiss)/ln
  branch_pct = 100*(br-brmiss)/br
  printf "  regions   %6.2f%%  (%d/%d)\n", 100*(reg-regmiss)/reg, reg-regmiss, reg
  printf "  functions %6.2f%%  (%d/%d)\n", 100*(fn-fnmiss)/fn, fn-fnmiss, fn
  printf "  lines     %6.2f%%  (%d/%d)   floor %s%%\n", lines_pct, ln-lnmiss, ln, min_l
  printf "  branches  %6.2f%%  (%d/%d)   floor %s%%\n", branch_pct, br-brmiss, br, min_b
  failed = 0
  if (lines_pct < min_l)  { printf "\nFAIL: line coverage %.2f%% is below the %s%% floor\n", lines_pct, min_l; failed = 1 }
  if (branch_pct < min_b) { printf "\nFAIL: branch coverage %.2f%% is below the %s%% floor\n", branch_pct, min_b; failed = 1 }
  exit failed
}'
exit $?
