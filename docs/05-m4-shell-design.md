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
live/      nova-live    no FLTK, no RtAudio, no real clock; owns threads 2-3
gui/       nova-gui     FLTK + RtAudio; owns thread 1 and thread 4; no DSP
```

**The thread column changed in session 23, and the rule above is what
changed it.** This table first said `nova-live` had "no threads of its
own" and `nova-gui` "owns threads". Building §2 showed those two
sentences pulling against the load-bearing rule directly below them: the
capture thread is where the ring is drained, the front end is run, the
session is pushed and the batch decode is launched — every one of which
can be wrong about a signal — so putting the thread in `nova-gui` would
have put all of that behind FLTK and RtAudio, where no screamer could
reach it. The threads `nova-live` owns are therefore threads 2 and 3
(`LiveEngine`), and what stayed in `nova-gui` is thread 1, which is
RtAudio's own callback, and thread 4, which is FLTK's main loop. Both of
those belong to a library the GUI links and neither runs a line of DSP.
The rule won; the table was wrong.

**The load-bearing rule: `nova-live` must not depend on FLTK, RtAudio, or
a real clock.** Everything M4 adds that can be wrong about a signal —
the streaming tone detector, the provisional renderer, the session state
machine, the retained store, the PNG writer — lives there and is driven
in tests by feeding it a fixture WAV in blocks, faster than realtime,
with no audio device and no window.

This is not architecture for its own sake. This project's entire quality
argument is its screamers, and a live path that can only be exercised by
a human with a radio has no screamers at all. §9 is what this rule buys.

`nova-gui` is then genuinely thin: device enumeration, widget layout,
event plumbing, the RtAudio callback and the timer that drains the queue.
If a bug is in `nova-gui`, it is a wiring bug, and a wiring bug is
visible on screen in seconds.

**Measured after the wiring landed (session 23), because a rule like this
is only worth stating if it can be checked:** `nova-live` is 3,378 lines
across seven translation units and their headers, all of it reachable
from a test with a fixture and no window; `gui/nova-gui.cpp` is 1,666
lines of widgets, one audio callback and a timer. The split held under
the one change most likely to break it — two thirds of the live path by
volume stayed on the testable side of the line.

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

**BUILT session 23 as `live/engine.{hpp,cpp}` — and the whole of it is
in `nova-live`, not in the GUI.** Everything on the diagram above can be
wrong about a signal, and §1's rule is that anything which can be wrong
about a signal must be drivable by a test with a fixture instead of a
sound card. `LiveEngine` therefore owns the ring, the front end, the
session, the batch handoff and the save path, and `gui/nova-gui.cpp` is
left with what §1 asks of it: widgets, the RtAudio stream that calls
`push_audio`, and the 50 ms timer that drains the queue. No DSP, no
state machine and no file naming is written inside a widget.

**The claim `live_engine` makes, and it is the only one worth making
about concurrency in a decoder: threading changes nothing about the
picture.** A recording driven through the engine — real producer thread,
real ring, real batch thread — produces the same state sequence, the
same rows at the same sample positions, and the same saved pixels as the
same recording driven through `LiveSession` on one thread with no ring
at all, at five audio block sizes from 1 sample to 65536. Without that,
every number the rest of the suite measures was measured on a path the
operator does not use.

**2.1 The audio ring.** Single producer, single consumer, fixed capacity,
no allocation. Capacity 4 s at the capture rate — orders of magnitude
more than a callback period, so an overrun means thread 2 is wedged, not
that the buffer was tight. Overrun is *counted and shown*, never
silently dropped: a dropped block is exactly the kind of capture-chain
sample loss the decoder spent sessions 9–12 learning to detect, and
manufacturing it in our own buffer while reporting a clean timebase would
be dishonest.

**BUILT session 23 (`live/ring.hpp`), and the ordering needed a test the
obvious one was not.** Header-only, capacity+1 slots so full and empty
are distinguishable without a count, each index written by exactly one
thread so no CAS appears anywhere. `live_ring` pins order and values
across ~19 wraps at 35 write/read block-size pairs, exact overrun
accounting in samples, and — with `operator new` counted on the
producer's own thread — **zero allocations across four million samples**
on the realtime side.

The lesson is in the fourth check. A producer and a consumer racing over
the shipping 4-second ring **did not notice** when every release/acquire
in the file was turned into relaxed: on a 192000-sample buffer the two
threads are never on the same slot, so the publish is never observed
early. Turning the memory ordering into a test needed a ring small
enough that they are always on top of each other — 16 samples, blocks of
four, neither side sleeping — and then the mutation fails immediately:
**2067 slots read before their write was published, against 0 on the
baseline's nine million.** On a weak memory model this is the difference
between a correct decoder and one that puts noise in the picture, and
the size of the buffer is what decides whether a test can see it.

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

**BUILT session 23 (`live/engine.{hpp,cpp}`), and this paragraph was
wrong by one producer.** It names thread 2 *and* thread 3 as pushing
onto the queue, and then calls the queue SPSC. Two producers is not
single-producer, and the choice was between building a multi-producer
queue and removing the second producer. The second producer is removed:
thread 3 posts its finished `DecodeResult` into a one-slot inbox, and
thread 2 picks it up, writes the file, calls `batch_done` and emits the
messages. Three things fall out of that, and the third is the reason to
prefer it:

1. the GUI queue really is single-producer;
2. `LiveSession` has exactly one owner, which is what `session.hpp`
   asks for in as many words;
3. **the observable event order is the session's own.** Session 22 had
   to fix a re-entrant `batch_done` to make the machine's history
   independent of the caller's callback discipline; letting thread 3
   post GUI messages directly would have reintroduced the same class of
   problem one layer up, with the order now depending on which thread
   reached the mutex first.

The progress reports are the one exception and they are handled the same
way: thread 3 writes a *slot* (stage, fraction), thread 2 turns whatever
it last saw into a message. Progress is a level, not an event, so
coalescing nine stages down to what a 20 Hz bar can show is exactly
right rather than a compromise.

The three queues are mutex-guarded, and that is not a violation of the
"never block" rule: that rule is about thread 1, the realtime one, which
touches only the lock-free ring. Threads 2, 3 and 4 may take an
uncontended mutex for the length of a vector append. Thread 2 **polls**
the ring rather than waiting on a condition variable, because the only
thing that could signal it is thread 1 and a realtime callback may not
touch a condvar; the poll interval is a latency floor, not a throughput
limit, since one wake drains everything the ring holds.

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

**[PARTIALLY BUILT session 22: the store and the freeze live in
`LiveSession` (`live/session.cpp`).** The append-only vector, the
pre-roll bound while monitoring, and the freeze into
`shared_ptr<const vector<float>>` at the end of the transmission are all
as written here, and the screamer pins the snapshot's bounds (the tone's
true start to the stop tone's start). What is NOT built: the second
snapshot — "the image currently displayed" is the caller's shared_ptr by
construction, so the two-role rule lands with the GUI wiring, not here.]

**[BUILT session 27 — the second snapshot, in `LiveEngine`
(`RetainedVideo` / `retained_video()`), pinned by `live_engine`.** The
frozen stream of the most recently decoded image is held with the
absolute offset it started at and the `DecodeOptions` that produced it,
so a re-render can be derived from the record rather than reconstructed
from memory. It changes hands in `collect_batch`, at the moment that
decode's image takes the pane, and nowhere else — so the next
transmission merely arriving, or being decoded, does not take it away.
That is the case §3 was written for and it is now a check: measured on
the fixture played twice with an operator Stop between, the first
transmission's stream is still the retained one after 158 s of the
second has arrived, and through all 651 observations of the second
decode. The rejected first draft ("release it when the next
transmission's snapshot replaces it") fails that check 0-for-640, which
is how the check was verified to be one.

Three things the build found that this section did not have.

- **Retention and reachability are two questions.** §3 says "keep the
  stream behind the image the operator may be adjusting"; §8.2 says "a
  transmission arriving mid-edit does not take the pane". Those are one
  decision seen from two sides, and §8.2 is ROADMAP item 6, *not built*.
  So while the next transmission's preview owns the pane, the stream is
  retained exactly as §3 asks but a correction is correctly NOT offered
  — the picture the operator is looking at then is the preview, not the
  chart. `RetainedVideo` therefore reports the two facts separately
  (`decoded` and `on_pane`), and `can_correct()` is their conjunction.
  Item 6 widens the second without touching the first.
- **The reason needs three sentences, not one.** §3 names only "raw
  stream no longer retained", which is the *folder-open* case — an image
  whose snapshot was released because the operator moved on. Two others
  exist and are different facts about the operator's situation: "no
  decoded image yet", and "receiving — this picture is provisional". The
  §3 string is built and is currently unreachable, because nothing can
  yet put an image on the pane that this engine did not just decode; it
  is the branch §8.3 item 6's folder view will reach.
- **The order inside `collect_batch` is load-bearing, exactly as it is
  for write-then-SAVED in §8.5 item 1.** The retained stream changes
  hands BEFORE the image reaches the pane. Thread 4 may look between the
  two, and the other order would show it a new chart on the pane backed
  by the PREVIOUS transmission's stream — a correction taken in that
  instant would re-decode the wrong transmission.

**Where the reason is shown**: a wrapped italic line in the sidebar,
directly under the Apply/Auto pair it explains, not in the sidebar's
empty lower area, which §8's item 5 above already spoke for. Apply and
Auto themselves stay grey after SAVED until item 4 gives them behaviour
— an active button that does nothing is the failure session 26 found and
this section will not reintroduce.]

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

**Start in `SAVED` re-arms to `READY`** [BUILT session 26
(`LiveSession::start_capture`, `live_session` T13)]. The table always
said SAVED leaves on "next transmission, or operator action", but the
machine only listened for Start in IDLE — so the button, active and
reading "Start" in SAVED, swallowed the click (found by Sara at the
keyboard, session 26). T13 pins the operator half of the exit rule; T10
pins the tone half.

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

**[BUILT session 22: `live/session.{hpp,cpp}` +
`tests/test_live_session.cpp`, running unguarded in every build.]** Four
things this section did not say, in the order they matter:

1. **How PHASING is detected at all.** The section names the states and
   is silent on who watches the phasing interval — and the answer is new
   code, because `detect_phasing` is a whole-recording function. The
   machine re-runs it once per second of new signal over the video since
   the start tone, at all three nominal line rates (the start tone names
   the IOC [WMO §5.2.2] but nothing in the opening names the rate, and
   the live path has no comb scan). PHASING is entered on a qualifying
   run; DRAWING is entered only on a CLOSED run — one the buffer outlasts
   by more than the run-assembly gap — because an open run's `t_end` is
   still moving and a decision taken on it would depend on when the scan
   happened to run, i.e. on the block size. The run's `t_end` is where
   drawing starts, its `anchor` is the preview's `phase_anchor` (the §6
   item-1 handoff, now real), and its measured `period` — a field added
   to `PhasingResult` for exactly this — is the rate seed. Measured on
   VMW: the seed sits **−14 ppm** from the batch fit, and the preview's
   dead sector lands **+6 px** from the saved image's column.
2. **What the preview may be fed.** First version: to the end of the
   received stream. That drew the stop tone into the picture, by a
   block-size-dependent amount — a stop tone qualifies `min_stop_sec`
   after it begins [§5], and the rows fed past the tone start in the
   meantime had already been drawn: 226 rows at 1000-sample blocks, 222
   at 65536, the last four being 450 Hz alternations. The fix lives in
   the tone detector, which now reports a `safe_horizon_samples()`: win +
   hop behind its classification frontier, capped at an open stop run's
   start. The preview is fed to the horizon and the drawn rows are
   identical at every block size; the cost is 0.375 s of preview latency.
3. **How the operator's Stop composes with a fast decode.** The decode
   callback may run inline and report from inside the same call that
   fired it; a re-entrant `batch_done` then recorded SAVED into its own
   return value before the outer call's DECODING, and the observable
   history read `DRAWING → SAVED → DECODING`. Re-entrant state changes
   now record into the outer call's output.
4. **Which opening, when there are two before one picture.** The batch
   rule "the LAST opening inside a known transmission" needs the stop
   tone, which has not happened yet live. The machine draws from the
   FIRST opening's phasing end (FAXSignal: 22.0 s, where the batch
   picture starts at 64.5), and the second opening passes through the
   preview as picture rows until the real picture arrives. Registered in
   §13, not fixed: a forward-only preview that waited to find out would
   not be a preview.

The §12-item-3 page cap is a `SessionOptions` duration (default 90
minutes — past the library's longest transmission, so it can only fire on
a missed stop tone), and its path is the operator stop's: freeze, decode,
DECODING, no invented STOP TONE.

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

**[BUILT session 21: `live/preview.{hpp,cpp}` + `tests/
test_live_preview.cpp`, running unguarded in every build.]** Five things
this section did not know, in the order they matter:

1. **The seed list is missing an entry, and on a white-only station the
   missing one is the only one that works.** §6 seeds IOC and RATE and
   says nothing about PHASE, which left the renderer finding its own
   anchor from image lines. That is the best available on a station that
   sends a black pulse and the *only wrong answer* on one that does not:
   a white-only dead sector carries no per-line phase at all (session 4),
   so the phasing interval's leading edge of white is the single place
   its anchor exists [WMO §5.2.3.4], and `decode_fax` takes it from
   exactly there. Measured before this was added, on
   `vmw-phasing-image-160s`: the preview drew the page **524 px** —
   nearly a third of a line — around from where the saved image put it.
   Now `PreviewOptions::phase_anchor`, filled in by the live state
   machine on its way out of `PHASING`, and used only where the image
   lines cannot answer, which is `decode_fax`'s own rule. The two paths
   now agree to **1 px** on both phasing-anchored fixtures.
2. **Acquisition is latency, not revision, and shorter is also better.**
   §6 forbids revising a drawn row, so the anchor has to be right before
   row 0 appears. The renderer holds 16 lines, builds the same across-line
   consistency profile `stage_dead_sector` builds, commits an anchor and
   never revisits it — and draws those same 16 lines first, so nothing is
   lost to the wait. The window is short because the two pressures on it
   oppose: a longer stack is a cleaner fraction but a blurrier one, since
   the profile rides the *seeded* period and a rate error e smears the
   pulse by `lines * period * e`. At 16 lines and a 300 ppm seed error
   that is 19 samples against a 90-sample pulse; at the 120 lines the
   batch path can afford — it has a refined period by then — the same
   error would smear 144, wider than the pulse itself.
3. **Re-acquisition is part of "the relock works forward", and leaving it
   out costs the page below a dropout.** `stage_track`'s whole-line sweep
   after a run of misses is forward-only and belongs here. Measured
   without it on `himawari-kiwisdr-dropout-120s`: **140 rows locked of
   238**, against 232 of 240 for the batch path — the tracker never came
   back and every row below the dropout was drawn torn. With it, 230 of
   239, and the preview's lock rate is 95–100% across the library.
4. **The forward EMA is measured away from on a pulse station, and that
   is the SYNC decision of §7.1 arriving one stage early.** Where the
   station sends a pulse the relock keeps measuring the real period and
   the EMA walks an operator's trim off within `ema_lines` rows; where it
   does not, nothing contradicts the operator and the value stands for the
   whole page. Measured with a deliberate +2000 ppm: **0 ppm of 2000
   still standing** on a pulse station, **2000 of 2000** on a white-only
   one. Implementing the live trim as a plain override would have made the
   live path and the batch path disagree about the same operator action.
5. **Block-size independence is a property of the trim schedule, which is
   not where anyone would look.** `fax_best_sync` walks its search window
   by accumulation, so where that window sits in the retained buffer
   decides the last bits of every probe position — at absolute magnitude
   the double grid is coarser than near zero, and the two sequences drift
   apart by ~1e-11 over a line. Releasing video per *push* rather than per
   *row* therefore made the picture depend on the audio callback's block
   size: identical at 1, 7, 333, 1000 and 2000 samples, different at 12345
   and 65536, on five fixtures. Releasing once per row makes `buf_start_`
   a function of the rows drawn and nothing else, which is what makes the
   picture one too.

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
  **[BUILT session 21: `StreamPreview::set_phase_anchor` /
  `set_clock_ppm`, marked on the row by `PreviewRow::phase_mark` /
  `sync_mark`, pinned by `live_preview`. Measured: the rows above an
  override are byte-identical to a render without it, exactly one row
  carries the mark, and the rows below move by the fraction asked for
  (−452 px against −453 asked). Two notes the section did not have.
  **PHASE is always taken FORWARD in the signal** — there may be no
  retained samples left to take it backward through — so a report of
  0.9 costs most of one row rather than winding back a tenth of one.
  And **SYNC behaves live exactly as §7.1 decided it must behave in the
  batch re-decode**: measured away where the per-line relock can
  contradict it (0 ppm of a deliberate 2000 still standing on a pulse
  station), and standing for the whole page where nothing can (2000 of
  2000 on a white-only one). Building the live trim as a plain override
  would have made the two moments disagree about the same operator
  action, which is the bug §7.1 exists to prevent — it just had one more
  place to hide in than §7.1 knew about.]**
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

**[BUILT session 24.** Both fields are in `DecodeOptions` as written
above, and `LiveSession` hands them to the batch decode of the
transmission the operator corrected. Three things the writing of it
turned out to need, none of which this section knew.

**1. The section named three conditions and the code has one.** "A
white-only station, a forced start, too few locked lines" all arrive at
`stage_fit` as the same fact — no segment of locked lines long enough to
pair a long baseline across — so the fallback is gated on the fit's own
emptiness rather than on three proxies for it. `DecodeResult::
clock_from_fallback` reports which way it went, and it has to exist: the
QA header's `Nova:Sync` said "operator" whenever the operator had TYPED
one, which under this decision is a claim the pixels usually cannot
support. Supplying a value and having it used are different facts and
the header now carries both.

**2. PHASE reached the anchor and then died two stages later, and the
reason is `stage_track`.** Setting `dead_start0` is not enough on a
station that sends a pulse: the re-acquisition sweep looks over HALF A
LINE, so it is free to walk the tracker back onto the feature the
automatic scan preferred — the one the operator was overruling. Measured
before the fix: a hint at half a line on JMH, and one 900 samples away
on a synthetic decoy, each moved the anchor and left the saved page
byte-identical. The sweep is now off when a hint is present. The cost is
real and accepted: with a hint, a tracker that falls off a dropout can no
longer sweep the line to find its way back. That is the right way round —
the sweep decides WHICH feature the line starts on, and once the operator
has answered that, a search free to answer differently is not a recovery.

Line 0's own initial search was narrowed too, and then that change was
**removed**: the mutation restoring it survived, on every fixture and on
the synthetic. It would only bite where a stronger competing feature sits
between `search_frac` and 5% of a line from the click, and nothing
available can be made to show it. Code whose effect cannot be
demonstrated does not stay, which is the same rule that deleted
`LiveState` in session 23 — the difference being that this one looked
principled, and being principled is not evidence.

**3. The hint must also outrank the PHASING anchor**, which §7.1 did not
say. `stage_phasing` overwrites the image anchor on a white-only station,
and if it overwrites the operator's too, the field works on pulse
stations and vanishes on exactly the recordings that need it — VMW and
its 520 px rotation are the whole reason auto-phasing has a wrong answer
to correct. The phasing delta is still measured and reported, so the two
answers can still be compared.

The refinement is a real measurement, not a formality: on
`vmw-phasing-image-160s`, five clicks spread across 3.5% of a line all
settle on ONE anchor, 13 px from what the phasing interval says
independently — on a white-only station, where the note above warned
there is no per-line phase to refine against at all. On a pulse station
four hints ±1% off the anchor each produce a byte-identical page.**]**

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

**The pane follows the newest row while drawing** [DECIDED 2026-08-14,
Sara, session 26 — found by the second live broadcast: once the chart
outgrew the pane, nothing scrolled, and watching the newest line meant
dragging the scrollbar by hand against a picture that moved under it].
While `DRAWING — PREVIEW` the bottom of the pane is the newest received
line, every row; a manual scroll up is corrected on the next row, which
is the price of a promise that cannot be misunderstood. Once the state
leaves DRAWING the scroll is the operator's again — the save taking the
pane does not move it.

**[BUILT session 26; the picture did not actually follow until session
27, and the reason is a property of the toolkit worth writing down.**
Sara: *"it scrolls like, bouncing up and down instead of smoothly going
down a bit."* **`Fl_Scroll` scrolls by MOVING its child, so the child's
position IS the scroll offset and `yposition()` is only a cached copy of
it.** `layout_view()` resizes the child on every row batch — the picture
grew — and it resized it to the pane's top-left, which silently scrolled
the chart back to the top while `Fl_Scroll` went on reporting the old
offset. The repair line already there, `scroll_to(keep, yposition())`,
could not repair it: `scroll_to` early-returns when its arguments equal
the cached values. Every later `scroll_to` then moved by a delta measured
against a number that was no longer true, landing the picture at
`max_y − previous` — so it alternated between the bottom of the chart and
the top of it, once per batch. Fix: resize the child AT the current
offset, so the invariant `Fl_Scroll` re-derives on draw holds
continuously and every actual move goes through `scroll_to`.

Two things this cost, recorded because they are the general lesson:

- **The follow was verified against the cached number, which is why it
  passed review and bounced on screen.** Measured on the pre-fix build,
  `yposition()` read 632 — exactly right — while the picture sat at 150.
  A check that asks a toolkit where it thinks it is will agree with the
  code that told it; the screamer added here (`--follow`, in
  `gui_shell`) asks the CHILD where it is instead.
- **The same desync silently reset the VERTICAL scroll on every zoom
  change**, by the same mechanism through `cb_zoom`, and nobody had seen
  it because vertical scrolling only became possible in session 23 and
  the only state that scrolls far is the one the follow was overriding.
  It is fixed by the same line. Zoom now keeps the vertical position as
  §8.4 item 2 keeps the left edge — as a pixel offset, not as a row, so
  the row at the top of the pane still changes with the scale. Whether
  it should be rescaled the way `rezoomed` rescales x is **open, and
  registered rather than built**: it is invisible during DRAWING and
  small after SAVED.]**

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

**9. The Device menu moves the sound card the moment it changes, and it
is insensitive while there is a chart to kill — DECIDED 2026-08-14 (Sara,
session 25).** The M4 item-1 run found the menu had never had a callback:
the stream opened once at window-show on the system default, and picking
BlackHole 2ch afterwards relabelled the menu while the meter kept
answering the operator's voice — the largest untested surface in M4
caught exactly the bug it existed to catch. So: changing the selection
reopens the stream on the new device at once; the menu is insensitive
from Start until the transmission ends (IDLE and SAVED are the states
with nothing to lose, the same deactivate-never-prompt idiom as Force
Start); and the choice persists in the preference file, matched back by
NAME — CoreAudio's enumerated ids are per-boot, so a persisted id would
be a dice roll that can open somebody's microphone. A remembered device
that is unplugged falls back to the default, not to an error. The greying
is pinned by `gui_shell`; the reopen itself needs a real card and stays
under §13's RtAudio gap.

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

What it holds is **the image folder** [§8.3 item 7] and, since session
25, **the input device's name** [§8.3 item 9] — both things the operator
chose. As first written this item also gave it the per-station PHASE/SYNC
memory of §7; **session 20 removed that** — PHASE and SYNC are
measurements of a signal, not preferences. See §8.5 item 6.

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

**[BUILT session 23 (`LiveEngine::collect_batch`), and the ORDER is the
part that needed pinning.** Thread 2 writes the PNG and then calls
`batch_done`, which is what enters SAVED — never the other way round, or
the status line would read SAVED over a file that is not there. Swapping
those two lines was invisible to every check `live_engine` made until the
test began recording the message order, and it is now the claim that
kills it. A save that fails does not suppress SAVED (the decode did
succeed) but posts the reason, which the status line shows.]**

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

**[BUILT session 27 — items 2, 3 and 4 together, because they are one
mechanism** (`LiveEngine::redecode` / `Correction`, `correction_for` in
the shell, `live_engine`'s `test_rerender` and `gui_shell`'s truth
table). Apply re-renders the picture on the pane from the raw stream
retained behind it [§3] and overwrites the file it was saved to; three
Applies leave one PNG in the folder, and the bytes change each time.
There is no Save button. Auto restores the automatic decode **byte for
byte** — the same file the transmission first wrote — which is only
possible because §3 retained the options as well as the stream.

Four things the build settled that the six answers above did not.

- **Auto is not a third mode: it is the empty correction.** "As
  measured" is the ABSENCE of the two values, which §7.1's own sentinels
  already say (−1 is "no hint" because column 0 is a legal anchor; NaN is
  "no fallback" because 0 ppm is a legal clock). So one entry point takes
  a `Correction` and Auto sends `{}`. This is the same "auto as a value"
  idiom `lpm = 0` and `ioc = 0` established, applied one level up.
- **This is the one decode `LiveSession` does not own** [DECIDED
  2026-08-14, Sara, session 27]. Every other decode is a state change the
  machine made; a re-render is the operator asking for the same
  transmission again, and the machine stays in SAVED throughout —
  `batch_done` from SAVED was already a no-op, so the state cannot be
  corrupted by it. It shares the one-slot batch inbox, thread 3, and the
  collect-save-post tail with the automatic path. The alternative, a
  re-decode state on the session, was weighed and rejected as more
  surface for a decode that changes no state. **Consequence, recorded
  because it is the cost:** §2's "the session owns every decode" now has
  one exception, and the shell can no longer read "is anything decoding"
  off the state — `LiveEngine::redecoding()` is the other half of that
  question, and the progress bar and the transport both consult it.
- **The busy flag is lowered when the FILE is written, not when the
  decode finishes** — and the first version got that wrong, which the
  screamer caught intermittently before it was made to spin rather than
  sleep. Lowering it early re-arms Apply while the PNG is still being
  written and lets a second Apply in on top of it. It is §8.5 item 1's
  own lesson a second time: the operator-visible signal comes after the
  file, never before it.
- **The edit's other end needed a stand-in.** Item 4 says an edit ends at
  Apply, at Auto, "or when the operator switches to the live view" — and
  there is no live view to switch to until §8.2's background buffer
  (ROADMAP item 6). The built rule is the honest equivalent: the edit
  ends when the pane stops showing the chart being corrected, at which
  point the boxes go back to blank [item 6 below: measured-or-blank, no
  memory between transmissions]. When item 6 lands, that sentence becomes
  the operator's own action rather than a consequence of one.

**What the buttons do when there is nothing to do: nothing, visibly.**
`correction_for` is a pure function of four booleans — live surface,
re-render possible, edit dirty, correction applied — so the rule is
checkable without a window, and `gui_shell` checks all sixteen
combinations against the rules rather than against a copy of the table.
Post-decode with nothing typed and nothing applied, both buttons are
grey: the picture already IS the measured render, so Apply would rewrite
the same file and Auto would undo nothing. An active button that does
nothing is the failure session 26 found on the air, and this surface has
three chances to reintroduce it.]**

**[BUILT session 28 — the SYNC steppers, and why PHASE has none.** Asked
by Sara after the first by-hand run of this surface: typing a whole ppm
each time is the wrong instrument for a judgement made by eye, which is
"a bit more", not "−93". Four buttons under the SYNC box: −10 −1 +1 +10.

*The sizes are measured.* At IOC 576 a line is 1810 px, so 1 ppm walks
the line start by 1810e−6 px per line — about 2.2 px of skew at the
bottom of a full ~1200-line chart, which is roughly the smallest step
worth having. Real errors run 30–180 ppm (session 5; the four white-only
fixtures read −70 to −118), so a fine button alone would be a hundred
clicks across the range the control exists to cross. Hence a coarse one.

*PHASE deliberately gets none.* PHASE is a SEED refined to the best
feature within `search_frac` of it — ±3% of a line, ±54 columns at IOC
576 [core/fax.cpp, `stage_dead_sector`]. A nudge smaller than that window
is refined straight back onto the same feature and the picture does not
move: a control that visibly does nothing, which is the paragraph above
wearing a different hat. PHASE's instrument is the click (ROADMAP item
5), and this asymmetry is the same one §7.1 already draws between the two
fields — seed versus trim — reaching the glass.

*Where a nudge starts, which is the one thing here that can be quietly
wrong.* A blank box means "as measured", and the measured clock is not
0 ppm — it is −70 to −118 on exactly the white-only stations this control
exists for. Starting a nudge at zero would make the operator's first
click a jump of the whole clock error, away from correct. So a nudge from
blank starts at the clock the picture on the pane was DRAWN on, which is
the number the Quality field is already showing them; a re-render posts
`kBatchDone` like any other decode, so after an Apply the next nudge is
relative to the corrected picture rather than the original measurement.
A TYPED value outranks the shown clock — including a typed `0`, which is
the operator saying zero ppm and is not the same thing as blank. That is
the same distinction core/ makes by giving `clock_ppm_fallback` a NaN
sentinel rather than 0.

*And a nudge has to declare the edit itself.* FLTK does not fire an
input's callback for a programmatic `value()`, so a stepper that only
wrote the box would move the number while the shell still believed
nothing had changed — the operator's own correction sitting in a control
that reports itself clean, with Apply grey over it. `nudge_sync` sets the
dirty flag by hand, which means the claim is a line of code that can be
lost, so it is screamed rather than trusted.

*Made checkable without a window*, since this is interaction surface and
the last three sessions each found a defect the suite could not see:
`--sync-step` prints where a nudge starts for thirteen cases and
`gui_shell` reads it as RULES, not as a transcript; `--nudge N` presses
the +1 stepper N times before `--metrics`, so "a nudge is an edit" is a
checkable claim; and `sync_steps_active` is a COUNT, so a stepper that
disagreed with its own box would show up as a number that is neither 0
nor 4. All three defects were deliberately reintroduced and each was
caught. What still needs hands: that the buttons are comfortable to hit
and the step sizes feel right on a real chart.]**

**[BUILT session 28 — click-to-set-PHASE (§8.3 item 1).** An `FL_PUSH`
handler on the image, calling `nova::column_at` on the shell's own
`view_state()`. **The same arithmetic the ruler draws from**, and that is
the whole design: ruler.hpp's correctness claim is that the column under
a screen x is the column the ruler names at that x, so a second mapping
would let the operator click a tick and get a different number than the
tick says. One mapping cannot disagree with itself.

*It sets, it does not apply.* §8.5 item 4 already names "the first click
on the image" as something that BEGINS an edit, and an edit ends at
Apply. A click that re-rendered immediately would also make the
operator's aim un-correctable; the natural motion is click, look, click
again, then Apply.

*A click past the image's right edge names nothing.* With the image
narrower than the pane, screen x beyond its right edge maps legitimately
past the last column [ruler.hpp, `column_at`]. There is no picture there,
so there is no dead sector there, and inventing a column the operator did
not point at is worse than doing nothing.

*The image is a control that cannot go grey*, so the CURSOR carries what
a greyed button would: a crosshair exactly where a click can act, the
plain arrow where it cannot. The reason line under Apply/Auto is still
the words; this is the affordance. One rule drives all three surfaces —
the box, the steppers, the image — off `correction_for`'s
`inputs_active`.

*The guard belongs at the point of effect.* With it only in
`ImageView::handle`, the rule sat on the far side of the FLTK seam where
no screamer reaches. `handle` still refuses to CONSUME the event (the
pane below may want it); `click_phase` refuses to ACT. Different
questions, same flag.

*And the instrument had to be fixed before its verdict meant anything*
[session 23's rule, and this is the sharpest instance of it yet]. With
the guard deleted, every box-and-dirty check still passed — because
`apply_state`'s edit-end rule clears the boxes whenever a correction is
impossible, so a click that wrongly acted was wiped a moment later and
the shell looked correct AFTERWARDS. Net-correct for an incidental reason
is not correct, and it stops being true the moment the other rule is
narrowed. `click_phase` now returns what it did and `--click` reports it,
which is what makes the guard observable at all. Exact columns are pinned
at the FIXED zooms, where the scale is an exact ratio and the expected
column is an integer the test derives independently (100% → x, 200% →
x/2, 25% → 4x); Fit is left to `ruler_mapping`, because any expectation
here would have to recompute the pane interior and would end up restating
the code.]**

**[BUILT session 28 — two-click SYNC (Sara's idea, geometry corrected).**
**SUPERSEDED IN PART by session 29 below: the gestures are now DECLARED
with an arming button, so "one gesture, no mode", "one number doing two
jobs" and "PHASE takes the upper click" are all withdrawn. The geometry —
what a slant is, why it needs a baseline, and that the measurement is a
residual — is unchanged and is what session 29 kept.**
She proposed one click for SYNC and two for PHASE; it is the other way
round. PHASE is a horizontal offset — one feature, one row, one click.
SYNC is a SLANT, which is the same feature at two ROWS:
`ppm = (dcol/drow) / width * 1e6`.

*Why it earns its place.* §7.1 apologises for this field — "a ppm
eyeballed off thirty seconds of preview is worse than one fitted over the
whole transmission" — and is right, because an eyeballed number has no
baseline behind it. Two clicks a thousand rows apart do. This is sessions
5, 8 and 9's finding (precision is baseline, not averaging) reaching the
operator's hand instead of only the decoder's fit.

*One gesture — no mode, no hidden modifier.* A click sets PHASE and
remembers where it landed; a second click far enough away measures the
slant between them. Rejected: a mode toggle (this document's whole idiom
is that "auto" is a value in the same list, never a separate mode) and a
shift-click (invisible, and §3's principle is that this surface explains
itself).

*One number doing two jobs.* `min_baseline_rows` is the honest precision
limit — one screen pixel of click error is `1/scale` columns, so the
baseline has to put the resulting ppm error an order of magnitude below
the 30–180 ppm the control exists to remove: 111 rows at 100%, 442 at
25%, 56 at 200%. It is ALSO what disambiguates the gesture, because a
second click too close to the first is not a bad slant measurement, it is
the operator re-picking PHASE. So it becomes a fresh first click. No
cancel button and no mode to leave, and the same rule that keeps the
measurement honest is the one that keeps the gesture unambiguous.

*The measurement is a residual*, read off the picture as drawn, so the
box gets `shown_ppm + slant`. That is the same operation a stepper nudge
performs and is now literally the same function — with one difference
worth stating: a nudge starts from whatever the operator has typed, but a
slant is READ OFF THE PICTURE, and the picture was drawn on the shown
clock whatever sits unapplied in the box. The evidence outranks the
draft.

*PHASE takes the UPPER of the two clicks*, not the first. On a slanted
chart the bottom column is wrong by exactly the slant being measured —
which can exceed the `search_frac` the seed is refined within — and the
anchor is where the line starts near the top of the picture. Two lines of
code remove an ordering the operator would otherwise have to know about.

*The reason line teaches the gesture*, naming the baseline the current
zoom needs. This is the only control on the surface with no widget of its
own, so §3's "say why" duty falls to the one place that can speak.

*And a screamer that passed against a broken build.* "No anchor survives
the edit's end" was first written as two `run_metrics` calls — which are
two PROCESSES, so the second began with nothing pending and passed
whatever the code did. The rule fires on a TRANSITION, and the seam could
not express one; `--then-state` exists for that. Four defects were
reintroduced (the width dropped from the slant, the baseline test
removed, PHASE keeping the first click, the anchor outliving the edit)
and only after the seam was fixed did all four fail.]**

**[BUILT session 29 — the gestures are DECLARED: an arming button each,
and clicks gated on it (Sara).** This reverses session 28's "one gesture,
no mode" above, and the reversal is evidenced rather than argued.

*What prompted it.* Sara, reading the built panel for the first time,
asked two things: whether an accidental click on the pane could move her
data, and whether the four steppers belonged to PHASE. Both answers were
bad. A stray click did set PHASE, silently; and the steppers are SYNC's,
which she could not tell from the layout — the one thing that layout most
had to carry, since PHASE having no steppers is deliberate. She proposed
a small button beside each box: press it, then click the picture.

*The prior art settles it, and it is prior art this project already
tracks.* **hamfax** (C. Schmitt DH1CS, GPLv2+; the H in Nova's
ACFax→HamFax→yahfax→weatherfax_pi lineage, until now recorded as
"feature-shape and lineage evidence only") has had exactly these two
gestures since 2001, and arms both:

| hamfax | Nova's equivalent | Interaction |
|---|---|---|
| Image → "Adjust IOC (change width)" | SYNC | prompt *"select first point of vertical line"*, then *"select second point"*, two clicks, ends |
| Image → "set beginning of line" | PHASE | prompt *"select beginning of line"*, one click, ends |

Both disable the rest of the controls while the gesture is live.

*Three things that settles which the session-28 reasoning did not have.*
**The lifecycle objection was simply wrong** — "a one-shot arming button
would break the two-click gesture, because you would have to re-arm
between clicks": no, you arm the GESTURE, not the click; one press covers
both and the arming ends when the measurement does. **It is not a mode you
can be stuck in**, which is what the AUTO-is-a-value idiom was protecting
against; it is per-gesture and self-clearing. And **it is VISIBLE**, which
is the criterion session 28 itself stated when it rejected shift-click —
so a button passes session 28's own test better than the undeclared
gesture session 28 built.

*What is taken, and what is left.* Taken: the arming, one button per
gesture, self-clearing. Left: the modal prompt dialog and disabling every
other control — Nova already has the reason line and the cursor, so the
armed state shows in three places that cannot disagree because all three
read one value (`Shell::arm`): the button is pushed, the reason line says
what to click, the crosshair appears. Also left: hamfax's `correctBegin`,
a circular shift of decoded pixels. Nova's PHASE is a SEED re-rendered
from the retained raw stream [§7.1], which is strictly better and means
the prompt must not promise the picture lands exactly where clicked.

*What arming BUYS, beyond the accident.* Two things fall out that could
not be had inside an undeclared gesture:

- **`min_baseline_rows` goes back to being one thing.** It was the
  precision limit AND the disambiguator; arming answers the second
  question outright, so it is demoted from a GATE to a REFERENCE. A short
  baseline now measures and is labelled with what it is worth
  (`slant_error_ppm`, which is the same arithmetic solved for the error
  instead of the rows, so the two cannot disagree). Sara's judgement, with
  the honesty attached rather than a refusal. The one measurement still
  refused is two clicks on the SAME ROW — no baseline at all, and 0/0;
  hamfax divides by zero there. Nova keeps the anchor and stays armed,
  because a bad aim has not ended the gesture.
- **The SYNC gesture stops touching PHASE**, and with it "PHASE takes the
  upper click" disappears. That rule existed only because the fused
  gesture had to set PHASE from a first click it could not yet know was a
  first click. A declared gesture does one thing; the coupling and its
  correction are both gone. Cost: three presses instead of two to set
  both. Bought: no rule about which click won.

*And the crosshair means something now.* It used to be on for the whole
of a correctable chart, which made it scenery. It follows the arming, so
it says the next click WILL act. `FL_MOVE` joins `FL_ENTER` in the
handler, because arming happens while the pointer is on the button and the
operator can come back onto the picture without an enter FLTK reports.

*The finding, and it is the session's sharpest.* **A rule that was
correct became wrong without being touched.** The pending anchor was
cleared inside the edit-end block, guarded by `edit_dirty` — safe in
session 28, because a slant's first click also set PHASE, so an anchor
could not exist without a dirty edit. Declaring the gestures broke that
coupling silently: the SYNC gesture's first click deliberately changes no
value the operator can see, so it does not dirty the edit, and anchors
began surviving into states with no picture behind them. It was still
unreachable — arming clears the anchor, and arming is the only route to a
second click — which is exactly the shape session 28 warned about: net-
correct for an incidental reason, one refactor from not being correct.
Found by hand on the built code, before the screamer ran. The clearing of
the anchor, the arming and the measurement's note is now separate from the
clearing of the VALUES, and guarded only on the surface being gone.

*Layout.* The steppers now FLANK the SYNC box — two buttons either side,
`-10 -1 [box] +1 +10` — because a control touching a box on both sides
cannot be read as belonging to another box, and a label could not have
fixed a misreading a label had already failed to prevent. That costs the
SYNC caption its place on the box's row, so the caption takes the row
above and carries SYNC's arming button; PHASE keeps caption, box and arm
on one row. The resulting asymmetry between the two blocks is the point:
what PHASE visibly lacks is what it is meant to lack. Same three rows as
before, so nothing below moved.]**

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

**[BUILT session 23 (`nova::sanitize_label` / `nova::image_filename`),
as free functions rather than as private members — a rule about a dozen
awkward strings is cheapest to defend by calling it with a dozen awkward
strings, and `live_engine` does.** One reading had to be chosen where
the sentence above is ambiguous: a RUN of bad characters collapses to a
single `-`, so `a///b` is `a-b` rather than `a---b`, which is the same
treatment the sentence gives explicitly to a run of whitespace and the
only reading under which `///` reduces to blank as the next clause
requires.]**

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
   **[BUILT session 21: `live/preview.{hpp,cpp}` +
   `tests/test_live_preview.cpp`, running unguarded in every build. All
   16 fixtures that hold a picture, plus a generated IOC 288 signal
   because no recording carries one. Measured: the dead sector lands
   within **1 px** of the batch image's column on all eleven pulse
   fixtures and both phasing-anchored white-only ones, and the image and
   every row's placement are bit-identical at all seven block sizes from
   1 sample to 65536. The "stated tolerance" turned out to need splitting
   by ANCHOR CLASS — see §13 — because two fixtures contain nothing that
   says where the dead sector is, and no tolerance can be right about
   those. Six more claims the writing of it needed: no row ever starts
   behind the retained buffer (forward-only as a property of the memory);
   the two paths agree what kind of dead sector the station sends; the
   forward tracker keeps the sync the batch path keeps (which is what
   pins re-acquisition); every row's pixels are the ones its own reported
   start and period produce; and the live halves of PHASE and SYNC.
   Verified by mutation, seven of them, all killed — two survived an
   earlier version and both survivals were holes in the test. See §10.]**
4. **`png_roundtrip`** — the hand-rolled writer's output decodes back to
   the source pixels (checked against an independent decoder, e.g.
   Python/`sips`, in the test), and the file is a valid PNG.
   **[BUILT session 22: `live/png.{hpp,cpp}` + `tests/test_png.cpp`. The
   independent decoder is python3's stdlib — zlib, struct, binascii,
   sharing no code with the writer — and it asserts the container, not
   just the pixels: every chunk's CRC, IHDR's fields, every row's filter
   byte, IEND last with nothing after it. Sizes: 1810×300, 1810×2400 (67
   stored deflate blocks) and 3×2. The tEXt chunks round-trip, and two
   writes of one image are byte-identical. Absent python3 the test skips
   (77) rather than fails. Verified by mutation: wrong adler32, filter
   byte 1, CRC over the payload alone — each rejected by the independent
   side.]**
5. **`override_phase_seed`** — a `phase_anchor_hint` set near but not
   exactly on the true anchor lands the picture on the *true* anchor,
   not on the hint; and a hint pointing at the wrong candidate feature
   moves the picture to that feature. Pins §7.1's "seed, then refine".
   **[BUILT session 24: `tests/test_overrides.cpp`, run as two ctest
   targets over one binary. "Lands on the true anchor" is measured as a
   BYTE-IDENTICAL page against the un-hinted decode — "close enough"
   would pass an implementation that obeys the hint to within a pixel,
   and a pixel is what the refinement is for. The wrong-candidate half
   is generated, because nothing in the library holds a decoy of known
   position: white paper with one black bar a fifth of the way across,
   present on two rows in three, which is the wrong candidate as
   `stage_dead_sector` itself describes it ("a chart border is dark on
   many lines, never on all of them") and which therefore loses the
   global scan while staying a feature the template can lock onto. The
   hinted page comes out rotated onto it by 410 px, against a decoy
   whose own measured column is 410. Four more claims the writing needed:
   the default is not a value (an un-hinted decode reports none); the
   hint outranks the phasing anchor; five clicks across 3.5% of a line
   settle on one anchor 13 px from the independent phasing witness; and a
   hint cannot flip the dead-sector STYLE, which would switch the
   per-line tracker off as a side effect nobody clicked for.]**
6. **`override_sync_fallback`** — a deliberately wrong
   `clock_ppm_fallback` changes nothing on a fixture whose baseline
   exists, and *is* the value used on a white-only fixture where it does
   not. Pins §7.1's "fallback, not winner", which is the half of the
   decision most likely to be quietly implemented as a plain override.
   **[BUILT session 24: same file. Five wrong ppm values, ±2000
   included, leave the pulse fixture's clock at −86.6 ppm and its page
   byte-identical; four are each the reported clock, the drawn line
   period AND a different page on the white-only one. Plus the claim the
   NaN sentinel exists for: a SYNC of **exactly 0 ppm** is USED, not read
   as "no value" — it replaces the −75.2 ppm the fold measured and
   redraws the page. An implementation reaching for the usual
   `if (ppm != 0)` idiom passes every other check in the file.]**

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
   the transport inert on an INSPECTION run, because --metrics brings up
   no capture — the reason changed in session 23 (the program can capture
   now) but the check did not, and it is now also what stops inspecting
   Nova from opening a microphone;
   and the preference file beside the program read at startup without an
   inspection run creating one. Guarded by `NOVA_BUILD_GUI`.
   **[BUILT session 20: `tests/gui_shell.cmake`, sharing its `--metrics`
   parser with `gui_layout` via `tests/gui_metrics.cmake`. Verified by
   mutation: making Start sensitive during DECODING, and lighting the
   ruler with the width unknown, each fail it.]**

One more, added session 22 with the component it covers:

10. **`live_session`** — drive the state machine with whole fixtures and
    assert the §4 sequence: tone-driven openings walk IDLE → READY →
    START TONE → PHASING → DRAWING — PREVIEW, `nmc-image-stop-tone-120s`
    reaches STOP TONE on real audio and leaves it only after the tone has
    actually ended, and the frozen snapshot's batch decode carries the
    machine to SAVED. Also the drawing point against the batch
    segmentation's picture start, the snapshot's bounds (pre-roll to
    tone-start, picture to tone-end), the operator stop being the
    stop-tone path minus the tone, the give-up on a tone with no phasing,
    two transmissions back to back, the page cap, and — the claim the
    component screamers cannot make — the whole session's outcome
    identical at block sizes 1, 1000 and 65536.
    **[BUILT session 22: `live/session.{hpp,cpp}` +
    `tests/test_live_session.cpp`, running unguarded. Measured: drawing
    starts within 0.12 s of the batch path's picture start on all three
    tone-driven fixtures; the phasing rate seed is −14 ppm from the batch
    fit on VMW; preview dead sector +6/+1/+0 px against the saved image.
    Verified by mutation, seven, all killed — one survived an earlier
    version: ignoring start tones in SAVED, because the test drove its
    second transmission into DECODING and never exercised the SAVED edge.
    Both cases are driven now. **Extended session 24 with T12**, the
    live→batch link for §7.1's two fields: a PHASE and a SYNC set while
    the preview is drawing arrive at the decode unchanged and
    INDEPENDENTLY (one does not conjure the other), and an operator who
    touched nothing hands over the two defaults rather than two zeroes —
    which they have to, since 0 ppm is a legal clock and column 0 a legal
    anchor.]**

Two more, added session 23 with the wiring they cover:

11. **`live_ring`** — the audio ring of §2.1: order and values preserved
    across many wraps at 35 block-size pairs, exact overrun accounting
    in samples, a real producer racing a real consumer over four million
    samples, and **zero allocations on the realtime side**, counted with
    a thread-local `operator new`.
    **[BUILT session 23: `live/ring.hpp` + `tests/test_ring.cpp`,
    unguarded, 0.5 s. Verified by mutation, four: relaxing every
    release/acquire, giving away the reserved empty slot, dropping the
    overrun count, and letting the consumer read past the producer. The
    ORDERING mutation survived the first version of the test and needed a
    tight-handoff case to kill — see §2.1 and §10.]**
12. **`live_engine`** — the wiring of §2: the same recording through the
    engine and through a single-threaded `LiveSession` produces the same
    states, the same rows and the same saved pixels, at five audio block
    sizes; the save is named by §8.5 item 5's rule and its bytes are a
    real PNG; the tEXt QA records PHASE/SYNC provenance [§8.5 item 3];
    the file is written **before** the state reads SAVED [§8.5 item 1];
    and a ring too small for the feed counts what it dropped.
    **[BUILT session 23: `live/engine.{hpp,cpp}` +
    `tests/test_live_engine.cpp`, unguarded, 18 s. Verified by mutation,
    five, all killed. TWO survived earlier versions and both were holes
    worth the finding: dropping the resampler's end-of-stream tail
    changed nothing, because every fixture is at 8 kHz and the engine's
    resampler was in passthrough — the test now upsamples a fixture to
    48 kHz and feeds it at 48 kHz, which is the rate a sound card
    actually offers; and entering SAVED before writing the file was
    invisible until the test recorded the message ORDER, which is what
    §8.5 item 1's claim is actually about.]**

The suite count is now **"34 (+2 with the GUI)"** — session 22 added
`live_session` and `png_roundtrip`, session 23 adds `live_ring` and
`live_engine`, all four unguarded `nova-live` tests.

**The block-size sweep is now the suite's dominant cost, and it is worth
seeing the whole bill in one place.** `live_tones` runs 25 s and
`live_preview` 23 s of a 146 s total — a third of the suite, spent
almost entirely on rendering or detecting the same signals seven times
over at seven block sizes. That is the price of the two "identical
whatever the blocking" claims, and both have now earned it: the sweep is
exactly what caught session 21's trim-schedule bug, which was invisible
at every block size below 12345 and which no amount of reading the code
would have suggested. A future session may still want to trade it — the
honest lever is fewer block sizes on the long fixtures rather than fewer
fixtures, since the pathological cases are 1 and 65536 — but it should
trade it knowing that this sweep has caught a real bug, not merely cost
time. Session 19 decided
"24 (+1 with the GUI)" — correcting session 18's "23 (+1)" — on the
argument that a test of dependency-free `nova-live` code should run
everywhere. That argument is unchanged and now applies twice: item 9 is
a second guarded GUI test (+1 → +2), and item 1 is a second unguarded
`nova-live` test (24 → 25).

**DECIDED 2026-08-13 (Sara): the two GUI scripts stay two ctest targets.**
The "+1" is not worth preserving by merging them — `gui_layout` and
`gui_shell` pin different things (where the regions are, versus what the
shell does), and two targets name which of those broke without reading
the output. The count follows the tests; the tests do not follow the
count.

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

**A fifth, found session 23 by building §2, and it is a one-word error
with a real consequence.** §2.3 calls the GUI queue SPSC — single
producer, single consumer — in a paragraph whose own first sentence
names **two** producers, thread 2 and thread 3. Written down, the
contradiction is obvious; it survived four readings of this document
because "the workers push, the GUI drains" reads as one-to-one until you
count the workers.

Resolved by removing the second producer rather than by relaxing the
queue: thread 3 posts to a one-slot inbox and thread 2 does all the
emitting. The full argument, including why that is better than a
multi-producer queue and not merely cheaper, is in §2.3. Worth noting
alongside contradiction 1: both are cases where a sentence pooled two
things that live on different timelines — there the receivers' protocol
states with the nine batch stages, here the live decode thread with the
batch decode thread — and in both the fix was to keep the two apart
rather than to soften the claim.

---

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

**A fourth, session 21, and it is the same lesson a third time — but the
survivor is a new species of it.** `live_preview` failed on its first
run and found three real bugs, so it had already been shown able to
fail. Mutation testing still found two of seven breakages surviving:

- **drawing every row at the prediction instead of at its own lock**
  survived, because the prediction is re-seeded from each lock, so all
  that is lost is the within-row correction — 0.1–3 px on this library.
  No threshold on either geometry statistic separates that from a clean
  render without also failing the two warp fixtures, whose spread is a
  real tear the preview is *supposed* to show. Pinned instead by
  re-deriving each row's pixels from its own reported `start_sample` and
  `period`: the row must be drawn where the row says it was drawn;
- **ignoring the phasing anchor** survived for a reason worth naming.
  The test classified each fixture by asking the RENDERER which anchor it
  had used — so a change that stopped using the phasing anchor
  *reclassified itself* into the class the test does not check. **A test
  must classify its subject from the inputs it supplied, never from the
  subject's own account of what it did**, or a broken implementation gets
  to choose which claim it is held to. The classification now comes from
  `PreviewOptions`, and the renderer's own report is a separate assertion
  against it.

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

Verified: `NOVA_BUILD_GUI=OFF` builds the three CLIs and all 28 unguarded
test suites, which pass, and produces no `nova-gui`. (23 when this was
written at session 18; every `nova-live` screamer since has been added
unguarded, which is the argument in §9.)

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
  **NARROWED session 23, and the narrowing was forced by a surviving
  mutation.** Dropping the engine's end-of-stream resampler tail changed
  nothing, because at 8 kHz in and 8 kHz out the resampler is a
  passthrough with no tail — which means the engine's whole resampling
  path, the one every real capture uses, was untested. `live_engine` now
  upsamples a fixture to 48 kHz and feeds it at 48 kHz, and the streaming
  resample matches the whole-file resample of the same audio row for row
  and pixel for pixel. That is real recorded CONTENT through a real
  resampler, which is more than a generated tone; it is still not a real
  48 kHz capture, and that half of the gap stays open.
- ~~**No fixture in the library carries a STOP tone**~~ and ~~**nothing
  in the library fades mid-tone**~~ (both registered session 20).
  **CLOSED session 21 by one fixture, as predicted.**
  `nmc-image-stop-tone-120s.wav` is NMC 2204Z 340–462 s: a real chart
  ending in a real 450 Hz stop tone at 111.38–116.50 s of the cut
  [WMO §5.2.5], which **fades to nothing for 0.88 s in its middle** —
  the run-assembly gap-bridging rule of §5 meeting a real fade instead
  of a generated one. It also brings NMC into the fixture library for
  the first time, and exercises the tail half of segmentation, which no
  other fixture did: the batch decode drops 22 lines of stop tone and
  ends the picture at 111.17 s. Pinned by `tones_fixture_nmc_stop` (the
  batch detector, including that the fade is bridged into ONE run rather
  than two bursts, neither of which would reach `min_stop_sec`) and
  carried by `live_tones` and `live_preview`. The streaming detector
  commits it **3.12 s before the run ends**, at the same start time as
  the batch path, identical at every block size.
- **A white-only station with no phasing interval has no anchor at all,
  and the preview may draw the page rotated** (session 21, measured).
  This is a property of the transmission, not of the renderer: a
  white-only dead sector carries no per-line phase (session 4), so the
  only evidence is an across-line consistency profile — and the batch
  path builds one over 120 lines where a forward renderer has 16. Where
  those two profiles pick different candidates the two pictures differ
  by however far apart the candidates are: measured **563 px on
  `gya-weak-white-120s`** and 46 px on `vmw-white-sector-120s`, the two
  fixtures in the library that are in this class. `live_preview`
  reports this and does **not** pin it, deliberately — a tolerance wide
  enough to admit a third of a page would stop the check failing at all.
  The answer is not a wider tolerance but the operator: `ISO §4.2.6`
  manual adjustment, §7's PHASE control, and the screamer demonstrates
  that **one click lands the page to within 1 px of the batch image**.
  Worth knowing before M4 ships: on these two stations the operator will
  see the preview jump when the saved image replaces it unless they
  phase it themselves, and §8.5's swap should probably say so.
- The preview's row-placement quality has no target number yet.
  `place_rms_px` exists for the batch path; the equivalent for the
  preview is not defined, so §9's screamer 3 pins determinism,
  dimensions and where the dead sector lands, not quality. Session 21
  measured the raw material for one: the drawn dead-sector edge has a
  row-to-row roughness of **0.00 px on every fixture**, with 7.02 px of
  *spread* on the two warp fixtures — which is a precise statement that
  their edge takes one step and stays there, rather than jittering. A
  target number would have to be stated in those terms.
- **Two openings before one picture are drawn from the FIRST one, live**
  (session 22, measured and pinned by `live_session` T4). The batch rule
  "the last opening inside a known transmission" needs the stop tone to
  bound the transmission, and the stop tone has not happened yet when the
  live machine must commit. So FAXSignal's preview starts at 22.0 s (the
  first phasing's end) where the saved image starts at 64.5, and the
  second opening passes through the preview as picture rows. The saved
  image is correct; the preview cannot be, and a forward-only preview
  that waited to find out would not be a preview. Not to be "fixed" by
  holding rows back.
- ~~**Nothing has looked at a pixel of the wired window**~~ — **CLOSED
  session 25 by the M4 item-1 run, and it caught exactly the bug it
  existed to catch.** Sara ran the wired window against a live station
  (HLL, via a KiwiSDR through BlackHole 2ch): the blit into the pane, the
  level meter, the progress bar and the status line's saved-file name all
  verified by eye, and a full start→phasing→draw→stop→decode→save cycle
  produced `20260814T200737Z-JMH.png` (802 lines, 764 locked, −77.7 ppm).
  The run also found that the Device menu had never had a callback — the
  stream opened once at window-show on the system default and the meter
  answered the operator's voice with BlackHole selected. Fixed (§8.3 item
  9): the menu now reopens the stream on change, is insensitive while a
  chart is live, and persists by device NAME. What no screamer can reach
  stays open: the reopen callback itself, and RtAudio generally.
- **The second retained snapshot is not built** (§3, session 23). One
  snapshot exists at a time, so a decoded image has no raw stream behind
  it and the post-decode correction controls stay visibly disabled. It
  was waiting on §7.1's two `DecodeOptions` fields, because those are
  what give it something to do; they landed in session 24, so this is
  now merely unbuilt rather than blocked.
- **The GUI's PHASE/SYNC fields are still not covered by a screamer**
  (session 24). The live→batch link now is: `live_session` T12 drives a
  whole session and checks the two values arrive at the decode unchanged,
  separately, and that an untouched operator hands over the two DEFAULTS
  rather than two zeroes. What no test reaches is the widget end — the
  FLTK callbacks that read the text fields and call `set_phase` /
  `set_sync` — which is on the far side of the same seam as everything
  else in §13's first entry.
  **Narrowed twice in session 28, and this is now a much smaller gap than
  the sentence above describes.** Covered: the SYNC steppers (`--nudge N`
  drives the real callback through the real widget), click-to-set-PHASE
  and the two-click slant (`--click X,Y`, repeatable, through
  `click_image`), and the rules that fire when the surface changes under a
  half-finished edit (`--then-state`). All three report through
  `--metrics`, and each was verified by reintroducing the defect it exists
  to catch.
  **What is still uncovered, precisely:** the TEXT FIELDS — typing into
  the PHASE or SYNC box and having `cb_edit` fire — and everything from
  `cb_apply` onward, which writes files. Those remain on the far side of
  the same seam as §13's first entry. Note the shape of what the two
  narrowings actually bought: not "the widgets are tested" but "the
  handlers behind them are reachable without a window", which is a
  different and smaller claim.
  **And a caution earned the hard way, twice in one session.** Both new
  seams first produced checks that passed against builds with the rule
  deleted — the click guard was masked by `apply_state`'s edit-end
  cleanup, and the anchor-clearing check spanned two processes and so
  could not observe the transition it was about. A seam makes a rule
  reachable; it does not make a check correct. Ask what else could make it
  pass.
  **Session 29 extended the covered half and sharpened the caution.**
  Covered now as well: the arming buttons (`--arm phase|sync`, through
  `set_arm`, the buttons' own callback body), and gestures as SEQUENCES —
  `--arm` and `--click` share one ordered list, because a gesture is
  arm-then-click-then-click and two separate lists could only express "all
  the arming, then all the clicking", which is not any gesture an operator
  makes.
  **The sharpened caution is about SURVIVORS, not seams.** Thirteen
  mutations against the new rules gave ten kills, one void and two
  survivors, and the two survivors were the same kind of thing: not
  equivalent mutants, not code to delete, but **rules that were only ever
  exercised where they could not fail**. Dropping the zoom term from
  `slant_error_ppm` survived because every measurement check ran at 100%
  zoom, where the scale is 1.0 and the term is invisible; removing the
  anchor-clearing from `set_arm` survived because nothing re-armed
  mid-gesture, though the code's own comment claimed the rule. Both are now
  checked. The lesson to carry: **a check that only ever runs at the
  identity value of a parameter is not checking that parameter**, and a
  claim made in a comment is not a claim under test.
