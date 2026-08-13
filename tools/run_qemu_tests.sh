#!/usr/bin/env bash
#
# Cross-compile METL's host test suite for Cortex-M and run each test under
# qemu-system-arm.
#
# Why this exists: METL's differentiator is that it is provably usable on bare
# metal, but until now the only thing executed on an MCU was one smoke program
# covering seven headers. Everything else was compile-and-link only. This runs
# the real tests on the real (emulated) target.
#
# DENY-LIST, NOT ALLOW-LIST. A curated list of "embedded-appropriate checks"
# would silently miss every type added afterwards — the same failure mode
# docs/SCOPE.md §8 records for the invariant probe. Here every test runs unless
# it is explicitly excluded below with a reason, so a new test gets target
# execution by default and skipping one requires saying why.
#
# Grading is on the METL_QEMU_EXIT line printed by the --wrap=main shim, not on
# QEMU's exit status: semihosting SYS_EXIT does not bring this machine down, so
# the emulator lingers after the program has finished and printed its result.
# (The first run of this job proved it by "timing out" on every test, including
# trivial ones, purely because the grading checked the timeout before the
# sentinel.) The sentinel therefore decides, and QEMU is killed as soon as it
# appears.
#
# That is still strict, which is the part that matters: a test that genuinely
# hangs never prints the line, and a crash never reaches it. No sentinel is a
# failure — a conformance gate that cannot tell a hang from a pass is not a
# gate.
#
# Usage: tools/run_qemu_tests.sh [--cpu cortex-m3] [--keep-going]

set -uo pipefail

CPU="cortex-m3"
MACHINE="mps2-an385"
TIMEOUT_SECONDS=20
KEEP_GOING=1

while [ $# -gt 0 ]; do
  case "$1" in
    --cpu) CPU="$2"; shift 2 ;;
    --machine) MACHINE="$2"; shift 2 ;;
    --stop-on-first-failure) KEEP_GOING=0; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build-qemu-conformance"
SHIM="${REPO_ROOT}/tests/embedded/qemu_runner_shim.cpp"
LINKER_SCRIPT="${REPO_ROOT}/tests/embedded/mps2-an385.ld"

# ---------------------------------------------------------------------------
# Deny-list: tests that cannot run freestanding, each with the reason.
# ---------------------------------------------------------------------------
declare -A DENIED=(
  ["sync/spsc_queue_threaded_test.cpp"]="needs <thread>; no OS scheduler on bare metal"
  ["sync/atomic_handle_threaded_test.cpp"]="needs <thread>"
  ["sync/mpmc_queue_threaded_test.cpp"]="needs <thread>"
  ["core/harden_floor_none_test.cpp"]="forked death test (unistd.h / sys/wait.h)"
  ["containers/fixed_vector_asan_test.cpp"]="forked death test; also assumes ASan"
  ["core/assert_test.cpp"]="setjmp/longjmp around an abort path; host-runtime specific"
)

mkdir -p "${BUILD_DIR}"

CFLAGS=(
  --specs=picolibc.specs --oslib=semihost
  -std=c++17 "-mcpu=${CPU}" -mthumb -Os
  -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti
  -Wl,--gc-sections -Wl,--wrap=main
  -T "${LINKER_SCRIPT}"
  -I"${REPO_ROOT}/include" -I"${REPO_ROOT}/tests"
)

passed=0
failed=0
build_failed=0
skipped=0
declare -a FAILURES=()
declare -a BUILD_FAILURES=()

cd "${REPO_ROOT}"
mapfile -t SOURCES < <(find tests -name '*_test.cpp' -not -path 'tests/embedded/*' | sort)

printf '%-52s %s\n' "test" "result"
printf '%-52s %s\n' "----" "------"

for src in "${SOURCES[@]}"; do
  rel="${src#tests/}"

  if [ -n "${DENIED[$rel]:-}" ]; then
    printf '%-52s SKIP  (%s)\n' "${rel}" "${DENIED[$rel]}"
    skipped=$((skipped + 1))
    continue
  fi

  elf="${BUILD_DIR}/$(echo "${rel}" | tr '/' '_' | sed 's/\.cpp$/.elf/')"
  log="${elf%.elf}.log"

  if ! arm-none-eabi-g++ "${CFLAGS[@]}" "${src}" "${SHIM}" -o "${elf}" > "${log}" 2>&1; then
    printf '%-52s BUILD-FAIL\n' "${rel}"
    # Print the diagnostic inline. Hiding it in an artifact means a red build
    # tells you only that something broke, not what.
    sed 's/^/      | /' "${log}" | head -20
    BUILD_FAILURES+=("${rel}")
    build_failed=$((build_failed + 1))
    [ "${KEEP_GOING}" -eq 0 ] && break
    continue
  fi

  # Run detached and poll for the sentinel rather than waiting for QEMU to exit.
  #
  # On this machine + libc combination semihosting SYS_EXIT does not bring QEMU
  # down — the smoke job's comment says as much, and the first run of this job
  # proved it by timing out on every single test including trivial ones. The
  # program had finished and printed its result; only the emulator lingered.
  #
  # So the sentinel decides, and the timeout only matters when the sentinel never
  # arrives. That is still strict: a test that genuinely hangs never prints the
  # line, and a crash never reaches it.
  : > "${log}.run"
  timeout "${TIMEOUT_SECONDS}" qemu-system-arm \
    -semihosting-config enable=on \
    -monitor none -serial none -nographic \
    -machine "${MACHINE},accel=tcg" \
    -kernel "${elf}" > "${log}.run" 2>&1 &
  qemu_pid=$!

  sentinel=""
  for _ in $(seq 1 $((TIMEOUT_SECONDS * 10))); do
    if sentinel="$(grep -m1 -oE '^METL_QEMU_EXIT [0-9]+$' "${log}.run" 2>/dev/null)"; then
      [ -n "${sentinel}" ] && break
    fi
    kill -0 "${qemu_pid}" 2>/dev/null || break
    sleep 0.1
  done
  kill "${qemu_pid}" 2>/dev/null
  wait "${qemu_pid}" 2>/dev/null
  sentinel="$(grep -m1 -oE '^METL_QEMU_EXIT [0-9]+$' "${log}.run" 2>/dev/null)"
  cat "${log}.run" >> "${log}"

  code="${sentinel##* }"
  if [ "${sentinel}" = "METL_QEMU_EXIT 0" ]; then
    printf '%-52s PASS\n' "${rel}"
    passed=$((passed + 1))
  elif [ -n "${sentinel}" ]; then
    printf '%-52s FAIL  (exit %s)\n' "${rel}" "${code}"
    sed 's/^/      | /' "${log}.run" | head -15
    FAILURES+=("${rel} (exit ${code})")
    failed=$((failed + 1))
  else
    # No sentinel: hung inside the test, crashed, or never started.
    printf '%-52s FAIL  (no METL_QEMU_EXIT line)\n' "${rel}"
    sed 's/^/      | /' "${log}.run" | head -15
    FAILURES+=("${rel} (no exit line — hung or crashed before main returned)")
    failed=$((failed + 1))
  fi

  [ "${KEEP_GOING}" -eq 0 ] && [ "${failed}" -gt 0 ] && break
done

echo
echo "=============================================================="
echo "  passed:       ${passed}"
echo "  failed:       ${failed}"
echo "  build-failed: ${build_failed}"
echo "  skipped:      ${skipped} (deny-listed, see the table above)"
echo "=============================================================="

if [ "${build_failed}" -gt 0 ]; then
  echo
  echo "Build failures (compiler output is in ${BUILD_DIR}/*.log):"
  for f in "${BUILD_FAILURES[@]}"; do echo "  - ${f}"; done
fi

if [ "${failed}" -gt 0 ]; then
  echo
  echo "Run failures:"
  for f in "${FAILURES[@]}"; do echo "  - ${f}"; done
fi

if [ "${failed}" -gt 0 ] || [ "${build_failed}" -gt 0 ]; then
  exit 1
fi
exit 0
