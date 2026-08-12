# 00 — Prior-art survey and reuse ledger (P0.5 record)

Date: 2026-08-12. Surveyor: Kimi Code CLI + Sara.

## Conclusion

The WEFAX/F3C signal is **fully specified in public standards**
(WMO-No. 386 Vol. I Part III §5; ISO 9876:2015 for receiver behaviour).
No reverse engineering is required anywhere in this project. All
surviving open-source decoders belong to one GPL lineage, so algorithm
reuse is lawful under Nova's GPLv3+ (GPLv2+ code incorporated under
its "or any later version" clause).

## The lineage (verified)

```
ACFax 0.981011  (A. Czechanowski DL4SDC, 1995–98, GPLv2+)
  └─ HamFax     (C. Schmitt DH1CS, 2001–11, GPLv2+)
       └─ yahfax (sourceforge)
            └─ weatherfax_pi (S. D'Epagnier, OpenCPN plugin, GPLv3)
                 └─ KiwiSDR extensions/FAX (jks-prv; file-level GPLv3)
JWX 3.0   (P. Lutus, 2011–13, GPLv2+) — independent lineage, studied only
fldigi    (W1HKJ et al., GPLv3+) — studied; same hamfax-era heritage
Isobar    (S. Sakuragawa, 2026, GPLv3+) — author's own prior work
```

Evidence: KiwiSDR's `FaxDecoder.cpp` header names weatherfax_pi and
"adapted from yahfax... an improved adaptation of hamfax"; its FIR
coefficient tables are ACFax's `firwide/firmiddle/firnarrow` integers
verbatim (checked against `mod_demod.c`).

## Per-tool notes

| Tool | Take | Leave |
|---|---|---|
| ACFax | fs/4 quadrature downconvert; 9-tap FIR tables (3 widths); amplitude-normalized delay discriminator; arcsin linearization idea; **retain raw demod stream → non-destructive post-adjustment** | 8-bit/8 kHz fixed point; `SHORT int` type punning; OSS audio |
| HamFax | feature shape: IOC scaling, TX exists here only | Qt4, autotools, 8 kHz AU |
| weatherfax_pi / KiwiSDR | fractional sample-rate tracking + fractional accumulator resampling; phasing wedge-fit over ~7% of line, median over ~40 lines, 10–90% spread rejection; false-start filtering | KiwiSDR runtime plumbing |
| JWX | Goertzel start/stop detection (250 ms window) as reference; failure-mode catalogue: one-shot sync average, no per-line resync, hardcoded IOC 576/120, AFC abandoned | Java Sound workarounds |
| Isobar | per-line sync lock approach; session/fixture doctrine | KG-FAX interop (out of scope) |
| fldigi | mature C++ modem idioms | suite architecture |

## Prior-art checks per change (added session 4, Sara's reuse-first rule)

**Line-start anchor / dead-sector location (session 4).** Checked JWX
`DecodeFax.s_sync` (source in the parent folder) and the weatherfax_pi /
KiwiSDR notes above. JWX accumulates a fold of ~20 s of lines with a
per-line clock correction (`CommonCode.clock_correct_line`), integrates
it, and takes the strongest negative excursion as the line start;
weatherfax_pi/KiwiSDR fit the phasing wedge over ~7% of the line and take
a median over ~40 lines with spread rejection. **Both locate the anchor
during the phasing stage, where there is no picture content to fool
them.** Nova has no phasing detection yet (M3), so it has to find the
anchor inside image lines — where a fold-average is exactly what fails
(it picks the strongest mean edge, which is content). Hence the
across-line *consistency* profile, which is new here and is written down
as such rather than presented as reuse. Two things to take from them when
M3 lands: (a) do the anchor during phasing, as they do; (b) JWX's
clock-corrected accumulation removes the smear that currently caps Nova's
profile at 120 lines.

**Line-period estimation for weak / white-only signals (session 5).**
Checked JWX first, since session 4's note pointed at its clock-corrected
accumulation as the prior art for this exact problem. **JWX does not
estimate the clock at all: its calibration value is typed in by the
operator** (`CalibrationController` is a text field; the alternative is
right-clicking the two ends of a vertical feature in `ChartPanel`), and
`clock_correct_line` merely applies it. So there is nothing there to
reuse for an automatic estimate — the reusable idea is the accumulation
itself, which lifts a stable line shape out of a fading signal, and that
is what Nova's block fold does. weatherfax_pi/KiwiSDR contribute the
median-with-spread-rejection treatment they apply to the phasing wedge
over ~40 lines; Nova applies the same pattern to block pairs, because a
picture restart in a long recording is the same kind of outlier as a bad
wedge line. Both are ideas, not code: nothing is copied, so the reuse
ledger below is unchanged.

Worth recording for M3, since it contradicts a natural assumption: JWX's
manual calibration is not a shortcoming of an old program, it is what a
mature decoder does when the signal carries no sync it can trust. Nova's
claim to do it automatically is only as good as the screamer behind it
(`fixture_weak_white`, roundtrip group [7]).

**Control-tone detection and phasing (session 6, M3).** All three mature
decoders were read before writing anything, and they disagree:

| | what it measures | window | accept rule |
|---|---|---|---|
| JWX | Goertzel power at 300 Hz on the **video** signal (`gstart`/`gend`, `DecodeFax.setup`) | `sample_rate/4` = 250 ms | power ≥ 0.5; edge-triggered — WAITSTB waits for active, WAITSTE for inactive |
| weatherfax_pi / KiwiSDR | per-line type vote (`typecount`) | 1 line | `m_StartStopLength*lpm/60 − leewaylines`, `threshold = 5` ("pretty arbitrary but works in practice") |
| fldigi | black↔white transition count with hysteresis (`x>215` / `x<40`) | `sample_rate/2` = 500 ms | derived freq within ±8 Hz of nominal, on **two consecutive** windows |

Two things Nova takes. (a) All three run the detector on **demodulated
video**, not audio — correct, because the control signals are alternating
black/white at a rate, not audio tones [WMO §5.2.2]. Session 3's library
tone survey ran an FFT on the raw audio and concluded only `jmh sample.wav`
carried a start tone; re-measured in the video domain, 14 of 20 recordings
do. The old method saw the tone only through incidental envelope ripple.
(b) fldigi's "two consecutive windows must agree on the frequency" is the
right shape of coherence test, and Nova generalizes it to a 10–90% spread
over the whole run.

One thing Nova adds, because none of the three has it. Every one of these
accepts on **rate** — Goertzel power, a type vote, or a transition count.
A transition counter cannot distinguish a clean 300 Hz square wave from
dense weather text that merely averages 300 transitions per second, and
text-heavy content is the named false-start trap for this milestone.
Nova's accept test is spectral **purity**: the fraction of a window's AC
power sitting in the tone's own bin, normalized so a pure sinusoid reads
1.0 and an ideal square wave reads 8/π² = 0.811. Measured over the whole
library (5.9 hours): picture content never exceeds **0.16** in any control
band, real tones run **0.68–0.99**, and the threshold sits at 0.35 in a gap
neither population comes near. fldigi's need for a separate "ignore the
stop tone during start" guard, and its correlation-based bail-out, are both
symptoms of the discrimination its transition counter cannot do.

Where Nova deviates from KiwiSDR's phasing constants, and why. The wedge
fit over a fraction of the line, the median over ~40 lines, and the 10–90%
spread rejection are all taken as ideas. But KiwiSDR rejects only when the
spread exceeds `m_SamplesPerLine/6` (667 samples on a 4000-sample line),
and at that limit Nova reported 439 s and 481 s of "phasing" on satellite
imagery whose dark lines fit a 5%-white template by accident. The
difference is architectural: KiwiSDR only ever runs this **inside** a
phasing stage its tone state machine has already entered, so a loose
constant costs it nothing, while Nova scans a whole recording blind. Nova
uses 1/24 (167 samples) plus a duration cap from the spec itself — phasing
is ~30 s [WMO §5.2.3], so a 480 s run is falsified by its length alone,
whatever it scores. Measured separation across the library: true phasing
spread 14–73 samples and score 0.88–0.97, false runs 288–635 and 0.48–0.62.

Nothing was copied in either case; the ledger below is unchanged.

## Reuse ledger

Running record — one row per reused artifact, added the day it enters
the tree. Empty at scaffold time.

| Artifact | From | Licence | Where in Nova | Date |
|---|---|---|---|---|
| _(none yet)_ | | | | |

## Access notes

- hamfax.sourceforge.net and sourceforge.net/projects/hamfax return
  HTTP 403 to this host (fetcher and curl); HamFax was surveyed via
  its GitHub mirror (sergioisidoro/ham-fax) README and the FreeBSD
  ports tree metadata.
- ACFax source: ftp.funet.fi/pub/ham/unix/Linux/misc/acfax-981011.tar.gz
  (still the FreeBSD MASTER_SITE).
- KiwiSDR repo has no root LICENSE; FaxDecoder.cpp carries a per-file
  GPLv3 header (D'Epagnier). Reuse per-file-licensed accordingly.
