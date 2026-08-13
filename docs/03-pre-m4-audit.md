# 03 — Pre-M4 audit: standards, prior art, and GUI readiness

Date: 2026-08-13. Auditor: Kimi Code CLI, at Sara's request.

## Scope and method

This audit covered the whole tree: `core/`, `cli/`, `tests/`, fixtures,
CMake, README/ROADMAP/NOTICE, `docs/00`–`docs/02`, and the session log's
current claims. The normative texts were checked directly from Sara's
PDFs: ISO 9876:2015 for receiver behaviour, and WMO-No. 386 Vol. I Part
III §5 in both the 2023 and 2009 editions for the signal. Extracts were
made only under ignored `build/audit/`; the licensed ISO text was not
copied into the repo.

Prior art checked: ACFax, HamFax, weatherfax_pi, KiwiSDR
`extensions/FAX`, JWX, fldigi, and Isobar/KG-FAX. JWX and Isobar are
local; the others were re-verified from their public sources on
2026-08-13.

## Verdict

**The core is sound enough to build M4 against, after the fixes recorded
here.** The decoder's standards story is now traceable clause by clause,
the provenance record matches the actual tree, and the missing automatic
IOC path is implemented and tested.

That verdict is about the **batch core**. Live audio is not "just add
FLTK": M4 still needs an explicit streaming/session design, progress and
cancellation seams, and the retained-raw/non-destructive adjustment model.
Those are design work, not hidden decoder defects.

## What changed in this audit

- `DecodeOptions::ioc` now defaults to automatic. The first 300/675 Hz
  start tone selects IOC 576/288 unless the caller overrides it, and
  `DecodeResult::ioc` reports the selection [ISO §4.2.5].
- `nova-decode --dev 150|400` exposes the implemented LF/HF deviation
  modes. CLI mode arguments are validated, and the output reports IOC and
  deviation.
- `core/phasing.cpp` now uses the wedge fit to identify phasing, then
  refines the position to the local white leading-edge crossing
  [WMO §5.2.3.4]. This fixes a clean ±150 Hz synthetic being reported as
  a stepping timebase.
- The generator gained `start_sec`, `dead_frac`, and `pulse_frac` so the
  standards' tolerance edges can be tested rather than asserted.
- New or extended roundtrip coverage:
  - `[4]`: the full ISO §5.4.1 matrix, {IOC 288,576} × {60,90,120 lpm},
    with automatic IOC and rate selection. All six legs measured 0.00 px
    bar-edge scatter. The 90 lpm legs correctly read −62.4/−62.5 ppm
    because the generator truncates 5333.33 samples to 5333; the test
    pins that generated truth.
  - `[6]`: ±150 Hz decodes straight **and** reports a linear timebase.
  - `[11]`: input amplitudes 0.5, 0.05, and 0.005 decode identically
    (179 locks, +0.0 ppm, 0.00 px at each level) [ISO §4.2.2,
    WMO §5.3.3].
  - `[12]`: eight gray bands recover as 0/36/72/109/145/182/218/255,
    within 1 LSB of the generated linear scale [WMO §5.4.3].
  - `[13]`: a 10 s start tone segments at the correct picture boundary
    (image starts at 25.5 s in the decoded grid; MAD from row 0 = 15.3)
    [WMO §5.2.2]. `lines_dropped_head` is 36 rather than a naive 50
    because the onset gate cannot see a line comb in a long pure tone;
    the test pins the boundary, not the misleading count.
  - `[14]`: 44.1 kHz input resampled to 8 kHz decodes cleanly (149 locks,
    +0.02 ppm, MAD 16.6, 0.00 px), exercising the production live-capture
    path.
  - `[15]`: WMO §5.1.3.3's four permitted tolerance corners — dead
    sector 4.0%/5.0%, pulse 1.0% or exactly half the sector — all decode
    straight, with phasing and pulse style found.
- `tones [7]` no longer picks a candidate rate by line count alone. A 1 Hz
  phasing signal has more candidate windows when tested as 2 Hz; measured
  after edge refinement, the true rate has 0.0 samples of spread and the
  harmonic has 35.5, so the harness now prefers agreement and uses count
  only as a tie-break.
- `NOTICE`, `docs/00`, `docs/01`, `docs/02`, README, and ROADMAP were
  reconciled with the verified standards and provenance evidence.

## Standards traceability

`docs/02-compliance-matrix.md` remains the acceptance checklist. This is
the audit summary behind it.

### ISO 9876:2015

| Clause | Audit status |
|---|---|
| §4.2.2 input signals | **Synthetic ✓.** ±150/±400 Hz both decode; LF is reachable from the CLI; amplitude span is pinned by `roundtrip [11]`. |
| §4.2.3 IOC 576/288 | **Synthetic ✓.** The clause is automatic **or** manual; Nova has both. Real 675 Hz material remains a fixture gap. |
| §4.2.4 speeds 60/90/120 | **Synthetic ✓**, automatic and manual. Real 90 lpm material remains a fixture gap. |
| §4.2.5 automatic operation | **✓ with a documented method deviation.** Nova uses spectral purity in demodulated video rather than the clause's line-sync method, because it is stronger against false starts. Start/stop and IOC selection are consumed. |
| §4.2.6 sync/phasing | **Synthetic+fixture ✓ for automatic operation.** Manual phase adjustment remains pending and is also an ISO §5.4.3 test item. |
| §4.2.7 pitch | **Met.** The old matrix cited a nonexistent assertion; it now cites the picture-domain straight-strip screamers, which are stronger than ISO §5.4.4's visual check. |
| §5.4.1/§5.4.2 six-combination chart test | **Synthetic ✓.** `roundtrip [4]` covers all six with automatic selection. |
| Hardware/paper/radio clauses | Out of scope by recorded decision in `docs/02`; no hidden software obligation found. |

### WMO-No. 386 Part III §5

| Clause | Audit status |
|---|---|
| §5.1.1 scan direction | Implemented by row-major assembly. |
| §5.1.2/§5.1.4 IOC and density | Implemented; IOC 288 real fixture still absent. |
| §5.1.3.3 dead sector | Both permitted styles handled; tolerance corners pinned by `roundtrip [15]`. |
| §5.1.5 scan rates | 60/90/120 handled; 240 lpm deliberately out of scope because ISO §4.2.4 does not require it. |
| §5.2.1–§5.2.6 control signals | Start on IOC-select or phasing, 300/675 Hz start, both phasing waveforms, stop, and ±1% tolerance are implemented and tested. A 10 s start is now pinned by `roundtrip [13]`. |
| §5.2.4 recording levels | Optional; unnecessary in Nova because the discriminator is amplitude-normalized. Recorded in `docs/01`. |
| §5.3.1.1 AM variant | Deliberately out of scope; Nova is FM/F3C. Recorded in `docs/01`. |
| §5.3.1.2 FM frequencies/stability | Implemented; transmitter stability figures are not receiver-side tests. |
| §5.3.2/§5.3.3 levels | Now recorded. Software amplitude tolerance is pinned by `roundtrip [11]`. |
| §5.4.1/§5.4.3 gray scale | Linear FM gray scale pinned by `roundtrip [12]`. |
| §5.5.1/§5.5.2 subcarrier FM/direct FSK | The 2009 and 2023 PDFs checked here carry the same distinction; ±400 HF and ±150 LF deviation are implemented. LF remains synthetic-only permanently by Sara's session-9 decision. |

## Contradictions found

1. **NOTICE duplicated its standards sentence** and claimed ACFax FIR
   tables were present. They were not: `git log -S firwide` reaches only
   the scaffold commit, where the strings are in NOTICE and the survey.
   Fixed in NOTICE and `docs/00`.
2. **NOTICE claimed third-party code while the reuse ledger was empty.**
   The truth is idea-level reuse with no copied code or tables. NOTICE
   and the ledger now say the same thing and name each reused rule.
3. **The HamFax access note was stale.** SourceForge answered 403 at the
   first survey, but answers 200 now; the GitHub mirror's
   `FaxDemodulator.cpp` confirms the ACFax table lineage.
4. **The 2009-vs-2023 WMO restructuring claim was wrong.** Session 2's
   log said §5.5 was restructured in 2023. The 2009 PDF checked in this
   audit carries the same §5.5.1/§5.5.2 split. `docs/01` and NOTICE are
   corrected; the historical log remains unchanged, as required.
5. **ISO §4.2.7 cited a nonexistent line-assembly assertion.** Fixed by
   citing the actual picture-domain tests.
6. **ISO §4.2.3 was being graded against the wrong bar.** It requires
   automatic **or** manual IOC; the missing automatic behaviour belonged
   to §4.2.5/§5.4.2. Both the documentation and the code are fixed.
7. **The decoder detected 675 Hz but never used it.** A generated IOC-288
   signal decoded at default options would have been drawn at 1810 px.
   Automatic selection is now implemented and pinned by `roundtrip [4]`.
8. **±150 Hz was implemented but unreachable from the product CLI.**
   `--dev 150|400` is now wired and smoke-tested.
9. **A clean ±150 Hz synthetic was falsely reported as a stepping
   timebase.** The wedge-score plateau alternated by 10 samples under LF
   carrier-phase alternation. The phasing position now comes from the
   white leading-edge crossing; `roundtrip [6]` pins the linear verdict.
10. **WMO level-range, AM-scope, and optional recording-level clauses were
    absent from the distilled spec.** Added to `docs/01`.

## Prior-art and provenance result

The final provenance record is in `NOTICE` and `docs/00`. Summary:

- **ACFax:** quadrature mix and amplitude-normalized discriminator
  architecture; retained-raw/non-destructive adjustment is the M4 model.
  ACFax's FIR tables are **not** used.
- **HamFax:** feature-shape and lineage evidence only.
- **weatherfax_pi / KiwiSDR:** wedge fit, median/spread rejection, leaky
  miss counter; constants re-measured. Fractional sample-rate tracking was
  surveyed and not taken.
- **JWX:** Goertzel/domain choice and clock-corrected fold ideas; manual
  calibration and post-decode realign are useful M4 precedents.
- **fldigi:** run-abandon shape, coherence test shape, and per-line
  correlation kept per line. The broad unrestricted correlation placement
  remains a measured failure; do not revive it outside the bracketed
  dropout rule.
- **Isobar/KG-FAX:** local canonical archive found at
  `../isobar-dev.zip`. Its `core/live.h` / `core/syncscan.cpp` are the
  most relevant M4 references: one incremental `LiveScan` state machine,
  batch mode as a single feed of the same machine, chunking invariance
  tests, one-line lookahead, manual phase nudge, and a thread-safe finish
  request. Reuse that architecture before inventing a second live/batch
  split.

No copied third-party code, tables, or files were found in Nova.

## GUI/live-audio readiness

### Ready as-is

- `nova-core` is already a static library with no global mutable state.
- The public results are plain value types and safe to move across a
  worker/GUI thread boundary.
- File decode works as a pure pipeline:
  `read_wav -> resample -> fm_demod -> decode_fax -> Image`.
- Options structs map naturally onto a settings panel.
- The 22-suite fixture/roundtrip system is a strong regression net,
  including picture-domain checks independent of decoder code.

### Small core changes worth doing at the start of M4

**Done, session 14** (`core/hooks.hpp`; screamers in the `hooks` suite;
fixture images, CLI output and all five debug streams verified
byte-identical against the pre-split baseline). The list below is what
was built:

1. Replace `std::getenv` debug printing with an optional log/progress
   callback.
2. Add cooperative cancellation and structured error kinds.
3. Split `decode_fax`'s numbered stages into named internal functions so
   progress, cancellation, and future incremental execution have seams.
4. Consolidate duplicated internal helpers only where the variants are
   truly identical; leave calibrated constants beside their measurements.
5. Keep comments synchronized during that split.

### M4 design decisions still open

**All eight were answered in session 15 — see `docs/04-receiver-ui-survey.md`,
"Answers to the eight open M4 questions." Five follow from a survey of 16
commercial receiver manuals; three were decided by Sara on 2026-08-13
(streaming model, redraw, storage). The list below is kept as posed.**

- Streaming model: growing-window re-decode, genuinely incremental
  pipeline, or monitor-then-decode on stop tone. Isobar argues for one
  incremental state machine with batch as a wrapper, but Nova's current
  long-baseline and bracketed-repair algorithms must be mapped honestly.
- Multi-transmission live sessions: the current model is one recording →
  one image; a live receiver will see repeated transmissions.
- Incremental tone scan: the current full scan costs ~9 s on JSC4.
- Early-window status: a clean 75 s generated CLI smoke decoded straight
  but reported +261 ppm from its short baseline. Live UI must mark clock
  and timebase as provisional until the measured baseline exists; this is
  the known first-minute limitation, not a regression from this audit.
- Provisional draw/redraw: dropout repair needs the far side of a run;
  row splitting needs neighbouring-line context.
- Retained raw stream and post-decode adjustment API: use ACFax/Isobar,
  and record the reuse the day it lands.
- Memory policy for an all-day capture.
- Manual phase adjustment UX, which is also the remaining ISO §4.2.6 /
  §5.4.3 compliance item.

## Validation performed

- Baseline before changes: **22/22 tests passed**, 61.91 s.
- Final suite after changes: **22/22 tests passed**, 83.19 s.
- `roundtrip`: all 15 groups passed; measured values are in the test
  output and summarized above.
- CLI smoke: generated IOC-288 ±150 Hz signal decoded with automatic IOC
  selection; after the phasing fix it reports a linear timebase.
- Pictures inspected after the phasing-edge change:
  - `fixtures/vmw-phasing-image-160s.wav` decoded with the phasing anchor;
    the Bureau of Meteorology title and chart boundary are placed
    correctly.
  - `fixtures/test-chart-jmh-60s.wav` decoded with the tracked image
    anchor; the chart border remains continuous.
- `git diff --check` clean.
- No recordings, standards, prior-art sources, or temporary audit tools
  are tracked.

## Recommended next step

Session 14 completed the first half of this: the core seam work (above)
is done. What remains, verbatim from the audit:

Design the FLTK/RtAudio shell around those seams. Use
Isobar's `LiveScan` single-state-machine/chunking-invariance architecture
and ACFax's retained-raw non-destructive model as the references, and
answer the "M4 design decisions still open" above on paper first.

**Status, session 15.** The "on paper first" condition is met: all eight
questions are answered in `docs/04-receiver-ui-survey.md`, and the
decided architecture is recorded under M4 in `ROADMAP.md`. The shell
design itself is still to be done.

**Status, session 16.** The last question `docs/04` created — how the
provisional live view gives way to the saved image — is decided (show
the transition: provisional label from the first row, one announced
swap at end of transmission), together with the PNG writer (hand-rolled,
uncompressed deflate) and the manual-correction model (forward-only on
the preview, non-destructive re-render on the saved image, live
overrides seeding the batch re-decode). `docs/04` was independently
re-verified against all sixteen manuals this session. **No design
question remains open; the gate for GUI code is fully clear.** What
remains is the shell design itself.
