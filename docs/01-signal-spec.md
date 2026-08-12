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

**Measured in the library (session 6), correcting session 3.** The control
signals are alternating black/white *in video*, so they must be looked for
in the demodulated video; session 3's survey looked in the raw audio and
found a start tone on one recording out of twenty. Re-measured properly:
**14 of 20 carry a 300 Hz start tone and 15 of 20 a detectable phasing
interval.** Every measured start read 299.8 Hz and every stop 450.5 Hz —
inside the ±1% of WMO §5.2.6. Durations match the spec: starts 4–10 s
against the specified 5–10 s (JSC4 is the outlier at 30 s), stops ~5 s,
phasing 30.0 s. No 675 Hz IOC-288 start tone exists anywhere in the
library, so that registered gap stands.

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
- Clock rate estimated from the whole signal after onset; per-line phase
  re-locked on the dead-sector sync pulse. [risk-register items 1–2]
- **Line-period accuracy comes from the BASELINE, not from lag
  resolution or from averaging.** A period measured over a short span is
  dominated by the quantization of its own position estimates; the error
  only becomes visible as phase drift accumulated across the recording.
  Both of Nova's estimators are built that way (session 5):
  - *No locks (white-only stations).* Blocks of lines are folded into
    profiles, consecutive profiles cross-correlated for the phase walk,
    and the median pairwise slope taken within runs uncontaminated by a
    picture restart. The 200 Hz autocorrelation it refines has a lag step
    of 1% of the line — 10 000 ppm — and is wrong by 30–180 ppm on real
    recordings (JSC6 +261 measured against +438 true).
  - *With locks.* Pairs of locked lines an eighth of the recording apart,
    median slope, cut at every step in the sync position. Neighbouring
    lines cannot serve: a one-line slope is the period plus ~2 samples of
    position noise, which on a 4000-sample line is ±500 ppm of scatter.
    Measured on JSC2 from one spos array: −75 ppm from neighbours, +178
    from long pairs, against +172 (fold) and +151 (image shear).
  A long baseline is only meaningful inside one regime, so both estimators
  segment first: phasing and image lines anchor the template a step apart
  [WMO §5.2.3.4], stream time-skips do the same, and a new chart restarts
  the paper at its own phase.
- Consistency check available at no cost: two recordings of the same
  station through the same receiver must yield the same clock. GYA
  2300Z/2324Z now read −116.8/−118.5 ppm (before: −28.6/−54.3) and VMW
  2215Z/2230Z −79.0/−79.6 (before: −38.3/−91.7).
- The dead sector is station-dependent within WMO §5.1.3.3: JMH/XSG/JSC
  and both satellite recordings send a black sync pulse; VMW, NMC and GYA
  send plain white. Which one a recording carries is measured, not
  configured: the dead sector is the only part of a line that looks the
  same on every line, so a per-position across-line consistency profile
  finds it, and the black->white *shape* (not the level) says whether the
  optional pulse is there. Library separation, session 4: pulse stations
  0.48–0.94, white-only 0.14–0.34.
- **Control tones are separated from picture content by spectral purity,
  not by rate.** The fraction of a window's AC power lying in the tone's
  own bin reads 1.0 for a pure sinusoid and 8/π² = 0.811 for an ideal
  square wave. Library measurement (session 6): picture content ≤ 0.16,
  real tones 0.68–0.99, threshold 0.35. A transition-rate test — which is
  what the prior art uses — cannot make this distinction, because dense
  weather text can average the right number of transitions per second
  without being a tone. [WMO §5.2.2, §5.2.5; docs/00]
- **The phasing stage is where a white-only station's line phase lives.**
  Phasing is ~30 s of alternating black/white at the line rate whose white
  leading edge marks dead-sector entry [WMO §5.2.3.4] — content-free by
  construction, which is why weatherfax_pi, KiwiSDR and JWX all locate the
  anchor there. Measured (session 6): phasing found on 15 of 20 library
  recordings including the white-only VMW and NMC, 30.0 s long, per-line
  positions agreeing to 4–73 samples of a 4000-sample line. Where a start
  tone is also present, the phasing begins where the start tone ends in 11
  of 14 recordings — two detectors sharing no code agreeing on a boundary
  neither was told about.
- **The phasing anchor and the image anchor mark the same edge, and the
  difference between them is the black porch.** Settled by measurement in
  session 7, on a fold of the video over the phasing region and over the
  image region on one common grid. On JMH the phasing white leading edge
  sits at −73 samples of 4000 from the decoder's pulse anchor and the
  image's dead-sector black run starts at −67: the same feature, six
  samples apart. Across the seven pulse-station recordings the offset is
  −1.65% to −2.86% of a line, and two recordings of one transmitter agree
  to **3 samples of 4000** (JMH −78.4/−75.7, XSG −114.3/−111.5). That is
  the porch: the dead sector is entered black, the sync pulse follows, and
  the phasing wedge marks the entry, not the pulse. Pinned on both phasing
  waveforms [`fixture_anchor_delta_jmh` 5/95, `fixture_anchor_delta_xsg`
  50/50], on pulse stations, where nothing else corroborates either anchor.
- **The porch is a property of the transmitter. The delta Nova reports is a
  property of the recording as well.** Two recordings disagreed with the
  family and with each other — JSC2 −234.5, JSC3 −54.8, against −66.1 to
  −114.3 on the other eight pulse recordings (session 8). Neither anchor is
  at fault. Measured across the phasing→image boundary itself — the last
  phasing lines against the first image lines, three lines apart, 50%
  crossings, no fold and no fitted period standing between them — JSC2's
  porch is −46 samples and JSC3's −3, where himawari and the test chart read
  ~0 by that same method. What the decoder reports instead compares a phase
  measured at the middle of the phasing interval against an image profile
  some 90 lines later, and carrying the first to the second assumes a linear
  timebase. Those two recordings do not have one.
- **Two library recordings carry a non-linear timebase: ~21-sample steps
  every few lines.** Measured line to line on the phasing wedge against the
  4000-sample nominal (session 8): JSC2 takes 5 steps of +21 in the 56
  intervals of its phasing interval and 22 in 179 image lines, JSC3 takes 3.
  Every other recording in the library — including all three white-only
  stations, which is what would have hurt — sits inside 3999–4001 with no
  step at all. The steps are in the recorded audio, not in Nova: they are
  still there at the m4a's native 44.1 kHz (+111 to +124 samples) through a
  separately written demodulator, and 2.6 ms is a fraction of an AAC frame
  (23.2 ms), so the m4a decode cannot have made them. Where they come from —
  capture, link, or the SDR's own audio pipeline — is not established.
  Three consequences, all measured:
  1. `clock_ppm` on those files is the clock PLUS the mean insertion rate.
     JSC2 reads +167 ppm where every other recording of its family reads
     about −85. Session 5 saw both numbers on this exact file (−75 ppm from
     neighbouring lines, +175 from long pairs) and had no way to tell which
     was the artifact; it is the long-pair figure, and it is the right one
     to draw with — the insertions are real displacements of the paper.
  2. A phase measured once and propagated on one fitted period arrives
     wrong: 160 samples over the ~90 lines between JSC2's two anchors.
  3. Pass B's step cut (0.02 of a line = 80 samples) does not fire on a
     21-sample step, and could not usefully — at one step per eight lines
     no segment would survive to fit. Averaging them into the rate is the
     correct answer, and both pictures decode straight and correctly phased.
- **Six, not two — and the decoder now says so.** Session 8's "every other
  recording sits inside 3999–4001 with no step" was true of the recordings
  it examined and false of the library: measured with two statistics in
  session 9, **all six JSC recordings step**, and nothing else does. The
  correction matters because JSC1, JSC5 and JSC6 are the library's 60 lpm
  material, and their clocks (+335, +343, +458 ppm against a −130…0 family)
  were being read as clock error by every reader of that number.
  Two statistics decide it, sharing no code, either sufficient alone:
  1. *Image domain, needs per-line sync.* The tracked sync residual, local-
     median smoothed over ±8 lines. A jump between neighbouring locked
     lines is mostly measurement noise; an inserted sample is PERSISTENT,
     so it survives the median. Rate per 1000 drawn lines of smoothed steps
     over 2 samples: nine clean recordings **0.0–7.0**, six JSC **64.8–339.8**.
  2. *Phasing domain, needs a phasing interval.* Phasing is one edge
     repeated at exactly the line rate [WMO §5.2.3], so its per-line
     positions must lie on a straight line, and what remains after removing
     the best one is non-linearity. Eleven clean recordings **1.0–3.8**
     samples, JSC2/3/4 **20.2–25.5**.
  The raw phasing spread cannot do job 2 — it is dominated by the clock, at
  0.66 samples per line at −90 ppm and so ~40 samples across a 60-line
  interval. That is why session 8's "phasing spread 72/47 against 1–19"
  does not reproduce against the decoder's own detector, which reads
  24–43 on clean recordings: the two were measuring different things.
  Thresholds sit mid-gap and are expressed in SAMPLES OF TIME, not
  fractions of a line, because an insertion is a fixed number of samples in
  a capture chain and knows nothing about the line rate — the same numbers
  separate 60 lpm and 120 lpm without rescaling.
  [`roundtrip [10]`, `fixture_timebase_steps`, `fixture_timebase_linear`]
- **The picture survives a stepping timebase, but not untouched.** Session
  8 recorded that JSC2 and JSC3 "decode straight". Measured against a
  synthetic with known ground truth (21 samples inserted every 11 lines),
  the tracked picture's straight-edge scatter is **3.35 px of 1810** where
  the same signal without insertions reads 0.00: the local median that
  tracks the steps lags them by a few lines, so each insertion costs a few
  lines of misalignment. Both bounds are pinned. On a white-only station,
  where nothing tracks, the same signal is convicted by the phasing
  statistic alone — the case no library recording covers.
- **A pulse station keeps its tracked anchor; the phasing anchor is measured
  there but never preferred.** Argued in session 7 on the grounds that a
  tracked reference beats a fixed one, tested in session 8. JSC2 is the case
  that decides it: its phasing anchor sits 234 samples — 106 px of 1810 —
  from the tracked one, and the tracked one is the one that draws the
  correct picture. A fixed reference propagated on a fitted clock cannot
  survive a timebase that steps; a per-line tracked one absorbs it without
  being told it is there.
- **On a white-only station the image anchor is not the dead sector, and
  the phasing anchor is.** The white-only anchor scores the rising edge of
  always-white, which is dead-sector entry only when nothing else on the
  line is reliably white. Charts have blank margins: VMW 2230Z's always-
  white run is 1350 samples where its dead sector is 180, so the anchor sat
  1149 samples early and the picture was drawn rotated by 520 px of 1810 —
  the paper's right margin wrapped around to the left edge. The phasing
  wedge sits in the LAST 4.5% of that white run. Measured disagreements:
  VMW +1149, NMC −1743, GYA 2324Z +287 samples; all three decode correctly
  once phased from the phasing interval, verified against the picture.
  Nova therefore takes the phasing anchor where the image gives no per-line
  sync, keeps the tracked pulse anchor where it does, and reports the delta
  either way [`fixture_phasing_anchor`].
- **The transmission sequence bounds the picture.** Start tone → phasing →
  image → stop tone [WMO §5.2.3, §5.2.5]; only the image is drawn. The
  boundaries come from the first opening sequence and the first stop tone
  that follows it, because a recording may hold more than one transmission
  (`jmh sample`: start 6 s, stop 404 s, the *next* start at 425 s).
  Segmentation crops the output only — onset, period, anchor and both
  tracking passes still see the whole recording, so nothing measured moves.
  [`roundtrip [9]`, `fixture_phasing_boundary`]
- **A white-only dead sector carries no per-line phase information.**
  Session 3 assumed a "white-sector matcher" would fix VMW; session 4
  built two and measured both to be worse than not tracking at all — they
  match the chart's own white margin, tear the picture into strips or
  slant it, and corrupt the fitted clock (VMW 2215Z: −121 → −285 ppm).
  There is nothing in a white dead sector that the paper does not also
  contain. Such stations are decoded on the measured clock and report
  zero locks; per-line sync for them would have to come from the phasing
  stage [WMO §5.2.3.4]. As of session 7 it does: they still report zero
  per-line locks — that measurement stands — but their line-start PHASE
  now comes from the phasing interval, which is a different thing and the
  only one they ever had.
- ±150 Hz LF mode is a deviation setting, same code path as ±400 Hz.
  [ISO §4.2.2 "and/or"]
- Phasing waveform may be 5/95 asymmetric — detectors must accept both.
  [WMO §5.2.3.2]
