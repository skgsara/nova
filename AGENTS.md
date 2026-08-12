# AGENTS.md — Nova

## What this project is
Nova is a cross-platform HF WEFAX (F3C) decoder written from public
standards: WMO-No. 386 Vol. I Part III §5 (signal) and ISO 9876:2015
§4.2 (receiver behaviour, as a design target — no certified-compliance
claim). C++17, FLTK + RtAudio, GPLv3+. No reverse engineering is
involved; DSP lineage is GPL reuse with attribution (see NOTICE).

## Read first, every session
START-HERE.md → top entry of SESSION-LOG.md → state the next step back
to Sara before acting. If the log's last entry has no next step, say so
and stop — that is a broken log, not an invitation to improvise.

## Lane
There are no dirty/clean lanes in this project — no analysis dir, no
decompilation. Instead there is a **provenance rule**: every reused
algorithm or table is recorded in NOTICE and in `docs/00` the day it
enters the tree, with source project and licence.

## Spec status
`docs/01-signal-spec.md` is authoritative for the signal;
`docs/02-compliance-matrix.md` is the acceptance checklist. Changes to
either require a note in SESSION-LOG.md and, once tests exist that pin
the old reading, a committed test update in the same change.

## Reuse before invention (Sara, session 4)
Nova is standards-first, not from-scratch-first. Before writing a new
algorithm, look at how the mature decoders already solved it:
**ACFax, HamFax, fldigi, JWX** (JWX source is in the parent folder),
**weatherfax_pi** and the **KiwiSDR** fax extension — both already good on
clock/slant and phasing-stage alignment — and **Isobar**, which is the
reference for the per-line sync lock approach specifically. If a good solution exists there, reuse it, with the
provenance rule below (NOTICE + `docs/00`, same day). Write something new
only when the prior art genuinely does not cover the case, and say in the
session log which projects you checked and what they did.

This is not only about effort. Their behaviour is the de-facto
compatibility target: a decoder that agrees with fldigi on a signal is
more likely right than one that agrees only with its own reasoning.

## Standing rules
- Claim checklists, not step lists. Always include a "contradictions
  found" section ("none" is a deliverable).
- Every load-bearing claim cites a spec section (e.g. `[WMO §5.2.2.1]`,
  `[ISO §4.2.5]`) and names its screamer test. No screamer = registered
  gap, written down as such.
- Never commit: `recordings/`, `*.m4a`, `*.pdf`, anything under the
  parent folder. The ISO PDF is a single-user licensed document — it is
  cited, never copied or quoted at length. **`SESSION-LOG.md` IS
  tracked** — Sara, session 1: "we don't need to hide anything as our
  develop history" (commit e49834d). This line used to list it as
  never-commit, which contradicted the decision and the repo; corrected
  session 5.
- WIP commits on a scratch branch are fine; `main` moves and pushes
  happen only when Sara asks — then don't leave it unpushed.
- No flattery. Disagree when you have grounds. Say when you don't know.
- Finish the small piece or revert; never leave the tree unbuildable.
- Update SESSION-LOG.md (append-only, newest first) before every stop,
  ending with the exact next step.
- Verify before claiming done: run the tests, look at the output image.

## Current risk register (top items)
1. ~~Auto slant/clock correction~~ — session 5: measured, not assumed.
   The previous entry here claimed this was proven "across the library";
   it was not. Nobody had measured the residual shear of the decoded
   images, and JSC2/3/4 were drifting a third of a page. Both period
   estimators were fitting over too short a baseline (docs/01 §5). Now:
   residual shear ≤ 10 ppm on every library recording, two recordings of
   the same station agree to ~1 ppm, and the synthetic round-trip
   recovers −137 ppm as −137.00.
   **The lesson for the next agent: a decoder metric that is never
   compared against the picture is not evidence. `locked_lines` was
   green on JSC2 for two sessions while the picture sheared.**
2. ~~Per-line resync~~ — session 4: anchor found by across-line
   consistency, pass A re-acquires after 8 misses. 88-99% honest locks on
   every pulse station in the library; time-skips heal (warp fixture
   max_step 54.3 -> 0.75 px). Remaining limit: weak/faded signals (GYA
   2300Z) still break the coarse period fit. White-only stations report
   zero locks BY DESIGN — a measured negative result, not a gap to close
   (docs/01 §5); their per-line phase, if it is recoverable at all, has
   to come from the phasing stage in M3.
3. ~~Start/stop tone detection~~ ~~+ auto sequencing~~ (M3) — session 7:
   wired. The phasing line-start anchor drives the decoder on white-only
   stations, and the tone events bound the drawn picture (start/phasing
   cropped from the head, stop from the tail). The false-start trap stays
   closed by spectral purity rather than transition rate (content ≤ 0.16,
   tones 0.68–0.99, threshold 0.35; zero false positives across 5.9 hours).
   **The lesson for the next agent: a number can agree with the picture
   on every recording you check and still be half a period wrong.** The
   absolute phasing anchor was referred to the MIDPOINT of the run, which
   is a half-line whenever the run has an even number of lines — and a 30 s
   phasing interval is 60 lines, so the whole library read exactly half a
   line off. Every synthetic test passed, because the generator emitted 30
   phasing lines too. Only XSG ASPN (53 lines, odd) read correctly, and it
   was the disagreement between one odd recording and every even one that
   exposed it. `tones` [11] now generates both parities.
   Session 6's lesson still stands underneath: session 3's "only `jmh
   sample.wav` carries a start tone" was an artifact of running an FFT on
   the raw AUDIO when the control signals are black/white alternations in
   VIDEO. The real answer is 14 of 20.
4. ±150 Hz LF deviation [ISO §4.2.2] — synthetic only; no fixture.
5. ~~Long recordings~~ — session 3: JSC4 61 min, XSG 23 min, Himawari
   17 min all decode end-to-end.

## Registered gaps
- 90 lpm fixture: none in the library (batch survey, session 3).
- IOC 288 fixture: none. Re-confirmed session 6 with a detector that works
  in the right domain and searches ±1.5% around 675 Hz: no IOC-288 start
  tone anywhere in the library. IOC 288 remains synthetic-only.
- ~~Which edge of the dead sector the phasing `line_start` marks~~ —
  settled session 7 by folding the video over the phasing region and the
  image region on one grid: it marks dead-sector ENTRY, on both styles.
  On pulse stations it lands one black porch before the sync pulse
  (−1.65% to −2.86% of a line; two recordings of one transmitter agree to
  3 samples of 4000). On white-only stations the image anchor is the one
  that is wrong. Verified against decoded pictures, not just the numbers.
- GYA 2300Z phasing: an 18-line candidate at 15.5–24.5 s scoring 0.77
  passed the looser first-pass thresholds and is rejected by the final
  ones. Deeply faded; whether it is a real partial phasing interval or
  dark content is not established either way.
- ±150 Hz LF mode: no real-world source known; synthetic-only testing.
- Short windows of a deeply faded signal: the whole GYA 2300Z recording
  measures −116.8 ppm and draws straight, but individual 120 s windows of
  its faded stretch scatter from −1223 to +320 ppm. Baseline is the
  instrument; a short window has none. Matters for live decode (M4).
- Content that mimics the sync pulse (dark run at a fixed position on
  every line, then white) is indistinguishable from it by design. See
  ROADMAP registered gaps.
- Multiple transmissions in one recording. Segmentation takes the FIRST
  (opening sequence → first stop tone that follows it) and drops the rest;
  `jmh sample` loses the 143 s of the next chart it happens to catch. One
  recording → one image is the current model. Splitting a recording into
  several images is unbuilt and unregistered as a milestone.
- The phasing anchor is measured ONCE, at the middle of the interval, and
  then propagated on the fitted clock for the whole recording. That is a
  fixed reference, not a tracked one: on a white-only station a mid-stream
  time-skip would shift the picture and nothing would re-acquire. No
  library recording exercises this (the one time-skip case, himawari, is a
  pulse station that re-acquires); it matters for M4 live decode.
- Segmentation costs a full `detect_tones` pass over the recording (~9 s
  on the 61-minute JSC4, against a 37 s decode). Fine offline, unbudgeted
  for M4 live decode, where the scan wants to be incremental.
