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

## M1 — mode generality  [mostly done 2026-08-12, session 3]
- Rate auto-detect: odd-harmonic comb scan, 60/90/120 from one code
  path (no hardcoded 576/120). Real-signal proof: JSC1/4/5/6 at 60 lpm
  (93-99% locks), everything else at 120.
- Signal onset gate: recordings opening with leader tones / stall-fill
  are detected and anchored (or refused) — session 3.
- Full-line image mapping (measured JMH line layout; no cropping).
- Fixtures: `kyodo-news-jsc1-60lpm-120s` (only 60 lpm signal),
  `himawari-jmh-warp-120s` (stream time-skip), `stall-fill-15s`
  (rejection screamer), KiwiSDR test chart (new primary).
- REMAINING: VMW white-dead-sector sync template (0 locks now,
  fixture: VMW 2215Z); wide re-acquisition after stream time-skips;
  weak-signal period estimation (GYA 2300Z).
- ±400 Hz proven on real signals; ±150 Hz synthetic only (gap).

## M2 — automatic slant correction  [pending]
- Clock-rate estimate from the 30 s phasing stage; fractional
  resampling (KiwiSDR approach); per-line dead-sector re-lock
  [WMO §5.1.3.3]; Isobar's sync_step_lock as reference.
- Screamer: longest fixture decodes straight WITHOUT manual
  calibration; measured max strip-jump count between known-bad and
  known-good bounds.

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
