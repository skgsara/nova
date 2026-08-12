# 01 — Signal specification (authoritative)

The HF weather-fax signal as Nova implements it. Normative source:
**WMO-No. 386, Vol. I, Part III, §5** — verified identical in the 2009
and 2023 editions except where noted — with definitions from
**ISO 9876:2015 §3**. Where the two differ in level of detail,
WMO governs the signal; ISO governs what the receiver must do with it.

Citations `[WMO §x]` / `[ISO §x]` are the claim IDs of this project —
every load-bearing number below traces to one. Section numbers follow
the 2023 edition (§5.5 was restructured vs 2009; see note in §2 below).

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
| Centre frequency | 1900 Hz | WMO §5.3.1.2, §5.5.1 |
| Black | 1500 Hz | WMO §5.3.1.2, §5.5.1 |
| White | 2300 Hz | WMO §5.3.1.2, §5.5.1 |
| Audio shift at receiver | ±400 Hz (HF circuits), ±150 Hz (LF circuits) about the 1900 Hz centre | ISO §4.2.2, WMO §5.5.2 |
| Tone stability | black/white freqs within 8 Hz over 30 s; 16 Hz over 15 min | WMO §5.3.1.2 |
| Gray scale | 8 tones, linear: 1500, 1614, 1729, 1843, 1957, 2071, 2186, 2300 Hz | WMO §5.4.3 |
| Input level range (receiver) | −10 dBm … +10 dBm equivalent | ISO §4.2.2 |

**Edition note (2023 vs 2009).** The 2023 edition restructured §5.5:
§5.5.1 is now explicitly the audio **subcarrier** FM (1500/1900/2300 Hz),
and a new §5.5.2 covers **direct FSK of the RF carrier** (HF: f₀±400 Hz;
LF: f₀±150 Hz). In the 2009 text the ±400/±150 figures sat under the
subcarrier heading and read ambiguously. For a decoder the practical
content is unchanged and is pinned by ISO §4.2.2 anyway: the audio
presented to the demodulator shifts by ±400 Hz (HF) or ±150 Hz (LF)
about 1900 Hz. Nova's deviation mode implements exactly that.

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
- Real recordings do not start with signal (leader/tuning tones, SDR
  stall-fill). Signal onset is detected by an odd-harmonic line-comb
  scan; no comb -> refuse to decode. [session 3 measurement]
- Clock rate estimated from the whole signal after onset (windowed
  autocorrelation); per-line phase re-locked on the dead-sector sync
  pulse. [risk-register items 1–2]
- The dead sector is station-dependent within WMO §5.1.3.3: JMH/XSG send
  a black sync pulse; VMW sends plain white (no pulse) — measured on
  recordings 2026-08-12 (VMW locks: 0/1162 lines with the pulse
  template). A white-sector matcher is required for VMW.
- ±150 Hz LF mode is a deviation setting, same code path as ±400 Hz.
  [ISO §4.2.2 "and/or"]
- Phasing waveform may be 5/95 asymmetric — detectors must accept both.
  [WMO §5.2.3.2]
