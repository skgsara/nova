#!/bin/sh
# check-suite-inventory.sh — assert WHICH suites ran, not just that ctest
# was happy.
#
# usage: tools/check-suite-inventory.sh BUILD_DIR EXPECT_REGISTERED EXPECT_SKIPPED
#
# Why this exists. CMakeLists.txt states the limit of the skip mechanism
# plainly: "ctest counts a skipped test as passed in its headline, so
# `ctest` still ends with 100% tests passed out of 39". On a CI runner,
# which has no off-air recordings, 30 of the 39 suites skip — so a green
# ctest exit code there means nine suites passed and thirty were absent.
# An automated gate that reads only the exit code would report a full
# regression having run one quarter of it, which is this project's oldest
# recurring failure: an instrument that reports success by default
# [SESSION-LOG, session 23 onward].
#
# So the gate is the INVENTORY. The expected numbers are passed in by the
# caller and are meant to be updated deliberately when a suite is added:
# a suite that silently stops being registered — a CMake branch that
# quietly excludes it, a renamed target — fails here instead of passing
# as a smaller green run.
set -eu

if [ $# -ne 3 ]; then
    echo "usage: $0 BUILD_DIR EXPECT_REGISTERED EXPECT_SKIPPED" >&2
    exit 2
fi

build="$1"
expect_registered="$2"
expect_skipped="$3"

registered=$(ctest --test-dir "$build" -N | sed -n 's/^Total Tests: //p')
if [ -z "$registered" ]; then
    echo "inventory: could not read the registered test count from ctest -N" >&2
    exit 1
fi

log=$(mktemp)
trap 'rm -f "$log"' EXIT

# The run itself must pass on its own terms first.
if ! ctest --test-dir "$build" --output-on-failure >"$log" 2>&1; then
    cat "$log"
    echo "inventory: ctest reported failures" >&2
    exit 1
fi

skipped=$(grep -c '\*\*\*Skipped' "$log" || true)
ran=$((registered - skipped))

echo "inventory: $registered registered, $ran ran, $skipped skipped"

fail=0
if [ "$registered" -ne "$expect_registered" ]; then
    echo "inventory: expected $expect_registered suites registered, found $registered" >&2
    echo "  a suite was added or lost. Update the expected count deliberately." >&2
    fail=1
fi
if [ "$skipped" -ne "$expect_skipped" ]; then
    echo "inventory: expected $expect_skipped suites skipped, found $skipped" >&2
    echo "  on a checkout without recordings the fixture-gated suites skip;" >&2
    echo "  a different number means the fixture gate itself moved." >&2
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo >&2
    echo "The suites that did not run:" >&2
    sed -n '/The following tests did not run/,$p' "$log" >&2
    exit 1
fi

echo "inventory: OK"
