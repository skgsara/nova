# ROADMAP.md — Nova

Milestone map. Done vs pending. Screamer tests owed by the risk
register are milestone entries, not intentions (SOP P1.5).

## M0 — headless core decodes one real recording  [DONE 2026-08-12]
- Dependency-free DSP core: resample-to-internal-rate, FM demod
  (quadrature + phase-difference discriminator), sync, line assembly.
- First fixture: `test chart.m4a` (JMH Tokyo 13988.5 kHz) — decodes
  straight and readable, incl. the portrait, unattended.
- Harness: `nova-gen` signal generator (start/phasing/image/stop,
  injectable ppm and noise). Round-trip test suite: 6 groups green.
- Screamers live: `ctest` = roundtrip (synthetic matrix) + fixture
  (real JMH excerpt, measured bounds).
- Key bring-up findings in SESSION-LOG 2026-08-12 session 2.

## M1 — mode generality  [DONE 2026-08-12, sessions 3 + 4]
- Rate auto-detect: odd-harmonic comb scan, 60/90/120 from one code
  path (no hardcoded 576/120). Real-signal proof: JSC1/4/5/6 at 60 lpm
  (93-99% locks), everything else at 120.
- Signal onset gate: recordings opening with leader tones / stall-fill
  are detected and anchored (or refused) — session 3.
- Full-line image mapping (measured JMH line layout; no cropping).
- Fixtures: `kyodo-news-jsc1-60lpm-120s` (only 60 lpm signal),
  `himawari-jmh-warp-120s` (stream time-skip), `stall-fill-15s`
  (rejection screamer), KiwiSDR test chart (new primary).
- ±400 Hz proven on real signals; ±150 Hz synthetic only (gap).

## M1b — dead-sector style + re-acquisition  [DONE 2026-08-12, session 4]
- Line-start anchor now measured by across-line consistency instead of a
  fold-average: the dead sector is the only part of a line that looks the
  same on every line. Scores the black->white *shape*, so the black space
  margin of a full-disk satellite image cannot fake a pulse.
- Dead-sector style (black pulse / white only) detected per recording,
  reported, and pinned by a screamer. Library separation: 0.48-0.94 vs
  0.14-0.34.
- Pass A re-acquires: after 8 unlocked lines it sweeps the whole line at
  a coarse step. This also healed the Himawari stream time-skip — the
  warp fixture's max_step fell from 54.3 px to 0.75 px — so "wide
  re-acquisition after time-skips" is done, not M2 work.
- Library effect (honest locks, before -> after): FAXSignal 65 -> 2170,
  XSG ASPN 116 -> 2566, JSC2 103 -> 2192, jmh sample 71 -> 1023, test
  chart 62 -> 711, HDSDR 105 -> 1790, JMH Himawari 740 -> 1953.
- NEGATIVE RESULT, recorded: the "VMW white-sector sync template" this
  roadmap asked for does not exist. Two were built and measured; both
  raise lock counts and make the picture worse. White-only stations are
  decoded on the measured clock and report zero locks. See docs/01 §5.
- REMAINING: weak-signal period estimation (GYA 2300Z, still slanted).

## M2 — automatic slant correction  [DONE 2026-08-12, session 5]
- Done in M0/M1b: clock-rate estimate from the whole signal, per-line
  dead-sector re-lock [WMO §5.1.3.3], whole-line re-acquisition.
- Session 5 — **the period was being measured over too short a baseline,
  in both estimators.** Accuracy comes from drift accumulated across the
  recording, not from lag resolution or averaging (docs/01 §5).
  - No locks (white-only): folded-block phase tracking replaces the bare
    200 Hz autocorrelation, whose lag step is 10 000 ppm and which was
    wrong by 30–180 ppm on real recordings.
  - With locks: pass B now pairs locked lines an eighth of a recording
    apart instead of neighbours ≤10 lines.
  - Both segment at discontinuities first (phasing↔image step, stream
    time-skip, chart restart) — a long baseline is only meaningful
    inside one regime.
- The claim "the library decodes straight" was FALSE when it was written.
  Measured residual shear, before → after: JSC2 −157 → +6 ppm, JSC3 −182
  → −5, JSC4 −172 → +2 (a third of a page of drift, on the newspaper
  faxes where it is hardest to see by eye), GYA 2300Z +50 → +4, VMW 2215Z
  −399 → +0.2, NMC −79 → −9. Nothing regressed.
- GYA 2300Z's frame line — a feature of the chart, not a statistic — is
  now straight to +1.7 ppm over 1358 lines (was −23.4).
- Screamers: `fixture_weak_white` (GYA 2324Z, fails at −51.6 ppm if the
  fold is removed — verified by removing it), roundtrip [7] white-only at
  a known +250 ppm with zero locks, roundtrip [8] −137.00 ppm measured
  against −137 true.
- OPEN: fractional resampling (KiwiSDR approach) — not needed so far,
  the fitted-line + local-median correction has been sufficient.

## M2b — timebase CORRECTION, not just detection  [session 11; white-only half open]

Sara reviewed all 20 decoded charts by eye and every complaint she made was
the same axis — where each line starts, horizontally. Six recordings "zig
zag"; two "lose sync" at one end; one is a staircase. Sessions 9 and 10
built two statistics that detect and report a bad timebase, and nothing
corrected one: the decoder told the truth about JSC and then drew the
picture anyway. Measured against the drawn pixels, the recordings she named
carried 3.0–4.6 px of dead-sector-edge scatter and the ones she passed over
carried 0.3–1.9.

Done:
- **Segment the tracked sync residual at change points.** A move is real
  when the kSegHalf=4 locked lines each side agree it happened, by more
  than kNonlinSec (10 samples) — the resolution the timebase test already
  claims. The assembly's ±8-line median window is truncated there, so it
  never averages across a step; the test's window is NOT, because it is a
  calibrated instrument (session 9's library thresholds were measured
  through it).
- **Fit a robust line, not a level, inside each segment.** The residual
  ramps between steps, because the period fit absorbs the mean insertion
  rate — 1.9 samples/line on the synthetic, 19 samples of tilt across an
  11-line segment. Theil-Sen (median of pairwise slopes); the median only
  where a segment is too short to see a slope.
- **Follow a real skip in one line.** A change point is exempt from the
  per-line clamp, so a recording that loses 1270 samples gets a one-line
  seam instead of a 12-line diagonal tear.
- **Believe a residual that disagrees with the FIT.** The old ±2*search
  gate dropped exactly the evidence a big skip produces; JMH KiwiSDR
  Himawari's whole first half was drawn 574 px across because of it.
- **Coasting no longer sets a correction level**, so a picture whose first
  lines do not lock starts at its first real measurement instead of
  clamping up to it from zero.
- **A picture-domain screamer at last** (`--expect-straight-strip`): the
  dead sector's edge measured in the finished pixels, sharing no code with
  the decoder, on five fixtures. `place_rms_px` is the decoder's own
  account of the same quantity and is held to the same bound.

Measured, whole library: 6 recordings better, 14 unchanged, 0 worse.
Ground truth (roundtrip [10]): bar scatter 2.19 px → 0.00 with the same
insertions, and 0.28 px of place error when they land on the line boundary.

Session 11b, after Sara reviewed the session-11 decodes and found three
things still wrong ("for JSCs, small zigzag are still zigzags, still cause
difficulties of reading"):
- **The rows were stretched, not moved.** A capture chain does not wait
  for a line boundary to insert samples, and when it lands mid-line
  everything after that point moves while everything before it stays. No
  per-line offset can place such a row. Measured in the session-11
  decodes: the two ends of a JSC row moved with correlation +0.12 and
  disagreed by 5-10 px of 1810, against 1 px on a linear recording. The
  size of the move is already known (it is the difference between this
  line's correction and the next line's); only its POSITION was missing,
  and the picture supplies it — the break goes where splitting the row
  makes it agree best with the row above. JSC2 fixture: the two ends
  disagree by 10.0 px without this pass and 1.0 px with it.
- **Rows a dropout stranded are placed by the picture.** Eight lines
  straddling JMH KiwiSDR Himawari's ~1270-sample move carry no lock, so
  the window placed them where the lines before the move are — ~75 px from
  the rest of the chart, the band Sara found still there. Their own sync
  template is not the answer (following HDSDR's equivalent bands tears the
  text apart, measured), and neither is a general "let the picture place
  every unlocked row" rule, which fails two screamers. Only rows inside a
  run that the phase moved across qualify.
- New fixture `himawari-kiwisdr-dropout-120s.wav` and two new picture-
  domain checks: `--expect-rigid-rows` (do the two ends of a row move
  together) and `--expect-rows-in-place` (does any row match the row above
  best at a large shift). 22 suites.

OPEN — the white-only half:
- A station with no sync pulse has no residual to segment, so VMW 2215Z's
  staircase is untouched. fldigi's discarded per-line correlation shift
  (docs/00, session 11) is the idea to take: correlate each line against
  the previous one, keep the per-line value instead of the histogram mode,
  accept only persistent moves. Fixture-in-waiting: VMW 2215Z 0–120 s.
- No screamer yet for a faded pulse station whose steps are below its own
  measurement noise.

## M3 — full auto sequencing  [done except manual override, which needs M4]

Done (session 6) — detection, measured against the library:
- Start/stop tones [ISO §4.2.5]: `core/tones.cpp`. Accept test is spectral
  **purity** in the tone's own bin, not a transition rate, because a rate
  test cannot tell a 300 Hz square wave from text that averages 300
  transitions/s. Library separation: content ≤ 0.16, tones 0.68–0.99,
  threshold 0.35. Zero false positives in 5.9 hours; 14 of 20 recordings
  carry a start tone, all measuring 299.8 Hz, all stops 450.5 Hz — inside
  the ±1% of WMO §5.2.6.
- Phasing [WMO §5.2.3]: `core/phasing.cpp`. Wedge fit, median, 10–90%
  spread rejection (KiwiSDR shape) plus a duration cap from the spec.
  Found on 15 of 20 recordings, 30.0 s long, spread 4–73 samples of 4000.
  Recovers the line rate (60/90/120) and both waveforms (5/95 and 50/50 —
  XSG is the real symmetric case).
- **This corrected session 3's tone survey.** That survey searched the raw
  audio, where these signals only appear as incidental envelope ripple,
  and concluded one recording in twenty carried a start tone. The correct
  domain is demodulated video, and the answer is 14 of 20.
- Screamers: `tones` [1]–[11], `tones_fixture_vmw` (real start tone +
  phasing in an off-air white-only recording), `tones_fixture_no_false_start`
  (zero events on 120 s of real newspaper text).

Done (session 7) — sequencing, wired and verified against pictures:
- **The edge convention, settled.** The phasing white leading edge marks
  dead-sector ENTRY [WMO §5.2.3.4], on both dead-sector styles. Measured
  by folding the video over the phasing region and the image region on one
  common grid: on JMH the phasing edge is at −73 samples of 4000 and the
  image's dead-sector black run starts at −67. Across seven pulse-station
  recordings the offset is the black porch, −1.65% to −2.86% of a line,
  and two recordings of one transmitter agree to 3 samples of 4000.
- **The phasing anchor drives the decoder on white-only stations.** Their
  image-derived anchor was catching the chart's blank margin, not the dead
  sector: VMW 2230Z decoded rotated by 520 px of 1810, NMC's caption torn
  across the line boundary, GYA 2324Z with a 130 px strip of its right edge
  on the left. All three correct now. Pulse stations keep their tracked
  anchor and are byte-identical (checked on all 10). The anchor delta is
  reported on every decode either way.
- **Segmentation.** Only the picture is drawn: start tone and phasing
  cropped from the head, stop tone from the tail. Output framing only —
  onset, period, anchor and both tracking passes still see the whole
  recording, so nothing session 5 measured moves.
- Screamers: `tones` [11] (absolute anchor, both run parities and a −137
  ppm clock), `roundtrip [9]` (row 0 of the output is image line 0),
  `fixture_phasing_anchor` (the VMW picture: content begins one dead
  sector into the line, 4.97% — reads 0.00% with the old anchor),
  `fixture_phasing_boundary` (a real phasing→image transition).

Done (session 8) — the anchor question closed on evidence:
- **A pulse station keeps its tracked anchor.** Session 7 argued it; JSC2
  decides it. That recording's phasing anchor sits 234 samples (106 px of
  1810) from the tracked one, and the tracked one draws the correct picture,
  because its timebase steps and a fixed reference propagated on a fitted
  clock cannot survive that. Now asserted, not assumed.
- **The two anchors are pinned against each other** on pulse stations, where
  the phasing anchor is measured but never used and so nothing else in the
  suite corroborates either. Bands from the whole library: −66.1 to −114.3
  samples of 4000, repeats of one transmitter within ~7.
- **Why two recordings disagreed.** JSC2 and JSC3 carry ~21-sample timebase
  steps every few lines — in the audio, still there at 44.1 kHz through a
  separate demodulator, absent from all 18 other recordings. Their true
  porch, measured across the phasing→image boundary with no clock model in
  between, is normal (−46 and −3). `clock_ppm` on those two files is the
  clock plus the mean insertion rate. [docs/01 §5]
- New fixture `xsg-phasing-image-100s.wav`: the library's only SYMMETRIC
  50/50 phasing station, whose anchor no fixture had covered.
- Screamers: `fixture_anchor_delta_jmh` (5/95), `fixture_anchor_delta_xsg`
  (50/50). Both fixtures have an even number of phasing lines on purpose —
  reverting session 7's integer-line anchor fix moves them half a line out
  and both fail.

Pending:
- Manual override for everything [ISO §4.2.6 "facility for manual
  adjustment"] — still untouched, needs the GUI (M4).

## M4 — GUI + live audio  [pending]
- FLTK GUI, RtAudio capture, spectrum/waterfall, image tools,
  post-decode realign / line-start adjust on retained raw stream
  (ACFax architecture: non-destructive).
- m4a input via runtime ffmpeg; WAV native.

## M5 — packaging & release  [pending]
- CI: tier 1 release-tested (Win64, macOS universal, Linux x86_64);
  tier 2 CI-built (32-bit Win/Linux, ARM, FreeBSD).
- Zero compiler warnings on all runners (grep the log, not the tick).
- Compliance matrix fully populated with test links [docs/02].
- Pre-release: provenance/NOTICE audit, artifact download-and-inspect,
  similarity spot-check recorded (SOP L10 adapted: attribution check
  is the deliverable, since reuse is declared).

## Registered gaps
- Short windows of a deeply faded signal. On the full recording GYA 2300Z
  measures −116.8 ppm and draws straight, but 120 s windows of its faded
  stretch give anything from −1223 to +320 ppm: with few blocks there is
  no baseline to be accurate over. Live decoding (M4) will hit this, since
  it cannot wait for the whole transmission. No screamer — the fixture
  deliberately uses the stable recording (GYA 2324Z) instead.
- Picture content that mimics the optional sync pulse. A dark run at a
  fixed position on every line, followed by white, is by construction
  indistinguishable from the pulse of WMO §5.1.3.3 — the synthetic test
  pattern's own black bar and gradient strip produced 629 "locks" on a
  signal generated with no pulse at all. No library recording does this
  (session 4: white-only stations score 0.14–0.34), so it is registered,
  not fixed.
- Multiple transmissions in one recording (session 7). Segmentation takes
  the first — opening sequence to the first stop tone that follows it —
  and drops the rest; `jmh sample` loses the 143 s of the next chart it
  happens to catch. One recording, one image is the current model.
  Splitting a recording into several images is unbuilt, and not a
  milestone: no other library recording needs it.
- The phasing anchor is measured ONCE, at the middle of the interval, and
  propagated on the fitted clock for the rest of the recording (session 7).
  That is a fixed reference, not a tracked one: on a white-only station a
  mid-stream time-skip would shift the picture with nothing to re-acquire.
  No library recording exercises it — the one time-skip case, himawari, is
  a pulse station that re-acquires — but M4 live decode will.
  Session 8: reachable, not hypothetical. JSC2's timebase steps put its
  propagated phase 160 samples out over the ~90 lines between its two
  anchors. It is a pulse station, so tracking absorbs it; a white-only
  station with that timebase would be drawn wrong and nothing would notice.
  Session 9 narrows it: something would now notice — that combination is
  built synthetically in `roundtrip [10]` and the phasing statistic
  convicts it with no locks anywhere in the recording. The picture is still
  drawn wrong; the difference is that the decoder now says the timebase is
  not linear instead of reporting a confident clock figure.
- ~~Timebase steps are neither detected nor reported~~ — closed session 9.
  `DecodeResult::timebase` reports kLinear / kSteps / kUnknown from two
  independent statistics, and the count is six recordings, not two: every
  JSC file in the library steps, including the three 60 lpm ones. Three
  limits remain, each registered rather than hidden:
  - The reported step RATE is a floor, not a count. The ±8-line median that
    makes a step visible is wider than the gap between dense steps, so they
    merge: a synthetic inserting 90.9 per 1000 lines reports 36.9. Good for
    convicting a recording, useless for measuring an insertion rate.
  - Two recordings can be measured by neither statistic and report
    kUnknown: GYA 2300Z and VMW 2215Z are white-only (no tracked residual)
    with no phasing interval found (no edge to fit). A stepping timebase on
    such a recording would still be invisible — which is precisely the
    dangerous combination, since nothing tracks there either.
    **Session 10 explains both and closes neither.** VMW 2215Z genuinely
    contains no phasing interval — the recording starts mid-transmission,
    and the whole file holds three isolated single lines at positions 2657,
    1638 and 0. That is a measured negative, not a gap to close. GYA 2300Z
    DOES contain one, 40 lines at 4.5–24.5 s, and it is now found and used
    for the anchor; but its edge is too noisy (~15 samples line to line,
    against a 10-sample threshold) to test for steps, so the timebase
    verdict stays kUnknown with the reason named. The blind spot is
    therefore smaller in the way that matters — GYA's picture is now phased
    correctly — and unchanged in verdict count.
    A faded interval that IS stepping remains undetectable by either
    statistic. No library recording is both, so this is unexercised.
    A short cut of
    a perfectly good pulse station reports kUnknown too: the rate needs 128
    drawn lines, and a 60 s fixture at 120 lpm has 120. That is a floor on
    what a rate can honestly be measured over, not a bug, but it means M4's
    live decode has no verdict for the first minute of a transmission.
  - ~~Nothing repairs a stepping timebase~~ — closed session 11, and it
    became a milestone the moment Sara looked at the pictures (M2b). Where
    per-line sync exists the steps are now corrected as well as counted:
    ground-truth synthetic 2.19 px of bar scatter -> 0.00, JSC2 fixture
    2.89 px of line-start error -> 0.61. Where it does NOT exist — a
    white-only station — nothing is repaired and the paper still moves;
    that half is M2b's open item, with VMW 2215Z's staircase as its
    fixture-in-waiting.
- Segmentation costs a full `detect_tones` pass over the recording
  (session 7): ~9 s on the 61-minute JSC4 against a 37 s decode. Fine
  offline, unbudgeted for M4, where the scan wants to be incremental.
- ~~`phasing_anchor_delta` asserted nowhere~~ — closed session 8:
  `fixture_anchor_delta_jmh` and `fixture_anchor_delta_xsg`. The JSC2/−234
  vs JSC3/−55 disagreement that was filed here as unexplained is explained:
  those two recordings' timebases step, and their true porch is normal.
- 240 lpm: deliberately out of scope (not required by ISO 9876 §4.2.4).
- 90 lpm real fixture: none in the library (session 3 batch survey).
- IOC 288 real fixture: none found (no 675 Hz start tone anywhere).
- ±150 Hz LF mode: synthetic testing only, permanently. Sara (session 9)
  knows of no operating station still carrying it, so this is not a gap to
  close by finding a fixture — it is the honest end state.
