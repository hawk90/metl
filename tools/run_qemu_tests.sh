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
# --plan prints what this script WOULD run and exits, without a toolchain, a
# QEMU or a build. It exists so the count in README.md can be checked instead of
# retyped: that figure is derived from a glob, it went stale by five without
# anything noticing, and tools/check_docs.py D7 now reads it from here. The
# discovery and deny-list logic is the same code path in both modes, so --plan
# cannot drift from what a real run does.
#
# Usage: tools/run_qemu_tests.sh [--cpu cortex-m3] [--machine mps2-an385]
#                                [--expect-build-fail a.cpp,b.cpp] [--plan]

set -uo pipefail

CPU="cortex-m3"
MACHINE="mps2-an385"
TIMEOUT_SECONDS=20
KEEP_GOING=1
EXPECT_BUILD_FAIL=""
PLAN=0

while [ $# -gt 0 ]; do
  case "$1" in
    --cpu) CPU="$2"; shift 2 ;;
    --machine) MACHINE="$2"; shift 2 ;;
    --stop-on-first-failure) KEEP_GOING=0; shift ;;
    # Comma-separated tests that MUST fail to compile on this target, because a
    # capability gate should reject them. Asserted, not tolerated: if one of them
    # builds, the gate has silently stopped working and that is a failure.
    --expect-build-fail) EXPECT_BUILD_FAIL="$2"; shift 2 ;;
    --plan) PLAN=1; shift ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
done

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build-qemu-conformance"
SHIM="${REPO_ROOT}/tests/embedded/qemu_runner_shim.cpp"
LINKER_SCRIPT="${REPO_ROOT}/tests/embedded/mps2-an385.ld"

# ---------------------------------------------------------------------------
# Deny-list: tests that cannot run freestanding, each with the reason.
#
# Held as `path|reason` lines rather than an associative array so this script
# runs under bash 3.2 -- which is what macOS ships, and therefore what a
# maintainer has locally. `declare -A` made `--plan` unrunnable outside CI, and
# a check nobody can run before pushing is a check that costs a round trip every
# time it is wrong.
# ---------------------------------------------------------------------------
DENIED_LIST="\
sync/spsc_queue_threaded_test.cpp|needs <thread>; no OS scheduler on bare metal
sync/atomic_handle_threaded_test.cpp|needs <thread>
sync/mpmc_queue_threaded_test.cpp|needs <thread>
sync/spsc_byte_ring_threaded_test.cpp|needs <thread>
core/harden_floor_none_test.cpp|forked death test (unistd.h / sys/wait.h)
sync/spsc_byte_ring_overcommit_test.cpp|forked death test (unistd.h / sys/wait.h); its positive half is index arithmetic that sync/spsc_byte_ring_test already runs on target
containers/fixed_vector_asan_test.cpp|forked death test; also assumes ASan
core/assert_test.cpp|setjmp/longjmp around an abort path; host-runtime specific
memory/arena_throwing_ctor_test.cpp|throws; needs -fexceptions
vocab/expected_regression_test.cpp|throws; needs -fexceptions
vocab/variant_regression_test.cpp|throws; needs -fexceptions
sync/atomic_ref_test.cpp|atomic_ref<8-byte> needs libatomic on ARMv7-M; see atomic_ref.hpp"

# The three `throws; needs -fexceptions` entries above deliberately `throw` to
# check what METL does when a USER's type throws. The freestanding build is
# -fno-exceptions (METL is a no-exception library), and enabling exceptions here
# would need unwind tables this target's linker script does not provide.
# Verifying exception-safety on a target that cannot do exceptions is not a
# meaningful check; the host build covers it.
#
# atomic_ref_test is a FINDING, not a workaround: metl::atomic_ref<T> for an
# 8-byte T lowers to __atomic_load_8/__atomic_store_8 on ARMv7-M, and bare-metal
# toolchains ship no libatomic, so it does not link. atomic_ref.hpp documents
# this. The test stays deny-listed until the header either constrains the size on
# such targets or the runner links a libatomic.

# Prints the deny reason for a test path, or nothing if it is not deny-listed.
deny_reason() {
  printf '%s\n' "${DENIED_LIST}" | while IFS='|' read -r path reason; do
    if [ "${path}" = "$1" ]; then
      printf '%s' "${reason}"
      break
    fi
  done
}

[ "${PLAN}" -eq 1 ] || mkdir -p "${BUILD_DIR}"

CFLAGS=(
  --specs=picolibc.specs --oslib=semihost
  -std=c++17 "-mcpu=${CPU}" -mthumb -Os
  -ffunction-sections -fdata-sections -fno-exceptions -fno-rtti
  -Wl,--gc-sections -Wl,--wrap=main
  # libstdc++ is prebuilt WITH exceptions, so a few of its members reference the
  # ARM unwinder even though nothing here can throw (-fno-exceptions). The
  # unwinder then wants __exidx_start/__exidx_end, which this linker script does
  # not define. Defining them as an empty range lets that unreachable code link;
  # it can never run, because unwinding requires a throw.
  #
  # Worth recording rather than hiding: the library itself does not drag the
  # unwinder in -- the `invariants` job fails the build on any _Unwind_* symbol
  # in the image. This is two TESTS pulling a libstdc++ facility that does.
  -Wl,--defsym=__exidx_start=0 -Wl,--defsym=__exidx_end=0
  -T "${LINKER_SCRIPT}"
  -I"${REPO_ROOT}/include" -I"${REPO_ROOT}/tests"
)

passed=0
failed=0
build_failed=0
skipped=0
xfailed=0
declare -a FAILURES=()
declare -a BUILD_FAILURES=()

cd "${REPO_ROOT}"
SOURCES=()
while IFS= read -r line; do
  SOURCES+=("${line}")
done < <(find tests -name '*_test.cpp' -not -path 'tests/embedded/*' | sort)

# Every --expect-build-fail entry must name a test that discovery actually
# found. A typo there is invisible otherwise: the misspelled entry matches
# nothing, the real test builds and passes, and the job stays green while the
# capability gate it was meant to assert goes unchecked.
#
# The deny-list is checked the same way, for the same reason from the other
# direction: an entry left behind after a test is renamed or deleted excludes
# nothing, and the reason it records stops being true without saying so.
names_a_real_test() {
  for src in "${SOURCES[@]}"; do
    [ "${src#tests/}" = "$1" ] && return 0
  done
  return 1
}

stale=0
if [ -n "${EXPECT_BUILD_FAIL}" ]; then
  # `read -a` into an empty string leaves the array UNSET under bash 3.2, and
  # `set -u` then kills the script -- on macOS only, where bash 4.4's
  # empty-array exemption does not apply. Guarding beats discovering it in CI.
  IFS=',' read -r -a EXPECT_LIST <<< "${EXPECT_BUILD_FAIL}"
  for want in "${EXPECT_LIST[@]}"; do
    [ -z "${want}" ] && continue
    if ! names_a_real_test "${want}"; then
      echo "::error::--expect-build-fail names '${want}', which discovery did not find" >&2
      stale=1
    fi
  done
fi
while IFS='|' read -r path _; do
  [ -z "${path}" ] && continue
  if ! names_a_real_test "${path}"; then
    echo "::error::the deny-list names '${path}', which discovery did not find" >&2
    stale=1
  fi
done <<< "${DENIED_LIST}"
[ "${stale}" -eq 1 ] && exit 2

# --plan: report what would happen, without building or running anything.
if [ "${PLAN}" -eq 1 ]; then
  plan_denied=0
  plan_xfail=0
  plan_run=0
  for src in "${SOURCES[@]}"; do
    rel="${src#tests/}"
    if [ -n "$(deny_reason "${rel}")" ]; then
      plan_denied=$((plan_denied + 1))
      continue
    fi
    case ",${EXPECT_BUILD_FAIL}," in
      *",${rel},"*) plan_xfail=$((plan_xfail + 1)); continue ;;
    esac
    plan_run=$((plan_run + 1))
  done
  echo "cpu:          ${CPU}"
  echo "discovered:   ${#SOURCES[@]}"
  echo "deny-listed:  ${plan_denied}"
  echo "xfail-build:  ${plan_xfail}"
  echo "would-run:    ${plan_run}"
  exit 0
fi

printf '%-52s %s\n' "test" "result"
printf '%-52s %s\n' "----" "------"

for src in "${SOURCES[@]}"; do
  rel="${src#tests/}"

  reason="$(deny_reason "${rel}")"
  if [ -n "${reason}" ]; then
    printf '%-52s SKIP  (%s)\n' "${rel}" "${reason}"
    skipped=$((skipped + 1))
    continue
  fi

  elf="${BUILD_DIR}/$(echo "${rel}" | tr '/' '_' | sed 's/\.cpp$/.elf/')"
  log="${elf%.elf}.log"

  expected_to_fail=0
  case ",${EXPECT_BUILD_FAIL}," in
    *",${rel},"*) expected_to_fail=1 ;;
  esac

  if ! arm-none-eabi-g++ "${CFLAGS[@]}" "${src}" "${SHIM}" -o "${elf}" > "${log}" 2>&1; then
    if [ "${expected_to_fail}" -eq 1 ]; then
      # The capability gate fired, which is the point of the gate.
      printf '%-52s XFAIL-BUILD  (capability gate fired, as required)\n' "${rel}"
      xfailed=$((xfailed + 1))
      continue
    fi
    printf '%-52s BUILD-FAIL\n' "${rel}"
    # Print the diagnostic inline. Hiding it in an artifact means a red build
    # tells you only that something broke, not what.
    sed 's/^/      | /' "${log}" | head -20
    BUILD_FAILURES+=("${rel}")
    build_failed=$((build_failed + 1))
    [ "${KEEP_GOING}" -eq 0 ] && break
    continue
  fi

  if [ "${expected_to_fail}" -eq 1 ]; then
    # It built on a target where the capability is absent. Same reasoning as the
    # invariant canary: a gate that has stopped rejecting is not a gate.
    printf '%-52s FAIL  (built, but the capability gate should have rejected it)\n' "${rel}"
    FAILURES+=("${rel} (capability gate did not fire)")
    failed=$((failed + 1))
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
echo "  xfail-build:  ${xfailed} (capability gate fired, as required)"
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
