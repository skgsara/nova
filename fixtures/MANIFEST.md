# Fixture manifest — what the test suite measures, and what it measures it on

**The recordings themselves are not in this repository, and never will be.**
They are off-air captures of meteorological chart broadcasts. The copyright
status of those transmissions varies by issuing service and in at least one
case (JSC — Kyodo News, a commercial news agency) there is no public-domain
argument available at all. Nova does not redistribute them.

What *is* here is everything about them except the audio: which station,
which recording, which window of it, the SHA-256 of the exact WAV the
bounds were measured against, and what each one covers that no other
fixture does. The pass/fail bounds live in `CMakeLists.txt`, next to each
`add_test`, and are public.

So the claim this project makes about its own decoder — *this recording,
this hash, decodes within these bounds* — is fully stated. It is
reproducible by anyone holding the same file, and checkable against a
recording of your own for everything except the exact numbers.

**Consequence, stated plainly: 30 of Nova's 38 test suites cannot run from
a clean public checkout.** Not 22 — the count people reach for is the
"real-fixture screamers", but the live-path, operator-override and GUI
shell suites feed on recordings too. The build detects their absence,
registers them anyway and reports them Skipped with the reason, so a
checkout without recordings does not look like a smaller green project.

The 8 that run anywhere are the synthetic matrices (the full {IOC 288, 576}
× {60, 90, 120 lpm} sweep, both deviations, the eight-tone gray scale,
clock error to ±250 ppm, heavy noise, both phasing waveforms), the ruler
mapping, the PNG round-trip, the audio ring, and the untrusted-input
guards — that last one generates its own malformed files precisely so that
the security-relevant checks survive in a checkout with no audio at all.

## Format

All 19 are mono, 16-bit PCM, 8000 Hz, trimmed from longer captures held
privately.

| File | Duration | SHA-256 |
|---|---|---|
| `faxsignal-two-openings-70s.wav` | 70s | `8bdf1e042b30ac751a84b504112485ee210590656c55e195cb56c6b3502c1d19` |
| `gya-faded-phasing-120s.wav` | 120s | `b34a616ee8b5761f8bf48bc507b461df2255f34a29ae37b7be4831ad5278586d` |
| `gya-weak-white-120s.wav` | 120s | `a15d3ab302df80c0e4d4d35a3115c022da85a192ee5394e65e173b21d1b87aae` |
| `himawari-jmh-warp-120s.wav` | 120s | `54ae1eb1cfaf39bb86db429ac3441be4f79595b58250fe51dae983a20e319f49` |
| `himawari-kiwisdr-dropout-120s.wav` | 120s | `e9067aa8d6f34b69cf50c901a857e631b5c974d230849d283a5935abbdb8fa4d` |
| `himawari-kiwisdr-phasing-jump-120s.wav` | 120s | `478068951f96d4edec158a09b8b7403b74e6a633c3e762ddcf44a4413511be7d` |
| `hll-2147z-false-locks-40s.wav` | 40s | `69fdff097512cd42045b7a91654a34b7fe22374ae5df1cce974236c98fde3f77` |
| `kyodo-news-jsc1-60lpm-120s.wav` | 120s | `bb90bff2bba9dd5bf52f5606f514930aa4efab6523b953f67e76b1edec04ca86` |
| `kyodo-news-jsc2-steps-120s.wav` | 120s | `ca3ccdf06affb00b3b5c6a0ce600bb95b421d8647f8a10707dbd5db53df7f530` |
| `nmc-image-stop-tone-120s.wav` | 122s | `d02f345424b458dec58cb270c1a527c10ae7d04c43c495ac7daaac2d93a029c4` |
| `stall-fill-15s.wav` | 15s | `556c71079af7066df6d4fcf30fd44318af3bf7c74a440d673eecc939ff4bdd4a` |
| `test-chart-jmh-60s.wav` | 60s | `8fb8d711e27383edc17544c92db249d34c28c451014229025be87ff541062346` |
| `test-chart-jmh-kiwisdr-60s.wav` | 60s | `a75c009235981b59284bcd5dfb126e2f94c9c85154948a5ba78d452fb232b4b2` |
| `test-chart-jmh-kiwisdr-image-60s.wav` | 60s | `eb0b80709358a574b7f7fa9c4fbece0008dad66e83f2b2df97eba8425b3914c0` |
| `vmw-phasing-image-160s.wav` | 160s | `1e2270a7e5ce3b2523a5656c1a9aedc51b943e14f2221ec55979205d6c56e2d8` |
| `vmw-start-phasing-100s.wav` | 100s | `b8686212fdb618b8e53541a7806562f6779a03cafd0dc523bfb4762467d99ac3` |
| `vmw-white-sector-120s.wav` | 120s | `5b5a4a8f68aabffc460c81fb773ad456c7ca5e38dd45939e198e266c7d6c6a19` |
| `xsg-fyci-phasing-head-120s.wav` | 120s | `fa0d9a27311fc8b9796d55849897782d97cc0da06cfb5f07c5b74da63361998d` |
| `xsg-phasing-image-100s.wav` | 100s | `4436f11a327f52ee16b8e47734dee196f7256c13b3d7fa8790436cc02d5bc6c8` |

## Stations

| Call | Service | Country |
|---|---|---|
| JMH | Japan Meteorological Agency, Tokyo | Japan |
| VMW | Bureau of Meteorology, Charleville | Australia |
| XSG | Shanghai | China |
| GYA | Charleville relay, 2324Z schedule | — |
| NMC | US National Weather Service / NOAA | USA |
| HLL | Korea Meteorological Administration | South Korea |
| JSC | **Kyodo News** — a commercial news agency, not a met service | Japan |

## Fixture doctrine

Each fixture covers something no other one can. That rule is why the set is
19 rather than 3, and why none of them can be dropped as redundant.

- **`test-chart-jmh-kiwisdr-image-60s.wav`** — PRIMARY. JMH Tokyo test
  chart via KiwiSDR 13986.6 kHz, 2026-08-12, window 140–200 s. Pure image
  content, no control signal, no echo, no dropouts. The honest-lock
  reference: 117 of 120 lines, max step 0.16 px.
- **`test-chart-jmh-kiwisdr-60s.wav`** — the phasing-boundary case, window
  80–140 s. Documented as "pure image" from session 3 until session 7
  measured it and found 45 of its 120 lines are phasing. Kept, and kept
  honest, because it holds a real phasing→image transition inside one
  fixture, which is what segmentation has to find.
- **`test-chart-jmh-60s.wav`** — the only fixture with a 300 Hz start tone
  *and* full phasing, plus a long-path ionospheric echo of ~144 ms. Low
  locked fraction by design: phasing and start-tone lines do not match the
  picture-line sync template.
- **`himawari-jmh-warp-120s.wav`** — photo content with a stream time-skip
  of ~1270 samples mid-picture (sync phase jumps ~164 ms ≈ 595 px). Until
  session 11 the first half of this picture was drawn 574 px across.
- **`himawari-kiwisdr-dropout-120s.wav`** — the dropout case. Eight lines
  straddling the drop carry no sync lock. Nothing the decoder reports about
  itself sees this (place error 0.60 px either way); only the picture does.
- **`himawari-kiwisdr-phasing-jump-120s.wav`** — phasing interval with a
  discontinuity inside it.
- **`hll-2147z-false-locks-40s.wav`** — KMA 500 hPa analysis, faded, with
  coastline content crowding the white gap so a position ~60 samples late
  out-scores the true one (0.72 vs 0.44). The porch-edge screamer; the
  dead-sector strip is unmeasurable here by construction.
- **`kyodo-news-jsc1-60lpm-120s.wav`** — the library's only 60 lpm signal,
  99% honest locks. Also the false-start trap: 120 s of real newspaper text
  that must produce zero tone events.
- **`kyodo-news-jsc2-steps-120s.wav`** — the non-linear timebase case and
  the only fixture isolating its image-domain half. Phasing is unavailable
  by construction (this window is pure image), so the verdict must come
  from the tracked sync residual alone. ~21 samples inserted every few
  lines; +323 ppm here against +167 for the whole file.
- **`gya-weak-white-120s.wav`** — weak *and* white-only at once. Nothing
  locks, so the measured clock is the only thing holding the picture
  straight and its bound *is* the picture screamer. Coarse autocorrelation
  reads −51.6 ppm here against the fold's −118.4, so a regression to
  coarse-only fails.
- **`gya-faded-phasing-120s.wav`** — a faded phasing interval, the case
  where automatic phasing matters most because the station has no other
  source of line phase.
- **`vmw-white-sector-120s.wav`** — a white-only dead sector, no sync pulse
  anywhere. Pins two things: the style is detected, and the decoder does
  **not** manufacture locks it cannot have. Zero is the correct answer and
  this screamer exists to keep it zero.
- **`vmw-phasing-image-160s.wav`** — start tone, a full 30 s phasing
  interval, 245 lines of chart. The screamer for the phasing line-start
  anchor, asserted against the picture rather than a number: without the
  phasing anchor this decoded rotated by 520 px of 1810.
- **`vmw-start-phasing-100s.wav`** — the tone-detection fixture: a real
  300 Hz start in an off-air recording, 120 lpm recovered from real phasing.
- **`xsg-phasing-image-100s.wav`** — the only symmetric 50/50 phasing
  waveform in the library, on a pulse station. One of the two two-anchor
  agreement screamers.
- **`xsg-fyci-phasing-head-120s.wav`** — a phasing interval followed by the
  *first* lines of the picture, where the assembly's smoothing window can
  still see the control signal. Invisible to every number the decoder
  produces; shows only as eight lines sitting 88.6 px from the rest.
- **`nmc-image-stop-tone-120s.wav`** — the library's only real 450 Hz stop
  tone, fading 0.88 s mid-tone. The signal that *ends* a transmission.
- **`faxsignal-two-openings-70s.wav`** — two transmission openings in one
  cut: a 300 Hz burst, then a second phasing interval.
- **`stall-fill-15s.wav`** — no line structure at all (stream stall-fill).
  The decoder must **refuse** it; before the gate existed this decoded as a
  confident +96735 ppm garbage image.

## If you have your own recordings

The bounds in `CMakeLists.txt` are specific to these files and will not
transfer. What does transfer is the shape of the check: `nova-test-fixture`
takes `<path> <lpm> <min_lines> <max_lines> <clock_lo_ppm> <clock_hi_ppm>
<min_locked_frac>` plus picture-domain predicates (`--expect-straight-strip`,
`--expect-white-only`, `--expect-timebase`, and the rest — see
`tests/test_fixture.cpp`). Point it at your own capture and set your own
bounds.
