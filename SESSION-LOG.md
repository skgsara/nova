# SESSION-LOG.md — Nova

Newest entry first. Append-only: correct by adding an entry, never by
rewriting an old one. Every entry ends with the exact next step.
This file is tracked in git (Sara, session 1: "we don't need to hide
anything as our develop history").

---

## 2026-08-12 — Session 9: the symptom session 8 recommended was the wrong one, and the library has six bad recordings, not two

Agent: Claude Opus 5.

**Task as accepted:** session 8's next step, first item — detect and report
the timebase steps — with prior art checked first, as the reuse rule
requires. Sara confirmed the step and added a fact about the second item
(below). The GUI stays untouched; core first, as instructed in session 8.

**Prior art, checked first, sources read rather than recalled.** JWX
(local), fldigi `wefax.cxx`, KiwiSDR and weatherfax_pi `FaxDecoder.cpp`.
**None of the four detects or reports a non-linear timebase**, and the way
each fails to is informative (docs/00, session 9 section). JWX applies one
operator-typed constant to every line. weatherfax_pi is the only one that
acknowledges lost samples at all and does it at the wrong layer — a
PortAudio `paInputOverflow` log line, which a recording read from a file
can never produce. KiwiSDR carries weatherfax_pi's phasing-spread test but
uses it as a false-phasing filter at a 24x looser threshold. fldigi comes
closest and then throws the evidence away: `correlation_shift()` builds a
histogram of per-line shifts over the whole reception and keeps only its
mode. Two ideas taken (KiwiSDR's phasing-position spread as the statistic
to look at; fldigi's per-line shift deserving a distribution), both
reinterpreted, nothing copied, ledger unchanged. Worth recording: a live
decoder is *more* exposed to this than an offline one, and none of them
looks.

**Session 8's recommended symptom does not work, and I did not find that
out by arguing.** It proposed the phasing spread on the strength of "JSC2
72, JSC3 47 against 1–19 everywhere else". Measured across the library
through the decoder's own detector, clean recordings read **24–43** — the
margin is not there. The reason is arithmetic: per-line phasing positions
are measured in windows of the TRUNCATED period, so a −90 ppm clock walks
the edge 0.66 samples per line and ~40 samples across a 60-line interval,
and that walk is most of what the raw spread reports. FAXSignal, whose
clock is exactly nominal, reads 1.0 where every −86 ppm recording reads
25–43. The fix is to remove the best straight line first and measure what
remains: **clean 1.0–3.8 samples, JSC2/3/4 20.2–25.5.** Had I calibrated
the screamer on session 8's number I would have shipped a test that
convicts the whole library — which is the trap session 8 itself described
one session earlier.

**Two statistics, sharing no code, either sufficient alone.** (a) *Image
domain*, needs per-line sync: the tracked sync residual, local-median
smoothed over ±8 lines — a jump between neighbouring locked lines is mostly
measurement noise, an inserted sample is PERSISTENT and survives a median.
Rate per 1000 drawn lines of steps over 2 samples: nine clean recordings
**0.0–7.0**, six JSC **64.8–339.8**. (b) *Phasing domain*, needs a phasing
interval: the residual above. Thresholds sit mid-gap and are in samples of
TIME, not fractions of a line, because an insertion is a fixed number of
samples in someone's capture chain and knows nothing about the line rate —
which is also why the same two numbers separate 60 lpm and 120 lpm without
being rescaled. `DecodeResult::timebase` is kLinear / kSteps / kUnknown,
and kUnknown is a real answer: GYA 2300Z and VMW 2215Z are white-only with
no phasing found, so neither statistic exists and the decoder says so
rather than reporting a clean bill.

**The library has six stepping recordings, not two.** All six JSC files,
including the three at 60 lpm — JSC1, JSC5 and JSC6, whose clocks read
+335, +343 and +458 ppm against a −130…0 family and had never been
questioned. The two statistics agree wherever both exist (JSC2/3/4), which
is the corroboration this project asks for. Session 8's "every other
recording sits inside 3999–4001 with no step" was true of what it examined
and false of the library; docs/01 §5 is corrected, and the risk register
now carries the general lesson: **a statistic that separates two files you
already suspect is not the same thing as one that separates them from
everything else.**

**Tests:** 17 suites green, zero warnings (was 15). `roundtrip [10]` is the
ground-truth screamer no recording can be — a generated signal, linear,
then samples inserted into it at a known rate (21 every 11 lines, JSC2's
measured signature). It pins detection, the +250 ppm false positive (a
clock error IS linear, and the raw-spread version of this test fails
here), and the white-only case that exists nowhere in the library: a
station with no sync pulse whose capture chain steps, convicted by the
phasing statistic with zero locks in the recording. `fixture_timebase_steps`
is the new fixture `kyodo-news-jsc2-steps-120s.wav` (JSC2 200–320 s), pure
image by construction so the phasing statistic cannot exist and the image
statistic has to carry it alone; `fixture_timebase_linear` is the
adversarial negative — himawari-jmh-warp-120s carries the library's largest
single phase jump (~595 px) and must NOT be called a stepping timebase,
because one skip is not a rate.

**Revert checks run, all three scream.** Raw spread instead of the residual
→ roundtrip [10] fails twice, including the +250 ppm false positive.
Image statistic disabled → `fixture_timebase_steps` fails and nothing else
does. Phasing statistic disabled → the white-only case fails and nothing
else does. Each half of the test is pinned by exactly one screamer.

**Contradictions found.** Three. (a) Session 8's phasing-spread margin, above.
(b) Session 8 recorded that JSC2 and JSC3 "decode straight"; measured
against a synthetic with known ground truth, a tracked picture under
insertions has **3.35 px of straight-edge scatter in 1810** where the same
signal without them reads 0.00 — the local median lags each step by a few
lines. The picture survives; it is not untouched, and both bounds are now
pinned. I had already written "the picture is unaffected" into the CLI text
before measuring it, and corrected it. (c) My own first framing of the
threshold as a fraction of the line was wrong for the same reason the
insertions are: at 60 lpm it put JSC4 within 1% of the boundary, where
absolute samples put it mid-gap.

**One thing the pictures caught that the tests did not.** Decoding the
primary fixture to look at it — the standing rule, not a formality — showed
`timebase not measurable (no per-line sync and no phasing interval)` on a
pulse station with 117 locks of 120. The verdict was right (120 lines is
under the 128-line floor a rate can honestly be measured over); the reason
given was a lie. The message now names which witness is missing, and
distinguishes "too short" from "nothing to measure with". No test would
have caught that, because both readings are kUnknown.

**Registered gaps added / narrowed:** the reported step rate is a FLOOR,
not a count — dense steps merge under the ±8-line median (synthetic: 90.9
inserted reads 36.9), so it convicts a recording but does not measure an
insertion rate; two recordings are measurable by neither statistic and
report kUnknown; nothing repairs a stepping timebase, and repair is not a
milestone. The session-7 gap about the phasing anchor being propagated on a
fitted clock is narrowed rather than closed: the white-only-plus-stepping
combination is now detected (`roundtrip [10]`), but the picture is still
drawn wrong — what changed is that the decoder no longer reports a
confident clock instead.

**Sara's fact about the ±150 Hz LF deviation, recorded as a decision.** She
knows of no operating station still carrying it. The old wording ("no
real-world source known") was an absence of evidence; this is stronger, and
it means the item is not a gap to close by hunting for a fixture. It is
implemented, synthetic-only [`roundtrip [6]`], and that is the honest end
state. Risk-register item 4 and both gap lists say so now, so no future
session spends itself looking.

**Next step:** the timebase work is done and reported; M3 is still closed
except the manual override, which needs the GUI. Two candidates, in this
order. First, **make the kUnknown verdict rarer** — GYA 2300Z and VMW 2215Z
are unmeasurable because no phasing interval is found in them, and GYA
2300Z has a registered 18-line phasing candidate at 15.5–24.5 s that scores
0.77 and is rejected by the final thresholds; deciding that one case either
way would close a gap and shrink the blind spot, and it needs no new
algorithm. Second, and larger, **M4**: the GUI plus live audio, where the
manual override lives and where the incremental-`detect_tones` cost noted
in session 7 comes due. Note for whoever takes M4: the timebase test as
built is offline — it needs the drawn segment and the whole phasing
interval — so a live decoder needs an incremental form of it, and the
prior art has nothing to offer. Tree is green, buildable, and committed.

---

## 2026-08-12 — Session 8: two recordings disagreed with the whole library, and the fault was in the recordings

Agent: Claude Opus 5.

**Task as accepted:** session 7's next step, in its order, with one change
I proposed and Sara confirmed — look at the unexplained JSC2/JSC3
disagreement FIRST, then write the screamer with a bound that reflects what
it shows. Calibrating the test on a number that might itself be a bug is
exactly the trap session 7 fell into. Sara also asked mid-session to finish
the core before touching the GUI, which is where this already was.

**No new algorithm was written this session, so the reuse rule had nothing
to bind on** — the deliverables are two screamers, one fixture and a
measurement. If the timebase-step detector in the next step gets built,
prior art is owed first.

**The question.** `phasing_anchor_delta` compares two anchors that share no
code: a wedge fit over 30 s of phasing, and an across-line dark-consistency
profile over 120 lines of picture. Eight pulse recordings put it at −66.1
to −114.3 samples of 4000, repeats of one transmitter within ~7 (JMH
−71.1/−74.1/−75.5/−75.7/−78.4 across two receivers and four cuts; XSG
−107.2/−111.5/−114.3). JSC2 read −234.5 and JSC3 −54.8, from one
transmitter. Session 7 filed that as unexplained.

**It is neither anchor.** I ruled out the propagation arithmetic first: both
anchors sit on the same `period0` grid and the refined period is good enough
that the ~90 lines between them cost 1–3 samples, not 180. Then the
pictures: both decode straight, correctly phased, and their column profiles
put the dead-sector black in the SAME place (columns 1780→38 of 1810 on
both), so the two image anchors agree with each other exactly. Then the raw
signal, folded over the phasing region and the image region on one grid the
way session 7 did — and the disagreement was there in the fold, so it was
real and in the signal.

**What it is: JSC2 and JSC3 have a non-linear timebase.** Tracking the
phasing wedge line by line, the spacing is not one period. It is ~4000 with
**5 steps of +21 samples in the 56 intervals of JSC2's phasing interval**,
3 in JSC3's, and 22 more in 179 image lines of JSC2. Every other recording
in the library sits inside 3999–4001 with no step — including all three
white-only stations, which is where it would have hurt. The steps are in
the recorded audio and not in Nova: they are still there at the m4a's
native 44.1 kHz (+111 to +124 samples) measured through a demodulator I
wrote separately in numpy, and 2.6 ms is a fraction of an AAC frame
(23.2 ms), so the m4a decode cannot have produced them. Where they come
from — capture, link, SDR audio pipeline — is not established, and I have
not claimed it.

**The porch is normal on both, measured properly.** With the lever arm
removed — the last phasing lines against the first image lines, three lines
apart, 50% crossings, no fold and no fitted period in between — JSC2 reads
−46 samples and JSC3 −3, where himawari and the test chart read ~0 by the
same method. The delta the decoder reports is the porch PLUS whatever the
timebase did over the ~90 lines between the two measurement epochs: 160
samples on JSC2, ~40 on JSC3, both matching the local step rate against the
fitted one to within a few samples.

**This retro-explains a session 5 measurement nobody could resolve.** That
session recorded, on this exact file, "JSC2 reads −75 ppm at k≤8 and settles
at +175 from k=128" and chose the long baseline. Both numbers are real:
−75 ppm is the clock (the same family as every other recording), +175 is
the clock plus the mean insertion rate. The long-baseline choice is the
right one to draw with — the insertions are real displacements of the paper
— but `clock_ppm` on those two files does not mean what it means elsewhere.

**The second question, answered on evidence.** Should a pulse station ever
prefer the phasing anchor? No, and JSC2 is the case that decides it: its
phasing anchor is 234 samples — 106 px of 1810 — from the tracked one, and
the tracked one draws the correct picture. A fixed reference propagated on a
fitted clock cannot survive a timebase that steps; one re-measured every
line absorbs it without being told it is there. Session 7's argument was
sound and is now tested rather than asserted (`!anchor_from_phasing` is
checked in both new screamers).

**Tests:** 15 suites green, zero warnings (was 13). `fixture_anchor_delta_jmh`
and `fixture_anchor_delta_xsg` pin the two-anchor agreement on pulse
stations — the one place nothing else corroborates either anchor, since the
picture check in `fixture_phasing_anchor` runs only where the phasing anchor
is USED, which is never there. One test per phasing waveform, because the
edge convention is what is pinned and 5/95 and 50/50 reach it through
different code. New fixture `xsg-phasing-image-100s.wav` (XSG ASPN
60–160 s), the library's only symmetric 50/50 station, whose anchor no
fixture covered at all; it reproduces its parent's delta (−107.2 vs
−111.5). Both fixtures were chosen with an EVEN number of phasing lines on
purpose: reverting session 7's integer-line anchor fix moves them to
+1928.7 and +1892.6 and both fail. My first choice for the JMH slot,
`test-chart-jmh-kiwisdr-60s.wav`, was wrong for that reason — its run is 45
lines, and an odd run is the one case that bug cannot reach; it survived the
revert unchanged at −74.1.

**Contradictions found.** Two. (a) My own first choice of fixture above,
caught only by running the revert check the standing rules demand rather
than assuming a screamer screams. (b) The registered gap "the phasing anchor
is propagated on the fitted clock… no library recording exercises this" is
false as written: JSC2 exercises it, 22 times over. It is a pulse station so
tracking hides it, but the gap is reachable in the library, not
hypothetical. Both gap entries corrected.

**Registered gaps added:** timebase steps are neither detected nor
reported, so `clock_ppm` silently means something different on those two
files and the anchor delta is unusable there without a human noticing why.
Two cheap symptoms were measured and neither is wired up: the phasing
spread (JSC2 72, JSC3 47 samples against 1–19 on every clean recording) and
the line-to-line step histogram.

**Next step:** M3 is closed except the manual override, which needs the GUI,
and Sara's instruction is core-first — so the GUI is not the next step. Two
candidates, in this order. First, **detect and report the timebase steps**:
the phasing spread already separates the two stepping recordings from the
other eighteen with a factor-of-three margin (72/47 against 1–19) and costs
nothing, since it is already computed and thrown away; a `timebase_steps`
flag on `DecodeResult` would stop `clock_ppm` from quietly meaning two
different things, and it is exactly the condition M4's live decode is most
likely to meet. Prior art first — fldigi, KiwiSDR and weatherfax_pi all
consume live streams and must have met this. Second, the ±150 Hz LF
deviation [ISO §4.2.2], which is the oldest untouched item in the risk
register and still synthetic-only. Tree is green, buildable, and committed.

---

## 2026-08-12 — Session 7: the anchor agreed with every picture I checked, and was still half a line wrong

Agent: Claude Opus 5.

**Task as accepted:** session 6's next step — wire the phasing line-start
in and segment the transmission, settling the dead-sector edge convention
against a decoded picture first.

**Prior art, checked first.** JWX's `s_sync` (source in the parent folder)
does **not** use the phasing interval at all: it accumulates 20 s of
*image* lines, integrates, and takes the strongest negative excursion —
an image-derived anchor of exactly the kind that fails on a white-only
station. The mature decoder does not solve this problem; its help file
shows the operator aligning by hand. So the reusable finding was a
negative one, and the answer had to come from the signal. Two JWX details
mattered anyway, and both are in docs/00: it carries its anchor forward by
a whole integer number of lines (immune by construction to the bug below),
and it applies an untested constant fudge where Nova now measures the same
quantity and reports it.

**The edge convention, settled by measurement.** I folded the video over
the phasing region and over the image region of one recording onto one
common line grid and read off where the white starts in each. On JMH the
phasing white leading edge sits at −73 samples of 4000 from the decoder's
pulse anchor, and the image's dead-sector black run starts at −67: **the
same feature, six samples apart.** Across seven pulse-station recordings
the offset is the black porch — −1.65% to −2.86% of a line — and two
recordings of the same transmitter agree to **3 samples of 4000**, twice
over (JMH −78.4/−75.7, XSG −114.3/−111.5). `line_start` marks dead-sector
ENTRY [WMO §5.2.3.4], on both dead-sector styles. Gap closed.

**On white-only stations the image anchor was not the dead sector.** It
scores the rising edge of always-white, which is dead-sector entry only if
nothing else on the line is reliably white — and charts have blank
margins. VMW 2230Z's always-white run is 1350 samples where its dead
sector is 180, so the anchor sat 1149 samples early and **the picture was
drawn rotated by 520 px of 1810**, the paper's right margin wrapped around
to the left edge. The phasing wedge sits in the last 4.5% of that white
run, which is the dead sector. NMC disagreed by −1743 samples and its
caption was torn across the line boundary; GYA 2324Z by +287 with a 130 px
strip of its right edge on the left. All three are correct now, checked by
looking at the pictures. Pulse stations keep their tracked anchor and are
**byte-identical** on all ten recordings tested.

**The bug that no number would have caught.** I referred the absolute
anchor to the MIDPOINT of the phasing run — and `(i+j)/2` is a half-line
whenever the run has an even number of lines. A 30 s phasing interval is
60 lines. So every anchor in the library came out **exactly half a period
off**, and every synthetic test still passed, because the generator emits
30 phasing lines and 30 is even too. What exposed it was one recording:
XSG ASPN, whose run is 53 lines, read −111.5 samples while every other
station read ~+1900. An odd number disagreeing with a field of even ones
is not a plausible physical result. The generator now takes
`phasing_lines`, and `tones` [11] generates both parities — reverting the
fix fails it at 49.75% of a line while the odd cases still pass.

**A second measurement error, found the same way.** The phasing run was
grown on score alone, and the 10–90% spread is a robust statistic, so up
to a tenth of the run could disagree wildly without the spread ever
showing it. Printing the per-line positions: the last line of VMW's
"60-line" interval sits **718 samples off the median**, and the generated
pattern's first two picture rows sit 256 off. All were being counted as
phasing and all moved the `t_end` that segmentation cuts the picture on.
The ends are now trimmed back to lines that agree in position (ends only —
a dropout in the middle is HF fading, not a boundary). The synthetic run
went 32 → exactly 30 lines, most library runs snapped to exactly 30.0 s,
and roundtrip [9]'s MAD against a known reference went 19.4 → 10.9.

**Segmentation** crops the OUTPUT only: onset, period, anchor and both
tracking passes still see the whole recording, so nothing session 5
measured moves. Boundaries are the first opening sequence and the first
stop tone that follows it — ordering that matters, because `jmh sample`
holds a start at 6 s, its stop at 404 s, and the *next* transmission's
start at 425 s. My first rule took the latest start tone and threw away
that recording's entire chart, keeping 143 s of the following one.

**Contradictions found.** Three, all mine, none caught by reasoning.
(a) The half-line anchor above. (b) The untrimmed run boundary above.
(c) The segmentation rule that took the latest start tone rather than the
first. There is also one that is not mine: `test-chart-jmh-kiwisdr-60s.wav`
has been documented as the PRIMARY "pure image content" honest-lock
reference since session 3, and it is not pure image — its parent's phasing
runs 72.5–102.5 s and the fixture is cut 80–140 s, so **45 of its 120
lines are phasing** and the rest are blank top margin. Segmentation is
what surfaced it. It is kept and re-documented as the phasing→image
boundary case (which no other fixture covers), and a real pure-image
reference was cut from 140–200 s: 117 of 120 locks, max_step 0.16 px.

**Tests:** 13 suites green, zero warnings (was 11). New `tones` [11]
(absolute anchor, both run parities, at 0 and −137 ppm), `roundtrip [9]`
(row 0 of the output IS image line 0, against a known reference — a slip
of a few rows blows the MAD bound), `fixture_phasing_anchor` on a new
160 s VMW fixture (start tone + phasing + 245 image lines) asserting the
PICTURE: content begins one dead sector into the line, 4.97%, and reads
0.00% with the old anchor. `fixture` now points at the new pure-image
fixture; `fixture_phasing_boundary` is the old one, re-bound to its true
75 drawn lines; `fixture_lpe` re-bound to 51. Every new screamer was
verified to FAIL with its fix reverted.

**Registered gaps added:** multiple transmissions in one recording (the
first is decoded, the rest dropped — one recording, one image); the
phasing anchor is measured once and propagated on the fitted clock, so a
mid-stream time-skip on a white-only station would shift the picture with
nothing to re-acquire (no library recording exercises this; it matters for
M4); and segmentation costs a full `detect_tones` pass (~9 s on the
61-minute JSC4 against a 37 s decode), which is unbudgeted for live decode.

**Next step:** M3 is done bar the manual override, which needs the GUI.
Two things are worth doing before M4 opens, in this order. First, the
`phasing_anchor_delta` is now printed on every decode but nothing asserts
it — the porch is stable to 3 samples across repeat recordings of a
transmitter, so a screamer that pins the *agreement between the two
independent anchors* on a pulse station is cheap and would catch a whole
class of regression neither existing test would. Second, decide whether a
pulse station should ever prefer the phasing anchor: it is currently never
used there, on the argument that a tracked reference beats a fixed one,
and that argument is sound but untested — JSC2's delta is −234 samples
against JSC3's −55 from the same transmitter, and nobody has looked at why.
Tree is green, buildable, and committed.

---

## 2026-08-12 — Session 6: the tones were always there; we had been listening in the wrong domain
Agent: Claude Opus 5.

**Task as accepted:** M3 — start/stop tone detection and auto sequencing
(session 5's next step), prior art first per Sara's reuse rule.

**Prior art, checked first.** JWX, weatherfax_pi/KiwiSDR and fldigi all
detect the control tones, and all three disagree about how: JWX runs a
250 ms Goertzel at 300 Hz with a 0.5 power threshold and edge-triggers on
it; KiwiSDR votes per line and needs `5s×lpm/60 − 4` lines; fldigi counts
black↔white transitions with 215/40 hysteresis over 500 ms and demands two
consecutive windows within ±8 Hz. Full comparison table in docs/00.

They agree on the one thing that mattered most: **all three run the
detector on demodulated VIDEO, not audio.** The control signals are
alternating black/white at a rate [WMO §5.2.2], not audio tones.

**That is what session 3 got wrong, and it had been in the risk register
ever since.** Session 3's library tone survey ran a Welch FFT on the raw
audio and concluded that only `jmh sample.wav` carried a start tone — a
finding that shaped the whole plan for this milestone ("synthetic-first
with one real check is the likely shape", session 5's next step). Measured
in the video domain: **14 of 20 recordings carry a 300 Hz start tone, 16
carry a 450 Hz stop, and 15 carry a full phasing interval.** The audio
survey saw its one tone only through incidental envelope ripple. A
constant-envelope FM signal has no component at the modulation rate; there
was never a good reason to expect one.

**The generator was emitting tones that were not control signals.**
`push_tone` truncated its half-period to whole samples, so at fs=8000 it
produced 307.7 Hz for 300, **500 Hz for 450, and 800 Hz for 675** — the
last two nowhere near the ±1% of WMO §5.2.6. Measured with the new tool
before touching it (306.0 / 499.0 / 800.5) rather than inferred from the
arithmetic. Had I calibrated the detector against this first, I would have
widened its search band to ±20% to "make it work" and shipped something
that accepted almost anything. Fixed, re-measured, and pinned by `tones`
[1][2][3], which assert the ±1% directly.

**What Nova adds to the prior art: purity, not rate.** Every one of the
three accepts on a rate — power at a bin, a line vote, a transition count.
None can separate a clean 300 Hz square wave from dense weather text that
merely averages 300 transitions per second, which is precisely M3's named
false-start trap. Nova's test is the fraction of a window's AC power in
the tone's own bin, normalized so a pure sinusoid reads 1.0 and an ideal
square wave 8/π² = 0.811. Hann-windowed, because rectangular leakage would
spill exactly the broadband content this is meant to reject into the bin.

**Measured separation, 5.9 hours of library audio:** picture content never
exceeds **0.16** in any control band; real tones run **0.68–0.99**;
threshold 0.35 sits in an empty gap two orders of magnitude wide on the
text-heavy JSC newspaper faxes (max 0.12). **Zero false positives.**

**The survey found the opposite failure instead — false negatives.** Four
recordings showed purity 0.73–0.96 outside any accepted event. All four
were real tones my run-assembly rules had discarded: a one-window gap
tolerance cannot survive HF fading, and the frequency-coherence test
rejected spreads above 3 Hz while the probe grid was spaced 4 Hz — a test
finer than its own resolution. Fixed with a 2 s gap tolerance, a hot-
fraction floor, and parabolic interpolation of the peak frequency. VMW
2230Z's stop tone went from rejected to 5.12 s; NMC's from 3.38 s to 5.12;
JMH Test Chart's start from two fragments to a single 10.00 s event.

**Phasing detection, and the payoff for white-only stations.** Wedge fit,
median, 10–90% spread rejection — the KiwiSDR shape — but their spread
limit of `SamplesPerLine/6` let satellite imagery report **439 s and 481 s
of "phasing"**. That is not a bad constant on their part: KiwiSDR only ever
runs this inside a phasing stage its tone state machine has already
entered, while Nova scans blind. Tightened to 1/24 plus a duration cap
from the spec itself (phasing is ~30 s [WMO §5.2.3], so 480 s is falsified
by length alone, whatever it scores).

Tightening did not just remove the false runs — it **recovered the real
ones underneath them**, because the candidate rule had been "longest run,
then test it" when it should have been "test every run, then take the best
valid one". himawari 233 s/spread 288 → 30.0 s/spread 19. jmh sample
91 s/360 → 30.0 s/**4**. XSG FYCI 146 s/635 → 30.0 s/12.

**The structural corroboration nothing in the code enforces:** where a
recording carries both, the phasing interval begins where the start tone
ends in **11 of 14** cases — VMW 2230Z 62.00→62.00, JSC4 57.00→57.00, XSG
FYCI 132.12→132.00 — and runs for exactly 30.0 s. Two detectors sharing no
code, agreeing on a boundary neither was told about, reproducing the
transmission sequence of WMO §5.2.3 from off-air recordings. VMW and NMC
are **white-only stations**: they have reported zero locks since session 4
because their dead sector holds no phase, and their phasing intervals are
right there, 60 lines each, spread 16–17 samples of 4000.

**Contradictions found.** Two, both mine, both caught by measurement
rather than reasoning. (a) I set the frequency-coherence limit at ±1% of
nominal without checking it against the probe spacing that feeds it, and
it silently rejected real tones for three of the four false-negative
cases. (b) My first phasing candidate rule took the longest run and tested
it afterwards, which is wrong whenever a recording holds both a long false
run and the real interval — it discarded five real phasing intervals that
the corrected rule finds.

**Tests:** 11 suites green, zero warnings (was 8). New `tones` [1]–[10]:
both start tones, stop, false-start on picture content, the purity margin
itself, noise, phasing rate recovery at 60/90/120, the symmetric 50/50
waveform [WMO §5.2.3.2], no phasing from image content, and line_start
against the known grid. New `tones_fixture_vmw` on a new 100 s fixture cut
from VMW 2230Z — a real start tone plus a real phasing interval on a
white-only station, asserting that phasing begins where the tone ends. New
`tones_fixture_no_false_start`: zero events on 120 s of real newspaper
text. The synthetic false-start test is deliberately not trusted on its
own — generated content peaks at 0.001 where real content reaches 0.16.
`nova-gen` gained `--phasing-sym`; the generator could not produce the
symmetric waveform the spec permits and docs/01 demanded be accepted.

**Registered gaps added:** which edge of the dead sector the phasing
`line_start` marks (it agrees with `fax.cpp`'s independent image-derived
anchor to ~23 samples of 4000 on JMH Test Chart, once the black porch is
allowed for — the same feature, not a settled convention); and GYA 2300Z's
18-line phasing candidate, which the final thresholds reject and which is
not established either way. IOC 288 re-confirmed absent from the library
by a detector that searches the right domain.

**Next step:** wire it in — nothing consumes any of this yet. `decode_fax`
still finds its anchor from image lines. Take the phasing `line_start` as
the line-start reference [WMO §5.2.3.4] and use the tone events to segment
start → phasing → image → stop. **Settle the edge convention against a
decoded picture before trusting the anchor** — session 5's lesson applies
directly here, and the honest test is VMW or NMC, where the phasing anchor
is the only per-line phase that exists and the current decode has none.
Tree is green, buildable, and committed.

---

## 2026-08-12 — Session 5: the baseline was the bug; the library was not straight
Agent: Claude Opus 5.

**Task as accepted:** weak-signal period estimation (session 4's next
step) — GYA 2300Z, recorded as "+3576 ppm off and slanted", with JWX's
clock-corrected accumulation as the prior art to check first.

**Prior art, checked first (Sara's reuse-first rule).** JWX does **not**
estimate the clock. Its calibration is a number the operator types in
(`CalibrationController` is a text field; the alternative is right-clicking
the two ends of a vertical feature), and `clock_correct_line` only applies
it. Session 4's note pointed here expecting an estimator; there isn't one.
The reusable idea is the accumulation itself, plus weatherfax_pi/KiwiSDR's
median-with-spread-rejection. Nothing copied; reuse ledger unchanged,
docs/00 updated with the finding — including that a mature decoder asking
a human for this number is a fair design, not a defect.

**The premise was stale, and the real bug was bigger.** GYA 2300Z no
longer fits +3576 ppm — session 4's onset gate fixed that. What remained
was a systematic error in how the period is measured, and it was not
confined to weak signals:

1. *Coarse fit.* `best_period` runs on 200 Hz video where one lag step is
   1% of a line — **10 000 ppm** — so everything finer comes from a
   parabola over three points. Measured against pass B across the library:
   wrong by 30–180 ppm (JSC6 +261 vs +438).
2. *Pass B.* Took the median slope over pairs of locked lines ≤10 apart.
   A sync position is good to a sample or two, so a one-line slope is the
   period ±500 ppm of noise, and the median of that does not recover the
   period. From the very same `spos` array on JSC2: **−75 ppm from
   neighbours, +178 ppm from pairs 500+ lines apart.**

**Three independent methods, then the picture.** Fold: +172. Image shear
at nominal clock: +151. Long-baseline fit of the sync positions: +178.
Pass B: −73. So pass B was wrong — and JSC2's shipped decode was visibly
sheared, the page border walking a third of a page before the local-median
correction froze (residuals past `2*search` are dropped, so nothing
downstream noticed). **`locked_lines` was 2192 of 2269 the whole time.**
The roadmap's "the library decodes straight without manual calibration"
was false when it was written; nobody had measured a decoded image.

**Both estimators rebuilt on the same principle: accuracy is BASELINE.**
- No locks → fold blocks of lines into profiles, cross-correlate
  consecutive profiles, median pairwise slope. Works with zero locks,
  which is the white-only case.
- Locks → pairs an eighth of the recording apart.
- Both segment first. A long baseline is only meaningful inside one
  regime: the phasing↔image step (+167 samples on the 60 s fixture), a
  stream time-skip, a chart restart. Each end of the baseline range was
  measured, not chosen: JSC2 needs k≥128, Himawari's time-skip breaks at
  k=1024 where every pair straddles it.

**Residual shear, before → after (ppm):** JSC2 −157 → +6, JSC3 −182 → −5,
JSC4 −172 → +2, GYA 2300Z +50 → +4, VMW 2215Z −399 → +0.2, NMC −79 → −9,
GYA 2324Z −59 → +0.2. Nothing regressed. GYA 2300Z's frame line — the
chart's own ink, not a statistic — is straight to +1.7 ppm over 1358
lines, and is now findable on 81 of 84 bands instead of 53.

**Free consistency check, worth keeping:** two recordings of one station
through one receiver must give the same clock. GYA 2300Z/2324Z now read
−116.8/−118.5 (were −28.6/−54.3); VMW 2215Z/2230Z −79.0/−79.6 (were
−38.3/−91.7). Nothing in the code enforces this, which is what makes it
evidence.

**Contradictions found.** Three, all mine. (a) I reported a "75% bias" in
the coarse fit measured through a 3-decimal printf — the bias is real but
that measurement was not clean; print fixed, re-measured. (b) My first
image-shear tool double-counted its own estimate each iteration and read
+100 ppm as −300, then −699 after a bad fix; a sign error, corrected and
calibrated before any conclusion rested on it. (c) The first long-baseline
pass B broke the two 60 s fixtures (+607 ppm) because half of a short
fixture is phasing — which is exactly what session 4's short baselines
were protecting against, and which I had read and not applied.

**Tests:** 8 suites green, zero warnings. New `fixture_weak_white`
(GYA 2324Z 180..300 s: weak *and* white-only, nothing to lock) — verified
to fail (−51.6 ppm) when the fold is removed. New roundtrip [7]: a
white-only signal generated at a known +250 ppm, decoded to +250.0 with
zero locks. New roundtrip [8]: −137 ppm recovered as −137.00. `nova-gen`
gained `--no-pulse`; generating a white-only line also had to drop the
black porch, since porch and pulse are the two halves of one dead sector
and the porch alone gave 629 phantom locks.

**Doc contradiction fixed (found while sweeping docs at Sara's request):**
`AGENTS.md` listed `SESSION-LOG.md` under "never commit" and START-HERE
called it gitignored, but it has been tracked since commit e49834d by
Sara's own session-1 decision. A future agent following the stale rule
would have deleted the project's history from the repo. Both corrected.

**Registered gaps added:** short windows of a deeply faded signal (GYA
2300Z's 120 s windows scatter −1223…+320 ppm while the whole recording is
solid — matters for live decode in M4); and picture content that mimics
the optional sync pulse, which is indistinguishable from it by
construction.

**Next step:** M3 — start/stop tone detection and auto sequencing.
300/675 Hz start, 450 Hz stop [ISO §4.2.5], phasing alignment by
wedge-fit + median + spread rejection (the KiwiSDR approach, and the
place where per-line phase for white-only stations may finally come
from), false-start rejection on text-heavy content. Library tone survey
from session 3 says only `jmh sample.wav` carries a start tone, so a
synthetic-first approach with that one real check is the likely shape.
Tree is green, buildable, and committed.

---

## 2026-08-12 — Session 4: the anchor was the bug; a template that shouldn't exist
Agent: Claude Opus 5.

**Task as accepted:** the VMW white-dead-sector sync template (session 3's
next step), then follow the roadmap.

**What the measurement actually said.** Before writing the template I
profiled the library. VMW is not a lonely special case — nine of twenty
recordings had near-zero honest locks, including `FAXSignal.wav`, which
carries a black sync pulse on 98% of its lines. That is not a missing
template; that is the anchor. Confirmed: the coarse phase came from a
40-line fold-average maximising contrast, and on FAXSignal it landed 211
samples from the pulse — outside pass A's ±120 sample search — so the
tracker never saw the sync it was sitting next to.

**Three fixes, each measured:**
1. *Anchor from across-line consistency.* The dead sector is the only
   part of a line that looks the same on EVERY line [WMO §5.1.3.3], so
   score, per position, the FRACTION of lines that are dark/white there,
   not the average contrast. Score the black->white SHAPE, taking the
   weaker half: a full-disk satellite image is black on 100% of lines
   over hundreds of samples at the margins, and a level-only test picks
   an arbitrary point inside that band (this is what broke himawari and
   FAXSignal on the first attempt). Profile skips the ~30 s phasing stage
   [WMO §5.2.3.1] — with phasing included, every station in the library
   scored 0.51-0.63 and the style decision was a coin toss.
2. *Pass A re-acquires.* After 8 unlocked lines, sweep the whole line at
   a coarse step. A tracker that only looks ±narrow around its own
   prediction can never come back from being wrong. This also healed the
   Himawari stream time-skip: warp fixture max_step 54.3 px -> 0.75 px.
   The roadmap listed that as separate M2 work; it is done.
3. *Parabolic-refinement guard.* `denom != 0` is not enough — at a
   coarse-scan winner the neighbours need not bracket a maximum and the
   vertex formula throws the position arbitrarily far. One such jump
   moved a FAXSignal line by 250k samples and poisoned the median
   intercept. Now requires a real maximum and clamps to ±1 sample.

**Honest locks, before -> after:** FAXSignal 65 -> 2170/2192, XSG ASPN
116 -> 2566/2633, JSC2 103 -> 2192/2269, jmh sample 71 -> 1023/1127, test
chart 62 -> 711/800, HDSDR 105 -> 1790/1851, JMH Himawari 740 -> 1953/2035,
JSC4 3431 -> 3592/3687. Nothing regressed.

**The white-sector template does not exist — measured, not assumed.**
Two were built. "White across the dead sector against the picture either
side" gave VMW 2215Z 753 locks of 1162 and tore the chart into strips (it
wanders inside the 45 ms always-white run, which is twice the 22.5 ms
dead sector because the chart has its own white margin). "Rising edge
into white" gave 879 locks and slanted the whole image, dragging the
fitted clock from -121 to -285 ppm. Both were matching the paper, not the
signal. A white-only dead sector contains nothing the picture does not
also contain, so it carries no per-line phase. VMW/NMC/GYA are now
decoded on the measured clock and report **zero** locks, which is the
truth and which produces the better picture. Sara's rule from session 3
applies: a lock metric that goes up while the image gets worse is the
vacuous metric wearing a new hat.

**Prior art checked first (Sara's reuse-first rule, added to AGENTS.md
this session).** JWX `DecodeFax.s_sync` folds ~20 s of clock-corrected
lines and takes the strongest negative excursion; weatherfax_pi/KiwiSDR
fit the phasing wedge with a median over ~40 lines. Both do it DURING
PHASING, where there is no content to fool a fold — Nova has no phasing
detection until M3, so it must work inside image lines, which is why the
consistency profile is new code rather than reuse. Recorded in docs/00
with two things to take from them at M3. Reuse ledger unchanged.

**Tests:** 7 suites green, zero warnings. New `fixture_white_sector`
(`vmw-white-sector-120s.wav`, VMW 200..320 s) pins style detection AND
that no locks are invented. Existing lock bounds re-bound upward
(0.6->0.85, 0.3->0.75, 0.4->0.85, 0.8->0.9) so a regression to the old
anchor fails.

**Contradictions found:** session 3's docs/01 §5 claim "a white-sector
matcher is required for VMW" is wrong and is now corrected in place with
the measurement that refutes it. My own first anchor attempt (level, not
shape) regressed himawari from 1892 locks to 22 — caught and fixed
before it left the session. Also worth flagging against my own framing
this morning: I described this as VMW work; it was library-wide work that
VMW happened to point at.

**Next step:** weak-signal period estimation — `GYA 2300Z.wav` (+3576 ppm
off, slanted, white-only so it coasts) and `GYA 2324Z.wav` (marginal).
The coarse autocorrelation fit is the suspect; JWX's clock-corrected
accumulation (docs/00, session 4 note) is the prior art to check first.
Then M2/M3 proper. Tree is green, buildable, and committed.

---

## 2026-08-12 — Session 3: two KiwiSDR recordings, three real bugs, M1 batch
Agent: Kimi Code CLI.

**Inbound fixtures (Sara):** `JMH Test Chart KiwiSDR 13986.6.m4a` (478 s)
and `JMH KiwiSDR Himawari 13986.6.m4a` (1033 s). Both clean of the old
fixture's 144 ms long-path echo (validated: row-xcorr peak at +523 px on
the OLD decode, only negative values on the new one — method + result).
Per the agreed procedure the KiwiSDR test chart became the primary M0
fixture (80..140 s excerpt); the old one stays as the LPE case.

**Measurement campaign (all persisted under recordings/):** the new
test chart has NO line structure for its first 72.5 s (leader/tuning
tone or stream stall-fill; BOM documents 60 s white tuning tones) and
the Himawari ~49 s (incl. a looping ~500 ms replay buffer that fakes a
2 Hz comb). Himawari has two genuine stream time-skips (410.5 s: 0.3 s
silence + ~164 ms phase jump; ~950 s) — Sara confirmed one single
transmission; the mid-image blocks are the dead sector rendered
mid-line after the phase jumps, not dropouts.

**Bugs found (every new recording finds one — here three):**
1. decode_fax assumed signal at t=0. Whole-file autocorrelation over
   fill returned a confident junk period (+96735 ppm on 60 s of pure
   fill); the phase fold over fill anchored the tracker to noise, so it
   content-locked for entire files and `locked_lines` was vacuous
   (counted "correction didn't jump"; real template locks were 25/956
   on the new chart, median sstr 0.010). Fixed: odd-harmonic line-comb
   onset scan (15 s windows, gate = max(0.06, 0.5*file_max), 2
   consecutive windows, lowest-clearing-rate wins — 120's teeth are a
   subset of 60's comb, the reverse is not true); period refined over
   onset..EOF; fold anchored at onset; no comb -> throw. Honest
   `locked_lines` = real sync-template matches (sstr >= 0.6).
2. Rate gate mis-sized at 60 lpm (comb teeth sit in the fade band):
   fixed by the relative gate + lowest-rate rule above. Verified on the
   whole library: JSC1/4/5/6 (Kyodo News newspaper fax) are 60 lpm —
   the library DOES cover 60 lpm (93-99% locks). 90 lpm: still none.
3. Line geometry (Sara, from screenshots + a reference decode): the
   dead sector is SPLIT around the line boundary. Measured on locked
   JMH lines: 7.5 ms sync pulse, 10.5 ms white gap, 474 ms picture,
   ~8 ms black porch. The old 4.5%/95.5% picture mapping cut ~16 px at
   the left edge and showed the porch as a 31 px black band at the
   right. Sara's call: render the true full line, no cropping ("the
   standard doesn't ask you to chop"). Assembly now maps pulse..pulse
   to the full width; gen + round-trip ref frame updated to the
   measured layout.

**Fixture suite (ctest, 6 suites green, zero warnings):** roundtrip,
fixture (new primary), fixture_lpe, fixture_warp (Himawari 350..470 s,
spans the 410.5 s time-skip), fixture_60lpm (JSC1 60..180 s), and
fixture_fill_reject (first 15 s must throw).

**M1 batch survey (all 20 library recordings, reports in
recordings/library-8k/decodes/_reports.txt):**
- Rates: 120 lpm everywhere except JSC1/4/5/6 at 60 lpm. No 90 lpm.
- IOC: only `jmh sample.wav` carries a start tone (300 Hz = IOC 576).
  IOC 288: none found — registered gap stands.
- VMW (both recordings): locked = 0/1162 and 60/1176 with the
  black-pulse template — MEASURED confirmation that VMW sends a
  white-only dead sector (BOM quote, thanks Sara). Decode still
  produces a legible straight chart by coasting (period is right).
  -> needs the white-sector sync template (next session's candidate).
- GYA 2300Z: weak/faded, period estimate +3576 ppm off, slanted — the
  weak-signal case. GYA 2324Z marginal (13 locks).
- FAXSignal.wav: Himawari full disk dated 25 Sep 2010 — a sample file
  from elsewhere (clock +0.8 ppm, suspiciously exact). Decodes great.
- Long decodes: JSC4 61 min end-to-end, 3431/3687 locks, newspaper
  text crisp (dead-sector diagonal drift visible over the hour —
  residual clock wander, bounded). XSG ASPN/FYCI 23 min fine.

**Contradictions found:** my first reading of the Himawari blocks as a
second transmission — wrong, corrected by Sara (internet-side drops);
my first geometry fix (crop to picture sector) — wrong direction,
corrected by Sara (render the full line). Both corrected in writing
here. Also: session 2's fixture "locked" bounds were passing on the
vacuous metric — now re-bound on honest locks.

**Next step:** VMW white-dead-sector sync template (fixture:
`VMW 2215Z.m4a`, currently 0 locks) + wide re-acquisition after stream
time-skips (warp fixture coasts after the jump; clamped-tracker
re-lock, M2 scope). Then commit — tree is green and buildable.

---

## 2026-08-12 — Session 2 close-out: documentation sweep
End-of-day pass: AGENTS.md risk register updated (slant + per-line
resync marked M0-proven, remaining risks re-ranked), README gains a
Status section, START-HERE alive-command now real. Tree is buildable,
all tests green, everything committed. Project sleeps at M1's door.

**Next step:** M1 — library mode survey + one full-length decode;
fresh JMH test-chart fixture inbound from Sara (see previous entry).

---

## 2026-08-12 — Session 2 addendum: WMO-386 2023 edition check
Sara supplied `386_2023-edition_en.pdf`. Compared Part III §5 + §6–7
against the 2009 edition parameter by parameter: ALL signal values
identical (IOC, dead sector, rates, control tones, FM frequencies,
gray scale, shifts). One structural change: §5.5 restructured —
§5.5.1 is now explicitly the audio subcarrier FM (1500/1900/2300 Hz);
new §5.5.2 is direct RF FSK (HF f₀±400, LF f₀±150). This REMOVES the
2009 ambiguity Sara's docs inherited; decoder-affecting content
unchanged (ISO §4.2.2 already pins the AF shifts). Updated: docs/01
(citations + edition note), NOTICE (both editions verified), gen.hpp
comment. No code changes needed. Contradictions found: none.

**Next step (unchanged):** M1 — library mode survey + one full-length
decode. **Inbound fixture:** Sara will record a fresh, cleaner JMH
test chart (morning JST) for cross-referencing the test harness;
current fixture carries a 144 ms long-path echo. When it lands:
convert to 8 kHz mono WAV, decode, compare sync/clock behavior and
image against `test-chart-jmh-60s` bounds; if cleaner, it becomes the
primary M0 fixture and the echo one stays as the LPE case.

---

## 2026-08-12 — Session 2: M0 DONE — real JMH test chart decodes
Agent: Kimi Code CLI.

**Done:**
- core/ written: wav (PCM in/out), resample (windowed-sinc + ratio
  form), demod (quadrature mix @1900 + 63-tap FIR + atan2
  discriminator — amplitude-normalized by construction), fax (sync +
  assembly), gen (harness signal generator), image (PGM).
- CLIs: nova-decode (wav -> pgm + report), nova-gen. CMake, -Wall
  -Wextra clean. ctest: roundtrip (6 synthetic groups) + fixture —
  all pass.
- Real fixture decoded: `test chart.m4a` = JMH Tokyo (3622.5/7795/
  13988.5 kHz header legible, WMO test chart + portrait readable).
  Straight, unattended decode. Clock measured -99 ppm (cluster of
  windowed autocorrs agrees; earlier whole-file estimate of -24 ppm
  was biased by tone regions).

**Failure modes found this session (the reason tests exist):**
1. Least-squares period fit bends ~+66 ppm when phasing and image
   lines are fitted together — the phasing wedge anchors the sync
   template ~half a dead sector (2.25% line = 90 samples @8k) earlier
   than the image sync pulse. Fixed with MEDIAN-slope fit over
   near-consecutive line pairs (regime-pure) + local-median residual
   tracking across the boundary.
2. Sequential tracker window (±1.5%) was smaller than that same 2.25%
   regime offset -> tracker fell off the grid at the phasing->image
   boundary and coasted to EOF. Window now ±3% with a comment saying
   why it may not shrink.
3. Test-metric bug: horizontal reference bars made "find the black
   bar edge" return junk on 5% of rows, inflating stdev to 28 px on a
   visually perfect decode. Metric now skips mostly-black rows.
4. "Strongest edge" sync locking abandoned before it shipped: content
   edges (white gradient -> black pulse) tie with sync edges. The
   black->white sync TEMPLATE is content-independent; use that.

**Discovered in the fixture:** a constant ~+523 px (144 ms) faint
duplicate of all content = long-path ionospheric echo in the
recording itself (stereo channels verified identical; autocorrelation
of the decode confirms a 144 ms/12% copy). Not a decoder bug. This
fixture now also covers "signal with LPE ghost".

**Contradictions found:** none in spec; one in my own earlier
analysis (whole-file clock estimate), corrected above in writing.

**Next step:** M1 — mode generality on real signals: batch-analyze the
whole recording library (rates/IOC per station from phasing tones),
identify which fixtures cover 60/90 lpm and IOC 288; decode one full
long recording end-to-end (e.g. JSC1, 27 min) and inspect.
- Scaffold committed as `20a9a64` on `main`; SESSION-LOG.md un-ignored
  and tracked per Sara's call. Regular commit standards from here on.
- Resolved: commit author is "skgsara <skgsara@riseup.net>", set
  repo-local; both commits amended (now `99408ab`, `e49834d`).

**Next step (unchanged):** M0 — harness signal generator + minimal FM
demod; decode `test chart.m4a` (via WAV) headlessly into an image.

---

## 2026-08-12 — Session 1: briefing, decisions, scaffold
Agent: Kimi Code CLI. Lane: n/a (no lanes in this project).

**Done:**
- Read SOP.md (v2 method), extracted and read WMO-386 Vol I Part III §5
  (pp. 287–291, the normative analogue-fax signal spec) and all of
  ISO 9876:2015 (9 content pages; §4.2 is the software-applicable core).
- Surveyed prior art: JWX source (PLL demod, Goertzel tones, hardcoded
  576/120, no per-line resync), HamFax (via GitHub mirror README;
  sourceforge.net 403s this host), fldigi (SF page), ACFax 0.981011
  source (fs/4 quadrature + normalized discriminator + arcsin LUT;
  raw-stream-retention architecture), KiwiSDR FaxDecoder.cpp (GPLv3,
  D'Epagnier/weatherfax_pi; contains ACFax's FIR tables verbatim —
  confirmed single GPL lineage ACFax→HamFax→yahfax→weatherfax_pi→
  KiwiSDR).
- Inventoried Sara's recording library (~600 MB, 7+ stations, m4a/AAC
  44.1–48k stereo + WAV 12/16/44.1k). Kept outside the repo.
- Decisions (Sara's): RX only; ISO 9876 §4.2 as acceptance contract,
  WMO-386 for the signal; NO 240 lpm (not ISO-required); GPLv3 with
  reuse from the surveyed lineage; tiered platform support; C++17 +
  FLTK + RtAudio; repo in subfolder `nova/`; project name "Nova";
  m4a via runtime `ffmpeg`, WAV native.
- Scaffolded: LICENSE (GPLv3), NOTICE (lineage + standards citations),
  README, .gitignore, AGENTS.md, START-HERE.md, ROADMAP.md, docs/00-02.

**Contradictions found:**
- Isobar README (v1.0.2, live) still cites "ITU-R F.460" — the SOP
  records that citation as a hallucination purged from five files.
  Nova cites WMO-386 / ISO 9876 only. Follow-up on Isobar side is
  Sara's call.
- KiwiSDR repo has no root LICENSE file (404); FaxDecoder.cpp carries
  a per-file GPLv3 header (D'Epagnier). Reuse is per-file-licensed.

**Next step:** git init + first commit ONLY when Sara asks. Then M0:
build the harness signal generator + minimal FM demod, decode
`test chart.m4a` (converted to WAV) headlessly into an image.
