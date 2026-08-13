# 05 — M4 shell design: FLTK + RtAudio

Status: **complete — every design question in this document is decided**
(Sara, 2026-08-13, session 17: all five originally open, plus the sixth
that her retention answer raised). Two of her answers corrected the
design rather than merely choosing from it: the retention rule (§3) and
the status panel (§8.1), the latter because Nova cannot know the
frequency at all. **The next step is code, not paper.**

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
- A live override carries into the batch re-decode, and both values
  persist per station.

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
│ Device [▾]  IOC [Auto▾]  Rate [Auto▾]   [Start] [Force Start]│  25 px
├───────────────────────────────────────────┬──────────────────┤
│ 0    100   200   300   400   500   600 ▲ruler                │
│ ┌───────────────────────────────────────┐ │  STATUS          │
│ │                                       │ │  mode  IOC  rate │
│ │        image pane                     │ │  state  quality  │
│ │        (preview, or saved)            │ │  started (clock) │
│ │                                       │ │  label [______]  │
│ │                                       │ ├──────────────────┤
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
LOCK, no ring [docs/04 answer 7].

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

---

## 9. How M4 gets screamers

Six, all runnable with no audio device and no window, all reusing the
existing 20 fixtures:

1. **`live_demod_equiv`** — block-with-overlap streaming
   resample+demod over a fixture equals the whole-file result, sample for
   sample, at every block size in a set. Pins §2.2.
2. **`live_tones`** — the streaming detector finds the same tone *kinds*
   in the same order, with start times within one hop, as `detect_tones`
   on every tone fixture. Pins §5, with the medians explicitly excluded
   per §5.
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

Registered as a gap up front: **nothing here tests RtAudio or FLTK.**
Device enumeration, callback behaviour under xrun, and widget wiring are
verified by running the app, not by a suite. That is the boundary the
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
   blank by default and legitimately blank, which keys the per-station
   PHASE/SYNC memory; the timestamp comes from the system clock and
   names the file. See §8.1, which corrects the status panel this
   mistake had already reached.

### Raised by the answer to item 2, and closed the same day

6. ~~**Which picture owns the pane while an edit is in progress?**~~
   **DECIDED 2026-08-13 (Sara, session 17): the edit holds the pane.**
   The incoming transmission draws into its background buffer behind a
   compact receiving indicator that switches on click. See §8.2 for why
   this costs nothing architecturally — the provisional renderer already
   pushes rows through the GUI queue rather than drawing to a widget, and
   the entire edit state is two numbers.

**No design question remains open.** Every item in `docs/03`,
`docs/04` and this document has an answer. The next step is code.

---

## 13. Registered gaps

- No screamer covers RtAudio or FLTK (§9).
- Live decode inherits the short-baseline gap already registered in
  `ROADMAP.md`: on a faded signal, 120 s windows of GYA 2300Z give −1223
  to +320 ppm. The preview's forward EMA (§6) will be wrong there, and
  the saved image is the answer — but the preview may look bad enough
  that an operator stops a good transmission. Unmeasured.
- The preview's row-placement quality has no target number yet.
  `place_rms_px` exists for the batch path; the equivalent for the
  preview is not defined, so §9's screamer 3 pins determinism and
  dimensions, not quality.
