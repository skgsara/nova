# 05 — M4 shell design: FLTK + RtAudio

Status: **complete — every design question in this document is decided**
(Sara, 2026-08-13, sessions 17–20: the original five plus the retention
follow-up, the eight from the skeleton, the five from preparing to code
it, and the three about the save/edit lifecycle asked while the surfaces
were being written). **The next step is code, not paper** — see §8.5's
closing note on why that sentence keeps being true and keeps needing a
new paragraph above it.

This document is the design that `docs/04`'s answers imply. `docs/04`
settled *what the operator sees and what the machine promises*; this one
settles *where the code lives, which thread runs it, and what can be
tested without a sound card*. Its visual companion is the mockup page
built in session 17 (link in `SESSION-LOG.md`); where the two disagree,
this file wins.

---

## Scope and method

Inputs: `docs/04` (all eight design questions answered, three further
decisions session 16), `docs/03` §"GUI/live-audio readiness", the session
14 seams in `core/hooks.hpp`, and the existing core API surface
(`core/fax.hpp`, `core/tones.hpp`, `core/demod.hpp`, `core/resample.hpp`).

Method: work backwards from the two hard constraints — `decode_fax` stays
batch and untouched [docs/04 answer 1], and the live view is never
revised [docs/04 answer 5] — to the smallest code shape that satisfies
both, then check the layout against the survey's conventions.

Toolkit availability confirmed on the development machine: FLTK 1.x
(`fltk-config` present) and RtAudio (`rtaudio/RtAudio.h`) are both
installed via Homebrew. Neither has been linked against yet.

---

## 1. The shape: three layers, and only the thinnest one knows the toolkit

```
core/      nova-core    no dependencies, batch, untouched by M4 except §7
live/      nova-live    no FLTK, no RtAudio, no threads of its own
gui/       nova-gui     FLTK + RtAudio; owns threads; contains no DSP
```

**The load-bearing rule: `nova-live` must not depend on FLTK, RtAudio, or
a real clock.** Everything M4 adds that can be wrong about a signal —
the streaming tone detector, the provisional renderer, the session state
machine, the retained store, the PNG writer — lives there and is driven
in tests by feeding it a fixture WAV in blocks, faster than realtime,
with no audio device and no window.

This is not architecture for its own sake. This project's entire quality
argument is its 23 screamers, and a live path that can only be exercised
by a human with a radio has no screamers at all. §9 is what this rule
buys.

`nova-gui` is then genuinely thin: device enumeration, widget layout,
event plumbing, and the three queues in §2. If a bug is in `nova-gui`, it
is a wiring bug, and a wiring bug is visible on screen in seconds.

---

## 2. Threads and data flow

Four threads. Ownership is stated for every shared structure, because
this is the first concurrency in the project.

| # | Thread | Owns | Never does |
|---|---|---|---|
| 1 | RtAudio callback (realtime, RtAudio-owned) | writes the audio ring | allocate, lock, log, throw |
| 2 | Live decode (one, spawned at capture start) | audio ring → video; the retained store; preview rows | touch a widget |
| 3 | Batch decode (one per completed transmission) | its immutable video snapshot | touch a widget, mutate shared state |
| 4 | FLTK main | every widget | block, decode, wait on a lock |

```
 [sound card]
      │  RtAudio callback  (thread 1, realtime)
      │  deinterleave, pick channel, float
      ▼
 ┌─────────────────┐   lock-free SPSC ring, fixed capacity (§2.1)
 │  audio ring     │
 └────────┬────────┘
          │  drained in blocks           (thread 2)
          ▼
   resample → fs 8000  [cli/nova-decode.cpp: kInternalRate]
          ▼
   fm_demod  (streaming, §2.2)
          ▼
     video block ──┬──────────────► retained store  (§3)
                   ├──────────────► streaming tone detector (§5)
                   └──────────────► provisional renderer (§6)
                                            │
                        rows + state changes│  GUI queue (SPSC, drained
                                            ▼  on an Fl::add_timeout tick)
                                    ┌───────────────┐
                                    │  FLTK main    │  (thread 4)
                                    └───────▲───────┘
   at stop: snapshot ──► decode_fax ────────┘
            (thread 3, existing batch core, DecodeHooks progress/cancel)
```

**2.1 The audio ring.** Single producer, single consumer, fixed capacity,
no allocation. Capacity 4 s at the capture rate — orders of magnitude
more than a callback period, so an overrun means thread 2 is wedged, not
that the buffer was tight. Overrun is *counted and shown*, never
silently dropped: a dropped block is exactly the kind of capture-chain
sample loss the decoder spent sessions 9–12 learning to detect, and
manufacturing it in our own buffer while reporting a clean timebase would
be dishonest.

**2.2 Streaming `fm_demod`.** `fm_demod` is a pure batch function over a
vector. The live path needs it over a growing stream, and the two must
agree sample-for-sample or the preview and the saved image would differ
for a reason that has nothing to do with the design. Two options; the
cheap one first:

- **Block-with-overlap (recommended).** Call the existing `fm_demod` on
  each block prefixed by a fixed tail of the previous block, and discard
  the warm-up samples. Zero change to `core/`; the overlap length is a
  measured property of the filter, and equality with a whole-file demod
  is a screamer (§9).
- Carry explicit filter state across calls. Requires a new stateful entry
  point in `core/`, i.e. the thing M4 was supposed to avoid.

The same applies to `resample`. Recommend block-with-overlap for both,
and let the screamer prove the overlap is long enough.

**BUILT session 20 (`live/stream.{hpp,cpp}`), and the measurement is
better than this section predicted.** `core/` is unchanged, as designed.
Numbers from `live_demod_equiv`, over a real recording and over
generated signals at 44100 and 48000 Hz, at eleven block sizes from one
sample to 44100:

- **The demodulator is bit-identical** — 0.0 difference on every sample.
  "Agree sample-for-sample" turned out to be literally true, though not
  for a reason worth relying on: the mixing oscillator restarts at phase
  zero on each segment, which is a constant rotation that cancels in the
  phase-difference discriminator, and the residue in the last bits of the
  doubles disappears when the result is rounded to `float`. Observed, not
  guaranteed by construction, so the screamer asserts a tolerance.
- **The resampler agrees to 5e-13**, its output positions being formed
  from segment-local indices. Ten orders of magnitude below one 8-bit
  grey level, and gone entirely by the time the video is demodulated.
- **Output counts are identical**, which is the claim that actually
  protects the picture: a sample-count drift between the two paths would
  slant it.

**The overlap, measured: 62 samples, and the arithmetic argument for 63
was wrong by one.** The I/Q lowpass is 63 taps, so from the tap count the
requirement looks like 62 samples to fill the FIR plus one fully-fed
sample for the discriminator. The sweep says the error reaches zero at
62 and is still 8e-6 at 61. The reason is the *window*, not the length:
the Blackman window is exactly zero at both endpoints, so `h[0]` and
`h[62]` carry no weight and the filter's effective support is two taps
shorter than its length. `kDemodOverlap` ships at 64, keeping two
samples of margin over a number that depends on those endpoints being
exactly zero.

**One constraint this section did not anticipate, and it is load-bearing
for the resampler.** A streaming resampler cannot consume arbitrary
block sizes and stay aligned with the batch call: output sample *i* sits
at input position *i / ratio*, so a segment starting at input *S*
reproduces those positions only when *S · ratio* is an integer. The
implementation therefore consumes input in whole blocks of *q* samples,
where *p/q* is the reduced ratio — 441 input samples per 80 output at
44100 Hz, 6 per 1 at 48000 — and holds a context window of history and
lookahead around each one, because the kernel is centred. This is
invisible from outside (`push()` takes any block size and buffers), but
it is why the streaming path has latency and the demodulator does not.

**2.3 GUI queue.** Thread 2 and thread 3 never touch widgets. They push
typed messages (`RowsDrawn`, `StateChanged`, `StatsUpdated`,
`BatchProgress`, `BatchDone`, `BatchFailed{DecodeErrorKind}`) onto an
SPSC queue; the FLTK main thread drains it on a 50 ms timeout and
repaints at most once per tick. No `Fl::lock()`/`awake()` from worker
threads — one drain point is simpler to reason about, and 20 Hz is well
past what a 120 lpm picture (2 rows/second) can justify.

---

## 3. The retained video store, and how long it lives

The retained raw stream is load-bearing now: the saved image *is* a
decode of it [docs/04 answer 6]. Design:

- Append-only `std::vector<float>` of demodulated video at 8 kHz, one per
  transmission in progress. Thread 2 is the only writer.
- At end of transmission, thread 2 **freezes** it into a
  `std::shared_ptr<const std::vector<float>>` and hands that to thread 3.
  Nothing is copied while decoding, nothing is mutated after freezing,
  and the two threads share no writable state. This is the whole
  concurrency story for the batch path.
- Cost: 8000 × 4 B = **32 kB/s, 115 MB/hour**. A 20-minute chart is
  ~38 MB. Two live at once (previous still open for adjustment, next
  already receiving) is the worst case.

**Retention policy** [DECIDED 2026-08-13, Sara, session 17]. Do **not**
write a raw sidecar next to the PNG: at ~100× the size of the image it
would quietly turn an unbounded image folder into an unbounded disk
problem, and the precedent agrees — the SR-97 states outright that
stored images cannot be modified.

**Two snapshots are retained, not one**, and the rule is stated by role
rather than by recency:

1. the transmission **currently being received**, and
2. the image **currently displayed** — the one the operator may be
   adjusting.

Usually these are the same object and only one exists. They diverge in
exactly the case Sara raised: *the operator is adjusting the chart that
just arrived when the next transmission starts.* The first draft said
"release it when the next transmission's snapshot replaces it", which
would have pulled the raw stream out from under a live edit — the
operator's PHASE/SYNC controls would go dead mid-correction, on the one
image they were actually working on.

Holding two is bounded and cheap, and this document's own cost analysis
already assumed it: 38 MB for a 20-minute chart, ~76 MB for two, which
is nothing on a modern machine. It is bounded at two **however long the
operator keeps editing**, because "currently displayed" is one image by
definition — a third can never accumulate. When the operator moves on,
the older snapshot is released.

**The consequence for older images.** Opening a PNG from the folder that
is no longer one of the two retained gives an image with no raw stream
behind it. The PHASE/SYNC controls must then be **visibly disabled with
the reason shown** — "raw stream no longer retained" — not silently
inert. Manual adjustment [ISO §4.2.6, §5.4.3] is satisfied for the
transmission you are looking at, which is when an operator actually does
it; it is not offered and then found not to work.

---

## 4. The live session state machine

`docs/04` Finding 3 says the operator is shown the state of the protocol,
never a percentage. These are the states, with the event that leaves each:

| State | Meaning | Leaves on |
|---|---|---|
| `IDLE` | not capturing | operator starts capture |
| `READY` | capturing, monitoring, no signal | start tone, or forced start |
| `START TONE` | start tone found; IOC selected from 300/675 Hz [WMO §5.2.2] | tone ends |
| `PHASING` | phasing interval running [WMO §5.2.3] | phasing ends / first picture line |
| `DRAWING — PREVIEW` | rows landing, provisional from the first row [docs/04, session 16] | stop tone, page cap, or operator stop |
| `STOP TONE` | 450 Hz found [WMO §5.2.5] | tone ends |
| `DECODING` | batch `decode_fax` on the frozen snapshot | batch completes or fails |
| `SAVED <name>` | the saved image has replaced the preview in the same pane | next transmission, or operator action |

Forced start [docs/04 Finding 2 — every receiver has one, without
exception] jumps `READY → DRAWING — PREVIEW` with operator-supplied IOC
and rate, skipping tone detection entirely.

**Operator stop [DECIDED 2026-08-13, Sara, session 18; §8.3 item 4].**
The Start button reads `Stop` from `READY` onwards, and pressing it takes
`DRAWING — PREVIEW → DECODING` by exactly the path a stop tone takes:
freeze the snapshot, batch decode, save. It is the first of docs/04
Finding 6's three ways a transmission ends, and the SR-97's behaviour is
the precedent for what it must not do — **stop holds the image, it does
not discard it.** From `READY`, where no rows have been drawn, Stop
returns to `IDLE` with nothing to save.

**Where the nine decode stages go.** They are the sub-progress *inside*
`DECODING`, and nowhere else — see the contradiction in §10.

**Clock and timebase readouts stay blank** until the batch decode
produces them [docs/04 answer 4]. The preview's own working period is an
internal number; the +261 ppm that a short baseline produces is never
displayed as a figure.

---

## 5. The streaming tone detector

`detect_tones` scans a whole recording (~9 s on JSC4). The live path
needs the same verdicts as they arrive [docs/04 answer 3].

The existing detector is already frame-based — Hann window
`win_sec = 0.25`, hop `0.125`, purity per frame, then runs assembled
with gap-bridging and coherence rules [`core/tones.hpp`]. Only the *run
assembly* is retrospective. So the streaming version keeps the identical
per-frame purity computation and replaces run assembly with an
incremental one that emits an event as soon as `min_start_sec` /
`min_stop_sec` of hot frames have accumulated within the gap and spread
rules.

Consequence to state plainly: a streaming detector emits **at the
earliest qualifying moment**, a batch one after seeing the whole run.
The measured `freq_hz` and `purity` (medians over the run) therefore
differ between the two for the same tone. This is inherent, not a bug —
but it means the screamer in §9 must compare *event kinds and start
times within a tolerance*, not medians.

**[BUILT session 20: `live/tone_stream.{hpp,cpp}` +
`tests/test_live_tones.cpp`, running unguarded in every build.]** Five
things this section did not know, in the order they matter:

1. **The two paths agree on which runs exist, and it can be argued
   rather than only measured.** The partition of frames into runs
   depends on nothing but the hot/cold pattern and `max_gap_sec`, and
   both paths walk the same frames. Batch evaluates a run once over
   `[first .. last_hot]`; at the moment the streaming detector receives
   that same `last_hot` frame its accumulated state *is* that interval,
   and it applies the same three tests. So every run batch accepts is
   accepted here at or before its last hot frame: **the streaming events
   are a superset of the batch events**, and the only possible
   divergence is an *early* emission on a prefix that qualifies while
   the whole run would not. Not observed on the library or on generated
   signals; if it ever appears, the honest fix is a confirmation delay,
   not a wider tolerance.
2. **`t_start` agrees exactly, not within a tolerance.** §9 asked for
   "within one hop"; the measured difference is **0.0000 s on all 12
   events**, because a run begins at the same frame in both paths. The
   screamer still asserts the hop tolerance and prints the number, on
   the `live_demod_equiv` principle that an exact agreement which is not
   guaranteed by construction should be reported rather than banked.
3. **The lead is 2.6–7.1 s per event, 48.75 s over twelve events** —
   this is what the section bought. The detector commits during the
   tone, and `live_tones` fails if that number is ever zero.
4. **The frames it commits on are the weakest ones, and the reported
   `purity` is not the run's quality.** On `xsg-fyci-phasing-head` the
   batch path reports purity 0.849 over the whole 9.12 s run; the
   streaming path commits at **0.391**, barely over the 0.35 threshold,
   because it decides on the opening two seconds where the tone is still
   coming up. The margin against content is intact (library content
   maxes at 0.16, so 2.4x), but a status line that shows an operator the
   live event's purity is showing them the worst of the tone, not the
   tone. §8.1's status panel should not treat it as a quality bar.
5. **No fixture exercises the gap rule, and none carries a stop tone at
   all** — see §13. Both are covered by generated signals inside the
   screamer.

---

## 6. The provisional renderer

Forward-only, single pass, never revised [docs/04 answer 5].

**The constraint that shapes everything here: the preview cannot use the
batch period estimator at all.** That estimator fits over a long
baseline and would retroactively change where already-drawn rows belong
— which is precisely the revision the decision forbids. The preview
instead runs a forward estimate: a short EMA over the last N locked
lines, seeded in this order of preference:

1. operator forced-start values (IOC + rate), or a live PHASE/SYNC
   override once set;
2. IOC from the start tone, rate from the phasing interval if one was
   seen;
3. nominal 120 lpm / IOC 576.

Per-line dead-sector relock (`autolock`, the weatherfax_pi/KiwiSDR
approach already in `core/fax.cpp`) works forward and is kept. What is
*not* available live, and must not be faked: bracketed dropout repair
(needs the far side of the run, session 12), intra-line break placement
(needs the row below, session 11b), and change-point timebase fitting
(session 9). Rows that the batch path would repair are drawn wrong in
the preview, once, and repaired in the saved image. That visible
difference is the announced swap, and it is why the pane says
"provisional" from the first row.

---

## 7. The manual override surface, and the two core fields it needs

Two numbers, everywhere, at both moments [docs/04 Finding 1, session 16].

**PHASE** — the operator reports *where the dead sector is*, never a
delta. A ruler runs along the top edge of the image pane (Furuno /
Samyung), and clicking the image sets the same value (SR-97), with a
numeric field carrying it for keyboard entry and reproducibility. Unit:
fraction of the line width, so it is independent of IOC and rate.

**SYNC** — a ppm trim on the line rate. Displayed beside the measured
value once one exists.

Behaviour differs by moment, as decided:
- *During reception*: applies forward from the next row only; drawn rows
  never move. The SR-97's "touch once and wait several lines before
  judging" caution earns a real affordance — after a live override the
  pane marks the row where it took effect.
- *After the stop tone*: a re-render from the retained snapshot,
  non-destructive and repeatable. This is the home of the remaining
  ISO §4.2.6 / §5.4.3 compliance item [docs/02].
- A live override carries into the batch re-decode — but **only into
  that transmission's**. Neither value persists between transmissions.
  This line read "and both values persist per station" until session 20
  reversed it; the reasoning is in §8.5 item 6, and it is a departure
  from the corpus [docs/04 Finding 1] with a stated cause.

### 7.1 How the live values reach the batch decode — and they do NOT behave the same way

**DECIDED 2026-08-13 (Sara, session 17).** `docs/04` said the live
override "seeds the batch re-decode as its initial anchor", which reads
as a hint the decoder may refine; the first draft of this section
specified two plain overrides that *replace* the measurement. Those are
different behaviours producing different pictures, and the ambiguity was
inherited rather than resolved. Resolved now, and the two fields differ:

**PHASE — seed, then refine locally.** When auto-phasing fails it is
usually because it picked the *wrong candidate* for the dead sector, and
the operator's click is what disambiguates which feature is which. But
that click was made through a preview drawn on a possibly-wrong period,
so it is approximate *in position*. The batch decode therefore starts
its anchor search at the operator's value and settles precisely nearby:
the operator's judgement about **which** feature, the decoder's
precision about **where**.

**SYNC — the operator's value is the fallback, not the winner.** A ppm
trim eyeballed off thirty seconds of preview will almost always be worse
than a fit over the whole transmission; sessions 5, 8 and 9 are entirely
about long baselines beating short ones, and session 5's lesson was that
both estimators were wrong precisely because their baseline was too
short. So the batch measurement wins **when it has a baseline to measure
over**, and the operator's value is used when it does not — a white-only
station, a forced start, too few locked lines. The core already knows
which case it is in: `per_line_sync == false`, or `timebase_lines == 0`,
or too few locked lines to fit.

The consequence, stated so it is not a surprise: **on a healthy
recording the operator's SYNC value will be measured away from.** That
is intended — the fit is better than the eyeball — but it means the
saved image can differ from the preview the operator just corrected by
hand, in the direction of correct.

**This requires exactly two new `DecodeOptions` fields** — the only
change M4 asks of `core/`. Named for the semantics above, so neither
reads as a plain override:

```cpp
// Where the operator says the dead sector is, as a fraction of the line
// width. The anchor search STARTS here and refines locally; it is a
// disambiguator, not a fixed answer. Negative = no hint (the default).
double phase_anchor_hint = -1.0;
// Line-rate trim in ppm, used ONLY when the period fit has no baseline
// to measure over (white-only station, forced start, too few locked
// lines). Where a baseline exists, the measurement wins. NaN = none
// (the default).
double clock_ppm_fallback = std::numeric_limits<double>::quiet_NaN();
```

Both follow the existing AUTO-as-a-value idiom — `lpm = 0`, `ioc = 0`
already mean "measure it" — which is `docs/04` Finding 2 landing
directly in the type system rather than in a mode toggle.

**One place the idiom breaks, and it is worth naming.** Zero cannot mean
auto for ppm: a perfect clock *is* 0 ppm. Hence NaN. Any other sentinel
(−1, a magic large value) would make a legal measurement unrepresentable.

**Two screamers this decision owes** (added to §9's list): a phase hint
placed near-but-not-exactly on the true anchor must still land on the
true anchor, not on the hint; and a deliberately wrong
`clock_ppm_fallback` must change nothing on a fixture whose baseline
exists, while being the value used on a white-only fixture where it does
not.

---

## 8. Window layout

Drawn to FLTK's real metrics — 25 px control rows, `FL_HELVETICA 12–14`,
boxy `FL_UP_BOX` / `FL_DOWN_FRAME` edges — in the mockup page. Regions:

```
┌──────────────────────────────────────────────────────────────┐
│ File   Settings   Help                                       │  menu
├──────────────────────────────────────────────────────────────┤
│ Device[▾] IOC[Auto▾] Rate[Auto▾] Zoom[Fit▾]  [Stop] [Force S]│  25 px
├───────────────────────────────────────────┬──────────────────┤
│ 0    200   400   600   800  1000  1200 ▲ruler (image columns)│
│ ┌───────────────────────────────────────┐ │  STATUS          │
│ │                                     ▲ │ │  mode  IOC  rate │
│ │        image pane                   ║ │ │  state  quality  │
│ │        (preview, or saved)          ▼ │ │  started (clock) │
│ │                                       │ │  label [______]  │
│ │ ◀═══════════════════════════════════▶ │ ├──────────────────┤
│ │                                       │ │  PHASE [ 412 ]   │
│ │                                       │ │  SYNC  [ +0.0 ]  │
│ └───────────────────────────────────────┘ │  [Apply] [Auto]  │
├───────────────────────────────────────────┴──────────────────┤
│ input level  ▁▂▄▆█▆▄▂▁                            −12 dBFS   │  18 px
├──────────────────────────────────────────────────────────────┤
│ DRAWING — PREVIEW          line 431        [====      ]      │  status
└──────────────────────────────────────────────────────────────┘
```

The status line carries the state name [Finding 3, Finding 4]; the
progress bar is populated **only** during `DECODING`, from the nine
stages. Saved images are a view of the user's folder — no slot table, no
LOCK, no ring [docs/04 answer 7]. The menu bar, the Zoom control, the
scrollbars and the Start/Stop relabelling are all session-18 decisions —
see §8.3.

### 8.0 Measured against real FLTK (session 18) — four corrections

The layout above was drawn in HTML at FLTK's documented metrics, which made
it a prediction. `gui/nova-gui.cpp` is the measurement, and it is inspectable
without a window: `nova-gui --metrics` prints every region's real geometry,
`--size WxH` builds at another size, `--resize WxH` puts it through FLTK's
own resize path. Four things the toolkit did not agree with.

1. **The chrome is `#c0c0c0`, not `#c6c6c6`.** That is FLTK 1.4.5's default
   `FL_BACKGROUND_COLOR` on this machine, read from the running program. The
   mockup page picked the wrong grey by six counts in each channel. Nothing
   depends on it; it is recorded so the two stop disagreeing.

2. **The ruler is aligned to the image pane's INTERIOR**, not to the left
   region and not to the pane's outer edge. The pane is an `FL_DOWN_BOX`
   with a 2 px bevel, so image column 0 is at `pane_x + 2`. The first
   version of the skeleton spanned the ruler across the whole left region
   from x = 0, which put tick 0 six pixels left of column 0 — and this ruler
   is the phase-entry affordance [docs/04, the ruler/coordinate pattern], so
   a tick that does not name the column beneath it is the one failure it
   cannot have. The ASCII above shows the ruler starting at the region edge;
   read it as starting at the pane's interior edge.

3. **The window size, which §8 never fixed: 980 x 700, minimum 740 x 420.**
   The minimum is set by the *control row*, not by the picture: captions and
   menus out to Rate occupy 548 px and Start/Force Start need 168 px against
   the right edge, so below ~720 px they collide. Worth stating because it
   is the opposite of the intuition that the image pane sets the floor.

4. **FLTK's resizable-group scaling cannot express this layout, and the
   window computes its geometry instead.** `Fl_Group::resize` scales every
   child overlapping the resizable widget's span, so `resizable(image_pane)`
   — the obvious choice, since the picture is what should absorb slack —
   dragged 980x700 to 1400x900 and stretched the Device menu from 240 px to
   370, grew the status rows from 20 px to 27, and moved the ruler to x = 7
   over a pane whose interior starts at 6, reintroducing correction 2 at
   every size but the one it was built at. The shell therefore sets no
   resizable child, uses `size_range` to stay user-resizable, and re-runs
   one `layout(W, H)` function from `resize()`. Verified: a window built at
   a size and a window dragged to it now produce byte-identical `--metrics`
   output at 740x420 and at 1400x900, and the ruler matches the pane
   interior exactly at 740, 980, 1200, 1400 and 1920 px wide.

**Registered gap:** correction 2 is a bug that came back once already, under
resize, and no test guards it. The check is three lines against `--metrics`
output and it is the recommended next screamer; it would also be the first
one to cover FLTK at all [§13].

### 8.1 What Nova cannot know, and why the survey's status line was wrong for it

**Corrected 2026-08-13 (Sara, session 17).** The first draft of this
layout showed `Freq 13920.0` and `Station VMW` in the status panel,
copied from `docs/04` Finding 4, whose table lists frequency, channel
number and station call sign as present on *all* sixteen receivers.

Sara's correction: **Nova is fed audio from a sound card. It cannot know
the frequency.** Nor the channel, nor the call sign. The whole Finding 4
field list has to be re-read in that light, because every receiver in
the corpus *contains its own radio* — the SFX-100 knows the frequency
because the SFX-100 tuned it. Nova is a decoder on the end of a cable
from someone else's receiver, and anything describing the radio is
outside its knowledge.

Sorting Finding 4's fields by what Nova can actually source:

| Field | Nova can | Source |
|---|---|---|
| Receive mode (AUTO / forced) | yes | its own state |
| IOC, line rate | yes | measured, or operator-set |
| Operating state | yes | §4 |
| Date and time of reception | yes | system clock |
| Signal quality (lock %, level) | yes | measured |
| Normal / reverse | yes | operator-set |
| **Frequency** | **no** | the radio, which Nova does not have |
| **Channel number, call sign** | **no** | same |

So the panel drops frequency entirely and replaces "station" with an
**operator-typed label** — free text, blank by default, and blank is a
legitimate value. This is not a workaround; it is Finding 7's pattern
("the operator declares the content type … nobody tries to detect it")
applied to identity instead of content.

This is why an audio-only decoder cannot inherit a whole receiver's
status line uncritically, and it is worth carrying into M4.5: a
frequency readout only becomes possible if Nova ever gains CAT control
of a receiver, which is not on the roadmap.

### 8.2 Which picture owns the pane while an edit is in progress

**DECIDED 2026-08-13 (Sara, session 17): the edit holds the pane.**

The retention rule (§3) keeps both raw streams, so nothing is *lost*
when a transmission arrives mid-edit. But one image pane cannot show the
operator's edit and the incoming live preview at the same time, and §8's
"one pane, announced swap" was written for two participants, not three.

The incoming transmission draws into its **background buffer**, and the
status area carries a compact receiving indicator — state name, line
count, small thumbnail — that switches to the live view when clicked.
Nothing interrupts a human mid-correction, and nothing is lost by
waiting, because the raw is retained either way; when the operator
switches or finishes, the buffered picture comes forward under the same
announced-swap rule as before.

Two things make this cheap rather than a feature:

- **The thread design already paid for it.** The provisional renderer
  never draws to a widget — it pushes `RowsDrawn` through the GUI queue
  (§2.3), and the FLTK thread decides where those rows land. Rendering
  into a buffer that is not currently displayed costs nothing extra;
  the renderer does not know or care whether anyone is looking.
- **Parking an edit is two numbers.** PHASE and SYNC are the entire
  edit state (§7), so switching away and back is not the state-machine
  problem it would be in an application with a real editing session.

Rejected: letting the new transmission take the pane automatically.
That is what a receiver with one sheet of paper has to do, and it would
interrupt the operator during the one interaction ISO §4.2.6 exists to
guarantee.

**The spectrum/waterfall is not in M4** [DECIDED 2026-08-13, Sara,
session 17]. It ships in M4.5. What stays in its place is a slim input
level meter, and the distinction is worth stating because it is not the
same cut: without a level readout, an operator whose audio input is
muted, clipping, or pointed at the wrong device has no diagnosis at all
— every failure looks like "no signal". The meter is also the one thing
here with direct precedent, since the FAX-30 shows signal strength and
S/N while receiving [docs/04 Finding 4]. **No receiver in the sixteen-
manual corpus has a waterfall**; it is an SDR-era affordance for tuning,
which is exactly the job M4 is deferring.

### 8.3 The picture area, the transport, and the desktop chrome

**Eight questions asked by Sara on seeing the skeleton, all DECIDED
2026-08-13 (Sara, session 18).** They are grouped here because the first
three are one mechanism. Two measurements shaped the answers, both taken
from the real decoder rather than assumed:

| IOC | Image width | Source |
|---|---|---|
| 576 | **1810 px** | `nova-decode` on `test-chart-jmh-kiwisdr-image-60s.wav` |
| 288 | **905 px** | `nova-decode` on a synthetic `nova-gen --ioc 288` file |

Width is `round(IOC × π)`. The image pane at the default 980 px window is
772 px wide, so **an IOC 576 chart does not fit at 100% — it fits at about
43%**, and a ten-minute chart at 120 lpm is ~1200 lines against a 613 px
pane. Everything below follows from those two numbers.

**1. The ruler reads in IMAGE COLUMNS: 0–1809 at IOC 576, 0–904 at IOC
288.** This is not a presentation choice. PHASE is a column position in
the decoded image [§7], so a ruler in any other unit cannot be the
phase-entry surface [docs/04 answer 8]. Two consequences:

- **Tick spacing is chosen from the displayed scale, not fixed**: the
  smallest step in {10, 20, 50, 100, 200, 500} image columns that leaves
  ≥ 40 px between labels on screen. At 200% it is 20 columns. At Fit it
  depends on the window, which the example as first written did not say:
  200 columns at and near the ~880 px minimum window, 100 at the 980 px
  default (pinned by `ruler_mapping`, session 19).
- **While IOC is unknown the ruler is blank and disabled.** In AUTO,
  before a start tone, Nova does not know whether the chart is 1810 or
  905 columns wide, and a ruler drawn on a guess would be a lie in the
  one place a lie is most expensive. This is the same rule §4 already
  applies to the clock and timebase readouts: blank until measured.

**2. Zoom is a five-value list — Fit (default), 25%, 50%, 100%, 200%.**
"Fit" is a value in the same dropdown, never a separate checkbox: that is
docs/04 Finding 2's AUTO-is-a-value pattern, already used for IOC and
Rate. The range extends *below* Fit and *above* 100% for opposite
reasons. Below, because a 1810-column chart at 100% shows 43% of itself.
Above, because at Fit one screen pixel is 2.3 image columns, and PHASE
placement is a per-column judgement — the operator has to be able to get
close enough to see the dead-sector edge they are clicking on.

**3. Scrollbars appear only when the image exceeds the pane**, in both
axes, which above Fit is almost always. **The ruler tracks zoom AND
horizontal scroll.** This changes the shape of the ruler's correctness
claim, and for the better: session 18 fixed "the ruler's left edge equals
the pane's interior left edge" [§8.0 correction 2], but with zoom and
scroll the real invariant is

> the image column under the cursor is the column the ruler names there,

which holds at every zoom, every scroll offset and both IOCs, and which
is testable without a window from the same mapping function the click
handler uses. That is the M4 screamer this whole area needs [§9].

**4. Start becomes Stop while receiving**, one button relabelled by
state, with Force Start insensitive during reception. What Stop *means*
is the load-bearing half: docs/04 Finding 6 says a transmission ends in
exactly three ways and operator stop is the first of them, with the
SR-97 then holding the image at a `SAVE?` prompt — **stop does not mean
discard.** So operator stop runs the same end-of-transmission path as a
stop tone: freeze the snapshot, `DRAWING — PREVIEW` → `DECODING`, batch
decode, save. It declares that the transmission ended here, not that its
picture is worthless.

**5. No waterfall region is reserved in the sidebar.** The sidebar is
200 px and a waterfall wants width for a frequency axis; its home in M4.5
is the full-width strip where the level meter already sits. Reserving
space early buys nothing, because §8's layout is computed from constants
in one `layout(W, H)` function — adding a region later is an edit, not a
redesign. And the sidebar's empty lower area is **already spoken for**:
it is where §8.2's compact receiving indicator (state, line count,
thumbnail) goes when an edit holds the pane.

**6. No autosave toggle: every completed transmission is saved.** This
follows the retention decision already taken [docs/04 answer 7 —
unbounded, user-set folder, no ring, no LOCK]. A toggle would create a
mode in which a transmission is lost, and an unsaved chart is the one
failure an operator cannot undo. The corpus's `SAVE?` prompts exist to
protect a 12-to-200-image ring, which is the same 1990s constraint that
already argued the ring buffer and the LOCK control away. Deleting is a
file operation, because the image list is a view of a folder.

**7. Settings sets the folder; the file type is not a choice.** The
folder was decided as "user-set" and the skeleton has nowhere to set it,
which is a real hole. The format stays **greyscale PNG only** [session
16]: BMP is a second writer to build and test, produces larger files, and
cannot carry metadata — whereas PNG text chunks are where Nova's decode
QA belongs (anchor source, clock ppm, timebase verdict), which has direct
precedent in the Furunos printing a `Phase OK` / `Phase NG` header on
every chart [docs/04 Finding 4 additions].

**8. An About item, and it is not decoration.** Nova is GPLv3+; About is
where the licence and no-warranty notice live, with a pointer to `NOTICE`
for the DSP reuse attributions the provenance rule requires.

**Its content, approved verbatim by Sara (session 19)** — the coding
session copies this text rather than inventing its own. The middle two
paragraphs follow the GPL's "how to apply" boilerplate deliberately; the
standards line keeps the "design target, no certified-compliance claim"
qualifier because dropping it would change what Nova promises; the
NOTICE pointer is what the provenance rule requires to be reachable from
the program itself. No version number until versioning exists (a release
question), and no mention of Isobar or KG-FAX — Nova is standards-first,
and its lineage story lives in `NOTICE`/`docs/00`, not here.

> **Nova** — an HF weather facsimile (WEFAX) decoder
>
> Copyright © 2026 Nova contributors
>
> Nova is free software: you can redistribute it and/or modify it under
> the terms of the GNU General Public License as published by the Free
> Software Foundation, either version 3 of the License, or (at your
> option) any later version.
>
> Nova is distributed in the hope that it will be useful, but WITHOUT ANY
> WARRANTY; without even the implied warranty of MERCHANTABILITY or
> FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
> for more details — the full text is in the LICENSE file distributed
> with this program.
>
> Built from public standards: WMO-No. 386 Vol. I Part III §5 (the
> signal) and ISO 9876:2015 §4.2 (receiver behaviour as a design target —
> no certified-compliance claim).
>
> DSP reuse attributions and linked-library licences (FLTK, RtAudio):
> see the NOTICE file.

**Where 7 and 8 live: a menu bar — File / Settings / Help — above the
control row.** No receiver in the corpus has one, and that is not an
objection: the survey constrains the *picture-correction* surface, which
is what those machines and Nova genuinely share, not whether a desktop
application has desktop chrome. Buttons were rejected because the control
row is already the width constraint on the whole window.

**The one metric consequence, stated because §8.0 correction 3 says the
control row sets the window's minimum width.** Adding `Zoom [Fit▾]`
costs about 120 px there, so the minimum window width rises from 740 px
to roughly 880. The picture still does not set the floor; the control row
does, now more than before.

### 8.4 Settings persistence, zoom-scroll, and the transport cycle

**Five questions raised by Sara while looking at the skeleton (session
19), all DECIDED 2026-08-13.** They are grouped here because they are all
about how the window behaves over time rather than where anything sits.

**1. Settings persist in a preference file next to the program.** A plain
preference file in the same directory as the executable — visible,
inspectable, movable with the program, and no hidden platform store.
Recorded consequence: if that directory is not writable (a system-wide
install), Nova runs without persistence for the session rather than
failing — settings are a convenience, never a precondition.

What it holds is **the image folder** [§8.3 item 7], and that is all. As
first written this item also gave it the per-station PHASE/SYNC memory of
§7; **session 20 removed that** — the folder is a path the operator
chose, while PHASE and SYNC are measurements of a signal, and only the
first is a preference. See §8.5 item 6.

**2. Zoom keeps the left edge.** On a zoom change, the image column at
the pane's left edge stays where it was; the scroll position is
preserved and clamped if the new scale shrinks the range. No re-centering
on the clicked feature, no jump to the start — the operator's frame of
reference does not move.

**3. Force Start requires explicit IOC and rate.** With both dropdowns
set to numbers, Force Start begins drawing immediately, skipping tone
detection [§4]. With either on Auto it cannot start — the operator must
pick values first. Mechanics: the button is **insensitive until both are
explicit**, the same deactivate-rather-than-fake rule the rest of the
shell follows [§3's PHASE/SYNC rule], rather than a click that opens a
prompt.

**4. The button during DECODING is insensitive, reading "Start".**
[DECIDED 2026-08-13, Sara, session 19.] Two things were settled to get
there. First, the button never reads a state name — "Decoding" is a
state, and states live in the status line [docs/04 Finding 3]; a button
that narrates is a fake control, and the progress bar exists precisely
because DECODING is the one state long enough to deserve it. Second, the
first version of the GUI is **serialized**: the button is greyed while
the batch decode runs and active again at SAVED, so one thing happens at
a time. The alternative — an active "Start" letting the next reception
begin monitoring while the previous decode still runs — stays
architecturally available (the thread design in §2 permits it, and §8.2
already accepts overlap elsewhere), but it would make the state machine
two-track, and that complexity is not bought yet.

**5. The ruler appears suddenly when AUTO resolves the IOC** — accepted
as designed. It is blank and disabled until the start tone, then lights
up with no transition and no announcement, the same blank-until-measured
rule the clock and timebase readouts already follow [§4].

### 8.5 The save/edit lifecycle: when the file is written, and what an edit owns

**Six questions raised by Sara while the §8.3 surfaces were being coded
(session 20).** Two of them turned out to be already answered elsewhere
in this document and are restated here rather than pointed at, because
the answers were spread across §3, §4, §8.2 and §8.3 item 6 and nobody
could see them in one place. Three were open. The sixth **reverses a
decision this document had already taken twice**, and is written at
length for that reason. All are **DECIDED 2026-08-13 (Sara)**.

**1. When is the picture saved automatically? At the end of the batch
decode — before any editing is possible.** `DECODING` completing writes
the image to the folder and the status line reads `SAVED` [§4]. There is
no prompt and no autosave toggle: every completed transmission is saved,
because an unsaved chart is the one failure an operator cannot undo
[§8.3 item 6]. The file is named from the system clock timestamp [§12
item 5]. By the time a corrected image is on screen, the automatic
version is already on disk.

**2. What does a re-render after an edit do to that file? It overwrites
it — one transmission, one file.** [DECIDED 2026-08-13, Sara.] The
corrected image is the same transmission, and the image folder *is* the
image list — no ring, no slot table, no LOCK [docs/04 answer 7] — so a
second file per correction would turn one chart into four or five
near-identical files the operator then has to weed. Rejected
alternatives: a new timestamped file per Apply, and writing nothing until
asked.

**Recorded consequence, stated because it is the real cost.** The
automatic version is reproducible only while the raw snapshot is still
retained: Auto restores the measured values and re-renders it. Once the
operator moves on and §3 releases that snapshot, the pre-edit image is
genuinely gone. That is accepted — by then the operator has decided the
edit is the version they want — and it is the same trade §3 already made
when it refused to write a raw sidecar.

**3. Is there a Save button after editing? No — Apply re-renders AND
writes.** [DECIDED 2026-08-13, Sara.] The file on disk always matches
what is on the screen. A Save button would restore precisely the failure
mode item 6 removed: a chart corrected, approved, and then closed with
the good version only ever in memory. The corpus's `SAVE?` prompts
protect a 12-to-200-image ring that Nova does not have. The panel's
controls therefore stay `[Apply] [Auto]`, as drawn in §8.

This composes with decision 2 rather than fighting it: hunting for the
right PHASE across five Applies writes the same path five times, which
costs nothing. Had the answer been a new file per Apply, a Save button
would have started to make sense, because each write would then be a
deliberate act — the two questions have one joint answer, not two
independent ones.

**Recorded consequence for the saved metadata.** A re-render's PNG text
chunks must record PHASE/SYNC as **operator-supplied** rather than
measured [§8.3 item 7 puts the decode QA there]. Overwriting otherwise
leaves a file whose metadata claims a provenance the pixels no longer
have, and the Furunos printing `Phase OK` / `Phase NG` on every chart are
the precedent for the header telling the truth about how the picture was
obtained [docs/04 Finding 4 additions].

**4. What counts as "an edit in progress"? Dirty controls, not a mode.**
[DECIDED 2026-08-13, Sara.] An edit **begins** at the first change to
PHASE or SYNC, or the first click on the image, and **ends** at Apply, at
Auto, or when the operator switches to the live view. This is the
boundary §8.2's "the edit holds the pane" was missing: without it, the
hold would protect only the instant while Apply runs, which protects
nothing. A typed-but-not-applied PHASE value holds the pane.

Two behaviours follow, and both were asked directly:

- **A new transmission arriving mid-edit does not take the pane** [§8.2,
  session 17]. It draws into its background buffer behind the compact
  receiving indicator — state, line count, thumbnail — which switches the
  pane when clicked. Nothing is lost, because §3 retains two raw
  snapshots by role: the transmission being received, and the image being
  displayed. When the edit ends by the rule above, the buffered picture
  comes forward under the announced-swap rule.
- **With no edit in progress, the next transmission is received
  automatically** [§4]. After `SAVED` Nova is still capturing and
  monitoring; the next start tone runs the normal path and the new
  preview takes the pane. Only operator Stop returns it to `IDLE`. The
  §8.2 protection is for an edit, not for an idle picture on screen.

**5. What names the file when the operator label is blank? The timestamp
alone.** [DECIDED 2026-08-13, Sara.] Blank is a legitimate value for the
label [§8.1], so it produces no placeholder, no `unlabelled`, and no
prompt — it produces a shorter filename:

| Label | File |
|---|---|
| blank | `20260813T220417Z.png` |
| `JMH` | `20260813T220417Z-JMH.png` |

The timestamp comes first and always, in UTC, to seconds. Chronological
order is then alphabetical order, which is what an unbounded folder with
no slot table needs; seconds make a collision impossible between two
charts in one minute; and there are no colons in it, because Nova is
cross-platform and `:` cannot appear in a Windows filename.

**The label is sanitized before it reaches a filename.** It is free text
— it can hold `/`, `:`, quotes, newlines, or two hundred characters.
Rule: anything in `\ / : * ? " < > |` and every run of whitespace becomes
a single `-`, the result is trimmed and capped at 32 characters, and if
nothing survives it is treated as blank. The full label goes into the PNG
text chunks regardless, so a conservative filename loses nothing.

**Nova never renames a saved file.** The name is fixed at the automatic
save. A label typed or changed afterwards reaches the PNG metadata on the
next Apply, and the file keeps the name it was written with. This is the
idiom §8.3 item 6 already established — *deleting is a file operation,
because the image list is a view of a folder* — and renaming is the same
category: the operator does it in their file manager. The alternative,
Apply renaming the file to match a changed label, was rejected for adding
a second way a file can move and a failure path (a rename that fails)
in exchange for tidiness in the minority case. The label identifies the
station being received and is typed before or during reception, not
after it.

**6. Is the per-station PHASE/SYNC memory worth storing at all? No — it
is removed.** [DECIDED 2026-08-13, Sara, session 20. **This reverses a
decision**, so it is written at length.] PHASE and SYNC reset to
measured-or-blank for every transmission. A live override still carries
into *that transmission's* batch re-decode under the asymmetry of §7.1,
which is untouched; what disappears is any memory between transmissions.
The preference file keeps the image folder and nothing else.

Sara raised it while reviewing what the preference file did not yet do,
and the question was the right one: *is this realistic to store?* Four
reasons say no, in ascending order of force.

- **The key is unstable free text.** One station is "JMH" tonight, "JMA"
  next week and "Japan" the week after, and blank is a legitimate value
  [§8.1]. The memory then silently misses — which is the *harmless*
  failure.
- **The receive chain changes the number, and the key does not notice.**
  Own radio, local SDR, KiwiSDR are three different sample clocks. Here
  the label still matches, so a wrong value loads confidently. That is
  the harmful failure, and it is the ordinary case for an SDR operator.
- **There is no correct key available.** `clock_ppm` is the sum of the
  transmitter's clock offset and the receive chain's — sessions 5 and 8
  measured a −66 to −114 ppm family across the library, and the two
  recordings of one station that agree to ~1 ppm were made on the *same*
  receiver. The value is therefore a property of the **(transmitter,
  receive chain) pair**, and a station label cannot name a pair. This is
  not a key that could be fixed by choosing a better one; the operator
  does not have the second half of it.
- **A stale PHASE is not merely useless, it is harmful — and §7.1 is why
  nobody saw it.** SYNC is a *fallback*, so a wrong remembered SYNC is
  ignored wherever the batch fit has a baseline. PHASE is a *seed* for
  the anchor search, which starts at the operator's value and refines
  locally; and PHASE's numeric position depends on when the capture began
  relative to the transmitter's line clock, which differs every
  reception. A remembered PHASE is therefore a seed aimed at last week's
  arbitrary line phase, and it can pull the search onto the wrong
  candidate feature — **the exact failure PHASE exists to correct**. The
  asymmetry that §7.1 established for the batch path turns out to decide
  the persistence question too, in opposite directions for the two
  values.

Add that auto re-measures from the phasing interval on every
transmission, and the mechanism's best case is a saved keystroke in a
narrow case, paid for with a confidently wrong number in a common one.

**This is a departure from the corpus, which is why it needs a cause.**
`docs/04` Finding 1 records that both values are per-station and persist:
the SR-97 stores the slant setting independently per station, and the
JAX-9B warns that SYNC holds for a station and usually needs
re-adjustment when the station changes. Those machines can do it because
of two properties Nova lacks, and both are the §8.1 insight again —
*every receiver in the corpus contains its own radio*:

1. their key is a machine-generated **channel or station preset**, not
   free text an operator retypes; and
2. their receive chain is **fixed** — one box, one clock — so the stored
   number cannot be invalidated by changing hardware.

Nova is a decoder on the end of a cable from someone else's receiver, so
it has neither. Finding 1's own sentence anticipated the outcome without
drawing it: *"Nova measures this per transmission, so it inherits the
benefit for free."* It does — and that is the whole benefit, obtained
without a memory.

**The accepted cost, stated so it is not hidden.** An operator on fixed
hardware receiving a white-only station nightly, who trims SYNC by eye
each time, will retype it each time. If that ever becomes a real
complaint, the correctly-keyed form is *per input device*, measured
automatically from a good decode rather than typed — and even that is
imperfect, because the number still contains the transmitter's share.
Not built, not scheduled, recorded here so the option is not rediscovered
from scratch.

**Consequence for §8.1's operator label.** The label loses its second
job. It was justified partly as the key for this memory [§12 item 5]; it
keeps its first and real job, which is telling the operator which chart
they are looking at, in the status panel and in the saved filename
[§8.5 item 5].

**The closing note this section owes the document.** "No design question
remains open" has now been written four times — sessions 17, 18, 19 and
here — and each time it was true of the questions then visible. Session
19's paragraph said paper closes the questions it can see, and code and
windows find the rest. Session 20 sharpens that: the four questions above
were not found by *looking* at a window, they were found by Sara asking
what happens over the whole life of one chart — arriving, being saved,
being corrected, being replaced. That is a dimension neither a static
mockup nor a running skeleton exhibits, and it is worth expecting the
next set to come from the same place: the parts of the story the current
artefact cannot act out.

---

## 9. How M4 gets screamers

Six, all runnable with no audio device and no window, all reusing the
existing 20 fixtures:

1. **`live_demod_equiv`** — block-with-overlap streaming
   resample+demod over a fixture equals the whole-file result, sample for
   sample, at every block size in a set. Pins §2.2.
   **[BUILT session 20: `live/stream.{hpp,cpp}` +
   `tests/test_live_equiv.cpp`, running unguarded in every build. Eleven
   block sizes from 1 sample to 44100, over a real recording at 8 kHz
   and generated signals at 44100 and 48000 — the fixtures are all at
   8 kHz and cannot exercise a resampler at all. Measured: the demod is
   bit-identical, the resampler agrees to 5e-13, counts match everywhere,
   and the overlap requirement is 62 rather than the 63 the tap count
   predicts. See §2.2.]**
2. **`live_tones`** — the streaming detector finds the same tone *kinds*
   in the same order, with start times within one hop, as `detect_tones`
   on every tone fixture. Pins §5, with the medians explicitly excluded
   per §5.
   **[BUILT session 20: `live/tone_stream.{hpp,cpp}` +
   `tests/test_live_tones.cpp`, running unguarded in every build. All 17
   fixtures, not only the six carrying a tone — the other eleven are the
   M3 false-start trap in a second implementation. 136 checks. Measured:
   same kinds and counts everywhere, `t_start` identical to 0.0000 s,
   the event list bit-identical at every block size from 1 sample to
   65536, and 12/12 events committed early (48.75 s of total lead).
   Three cases no fixture can provide are generated inside the test: the
   stop tone, the IOC 288 opening, and a tone that fades mid-run.
   Verified by mutation, five of them: dropping the one-event-per-run
   guard, shifting the frame grid by one sample, halving the
   gap-bridging tolerance, dropping `min_hot_frac`, and emitting only at
   the run's end each fail it. Two of those five passed against an
   earlier version of the test and are why the last two generated cases
   exist — see §10.]**
3. **`live_preview`** — feeding a fixture through the preview renderer
   produces an image of the expected dimensions whose dead-sector edge is
   within a stated tolerance, and — the real claim — **is bit-identical
   whatever the block size**. A preview that depends on how the audio
   callback happened to chunk the stream is broken.
4. **`png_roundtrip`** — the hand-rolled writer's output decodes back to
   the source pixels (checked against an independent decoder, e.g.
   Python/`sips`, in the test), and the file is a valid PNG.
5. **`override_phase_seed`** — a `phase_anchor_hint` set near but not
   exactly on the true anchor lands the picture on the *true* anchor,
   not on the hint; and a hint pointing at the wrong candidate feature
   moves the picture to that feature. Pins §7.1's "seed, then refine".
6. **`override_sync_fallback`** — a deliberately wrong
   `clock_ppm_fallback` changes nothing on a fixture whose baseline
   exists, and *is* the value used on a white-only fixture where it does
   not. Pins §7.1's "fallback, not winner", which is the half of the
   decision most likely to be quietly implemented as a plain override.

Two more, added session 18. Both are cheap, and both exist because the
skeleton found the same layout bug twice by two different mechanisms:

7. **`gui_layout`** — from `nova-gui --metrics`, the ruler's x and width
   equal the image pane's interior (`x + 2`, `w - 4`) at several window
   sizes, and a window *built* at a size produces output identical to one
   *dragged* to it (`--size` vs `--resize`). Pins §8.0 corrections 2 and
   4. Guarded by `NOVA_BUILD_GUI` — it is the +1 in the suite count.
   **[BUILT session 19: `tests/gui_layout.cmake`, driving the binary
   through `cmake -P`.]**
8. **`ruler_mapping`** — the stronger form of the same claim, and the one
   §8.3 needs: **the image column under a given screen x is the column
   the ruler names at that x**, at every zoom value, every horizontal
   scroll offset, and both IOC widths (1810 and 905). It tests the
   mapping function the click handler uses, so it needs no window and no
   audio device — which is also the argument for that mapping being a
   pure function in `nova-live` rather than arithmetic inside a widget.
   **[BUILT session 19: `live/ruler.{hpp,cpp}` + `tests/
   test_ruler_mapping.cpp`, running unguarded in every build. Extended
   session 20 with §8.4 item 2's left-edge retention, `rezoomed()`.]**

One more, added session 20 while the §8.3 surfaces were being written,
because `gui_layout` pins where the regions *are* and nothing pinned what
the shell *does* — and the behaviour rules are precisely the ones a
widget edit can break without moving a pixel:

9. **`gui_shell`** — from `nova-gui --metrics --state NAME`: the button
   relabelled by state and never reading a state name, insensitive during
   DECODING and active again at SAVED; Force Start gated on IOC *and*
   rate being explicit; the ruler blank and disabled until the image
   width is known, and lit with the right width and tick step when it is;
   the transport inert on a plain run, because nothing can capture yet;
   and the preference file beside the program read at startup without an
   inspection run creating one. Guarded by `NOVA_BUILD_GUI`.
   **[BUILT session 20: `tests/gui_shell.cmake`, sharing its `--metrics`
   parser with `gui_layout` via `tests/gui_metrics.cmake`. Verified by
   mutation: making Start sensitive during DECODING, and lighting the
   ruler with the width unknown, each fail it.]**

The suite count is now **"26 (+2 with the GUI)"** — `live_tones` is the
third unguarded `nova-live` test. It is also the slowest test in the
suite at 25 s of the 122 s total, and the cost is the block-size sweep:
seven block sizes over seventeen fixtures, nine detector passes per
signal. That is the price of the "identical whatever the blocking"
claim, and it is worth saying out loud so a future session can trade it
knowingly rather than discover it. Session 19 decided
"24 (+1 with the GUI)" — correcting session 18's "23 (+1)" — on the
argument that a test of dependency-free `nova-live` code should run
everywhere. That argument is unchanged and now applies twice: item 9 is
a second guarded GUI test (+1 → +2), and item 1 is a second unguarded
`nova-live` test (24 → 25). Sara should say if she would rather the two
GUI scripts were one ctest target to keep the "+1".

Registered as a gap up front: **nothing here tests RtAudio, and with
screamers 7, 8 and 9 built (sessions 19–20) the FLTK gap is narrower
again but not closed.** Device enumeration and callback behaviour under
xrun are still verified by running the app, not by a suite; so is
anything that only exists once it is *drawn* — the ruler's ticks are
pinned as numbers by `ruler_mapping` and as sensitivity by `gui_shell`,
but no test looks at a pixel of them. That is the boundary the
three-layer split in §1 is drawn to make small.

---

## 10. Contradictions found

**One, and it matters.** `docs/04` Finding 3 states that the receivers'
protocol narration is "a one-to-one match with Nova's nine decode
stages (session 14) — onset, dead-sector, phasing, sync-track,
period-fit, segmentation, timebase, change-points, assembly."

Under the two-path decision taken one section later in the same document
[answer 1], it is not. The nine stages are *batch analysis* stages, and
after answer 1 they all run **after** the transmission has ended. The
receivers' states (`WEFAX READY` → `APT DETECTED` → `SYNCHRONIZING` →
drawing) describe *where in the transmission the incoming signal is* —
which is the live machine in §4, a different thing on a different
timeline. Taking the "one-to-one" literally would put `change-points` on
screen as a protocol state while the operator waits for a picture.

Resolved as: the §4 states are the live narration; the nine stages are
sub-progress inside `DECODING`, which is the one state that lasts long
enough to deserve a bar. Both halves of Finding 3 survive — the state
*names* belong on screen, and the session-14 progress callback is the
seam that feeds them — only the claimed identity does not.

**A second one, found by Sara, and it is the more embarrassing of the
two.** `docs/04` Finding 4 pools the status-line fields of sixteen
receivers and lists frequency, channel and call sign as present on all
of them. This document copied that into §8 without asking whether Nova
*can* know them. It cannot: every receiver in the corpus contains its
own radio, and Nova is a decoder fed line audio from someone else's.
Corrected in §8.1 — frequency and channel are dropped, station becomes
an operator-typed label.

The general lesson, worth more than the specific fix: **the survey
corpus were whole receivers, and Nova is one component of one.** Any
finding about what appears on screen has to be filtered through what an
audio-only decoder can source. Findings 1, 2, 3, 5, 6 and 7 survive that
filter unchanged; Finding 4 did not, and Finding 8 (scheduling by
channel and frequency) will not either, whenever it is picked up.

**A third, session 20, and it was in a screamer rather than in the
design.** §9's `live_tones` was written and passed on its first run,
against every fixture in the library. Mutation testing found that two of
five deliberate breakages passed it unharmed:

- a **frame grid shifted by one sample** passed, because the test asked
  the detector for its own window length and then confirmed the frame
  count agreed with it. That is a test of arithmetic, not of the code.
  The expected window and hop are now formed in the test from
  `ToneOptions` by the same expressions `core/tones.cpp` uses;
- **halving the gap-bridging tolerance** changed no verdict anywhere,
  because all six library fixtures that carry a control tone carry a
  clean one. Nothing in the library fades mid-tone, so the run-assembly
  rule that §5 is *entirely about* was untested in the streaming path.
  Covered now by a generated tone with a 1.5 s fade placed early enough
  to matter, plus a two-burst case for the opposite rule.

**The lesson for the next agent, and it is the same shape as session 5's
`locked_lines`:** a screamer that passes on the first run has not been
shown to be able to fail. Both survivors were invisible to a green
suite, and one of them hid a real coverage hole in the fixture library
rather than a flaw in the test's wiring.

No other contradiction found between `docs/03`, `docs/04`, `ROADMAP.md`
and the code as it stands.

---

## 11. Dependencies, and what they cost M5

`nova-core` gains **no** dependency: no `find_package`, no link. The PNG
writer is hand-rolled [decided session 16] and lives in `nova-live`,
which also stays dependency-free.

`nova-gui` alone links FLTK and RtAudio. Proposed CMake shape:

```cmake
option(NOVA_BUILD_GUI "Build the FLTK/RtAudio shell" ON)
# ...find FLTK + RtAudio; if either is missing, warn and skip the target,
# never fail the build. Tests and CLIs must build with NOVA_BUILD_GUI=OFF.
```

**Built session 18, and two details the proposal did not anticipate.**

- **Neither library ships a CMake config package** under Homebrew (FLTK
  1.4.5, RtAudio 6.0.1), so `find_package` is not the way in. Both ship the
  interface they document instead: `fltk-config` and a pkg-config `.pc`
  file. The block uses `find_program(fltk-config)` and
  `pkg_check_modules(rtaudio)`, and either one missing prints
  `nova-gui: SKIPPED - ...` and configures successfully.
- **`target_link_options` corrupts FLTK's link line.** `fltk-config
  --ldflags` ends in `-weak_framework UniformTypeIdentifiers
  -weak_framework ScreenCaptureKit`; CMake de-duplicates the repeated
  `-weak_framework` token and hands the linker a bare `ScreenCaptureKit`,
  which fails as a missing file. The flags go through the `LINK_FLAGS`
  string property instead, which is passed verbatim.

Verified: `NOVA_BUILD_GUI=OFF` builds the three CLIs and all 23 test suites,
which pass, and produces no `nova-gui`.

M5 consequence, stated now rather than discovered at packaging: the
tier-1/tier-2 target matrix stays cheap for the CLIs and the test suite,
which remain dependency-free everywhere; the FLTK/RtAudio burden lands on
the GUI binary on the three tier-1 platforms only. `ROADMAP.md`'s "Nova
has no external dependencies" is true of the core and the CLIs after M4,
and false of the GUI — that sentence needs the qualifier.

---

## 12. Open questions for Sara

All five original questions are now answered. One new question, raised by
Sara's answer to item 2, is open at the end.

1. ~~**Capture device sample rate.**~~ **DECIDED 2026-08-13 (Sara):
   accept whatever the device offers and resample to 8 kHz.** It is what
   the file path already does, so this is proven code rather than new
   code; a device that will not give exactly 8 kHz has no reason to be
   refused.
2. ~~**Retention policy (§3).**~~ **DECIDED 2026-08-13 (Sara): no raw
   sidecar, and the "cannot re-phase a three-hour-old image"
   consequence is accepted** — *with a correction from Sara that changed
   the rule.* She asked what happens if the operator is adjusting the
   image that just arrived when the next transmission starts. The
   original "current image only" would have released the raw stream out
   from under a live edit. §3 now retains **two** snapshots, by role —
   the transmission being received, and the image being displayed —
   bounded at two however long the edit lasts. See §3.
3. ~~**Page cap default.**~~ **DECIDED 2026-08-13 (Sara): 1 page.** Stop
   at the first stop tone; the cap exists purely as the guard for when
   that tone is missed. Matches Nova's existing offline behaviour of
   taking the first transmission and dropping the rest.
4. ~~**Does M4 ship the waterfall?**~~ **DECIDED 2026-08-13 (Sara,
   session 17): it ships in M4.5.** A slim input level meter stays in
   M4 — see §8 for why that is a different thing and not a partial
   reversal of the cut. Nothing else in this document changes: the
   waterfall was the one region that served tuning rather than decoding,
   and no thread, seam, screamer or core field depended on it.
5. ~~**Station identity.**~~ **DECIDED 2026-08-13 (Sara): frequency is
   not available at all**, because Nova is fed audio from a sound card
   and never sees the radio. This retired the question as posed — the
   proposed key did not exist. Resolved as an **operator-typed label**,
   blank by default and legitimately blank; the timestamp comes from the
   system clock and names the file. See §8.1, which corrects the status
   panel this mistake had already reached. *(Session 20: this item also
   gave the label a second job, keying the per-station PHASE/SYNC memory.
   That memory is gone — item 25 — and the label keeps only its first
   job, which was always the real one.)*

### Raised by the answer to item 2, and closed the same day

6. ~~**Which picture owns the pane while an edit is in progress?**~~
   **DECIDED 2026-08-13 (Sara, session 17): the edit holds the pane.**
   The incoming transmission draws into its background buffer behind a
   compact receiving indicator that switches on click. See §8.2 for why
   this costs nothing architecturally — the provisional renderer already
   pushes rows through the GUI queue rather than drawing to a widget, and
   the entire edit state is two numbers.

### Raised by Sara on seeing the skeleton, and closed the same day

**"No design question remains open" was true when session 17 wrote it and
stopped being true the moment there was something to look at.** Eight
questions followed from a window with nothing behind it, five of them
about surfaces this document had never specified at all — zoom, scrolling,
manual stop, the settings folder, About. All are **DECIDED 2026-08-13
(Sara, session 18)** and written up in §8.3; they are listed here so the
question set stays in one place.

7. ~~**What is the ruler's range, given both IOC 576 and IOC 288?**~~
   Image columns, 0–1809 and 0–904 respectively (width = `round(IOC × π)`,
   measured). Blank and disabled while IOC is unknown.
8. ~~**Should the picture area zoom, and over what range?**~~ Fit
   (default), 25%, 50%, 100%, 200% — Fit as a value in the list. The
   question assumed 100% fits; it does not, and that reframed the answer.
9. ~~**Scrollbars when the image exceeds the pane? Does the ruler follow
   the zoom?**~~ Yes and yes, and the ruler follows the horizontal scroll
   too — which upgrades §8.0's edge-alignment invariant into a mapping
   invariant, and gives §9 screamer 8.
10. ~~**How does the operator stop a reception in progress?**~~ Start
    becomes Stop; stop runs the full end-of-transmission path and saves.
    Stop does not mean discard [docs/04 Finding 6].
11. ~~**Reserve sidebar space for the M4.5 waterfall?**~~ No — wrong
    shape, wrong place, and the sidebar's spare area is already §8.2's.
12. ~~**An autosave on/off control?**~~ No; every completed transmission
    is saved, which is the retention decision already taken.
13. ~~**A settings dialog for the folder and the file type?**~~ Folder
    yes, file type no — greyscale PNG only.
14. ~~**An About box?**~~ Yes; GPLv3+ makes it load-bearing rather than
    decorative. It and Settings live in a File / Settings / Help menu bar.

### Raised by Sara preparing the §8.3 coding session (session 19)

Four decided the day they were asked, one open; all written up in §8.4.

15. ~~**Where do settings persist between launches?**~~ A plain
    preference file in the same directory as the executable; a
    non-writable directory means no persistence for the session, never a
    failure. *(Session 20: what it holds narrowed to the image folder
    alone — item 25.)*
16. ~~**What happens to the scroll position on a zoom change?**~~ The
    left edge is kept; no re-centering, no jump to the start.
17. ~~**Force Start with the dropdowns on Auto?**~~ It cannot start —
    the button is insensitive until IOC and rate are both explicit.
18. ~~**The button during DECODING?**~~ Insensitive, reading "Start";
    active again at SAVED. Never a state name — states live in the status
    line. The first GUI is serialized; an overlapping next reception
    stays architecturally available but unbought.
19. ~~**How does the ruler appear when AUTO resolves the IOC?**~~
    Suddenly, with no transition — the blank-until-measured rule.

**No design question remains open, a third time** — item 18 closed the
same day it was asked. The pattern from session 18 repeated at half
strength: these five were, again, mostly about behaviour this document
had never specified — persistence, zoom-scroll interaction, the
transport cycle — and they surfaced the moment someone prepared to *code*
the surfaces rather than to look at them. Paper closes the questions it
can see; code and windows find the rest.

### Raised by Sara during the §8.3 coding session (session 20)

Five asked, two of them already answered elsewhere in this document and
restated rather than pointed at, three open and decided the same day.
All written up in §8.5.

20. ~~**When is the picture saved automatically?**~~ At the end of the
    batch decode, before any editing is possible — no prompt, no toggle,
    filename from the system clock. Already decided across §4 and §8.3
    item 6; the question was that it could not be read in one place.
21. ~~**What does an edited re-render do to the saved file?**~~ It
    overwrites it: one transmission, one file. The pre-edit image is
    reproducible via Auto only while §3 still retains the raw snapshot,
    and losing it after that is accepted.
22. ~~**Is there a Save button after editing?**~~ No — Apply re-renders
    and writes, so the file always matches the screen. This and item 21
    have one joint answer: a new file per Apply would have justified a
    Save button, and overwriting does not.
23. ~~**What counts as an edit in progress?**~~ Dirty controls, not a
    mode: it begins at the first PHASE/SYNC change or the first click on
    the image, and ends at Apply, Auto, or switching to the live view.
    This is the boundary §8.2's pane-hold was missing.
24. ~~**What names the file when the label is blank?**~~ The timestamp
    alone — `20260813T220417Z.png`, and `20260813T220417Z-JMH.png` with
    a label. UTC to seconds, timestamp first, label sanitized and capped,
    and Nova never renames a saved file.
25. ~~**Is the per-station PHASE/SYNC memory worth storing at all?**~~
    **No — and this reverses a decision taken in §7 and repeated in §8.4
    item 1.** The label is unstable free text; the receive chain changes
    the number while the key does not notice; `clock_ppm` belongs to the
    (transmitter, receive chain) pair, which no label can name; and a
    stale PHASE is *harmful* rather than useless, because PHASE is a seed
    for the anchor search and its position differs every reception. The
    preference file keeps the image folder alone. See §8.5 item 6, which
    also records why the corpus can do this and Nova cannot.

**No design question remains open, a fourth time** — and this is the
first time the set contained a **reversal** rather than a gap. Items
20–24 were things the document had never said; item 25 is something it
said twice and had wrong, and it survived two "complete" declarations
because it was inherited from the receiver corpus, where it is correct.
Copying a good decision from a machine that contains its own radio is
exactly the mistake §8.1 caught once already, with the frequency readout.
It is worth expecting a third instance somewhere in this document.

---

## 13. Registered gaps

- No screamer covers RtAudio; the FLTK gap narrowed to widget wiring and
  callback behaviour when §9's screamers 7 and 8 landed (session 19) (§9).
- Live decode inherits the short-baseline gap already registered in
  `ROADMAP.md`: on a faded signal, 120 s windows of GYA 2300Z give −1223
  to +320 ppm. The preview's forward EMA (§6) will be wrong there, and
  the saved image is the answer — but the preview may look bad enough
  that an operator stops a good transmission. Unmeasured.
- **No recording in the library exercises the capture-rate path**
  (session 20). All 20 fixtures are already at 8 kHz, so `resample` is a
  passthrough over the whole library and a streaming bug in it would be
  invisible there. `live_demod_equiv` covers 44100 and 48000 with
  generated signals, which is the right tool — but a generated signal
  cannot surprise the resampler the way a real capture chain might, and
  no fixture can close this, because every recording in the library
  reached us through someone else's resampler already.
- **No fixture in the library carries a STOP tone** (survey, session 20).
  Six of the seventeen carry a control tone and all six carry a *start*
  tone; the stop tone — the signal that ends a transmission, and
  therefore the one the live state machine at §4 leans on hardest — is
  exercised only by generated signals in `live_tones` and by the
  synthetic matrix in `tones`. Unlike the capture-rate gap above, **this
  one is closable**: AGENTS.md records real stop tones measured in VMW
  2230Z, NMC 2204Z and GYA 2300Z (session 6), fading 0.5–1.5 s at a
  time. The fixtures were simply cut from other parts of those
  recordings. A stop-tone fixture cut from one of them would also be the
  library's first real test of the gap-bridging rule, which is the next
  gap down.
- **Nothing in the library fades mid-tone**, so the run-assembly rule
  §5 rewrote is exercised on real audio nowhere (session 20, found by
  mutation — halving the streaming detector's gap tolerance changed no
  verdict on any fixture). `live_tones` covers it with a generated tone
  interrupted by 1.5 s of noise, and with a two-burst signal for the
  opposite rule. A real faded stop tone would close both this and the
  gap above at once.
- The preview's row-placement quality has no target number yet.
  `place_rms_px` exists for the batch path; the equivalent for the
  preview is not defined, so §9's screamer 3 pins determinism and
  dimensions, not quality.
