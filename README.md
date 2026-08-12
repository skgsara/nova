# Nova

A cross-platform HF weather-fax (WEFAX, emission F3C) decoder, built
from the public international standards:

- **WMO-No. 386, Vol. I, Part III §5** — the signal specification
  (scan rates, IOC, start/stop/phasing signals, subcarrier frequencies).
- **ISO 9876:2015 §4.2** — the receiver-side behaviour Nova is designed
  to satisfy (see `docs/02-compliance-matrix.md`; no claim of certified
  compliance is made).

Nova decodes the weather charts broadcast over shortwave by stations
such as JMH (Japan), GYA (UK), NMC (USA), VMW (Australia) and others —
using only a computer's sound input and an HF receiver.

## Features (target)

- IOC 576 and 288, automatic and manual selection
- 60 / 90 / 120 lines per minute, automatic and manual selection
- ±400 Hz (HF) and ±150 Hz (LF) subcarrier deviation
- Automatic start (300/675 Hz), stop (450 Hz), and phasing
- Automatic clock-error (slant) correction: rate estimated from the
  signal, phase re-locked per line from the dead-sector sync pulse where
  the station sends one — no TCXO or GPS-disciplined clock required
- Post-decode image realignment and line-start adjustment on the
  retained raw stream (non-destructive)
- Input: WAV natively; m4a/AAC via an installed `ffmpeg`
- Live reception via sound input

## Status

**M0 and M1 done (2026-08-12):** the headless core decodes real off-air
recordings straight and readable, with automatic line-rate detection
(60/120 lpm proven on air), signal-onset gating, automatic clock-error
correction, and per-line sync that re-acquires after dropouts. Verified
across a 20-recording library from JMH, JSC, XSG, NMC, VMW, GYA and two
satellite sources: 88–99% of lines carry a real sync-template match on
every station that transmits a sync pulse.

Stations that send a plain white dead sector — permitted by
WMO §5.1.3.3, and five of the twenty recordings do — decode on the
measured clock and report **zero** locks rather than a number that
flatters the decoder. See `docs/01-signal-spec.md` §5.

**M2 done (2026-08-12):** the line period is measured from phase drift
accumulated across the whole recording, which is the only thing that
makes it accurate — over a short span the answer is dominated by the
quantization of its own measurements. Two estimators share that
principle: folded-block phase tracking, which needs no sync locks and so
serves the white-only stations, and a long-baseline fit over locked lines
for everyone else. Residual shear of the decoded image is ≤10 ppm across
the library; a signal generated at −137 ppm is recovered as −137.00.

This replaced a fit over neighbouring lines that was wrong by up to 250
ppm and had been shearing three of the library's recordings by a third of
a page while every lock metric stayed green — found by measuring decoded
pictures rather than decoder statistics.

**M3 done bar manual override (2026-08-12):** the control signals — 300/675 Hz start,
450 Hz stop [WMO §5.2.2, §5.2.5] and the 30 s phasing interval
[WMO §5.2.3] — are detected and measured. Detection accepts on spectral
*purity* rather than a transition rate, which is what lets it tell a
control tone from dense weather text: across 5.9 hours of library audio,
picture content never exceeds 0.16 in a control band while real tones run
0.68–0.99. Zero false positives; 14 of the 20 recordings turn out to carry
a start tone, and 15 a full phasing interval.

Sequencing is wired. The decoder draws the picture and not the control
signals, and on stations sending a plain white dead sector it takes its
line start from the phasing interval [WMO §5.2.3.4] — the only per-line
phase such a station has. That was worth more than it sounds: their
image-derived anchor had been locking onto the chart's blank margin
rather than the dead sector, and VMW's charts were coming out **rotated
by 520 px of 1810**, with the right-hand margin wrapped around to the left
edge. Stations that do send a sync pulse keep tracking it, and decode
byte-for-byte as before; the disagreement between the two anchors is
reported on every decode, and on pulse stations it measures the black
porch — two recordings of the same transmitter agree on it to 3 samples
of 4000.

Manual override of everything [ISO §4.2.6] is still untouched and needs
the GUI (M4).

Synthetic tests cover 60/90/120 lpm, IOC 288/576, ±150/±400 Hz
deviation, clock error to ±250 ppm (including a white-only signal with
nothing to lock onto), heavy noise, and both phasing waveforms.
See `ROADMAP.md` for the milestone map.

## Platforms

Tier 1 (release-tested): Windows 64-bit, macOS (universal), Linux x86_64.
Tier 2 (CI-built, community-tested): 32-bit Windows/Linux, ARM, FreeBSD.

## Build

Plain C++17 + CMake. Dependencies: FLTK (GUI), RtAudio (live audio);
the DSP core is dependency-free.

```
cmake -B build -S .
cmake --build build
ctest --test-dir build
```

## License

GPLv3+ — see `LICENSE`. Third-party lineage and standards citations in
`NOTICE`. Nova's DSP lineage traces to ACfax → HamFax → yahfax →
weatherfax_pi → KiwiSDR FAX, used under their GPL licences with
attribution.
