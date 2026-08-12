# ROADMAP.md — Nova

Milestone map. Done vs pending. Screamer tests owed by the risk
register are milestone entries, not intentions (SOP P1.5).

## M0 — headless core decodes one real recording  [pending]
- Dependency-free DSP core: resample-to-internal-rate, FM demod
  (quadrature + normalized discriminator, ACFax lineage), line
  assembly.
- First fixture: excerpt of `test chart.m4a` (known content) → image
  a human can judge.
- Harness: internal signal generator producing on-spec WMO §5 signals
  (start/phasing/image/stop) with injectable clock error and noise.
  This is a test tool, not a product feature.
- Screamers: sync-lock rate on fixture; generator→decode round-trip.

## M1 — mode generality  [pending]
- IOC 576/288, 60/90/120 lpm from one mode table (no hardcoded
  576/120).
- ±400 Hz and ±150 Hz deviation [ISO §4.2.2].
- Fixtures: identify recordings covering 60/90 lpm and IOC 288 in the
  library (phasing-tone analysis of each file); registered gap if none.

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
- 90 lpm / IOC 288 real fixtures: unconfirmed in library.
- ±150 Hz LF mode: synthetic testing only; no known on-air source.
