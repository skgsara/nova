# FIXTURE-REGRESSION.md — the out-of-band full-suite run

Written by `tools/record-fixture-regression.sh`. Do not edit by hand:
`.github/workflows/release.yml` reads the commit below, and a hand-edited
record is a defeated release gate rather than a convenience.

**Why this file exists instead of a CI job.** 30 of Nova's 39 suites need
off-air recordings that this repository does not redistribute
(`fixtures/MANIFEST.md` carries their identity and SHA-256 instead), so a
public runner cannot execute them. Whoever holds the recordings runs them
here, and the tag gate then checks that the code being released is the code
that was tested: the commit below must be an ancestor of the tag, with
nothing but this file differing between them [E-GAP-002]. It is compared
that way rather than by SHA equality because committing this record moves
HEAD, so the recorded commit and the tagged commit are never the same one.

- **Commit:** `bc1c595064217d5248d36f9d3161f840d2cdea34`
- **Branch at the time:** `m4-gui-surfaces`
- **Run at:** 2026-08-20T14:23:45Z
- **Host:** Darwin arm64
- **Result:** all 39 suites passed, 0 skipped, 39 registered

```
      Start 39: version_flag
39/39 Test #39: version_flag .....................   Passed    1.09 sec

100% tests passed out of 39

Total Test time (real) = 267.39 sec
```
