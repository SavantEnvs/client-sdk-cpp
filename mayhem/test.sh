#!/usr/bin/env bash
#
# mayhem/test.sh — RUN the upstream gtest unit tests (src/tests/unit/*) that mayhem/build.sh built into
# build-tests/livekit_unit_tests with normal flags, and report them as CTRF. The tests assert values
# (frame sizes/durations, JWT validity, token-source caching, RPC error codes, event conversions).
#
# IMPORTANT:
#  * Must assert BEHAVIOR/OUTPUT, not just exit status. The oracle has to check asserted values /
#    golden-output diffs / known-answer results — so a PATCH that "fixes" a bug by making the program
#    exit(0) (or any no-op) FAILS here. Running inputs and checking only "exit 0 / didn't crash" is
#    NOT a functional test (it's trivially reward-hackable) — use the project's real assertion suite.
#  * Do NOT build here — mayhem/build.sh already compiled the test suite (with the project's normal
#    flags). This script only RUNS the pre-built tests and reports counts. If the test runner is
#    missing, that's a build.sh bug — fail loudly rather than silently rebuilding.
#  * REQUIRED OUTPUT — a CTRF (https://ctrf.io) summary so Mayhem/the PATCH grader reads the counts:
#      - writes a CTRF JSON report to ${CTRF_REPORT:-$SRC/ctrf-report.json}, and
#      - prints a one-line `CTRF {...}` marker to stdout (same JSON, compact).
#    Only `results.summary` (with tests/passed/failed/pending/skipped/other) is required.
#    Use the emit_ctrf helper below; it computes tests = passed+failed+skipped and sets the exit
#    code (0 iff failed==0). Map your framework's output to passed/failed/skipped.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${MAYHEM_JOBS:=$(nproc)}"   # build parallelism; env-overridable, falls back to nproc (use -j"$MAYHEM_JOBS")
cd "$SRC"

# emit_ctrf <tool> <passed> <failed> [skipped] [pending] [other]
# Writes a CTRF report (file + stdout `CTRF {...}` marker) and returns non-zero iff failed>0.
emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

BIN="$SRC/build-tests/livekit_unit_tests"
[ -x "$BIN" ] || { echo "missing $BIN — mayhem/build.sh did not produce the unit-test runner" >&2; exit 2; }

out="$("$BIN" 2>&1)"; rc=$?
printf '%s\n' "$out" | tail -n 40

# Parse gtest's own summary; a run that printed none (crash, neutered binary) counts as one failure.
ran="$(printf '%s\n' "$out" | sed -nE 's/^\[==========\] ([0-9]+) tests? from .* ran\..*/\1/p' | tail -1)"
passed="$(printf '%s\n' "$out" | sed -nE 's/^\[  PASSED  \] ([0-9]+) tests?\./\1/p' | tail -1)"
skipped="$(printf '%s\n' "$out" | sed -nE 's/^\[  SKIPPED \] ([0-9]+) tests?, listed below:/\1/p' | tail -1)"
failed="$(printf '%s\n' "$out" | sed -nE 's/^\[  FAILED  \] ([0-9]+) tests?, listed below:/\1/p' | tail -1)"
ran="${ran:-0}"; passed="${passed:-0}"; skipped="${skipped:-0}"; failed="${failed:-0}"

# The suite has ~90 tests; far fewer means the runner was gutted or only partly built.
if [ "$ran" -lt 80 ] || { [ "$rc" -ne 0 ] && [ "$failed" -eq 0 ]; }; then failed=$(( failed + 1 )); fi
emit_ctrf "gtest" "$passed" "$failed" "$skipped"
