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

## M2 — automatic slant correction  [mostly done; one case open]
- Done in M0/M1b: clock-rate estimate from the whole signal, per-line
  dead-sector re-lock [WMO §5.1.3.3], whole-line re-acquisition. The
  library decodes straight without manual calibration, longest fixture
  included (JSC4, 61 min).
- OPEN: weak/faded signals. `GYA 2300Z` fits +3576 ppm and slants; it is
  also white-only, so nothing per-line rescues it. Suspect is the coarse
  autocorrelation period fit. Prior art to check first: JWX's
  clock-corrected line accumulation (docs/00, session 4 note).
- OPEN: fractional resampling (KiwiSDR approach) — not needed so far,
  the fitted-line + local-median correction has been sufficient.

## M3 — full auto sequencing  [pending]
- 300/675 Hz start, 450 Hz stop [ISO §4.2.5], phasing align with
  wedge-fit + median + spread rejection (KiwiSDR approach), manual
  override for everything [ISO §4.2.6 "facility for manual adjustment"].
- False-start rejection on text-heavy content.

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
- 240 lpm: deliberately out of scope (not required by ISO 9876 §4.2.4).
- 90 lpm real fixture: none in the library (session 3 batch survey).
- IOC 288 real fixture: none found (no 675 Hz start tone anywhere).
- ±150 Hz LF mode: synthetic testing only; no known on-air source.
