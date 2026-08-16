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
   Session 8 closed the loose end: the two anchors are now pinned against
   each other on pulse stations, on both phasing waveforms
   (`fixture_anchor_delta_jmh` 5/95, `fixture_anchor_delta_xsg` 50/50), and
   a pulse station keeps its tracked anchor on evidence rather than on
   argument. **The lesson for the next agent: when two of your recordings
   disagree, suspect the recording, not only the code.** JSC2 and JSC3 read
   −234.5 and −54.8 against a family of −66 to −114 because those two files
   carry ~21-sample timebase steps every few lines — real, in the audio,
   present at 44.1 kHz through a separate demodulator, and absent from every
   other recording in the library. `clock_ppm` on them is the clock plus the
   insertion rate. Measured across the phasing→image boundary with no clock
   model in between, both files' porch is normal. See docs/01 §5.
4. ±150 Hz LF deviation [ISO §4.2.2] — synthetic only, permanently: no
   operating station is known to still carry it (Sara, session 9). Not a
   risk to retire by finding a fixture; see registered gaps.
   ~~Timebase linearity~~ — session 9: measured and reported, not assumed.
   Every number the decoder produces about time — `clock_ppm`,
   `line_period_s`, `phasing_anchor_delta` — presumes the recording has one
   straight time axis, and six of twenty library recordings do not.
   **The lesson for the next agent: a statistic that separates two files
   you already suspect is not the same thing as a statistic that separates
   them from everything else.** Session 8 proposed the phasing spread on
   the strength of "72/47 against 1–19"; measured across the library with
   the decoder's own detector it reads 24–43 on clean recordings and would
   have convicted almost everything. What the raw spread mostly measures is
   the clock (0.66 samples per line at −90 ppm), which is the very thing
   the test is supposed to be independent of.
5. ~~Long recordings~~ — session 3: JSC4 61 min, XSG 23 min, Himawari
   17 min all decode end-to-end.

## Registered gaps
- 90 lpm fixture: none in the library (batch survey, session 3).
- ~~Stop-tone fixture: none~~, and ~~nothing in the library fades
  mid-tone~~ — both closed session 21 by one cut, as session 20 predicted
  they would be. `fixtures/nmc-image-stop-tone-120s.wav` is NMC 2204Z
  340–462 s: a real chart ending in a real 450 Hz stop tone at 111.38 s
  of the cut, which fades to nothing for 0.88 s in its middle. First NMC
  fixture in the library, and the first to exercise the TAIL half of
  segmentation (22 lines of stop tone dropped). The other two candidates
  measured the same day, for whoever wants a second: VMW 2230Z's stop
  tone at 631.75 s fades twice (0.50 s and 0.25 s), GYA 2324Z's at
  670.62 s is nearly clean.
- **A white-only station with no phasing interval has no line-start
  anchor at all** (session 21). Not a decoder limitation — the
  transmission contains no such information: the dead sector carries no
  per-line phase (session 4) and there is no phasing edge to fall back
  on [WMO §5.2.3.4]. Both paths must guess from an across-line
  consistency profile, and they guess over different numbers of lines
  (batch 120, live preview 16), so their pictures can differ by however
  far apart the candidates are: 563 px on `gya-weak-white-120s`, 46 px
  on `vmw-white-sector-120s`. `live_preview` reports it and does not pin
  it; the operator's PHASE control is the answer and one click lands the
  page to 1 px. Do not "fix" this by widening a tolerance. Since session
  24 the click also reaches the SAVED image, not just the preview
  (`DecodeOptions::phase_anchor_hint`), and it is a seed the anchor search
  refines rather than a position it obeys: on `vmw-phasing-image-160s`,
  five clicks spread across 3.5% of a line settle on one anchor, 13 px
  from the independent phasing witness.
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
- ~~GYA 2300Z phasing~~ — session 10: settled, and the gap described it
  wrong. It is not an 18-line candidate at 15.5–24.5 s; it is a REAL
  40-line interval at **4.5–24.5 s**, the same interval GYA 2324Z puts its
  clean phasing in. The "18 lines" was an artifact of a detector that grew
  runs from consecutive qualifying lines chopping a faded interval into
  fragments. GYA is white-only, so this is the only place its line phase
  exists; the chart now draws with its title box at the left margin,
  matching GYA 2324Z, where the image anchor put it half a line out.
  **The lesson for the next agent: a threshold measured on strong signals
  describes strong signals.** `min_score` was calibrated in session 6 on
  true phasing at 0.88–0.97 against dark content at 0.48–0.62, and the
  comment in `phasing.hpp` said it "sits in that gap". GYA 2300Z's real
  phasing lines score 0.34–0.88 — through the gap and out the other side —
  because fading takes the contrast and leaves the edge. No threshold on
  that score could have worked; the fix was to stop asking it to decide
  membership at all.
- The phasing timebase witness cannot test a FADED interval, and says so
  rather than guessing (session 10). GYA 2300Z's per-line edge moves ~15
  samples line to line against a 10-sample threshold, so the honest verdict
  is `kUnknown` with the reason named. Both library `kUnknown` recordings
  therefore remain `kUnknown` — VMW 2215Z because the recording genuinely
  contains no phasing interval (three isolated single lines, measured), GYA
  2300Z because its interval is too noisy to resolve a step. What changed
  is that neither is now unexplained, and GYA gained its anchor.
- `--expect-phasing-anchor`'s picture assertion (content begins 2–8% into
  the line, from a column that is white on ≥90% of rows) does not apply to
  a deeply faded recording: GYA 2300Z's whitest column reaches 0.90 only at
  column 429. The anchor there was verified by eye against GYA 2324Z
  instead. A faded-signal form of that automated check does not exist.
- ±150 Hz LF mode: synthetic-only testing, and it will stay that way.
  Session 9, Sara: she knows of no operating station still carrying it.
  That is stronger than the old wording ("no real-world source known",
  which was an absence of evidence): do not spend a session hunting for a
  fixture. The code path is the same as ±400 Hz with one constant changed
  [ISO §4.2.2 "and/or"], `roundtrip [6]` covers it, and the honest status
  is "implemented, synthetic-only, untestable against the air".
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
  **Session 10: the phasing detector did not share that rule and nobody
  had noticed**, because the right answer was winning by luck. It took the
  LONGEST qualifying run; `jmh sample`'s two transmissions have 59 and 60
  phasing lines, and the day the second one grew by a line the head crop
  fell from 62 lines to 3 and 59 phasing lines were drawn into the chart.
  **The lesson for the next agent: two recordings can want opposite answers
  from the same rule.** "Take the first", the obvious fix and the rule
  segmentation already uses, is wrong for FAXSignal — it holds two OPENINGS
  before ONE picture, and taking the first drew 68 lines of the second into
  the chart. The control tones decide it now: the LAST opening inside a
  known transmission, the FIRST when no bounds are known. The tone scan
  moved to §2b and is shared with segmentation instead of run twice.
- The phasing anchor is measured ONCE, at the middle of the interval, and
  then propagated on the fitted clock for the whole recording. That is a
  fixed reference, not a tracked one: on a white-only station a mid-stream
  time-skip would shift the picture and nothing would re-acquire.
  **Session 8: no longer hypothetical.** JSC2 and JSC3 carry ~21-sample
  timebase steps every few lines, and on JSC2 the propagated phase arrives
  160 samples wrong across the ~90 lines between the two anchors. Both are
  pulse stations, so tracking absorbs it and the pictures are right. Had
  either been white-only, the picture would have been drawn wrong with
  nothing to catch it. No white-only recording in the library steps — the
  gap is still unexercised, and now it is known to be reachable.
- ~~Timebase steps are not detected or reported~~ — session 9: closed.
  `DecodeResult::timebase` is kLinear / kSteps / kUnknown, decided by two
  statistics that share no code (smoothed sync-residual step rate; phasing
  edge non-linearity), either sufficient alone, and nova-decode says so in
  words wherever it is not linear. **The library has six such recordings,
  not two** — all six JSC files, including the three 60 lpm ones whose
  +335/+343/+458 ppm clocks nobody had questioned. Of session 8's two
  proposed symptoms, one was wrong as stated: the raw phasing spread does
  NOT separate 72/47 from 1–19 when measured by the decoder's own detector
  (clean recordings read 24–43, because that spread is dominated by clock
  error at 0.66 samples/line). The residual about a fitted line does.
  Remaining limits, all reported rather than hidden: the step RATE is a
  floor, not a count (dense steps merge under the ±8-line median: 90.9
  inserted reads 36.9); two recordings can be measured by neither statistic
  (GYA 2300Z, VMW 2215Z — white-only with no phasing found) and report
  kUnknown; and ~~nothing repairs a stepping timebase~~ — session 11
  repairs it wherever per-line sync exists (M2b), after Sara's by-eye
  review of all 20 charts found the six zig-zagging recordings that every
  decoder statistic had called fine. Session 12 extends it across KiwiSDR
  dropouts: a run of rows unlocked by a sample drop is bracketed by two
  known levels, and the sync pulse is still in the audio at the far one —
  probing each row at both levels re-locks every row but the one the drop
  landed in (far side 0.66-0.96, near side <= 0.22, on all five library
  dropouts), which is split over the whole line. **The lesson for the next
  agent: a statistic that says a recording is faulty is not the same thing as
  a decoder that draws it correctly, and only the picture can tell you
  which one you have.** The suite had no picture-domain check at all
  until this session; it has one now (`--expect-straight-strip`).
- False per-line locks on a faded signal whose picture crowds the white
  gap (session 26, HLL 2147Z): the pulse template's white window is
  polluted, a position ~60 samples late out-scores the true one (0.72 vs
  0.44 measured), and the hop returns 1–3 lines later with a dipped lock
  score. **The lesson for the next agent: a seam count counts decoder
  moves, not proven audio drops** — the two coincided until they didn't,
  and the raggedness Sara sent back twice was the tracker, not the
  network. The picture is straightened by seam ADMISSION (a move that
  returns within the vouching distance is cancelled; `docs/01` §5 item 3,
  `fixture_false_locks`); the template itself is untouched — the white-
  window robustness fix is registered for the day the library shows the
  need, and the admission rule's blind spot (a real drop compensated by a
  real insertion within three lines would be cancelled too) is written
  down in the same place. The porch-edge check (`--expect-straight-porch`)
  exists because the strip statistic is unmeasurable on this chart — the
  same coastline that causes the false locks crowds the strip — and
  because a percentile is blind to a two-row jog by design.
- A widget toolkit's idea of its own state is not the state (session 27,
  the bouncing scroll). `Fl_Scroll` scrolls by MOVING its child, so the
  child's position IS the scroll offset and `yposition()` is a cached copy
  that any child `resize()` invalidates — and `scroll_to` early-returns
  when its arguments equal the cache, so the obvious repair line repairs
  nothing. Session 26's follow-the-newest-row set the cached number
  perfectly and the picture bounced between the bottom of the chart and
  the top of it, once per row batch. **The lesson for the next agent: when
  a check and the screen disagree about a widget, suspect the check —
  ask the thing that is actually drawn where it is, not the object that
  was told where to put it.** Measured on the pre-fix build,
  `yposition()` read 632 while the picture sat at 150. `nova-gui --follow
  BATCHESxROWS` reports the child's real offset and `gui_shell` pins it;
  it needs no window and no draw, because the divergence is in the widget
  positions. Same mechanism had been silently resetting the vertical
  scroll on every zoom change since session 23.
- A test can watch the right thing at the wrong TIME and prove nothing
  (session 27, the retained snapshot). The check for "the older raw stream
  survives the next transmission" was built, passed, and then the rejected
  design was introduced deliberately — and it still passed, because the
  window it watched (the next transmission being RECEIVED) closes before
  the two designs differ, which is while that transmission is being
  DECODED. **The lesson for the next agent: when you introduce the bug to
  verify the check, and the check still passes, the check is measuring a
  different moment — find the moment the two designs actually disagree and
  observe THAT.** A decode that runs inside `shutdown()` is over before
  anything can look at it; the fix was a second operator Stop and polling
  through the decode. Related: [session 23's rule that a mutation harness
  must be verified before its verdict means anything].
- The operator-visible signal comes AFTER the file, never before it — and
  the project has now got this wrong twice (session 23: the status line
  read SAVED over a file not yet written; session 27: the re-render busy
  flag was lowered when the decode finished rather than when the PNG was
  written, which re-arms Apply mid-write and lets a second Apply in on top
  of it). **The lesson for the next agent: whenever a flag or a message
  says "done", find the last side effect it is promising and put it after
  THAT.** Both were invisible until a test recorded order rather than
  outcome, and the second one only became reliably catchable when the test
  spun instead of sleeping — a poll that sleeps arrives after the window
  it is meant to observe.
- **A default of "blank" is not a default of ZERO — find the number the
  operator would mean and start there** (session 28, the SYNC steppers).
  Blank in a correction box means "as measured", and the measured value
  is generally not zero: on a white-only station the clock is −70 to −118
  ppm, so a stepper that nudged from zero would make the operator's first
  click a jump of the whole error, away from correct, on exactly the
  signals the control exists for. Note this is the SAME distinction core/
  already makes by giving `clock_ppm_fallback` a NaN sentinel instead of
  0 — "a perfect clock IS 0 ppm", so zero cannot also mean absent. The
  lesson generalizes: **when a control has an empty state, ask what value
  the picture is currently being drawn with, not what value the variable
  is initialized to.**
- **"Net-correct" is not correct — find the seam where the rule you are
  testing is the ONLY thing that could produce the answer** (session 28,
  click-to-set-PHASE, and the sharpest instance of session 23's rule so
  far). The click handler's "do not act where a correction is impossible"
  guard was deleted as a mutation and EVERY box-and-dirty check still
  passed: `apply_state`'s edit-end rule clears the boxes whenever a
  correction is impossible, so the wrongly-acting click was wiped a moment
  later and the shell looked right afterwards. A second rule was
  concealing the absence of the first, and would have stopped doing so the
  moment it was narrowed. The fix was to make the handler REPORT what it
  did — a return value the production path ignores — so the guard has an
  observable of its own. **Ask of every new check: what else could make
  this pass?**
- **A test that spans two PROCESSES cannot observe a transition** (session
  28, two-click SYNC). "No half-made measurement survives the edit's end"
  was written as two `--metrics` runs; each is a fresh program, so the
  second started with nothing pending and passed against a build with the
  clearing deleted. Any rule that fires when something CHANGES needs both
  sides of the change inside one process — `--then-state` was added for
  exactly that. Corollary for this shell's inspection seams: `--state`
  builds the shell already in a state, so it can never exercise a rule
  about entering one.
- **A check that only ever runs at a parameter's IDENTITY VALUE is not
  checking that parameter** (session 29, the two mutation survivors). Both
  survivors were of this shape and neither was an equivalent mutant.
  Dropping the zoom term from `slant_error_ppm` survived because every
  measurement check ran at 100% zoom, where the scale is 1.0 and the term
  multiplies by one; removing the anchor-clearing from `set_arm` survived
  because nothing anywhere performed the action the rule is about
  (re-arming mid-gesture), even though the code's comment claimed the
  rule. Two habits fall out: vary a parameter across at least one value
  where it BITES, and treat a rule stated only in a comment as untested by
  default. Corollary: a survivor has a third category beyond "equivalent
  mutant" and "delete the code" — **a real rule exercised only where it
  cannot fail**, and that one is a hole in the test.
- **Restoring the source is not restoring the tree** (session 29). The
  mutation harness put the file back and left the MUTATED BINARY in place;
  the next ctest run failed against innocent code. Any harness that
  rebuilds must also rebuild on the way out — `rm` the object file and the
  binary in the restore path, not only in the setup path.
- **A widget set programmatically does not tell anyone** (session 28).
  FLTK fires an input's callback for typing, not for `value()`, so any
  control that writes another control must re-declare whatever the typed
  path would have declared — here, that an edit is in progress. The
  failure is silent and asymmetric: the box shows the operator's change
  while the shell reports itself clean, so Apply stays grey over it. Look
  for this wherever one control drives another.
- Segmentation costs a full `detect_tones` pass over the recording (~9 s
  on the 61-minute JSC4, against a 37 s decode). Fine offline, unbudgeted
  for M4 live decode, where the scan wants to be incremental.
