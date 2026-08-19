#!/usr/bin/env bash
#
# Deduplicating clang-tidy runner for METL's public headers.
#
# Why this exists
# ---------------
# METL is header-only, so there is nothing to analyse but headers. clang-tidy
# needs a translation unit, so the CI job compiles each public header as its
# own TU. With `HeaderFilterRegex: '.*'` every one of those runs also reports
# the findings of every header it includes -- and METL's headers include each
# other heavily. One finding is therefore reported once per includer.
#
# Measured 2026-08-19 (clang-tidy 22, `include/metl/**.hpp`):
#
#     raw warning lines   2564
#     distinct findings    546      <- 79% of the raw count is echo
#
# The duplication is a *reporting* problem, not an analysis problem, and the
# tempting fix is the wrong one. Narrowing to main-file diagnostics
# (`--header-filter='$^'`) drops the raw count to 528 with no duplication, but
# it also loses 18 real findings: diagnostics that only exist when a header is
# instantiated from another header's context (the `cert-dcl58-cpp` hits in
# lock.hpp / register_access.hpp, the const-data-member hits in
# fixed_function.hpp / function_ref.hpp) never fire when that header is
# compiled alone. So this script keeps the wide analysis and deduplicates the
# report instead.
#
# Size the backlog off the distinct count. The raw number makes a day of work
# look like a rewrite.
#
# Usage:
#   tools/clang_tidy_report.sh [-p BUILD_DIR] [--max N] [--clang-tidy PATH]
#
#   -p BUILD_DIR     directory holding compile_commands.json (default: build)
#   --max N          exit 1 if the distinct-finding count exceeds N. This is the
#                    ratchet for promoting clang-tidy from advisory to blocking:
#                    set it to today's count, and it can only go down.
#   --clang-tidy P   clang-tidy binary (default: clang-tidy on PATH)
#
# Note that the count is clang-tidy-version dependent -- a newer binary knows
# more checks. Pin the version wherever you pin the budget.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="build"
MAX_FINDINGS=""
CLANG_TIDY="clang-tidy"

while [[ $# -gt 0 ]]; do
  case "$1" in
    -p) BUILD_DIR="$2"; shift 2 ;;
    --max) MAX_FINDINGS="$2"; shift 2 ;;
    --clang-tidy) CLANG_TIDY="$2"; shift 2 ;;
    -h|--help) sed -n '2,45p' "${BASH_SOURCE[0]}"; exit 0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
  echo "error: ${BUILD_DIR}/compile_commands.json not found." >&2
  echo "       cmake -B ${BUILD_DIR} -S . -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
  exit 2
fi

RAW_LOG="$(mktemp)"
trap 'rm -f "${RAW_LOG}"' EXIT

# -P: one TU per header, in parallel. clang-tidy exits nonzero on findings and
# we are the ones deciding whether findings are fatal, so do not let `set -e`
# take that decision away.
find "${REPO_ROOT}/include/metl" -type f -name '*.hpp' -print0 \
  | xargs -0 -n1 -P"$(getconf _NPROCESSORS_ONLN)" "${CLANG_TIDY}" -p "${BUILD_DIR}" --quiet \
  > "${RAW_LOG}" 2>/dev/null || true

# A finding is identified by file:line:col plus its message and check name --
# the same finding reported from two includers is byte-identical apart from
# ordering, so `sort -u` is the whole dedup.
DISTINCT="$(mktemp)"
trap 'rm -f "${RAW_LOG}" "${DISTINCT}"' EXIT

sed "s|${REPO_ROOT}/||" "${RAW_LOG}" \
  | grep -E '^[^ ]+:[0-9]+:[0-9]+: warning: .*\[[a-z][^]]*\]$' \
  | sort -u > "${DISTINCT}" || true

raw_count="$(grep -cE '^[^ ]+:[0-9]+:[0-9]+: warning: .*\[[a-z][^]]*\]$' "${RAW_LOG}" || true)"
distinct_count="$(wc -l < "${DISTINCT}" | tr -d ' ')"

echo "clang-tidy: ${distinct_count} distinct findings (${raw_count} raw warning lines)"
echo

if [[ "${distinct_count}" -gt 0 ]]; then
  echo "By check:"
  # A finding can carry several check names ([a,b]); count it under each.
  sed -E 's/.*\[([a-z][^]]*)\]$/\1/' "${DISTINCT}" \
    | tr ',' '\n' \
    | sort | uniq -c | sort -rn \
    | awk '{ printf "  %6d  %s\n", $1, $2 }'
  echo
  echo "Distinct findings:"
  sed 's/^/  /' "${DISTINCT}"
fi

if [[ -n "${MAX_FINDINGS}" ]]; then
  echo
  if [[ "${distinct_count}" -gt "${MAX_FINDINGS}" ]]; then
    echo "FAIL: ${distinct_count} distinct findings exceeds the budget of ${MAX_FINDINGS}."
    exit 1
  fi
  echo "OK: ${distinct_count} distinct findings, within the budget of ${MAX_FINDINGS}."
  if [[ "${distinct_count}" -lt "${MAX_FINDINGS}" ]]; then
    echo "     The budget is now slack by $(( MAX_FINDINGS - distinct_count )). Lower it to ${distinct_count}."
  fi
fi
