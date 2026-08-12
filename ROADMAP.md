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

## M3 — full auto sequencing  [detection done, sequencing pending]

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
- Screamers: `tones` [1]–[10], `tones_fixture_vmw` (real start tone +
  phasing in an off-air white-only recording), `tones_fixture_no_false_start`
  (zero events on 120 s of real newspaper text).

Pending:
- **Sequencing itself: nothing consumes the detections yet.** `decode_fax`
  still finds its anchor from image lines. Wiring the phasing line-start
  in is the payoff — it is the only per-line phase source that exists for
  the white-only stations (VMW/NMC/GYA), which report zero locks by design.
- Blocker to resolve first: which edge of the dead sector the phasing
  `line_start` marks. On JMH Test Chart it agrees with `fax.cpp`'s
  image-derived anchor to ~23 samples of 4000 (0.6% of a line) once the
  black porch is accounted for — close enough to be the same feature,
  not close enough to call the convention settled. Arithmetic cannot
  settle it; a decoded picture can.
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
- 240 lpm: deliberately out of scope (not required by ISO 9876 §4.2.4).
- 90 lpm real fixture: none in the library (session 3 batch survey).
- IOC 288 real fixture: none found (no 675 Hz start tone anywhere).
- ±150 Hz LF mode: synthetic testing only; no known on-air source.
