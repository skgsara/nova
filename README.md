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

Synthetic tests cover 60/90/120 lpm, IOC 288/576, ±150/±400 Hz
deviation, +100 ppm clock error, and heavy noise.
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
