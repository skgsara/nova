# 01 — Signal specification (authoritative)

The HF weather-fax signal as Nova implements it. Normative source:
**WMO-No. 386, Vol. I, Part III, §5** (2009 edition), with definitions
from **ISO 9876:2015 §3**. Where the two differ in level of detail,
WMO governs the signal; ISO governs what the receiver must do with it.

Citations `[WMO §x]` / `[ISO §x]` are the claim IDs of this project —
every load-bearing number below traces to one.

## 1. Scanning geometry

| Parameter | Value | Source |
|---|---|---|
| Scan direction | left→right, top→bottom | WMO §5.1.1 |
| Index of cooperation (IOC) | **576** or **288** | WMO §5.1.2, ISO §4.2.3 |
| IOC definition | M = L·F/π (line length × line density) | WMO §5.1.2, ISO §3.4 |
| Line density | 3.8 lines/mm (IOC 576); 1.9 lines/mm (IOC 288) | WMO §5.1.4 |
| Dead sector | 4.5% ± 0.5% of line length; mostly white; a black pulse ≤ half the dead sector is permitted (this is the sync pulse) | WMO §5.1.3.3, ISO §3.1 |
| Scan rates | **60, 90, 120** lpm (240 lpm exists in WMO §5.1.5 but is OUT OF SCOPE — not required by ISO §4.2.4) | WMO §5.1.5, ISO §4.2.4 |
| Scan-rate tolerance | ±5×10⁻⁶ (transmitter); receiver sync accuracy ±2×10⁻⁶, stability ±2×10⁻⁵ | WMO §5.1.5, ISO §4.2.6 |

Derived: full-line pixels at IOC 576 ≈ 576·π ≈ 1809.6 (JWX/others use
1810); active picture ≈ 95.5% of the line.

## 2. Subcarrier modulation (FM)

| Parameter | Value | Source |
|---|---|---|
| Centre frequency | 1900 Hz | WMO §5.3.1.2 |
| Black | 1500 Hz | WMO §5.3.1.2 |
| White | 2300 Hz | WMO §5.3.1.2 |
| Deviation on HF radio circuits | ±400 Hz about assigned f₀ | WMO §5.5.1(a) |
| Deviation on LF circuits | ±150 Hz | WMO §5.5.1(b), ISO §4.2.2 |
| Tone stability | black/white freqs within 8 Hz over 30 s; 16 Hz over 15 min | WMO §5.3.1.2 |
| Gray scale | 8 tones, linear: 1500, 1614, 1729, 1843, 1957, 2071, 2186, 2300 Hz | WMO §5.4.3 |
| Input level range (receiver) | −10 dBm … +10 dBm equivalent | ISO §4.2.2 |

## 3. Remote-control signals

| Signal | Spec | Source |
|---|---|---|
| Start / IOC select | alternating black/white, 5–10 s, rectangular envelope: **300 Hz = IOC 576**, **675 Hz = IOC 288** (or 576 alternate-line) | WMO §5.2.2, ISO §3.5 |
| Phasing / line-rate select | 30 s alternating black/white: **1.0 Hz = 60 lpm, 1.5 Hz = 90, 2.0 Hz = 120**; waveform symmetric (50/50) OR asymmetric (5% white / 95% black) | WMO §5.2.3.1–.2, ISO §3.8 |
| Phasing reference | leading edge of white, aligned with entry into dead sector | WMO §5.2.3.4 |
| Stop | 5 s of 450 Hz alternating black/white + 10 s continuous black | WMO §5.2.5, ISO §3.13 |
| Control-signal freq tolerance | ±1% | WMO §5.2.6 |
| Auto start may also trigger on | IOC-select OR phasing signal alone | WMO §5.2.1 |

## 4. Transmission sequence

1. (optional) start/IOC-select tone, 5–10 s
2. phasing, ~30 s
3. image lines (each = active sector + dead sector with sync pulse)
4. stop: 450 Hz × 5 s, then 10 s black

## 5. Design consequences (decisions, recorded)

- One internal sample rate; all inputs resampled. Mode table drives
  IOC/rpm — no hardcoded 576/120.
- Clock rate estimated during the 30 s phasing (known line rate);
  phase re-locked per line from the dead-sector sync pulse.
  [risk-register items 1–2]
- ±150 Hz LF mode is a deviation setting, same code path as ±400 Hz.
  [ISO §4.2.2 "and/or"]
- Phasing waveform may be 5/95 asymmetric — detectors must accept both.
  [WMO §5.2.3.2]
