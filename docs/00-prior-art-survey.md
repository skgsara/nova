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

### Session 7 — where the line-start anchor comes from, and how it is carried

Checked: **JWX** (`DecodeFax.java`, source in the parent folder),
**KiwiSDR**/**weatherfax_pi**, **fldigi**, **Isobar**.

**JWX does not use the phasing interval for the anchor at all.** Its
`s_sync` state accumulates 20 s of *image* lines into one averaged line
(clock-corrected per line by `clock_correct_line`), integrates it, and
takes the strongest negative excursion as the sync bar. That is an
image-derived anchor of exactly the kind Nova used before this session —
so on a white-only station, where no such excursion exists, JWX has the
same problem Nova had, and its own documentation shows the operator
nudging alignment by hand. The reusable finding is a negative one: the
mature decoder does not solve this, which is why Nova's answer had to come
from the phasing interval and be checked against a picture rather than
against JWX's behaviour.

Two details of JWX's are worth recording because they bear directly on the
bug this session found:

- JWX carries its anchor forward by the **whole elapsed accumulation
  window** (`sync_lines * row_len * calibration_val`) — an integer number
  of lines, referred to the window's end. Nova referred its anchor to the
  *midpoint* of the phasing run instead, which is a half-line whenever the
  run has an even number of lines, and 30 s of phasing is 60 lines. JWX's
  choice of endpoint over midpoint is immune to that by construction. Nova
  keeps the midpoint (it halves the lever arm of a clock error) but now
  rounds it to a line that exists, and `tones` [11] generates both
  parities so the class of error cannot come back.
- JWX then subtracts a fixed fudge (`sync_interval * 0.12 * sample_rate`)
  to "align sync bar", compensating its integrator's group delay. It is a
  tuned constant with no measurement behind it in the source. Nova instead
  *measures* the equivalent quantity — the black porch between dead-sector
  entry and the sync pulse — and reports it per recording
  (`phasing_anchor_delta`), which is what turned it from a fudge into
  evidence: two recordings of one transmitter agree to 3 samples of 4000.

KiwiSDR/weatherfax_pi do run their wedge fit inside a phasing stage the
tone state machine has already entered, which is the shape Nova follows
for segmentation (opening sequence → image → stop). Nothing copied.

### Session 9 — does any mature decoder notice a non-linear timebase?

Checked, sources read rather than recalled: **JWX** (`DecodeFax.java`,
`CommonCode.java`, parent folder), **weatherfax_pi** (`src/FaxDecoder.cpp`),
**KiwiSDR** (`extensions/FAX/FaxDecoder.cpp`), **fldigi**
(`src/wefax/wefax.cxx`).

**None of them detects or reports one.** The four positions:

| | what it does about a timebase that steps |
|---|---|
| JWX | Nothing. `clock_correct_line(a, line, delta)` applies ONE operator-typed constant (`CalibrationController` is a text field) uniformly to every line — a pure linear model with no estimate and no residual. |
| weatherfax_pi | The only one that acknowledges lost samples at all, and at the wrong layer: `if(err == paInputOverflow) wxLogMessage("Port audio overflow on input, some data lost!")`. That is PortAudio telling it; a recording read from a file never does, and nothing in the decoder measures the consequence. |
| KiwiSDR | Inherits the above. Its `(ninety_pct - ten_pct) > m_SamplesPerLine/6` test on the phasing positions is the same *statistic* Nova uses for the phasing half, but it is a false-phasing filter, at a threshold 24x looser, and it is never read as a timebase diagnostic. |
| fldigi | The richest machinery, used to absorb rather than to report: `correlation_shift()` searches 1..100 samples for the local max of line-to-line correlation and accumulates a HISTOGRAM over the whole reception, then takes its mode after 100+ calls ("Specific to the antenna and the reception"). A stepping timebase is exactly a multi-modal or drifting version of that histogram — fldigi computes the distribution and throws away everything but its peak. |

So the reusable finding is again a negative one, and the specific ideas
taken are: KiwiSDR's phasing-position spread as the statistic to look at
(reinterpreted — the residual about a fitted line, not the raw spread,
which is dominated by clock error), and fldigi's insight that per-line
shift is worth a *distribution* rather than a single number. The
smoothed-residual step rate is new here and is written down as such.
Nothing copied; the ledger below is unchanged.

Worth recording because it contradicts a natural assumption: a live
decoder is MORE exposed to this than an offline one — every one of these
runs on a live stream where an overrun inserts or drops samples silently —
and none of them looks. Nova is offline today and looks anyway, which is
the right way round for M4.

### Session 10 — how does a mature decoder hold a phasing interval together when the signal fades?

Checked, sources read rather than recalled: **fldigi**
(`src/wefax/wefax.cxx`, `decode_phasing`), **KiwiSDR**
(`extensions/FAX/FaxDecoder.cpp`), **weatherfax_pi**
(`src/FaxDecoder.cpp`), **JWX** (`DecodeFax.java`, `s_sync`).

The question is Nova's specific one: GYA 2300Z carries a real 40-line
phasing interval in which only 23 lines clear a per-line contrast test,
and no more than three of those are consecutive. Nova grew runs from
CONSECUTIVE qualifying lines and therefore found ten fragments of one to
six lines and reported no phasing at all.

| | how a faded phasing line is handled |
|---|---|
| fldigi | The closest thing to an answer. `decode_phasing` counts qualifying lines and needs only **4** of them; a line that fails does not end the phase — `m_num_phase_lines` counts consecutive failures and it gives up only at **5**. So the run is explicitly allowed to be interrupted. Its per-line test is a Schmitt trigger (white at 200/255, black at 25/255) plus a duty-cycle check, not a contrast score. |
| KiwiSDR | Never faces the question: its tone state machine hands the phasing stage a fixed 40-line window, the first 2 discarded, and the median over the rest absorbs any dropouts. Its per-line acceptance test does not exist because it does not need one. Also relevant: its START/STOP line counter is deliberately LEAKY (`typecount--` on a miss, with the comment "can deal with noisy input if we had a miss rather than reset here") — the same instinct applied to a different run. |
| weatherfax_pi | As KiwiSDR minus the spread filter: a plain median over 38 lines of a fixed window. No per-line test at all. |
| JWX | Does not judge lines individually in the first place. `s_sync` ACCUMULATES 20 s of lines into one array — clock-corrected per line, then summed — and finds the edge on the sum, with the comment "must accumulate to work with noisy signals and clock errors". A fold, not a vote. |

**Two ideas taken, neither copied.** From fldigi, the shape of the rule: a
phasing run survives lines that fail its test, and ends only after a
measured number of them in a row. Nova's number is 8, measured here
(GYA 2300Z's widest internal gap between qualifying lines is 6; the
generated test pattern's phasing-like picture rows are 49 apart), and the
counter is renewed by score alone rather than by any weaker evidence —
without that, a white-only station's run never ends at all, because its
image dead sector is white at the phasing position. From KiwiSDR's leaky
`typecount`, the confirmation that this is the standard instinct for a
noisy run and not a special case.

What is NEW here and written down as such: **carrying the run on POSITION
agreement** rather than on score. None of the four does this — three never
need a membership rule and fldigi's is a fixed per-line test. It is forced
by the measurement: a faded phasing line's contrast collapses while its
edge does not move, so on GYA 2300Z real phasing scores 0.34-0.88 and
reaches BELOW the 0.48-0.62 band that dark picture content scores. Score
cannot separate them at any threshold; position can.

JWX's fold is the one idea NOT taken, and worth recording as a live option
for M4: it would work on exactly this signal and needs no per-line
decision at all. It is rejected here only because Nova scans a whole
recording blind and needs run BOUNDARIES, which a fold does not give.
Nothing copied; the ledger below is unchanged.

### Session 11 — does any mature decoder REPAIR a per-line timebase, rather than measure a clock?

Checked, sources read rather than recalled: **fldigi**
(`src/wefax/wefax.cxx`, `correlation_shift`, `decode_image`), **KiwiSDR**
(`extensions/FAX/FaxDecoder.cpp`, `phasingSkipData` → `m_skip`,
`ProcessSamples(..., float shift)`), **weatherfax_pi**
(`src/FaxDecoder.cpp`, `phasingSkipData`/`skiplen`), **JWX**
(`DecodeFax.java` `s_sync`/`s_proc`, `CommonCode.clock_correct_line`).

The question is the one Sara's review of the whole library raised: the six
JSC recordings insert samples every few lines, and their pictures show it —
"the black strip, it's actually zig zagging, not solid at all". Nova already
tracked the sync per line and then drew each line at a ±8-line MEDIAN of
that track, which lags every real move.

| | where each drawn line's horizontal position comes from |
|---|---|
| fldigi | Sample count and nothing else: `curr_col = m_img_width * frac(m_img_sample / m_smpl_per_lin)`, evaluated per row in `decode_image`. It DOES compute a per-line correlation shift against the previous line (`correlation_shift`), and then keeps only the MODE of a histogram of them, for space-echo detection — the per-line number exists and is discarded. Separately it notices a globally low sample rate (`estim_smpl_rate < 0.95 * samplerate`) and adjusts a buffer length once. |
| KiwiSDR | One median of the phasing-line positions, applied once as a sample skip (`m_skip`), plus a manual `shift` an operator can pass into `ProcessSamples`. Never re-measured mid-picture. |
| weatherfax_pi | The same one-time phasing median (`skiplen`), no per-line term, and its only acknowledgement of lost samples is a PortAudio `paInputOverflow` log line — which a file can never produce. |
| JWX | Finds the sync ONCE by folding `sync_lines` lines, then rotates every subsequent line by `(int)(delta * line)` — one operator-typed calibration constant times the line number (`clock_correct_line`). Manual whole-image realign lives in the GUI (`ImagePanel`), post-decode and non-destructive. |

**None of the four repairs a timebase.** All four correct a CLOCK — a
constant rate error — and three of them do it from one measurement taken
before the picture starts. The one decoder that already computes the
per-line quantity throws it away. So there is nothing to reuse for this,
and the ledger below is unchanged again.

What is NEW here and written down as such: **segmenting the tracked sync
residual at change points, and fitting a robust line within each segment.**
Two measurements from the library forced both halves. (a) The residual
MOVES: a ±8-line median through a step is wrong on both sides of it, and
disabling the segmentation costs 2.89 px of line-start error on the JSC2
fixture against 0.61 with it. (b) Inside a segment the residual is not
flat but RAMPS, because the period fit absorbs the mean insertion rate —
1.9 samples per line on the ground-truth synthetic, 19 samples of tilt
across an 11-line segment, twice the step it sits between. A median
through a slope is wrong at both ends by half of it; Theil-Sen is not.

fldigi's discarded per-line correlation is the idea to take for the case
this session does NOT solve — a white-only station, which has no sync
pulse to track and therefore no residual to segment (VMW 2215Z's
staircase). Kept per line rather than collapsed to a mode, it is a
line-start measurement for a station that has none. Recorded as the live
option for that work, not taken here.

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
