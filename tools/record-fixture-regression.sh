#!/bin/sh
# record-fixture-regression.sh — run the FULL suite where the recordings
# are, and write down that it happened, against a commit.
#
# usage: tools/record-fixture-regression.sh [BUILD_DIR]
#
# The problem this solves. Pass E requires CI to run the fixture
# regression with failure blocking release [E-GAP-002]. Nova's 19 off-air
# recordings are deliberately not published — the copyright of a
# transmitted chart varies by issuing meteorological service and one
# station is a commercial newspaper [Pass B] — so a public runner cannot
# run 30 of the 39 suites, and no amount of workflow YAML changes that.
#
# The honest alternative, which Pass E's RESOLVES IF names as acceptable:
# whoever holds the recordings runs the full suite out of band and records
# the result, and the release gate then refuses to pass a tag whose commit
# is not the commit that was tested. This script is the "records the
# result" half; .github/workflows/release.yml is the refusing half.
#
# It refuses to write a record it cannot stand behind:
#  - a dirty working tree, because then the SHA does not describe what ran
#  - a checkout without the recordings, because that is the very run this
#    mechanism exists to substitute for
set -eu

build="${1:-build-regression}"
root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

record="docs/audit/FIXTURE-REGRESSION.md"
sentinel="fixtures/test-chart-jmh-kiwisdr-image-60s.wav"

if [ ! -f "$sentinel" ]; then
    echo "regression: no recordings in fixtures/ — this machine cannot run" >&2
    echo "  the fixture regression. See fixtures/MANIFEST.md." >&2
    exit 1
fi

if [ -n "$(git status --porcelain)" ]; then
    echo "regression: working tree is dirty; the recorded commit would not" >&2
    echo "  describe what actually ran. Commit or stash first." >&2
    exit 1
fi

sha=$(git rev-parse HEAD)
branch=$(git rev-parse --abbrev-ref HEAD)
when=$(date -u +"%Y-%m-%dT%H:%M:%SZ")
host="$(uname -s) $(uname -m)"

echo "regression: building in $build at $sha"
cmake -B "$build" -S . >/dev/null
cmake --build "$build" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" >/dev/null

log=$(mktemp)
trap 'rm -f "$log"' EXIT

set +e
ctest --test-dir "$build" --output-on-failure >"$log" 2>&1
rc=$?
set -e

registered=$(ctest --test-dir "$build" -N | sed -n 's/^Total Tests: //p')
skipped=$(grep -c '\*\*\*Skipped' "$log" || true)
ran=$((registered - skipped))

if [ "$rc" -ne 0 ]; then
    echo "regression: FAILED — nothing recorded" >&2
    tail -40 "$log" >&2
    exit 1
fi

if [ "$skipped" -ne 0 ]; then
    echo "regression: $skipped suites skipped on a machine that has the" >&2
    echo "  recordings. That is the run this record is supposed to be OF." >&2
    sed -n '/The following tests did not run/,$p' "$log" >&2
    exit 1
fi

cat >"$record" <<EOF
# FIXTURE-REGRESSION.md — the out-of-band full-suite run

Written by \`tools/record-fixture-regression.sh\`. Do not edit by hand:
\`.github/workflows/release.yml\` reads the commit below, and a hand-edited
record is a defeated release gate rather than a convenience.

**Why this file exists instead of a CI job.** 30 of Nova's $registered suites need
off-air recordings that this repository does not redistribute
(\`fixtures/MANIFEST.md\` carries their identity and SHA-256 instead), so a
public runner cannot execute them. Whoever holds the recordings runs them
here, and the tag gate then checks that the code being released is the code
that was tested: the commit below must be an ancestor of the tag, with
nothing but this file differing between them [E-GAP-002]. It is compared
that way rather than by SHA equality because committing this record moves
HEAD, so the recorded commit and the tagged commit are never the same one.

- **Commit:** \`$sha\`
- **Branch at the time:** \`$branch\`
- **Run at:** $when
- **Host:** $host
- **Result:** all $ran suites passed, $skipped skipped, $registered registered

\`\`\`
$(tail -6 "$log")
\`\`\`
EOF

echo "regression: PASSED — $ran/$registered suites, recorded in $record"
echo "regression: commit the record, then tag."
