# Pre-release delta audit — sessions 32–38

```
PASS:          PRE-RELEASE DELTA (supplements Passes A–E, 2026-08-16)
AUDITOR:       Kimi (Moonshot AI), 2026-08-27, orchestrating three
               independent fresh-context read-only review agents plus a
               direct privacy audit. Not the authoring model (Claude
               Opus 5); not the pass auditor (Claude Sonnet 5).
INPUTS:        Tree HEAD ddfab53 (clean, no tags, no remote);
               docs/audit/PASS-{A,B,C,D,E}-REPORT.md;
               docs/06-audit-gate0.md; docs/07-audit-protocol.md;
               remediation commits 0aeec53..d3081f5 (sessions 32–34);
               new-code commits 657e86c..949976c (sessions 35–38).
ISOLATION:     Review agents never read SESSION-LOG.md, ROADMAP.md, or
               AGENTS.md. No builds or test runs by the review agents.
SCOPE:         Everything that changed after the signed-off audit of
               2026-08-19: the remediation commits themselves, and all
               new code since. Pre-audit code not re-audited except
               where the delta touches it.
```

Method note: three review agents worked independently (remediation
verification; new-code review; Pass E re-run). Where two agents found
the same defect it appears once below. Findings use the protocol
schema with PR- IDs. Remediation is a separate, human-directed
activity; nothing in this report has been acted on yet.

---

## Findings register

### Code defects

```
ID:            PR-001
SEVERITY:      major
LOAD-BEARING:  no (no release statement depends on it; wrong decoder
               state on a reachable input)
SOURCE:        live/session.cpp:65-101, call site 251-253
FINDING:       The C-MAINT-010 extraction of tone-event dispatch passes
               SessionState BY VALUE; the pre-refactor loop re-read the
               state_ member per event. With two start-kind events in
               one push batch (tone_stream.hpp:76-79 documents that runs
               of different kinds can qualify in the same block), the
               second event dispatches against the stale entry state:
               begin_opening runs twice and the second call erases
               retained_ up to the second tone's t_start
               (session.cpp:468-474), losing the first start tone and
               corrupting IOC auto-selection. The extraction comment
               "the decisions below move here unchanged" (session.cpp:64)
               is false. Introduced by remediation commit 181793f.
FIX SHAPE:     Re-read state per event (restore the old semantics);
               screamer: two start-kind events in one push batch must
               open exactly one transmission retaining the first tone.
```

```
ID:            PR-002
SEVERITY:      minor
LOAD-BEARING:  yes (both sides of a written contract reason from it)
SOURCE:        live/engine.hpp:320-325, gui/nova-gui.cpp:2840-2842,
               live/session.cpp:462, live/session.hpp:239
FINDING:       engine.hpp documents measured_lpm as "returns to 0 when a
               transmission ends". False: preview_ is reset only at the
               next transmission's begin_opening, never at
               end_transmission/kDecoding/kSaved, so measured_lpm()
               stays non-zero through SAVED. The GUI guard that relies
               on the zero ("the decode is the authority now") never
               fires and is dead code. Benign in practice (both values
               are ≈ the measured period), but the contract is wrong on
               both sides.
FIX SHAPE:     Either make the session drop the renderer at
               end_transmission (code follows comment) or rewrite the
               contract (comment follows code). Human decision: which
               behavior is wanted during post-decode correction.
```

```
ID:            PR-003
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        gui/nova-gui.cpp:1343, 1358-1360; live/spectrum.cpp:65-69
FINDING:       The 1500/2300 Hz marker lines and axis labels are drawn
               at the LEFT edge of the tone's column, while
               spectrum_column_hz names the column CENTER and the
               waterfall's pixel mapping places the centroid half a
               column over. The markers sit a systematic half column
               (≈4.3 Hz, ~1.7 px) left of the frequency they name. No
               suite sees pixel placement (gui_layout reads stored
               columns via --metrics).
FIX SHAPE:     Add the half column at pixel placement. The tuning strip
               is the instrument the operator tunes by; a systematic
               offset in it is worth the one-line fix.
```

```
ID:            PR-004
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        gui/nova-gui.cpp:2670-2680 vs 2566-2573
FINDING:       start_live hands the engine opt.spectrum = strip_opt with
               the comment "one object, so the analyser and the widget
               cannot be configured differently". The offline-capture
               path (feed_wav) builds its engine WITHOUT it, so on that
               path analyser and widget can be configured differently.
               Harmless at current defaults.
FIX SHAPE:     Pass strip_opt on the feed_wav path too, or weaken the
               comment.
```

```
ID:            PR-005
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        live/engine.cpp:627-634, live/session.cpp:438-441
FINDING:       started_utc is documented "when the transmission BEGAN".
               Auto path stamps at tone DETECTION (by design min_start_sec
               into the tone); a pending_start_ admitted after a previous
               decode is stamped at admission, potentially a whole decode
               after the tone arrived. Both stamps name an event later
               than the beginning they claim.
FIX SHAPE:     Document the semantics (detection-time stamp), or subtract
               the known detection delay. A "Started" row that is 30-60 s
               late on a queued transmission is a small operator-facing lie.
```

### Stale numbers in prose (one defect, five files)

The suite inventory was re-measured in sessions 35–36 (40 with GUI /
38 without; 10 or 9 fixture-independent) and some files were updated
while others were not. All of these say numbers the tree contradicts:

```
ID:            PR-006
SEVERITY:      minor
LOAD-BEARING:  yes (README internal contradiction, operator-facing)
SOURCE:        README.md:423, 435 ("9 suites run", "The 9 that run
               anywhere") vs README.md:404, 459-460 ("40 registered and
               30 skipped", "the 10 fixture-independent suites")
FINDING:       The Build section presents the no-FLTK count
               unconditionally and contradicts the CI section.

ID:            PR-007
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        fixtures/MANIFEST.md:20, 26-32
FINDING:       "30 of Nova's 38 test suites" / "The 8 that run anywhere"
               — wrong under every current configuration.

ID:            PR-008
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        .github/workflows/ci.yml:5-9, 70, 111
FINDING:       Header says "10 fixture-independent suites" but lists
               nine (tuning_spectrum missing). Step names "9 run" /
               "8 run" vs their own asserted inventories (10 ran / 9 ran).
               The asserted numbers are correct; only the prose is stale.

ID:            PR-009
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        CMakeLists.txt:112, 129-131, 139-140, 599-600
FINDING:       "shrinks from 38 to 8", "The other 8", "10 synthetic
               suites still run" (unconditional; it is 9 without FLTK),
               "the 24 GUI-independent tests" (now 38).

ID:            PR-010
SEVERITY:      minor
LOAD-BEARING:  yes (Pass E mandate: disclosure must describe the
               CURRENT state of human review)
SOURCE:        CHANGELOG.md:135-138 vs README.md:24-36
FINDING:       CHANGELOG's M4.5 entry says the tuning strip "has only
               been driven headlessly... a release blocker". README's
               "What has been reviewed by a person" section predates
               M4.5 and does not disclose that the strip — the surface
               whose entire job is to be looked at — is human-unreviewed.

ID:            PR-011
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        live/engine.hpp:134
FINDING:       Comment points at "cli/nova-decode.cpp: kInternalRate";
               eda1f71 moved the constant to cli/internal_rate.hpp:8.
```

### Maintainability thresholds (Gate 0)

```
ID:            PR-012
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        live/engine.cpp:964-1058 (thread2, 95 lines),
               gui/nova-gui.cpp:3630-3712 (print_metrics_detail, 83),
               gui/nova-gui.cpp:4271-4357 (run_actions, 87)
FINDING:       MAX_FUNCTION_LINES=80 violated by three functions. At the
               remediation's closing commit 92103f4 they measured
               77/75/75; sessions 35–38 grew them past the limit. A
               post-remediation regression, not a remediation defect.
FIX SHAPE:     Extract the session-35..38 additions into helpers, same
               pattern as the remediation used.

ID:            PR-013
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        core/fax.cpp:1954-1999 (relock_dropout_runs), esp. 1997-1999
FINDING:       C-MAINT-002 Chain A: the braced if/else if/else was
               extracted into relock_row, but a braceless
               `if (relock_row(...))` remains at depth 5 counted as
               control flow (while > if > if > for > if). Passes counted
               as brace depth (4). Gate 0 does not define which counting
               convention MAX_NESTING_DEPTH uses.
FIX SHAPE:     Human decision: define the convention in docs/06, then
               either accept or restructure. Note the remediation's own
               appendix claims "none deeper than 4".
```

### Release machinery

```
ID:            PR-014
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        docs/audit/FIXTURE-REGRESSION.md:17-21,
               .github/workflows/release.yml:84-99
FINDING:       The committed record names bc1c595 (39 suites); sessions
               36–38 changed 26 files and added a 40th suite. The gate
               would correctly REFUSE a tag today — it fails safe — but
               no document flags that M4.5 onward has never had a
               recorded fixture-gated run. Re-running
               tools/record-fixture-regression.sh and committing the
               fresh record is a hard prerequisite for any tag.
               (Already the plan; recorded here for completeness.)

ID:            PR-015
SEVERITY:      minor (gap)
LOAD-BEARING:  no
SOURCE:        tools/record-fixture-regression.sh:62-77,
               release.yml:101-107
FINDING:       The recorder asserts ctest success and zero skips but
               never asserts the REGISTERED suite count.
               check-suite-inventory.sh does this for CI, but CI has
               never run (no remote). A record of 39-of-39 after a quiet
               registration loss would pass the gate.
RESOLVES IF:   The recorder takes an expected count argument (as
               check-suite-inventory.sh does), or release.yml compares
               the record's count against `ctest -N` on the tag.

ID:            PR-016
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        tools/record-fixture-regression.sh:50-52
FINDING:       The recorder reuses an existing build-regression/
               incrementally (no clean). The record asserts the suite
               ran against "these exact bytes"; incremental reuse of a
               build tree from an older commit is not byte-assured.
               Low practical risk (CMake dependency tracking); the
               d2ab7e6 "ran twice" support makes reuse the normal case.
FIX SHAPE:     Wipe or fresh-configure the build dir per run, or soften
               the record's strength claim.

ID:            PR-017
SEVERITY:      informational (gap)
LOAD-BEARING:  yes
SOURCE:        README.md:417-421 (build instructions)
QUESTION:      Does the documented build reproduce from a clean checkout
               TODAY? Pass E verified it 2026-08-16 by building;
               sessions 36–38 (new module live/spectrum.cpp, new suite,
               GUI changes) landed since, and CI has never run.
RESOLVES IF:   A clean-clone configure+build+ctest of HEAD on the
               maintainer's machine. Cheap to close.

ID:            PR-018
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        core/wav.cpp:38, 96-106, 158-166, 65-67, 188
FINDING:       Pre-existing parser weaknesses, NOT introduced by the
               refactor and out of its claimed scope: (a) D-PERF-004 —
               pcm_traits<float> memcpys file bytes into a native float
               with no little-endian-host assumption recorded;
               (b) D-PERF-005 — a fmt chunk with size < 16 is unhandled
               (fixed 16-byte read desyncs the stream); (c) pcm24's
               int32 accumulator has a theoretical signed-overflow path
               (channels has no upper bound). All predate the refactor;
               none is worsened. D-PERF-004/005 remain NOT CLOSED from
               the original Pass D register.
```

---

## Pre-push privacy audit (direct, this session)

`git rev-list --objects --all` across ALL history: 109 unique paths,
every one a legitimate project file. No `recordings/`, no `*.m4a`, no
`*.pdf`, no `*.wav` (fixtures/ has only ever held MANIFEST.md), no
analysis-dir artifacts, no secret-file names, no stashes. The session-32
history purge is intact. Verdict: **clean to push**.

## Verified clean by this audit (no findings)

- **Spectrum→Hz math** (the highest-risk new code): correct. Analyser
  constructed at the CAPTURE rate (engine.cpp:168-171); bin width,
  center-based invertible column mapping, FFT, Hann normalization, ring
  indexing, waterfall wrap all check out. tests/test_spectrum.cpp would
  catch a wrong mapping (9 tones, 0.75-bin tolerance, at 44100 AND
  48000 Hz). PR-003 (pixel placement) is the only defect here.
- **Session-37 threading**: no new unsynchronized shared state;
  atomics release/acquire; label via the existing command queue;
  spectrum shared under its own mutex.
- **Release gate logic**: ancestor check + tree-diff-excluding-only-
  the-record; no code change can ride in behind a stale record; tag
  objects peel safely; version-tag sed matches CMakeLists.txt.
- **CI build commands** match CMakeLists and README; suite-count
  arguments 40/30 and 38/29 correct against actual registration;
  fixture-less clone skips correctly.
- **Version single-sourcing**: CMakeLists → version.hpp.in → kVersion →
  version_flag suite; README/CHANGELOG agree at 0.4.5.
- **No unqualified "ISO 9876:2015 compliant" claim anywhere.**
- **NOTICE ↔ docs/00 reuse ledger ↔ in-file attribution** spot-checks
  all present (ACFax, HamFax, weatherfax_pi, KiwiSDR, JWX, fldigi,
  Isobar sites).
- **Fixtures manifest consistency**: all 19 entries referenced by
  tests; no test references an unmanifested fixture.
- **Security posture**: README statements match tests/test_malformed.cpp
  and the D-PERF-003 fix (max_opening_sec, pinned by T14).

## Remediation ledger (sessions 32–34 vs the original findings)

- C-MAINT-001 CLOSED · C-MAINT-002 PARTIALLY (Chain B closed; Chain A
  depth survives bracelessly → PR-013) · C-MAINT-003/004/005/006/007
  CLOSED · C-MAINT-008/009 CLOSED · C-MAINT-010 CLOSED for length but
  its extraction introduced PR-001 · C-MAINT-011 CLOSED · C-MAINT-012
  CLOSED (since regrown → PR-012) · C-MAINT-013 CLOSED · C-MAINT-015
  CLOSED (one stale pointer → PR-011) · C-MAINT-016 CLOSED ·
  C-MAINT-018 CLOSED · C-GAP-003 CLOSED (code-side) · C-GAP-004 CLOSED
- D-PERF-001/002/003 CLOSED · D-PERF-004/005 NOT CLOSED (out of
  claimed remediation scope; unchanged → PR-018)
- A-CLAIM-013 CLOSED (flag + CLI print + synthetic roundtrip pin [16])
- Token-level diff of the core/fax.cpp refactor: move-dominant, no
  deleted logic without reinsertion; place_rms_px ordering disclosure
  verified accurate.

## Summary

- Findings by severity: **critical 0, major 1 (PR-001), minor 11,
  informational 6**.
- Load-bearing: PR-002, PR-006, PR-010, PR-014, PR-017.
- The only finding that can produce wrong behavior is PR-001, and only
  on an exotic-but-reachable input (two different-frequency start-tone
  runs qualifying in one 0.5 s block). Everything else is prose,
  thresholds, or release-machinery hardening.
- NOT covered: no builds or test runs by the review agents;
  gui/nova-gui.cpp not read end-to-end (4 448 lines; scanner +
  targeted reads); the reference standards themselves (README clauses
  checked against docs/01–02, not the PDFs); fixture hashes (fixture
  bytes untracked by design); the quantitative performance claims in
  README (PR-GAP-003 of the Pass E re-run — verifiable only by running
  on the fixture-holding machine).
