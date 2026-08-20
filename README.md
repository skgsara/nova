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

## How Nova was written

**Nova's code was written by an AI agent (Claude), directed
session-by-session by Sara Sakuragawa, who owns every design decision in
it.** The development history is public and unedited: `SESSION-LOG.md`
records each session newest-first, append-only, and is corrected by
adding entries rather than by rewriting old ones.

**What has been reviewed by a person:** every design decision, and the
behaviour of the decoder against real off-air recordings — the charts
were looked at by eye. Several defects reached the fix stage only
because a human ran the program against a live signal and saw something
a passing test suite could not.

**What has not:** the code has not been line-by-line human-reviewed, and
AI training-data provenance is not introspectable by any party,
including the agent that wrote it. Nova's third-party lineage is
idea-level and documented in `NOTICE`, and that claim has been checked
against the actual upstream sources (see the audit below) — but no
process available here can rule out unconscious reproduction at a level
below what comparison detects.

**The audit.** Nova has been audited against a written protocol, by
agents that did not author it and under different models, with
cross-verification of every load-bearing citation by a model from a
different vendor, and a completed human sign-off gate that the audit
explicitly did not replace:

- the protocol — [`docs/07-audit-protocol.md`](docs/07-audit-protocol.md)
- its parameters — [`docs/06-audit-gate0.md`](docs/06-audit-gate0.md)
- the pass reports, cross-verification, and sign-off — [`docs/audit/`](docs/audit/)

The audit is a filter, not an assurance. Findings that are still open
are recorded in the reports as they stand, including the ones that
count against release.

## Features (target)

- IOC 576 and 288, automatic and manual selection
- 60 / 90 / 120 lines per minute, automatic and manual selection.
  WMO §5.1.5 lists a fourth rate, 240 lpm, which Nova does not
  implement — ISO 9876:2015 §4.2.4 requires only these three of a
  receiver. A 240 lpm broadcast will not decode at all.
- ±400 Hz (HF) and ±150 Hz (LF) subcarrier deviation
- Automatic start (300/675 Hz), stop (450 Hz), and phasing
- Automatic clock-error (slant) correction: rate estimated from the
  signal, phase re-locked per line from the dead-sector sync pulse where
  the station sends one — no TCXO or GPS-disciplined clock required
- Post-decode image realignment and line-start adjustment on the
  retained raw stream (non-destructive)
- Input: **WAV only.** m4a/AAC via an installed `ffmpeg` is planned and
  **not implemented** — there is no ffmpeg or subprocess code in the tree,
  and an m4a file fails with `not a RIFF file` [audit Pass E, E-CLAIM-003].
  Convert first.
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

**M2 done (2026-08-12):** the line period is measured from phase drift
accumulated across the whole recording, which is the only thing that
makes it accurate — over a short span the answer is dominated by the
quantization of its own measurements. Two estimators share that
principle: folded-block phase tracking, which needs no sync locks and so
serves the white-only stations, and a long-baseline fit over locked lines
for everyone else. Residual shear of the decoded image is ≤10 ppm across
the library; a signal generated at −137 ppm is recovered as −137.00.

This replaced a fit over neighbouring lines that was wrong by up to 250
ppm and had been shearing three of the library's recordings by a third of
a page while every lock metric stayed green — found by measuring decoded
pictures rather than decoder statistics.

**M3 done bar manual override (2026-08-12):** the control signals — 300/675 Hz start,
450 Hz stop [WMO §5.2.2, §5.2.5] and the 30 s phasing interval
[WMO §5.2.3] — are detected and measured. Detection accepts on spectral
*purity* rather than a transition rate, which is what lets it tell a
control tone from dense weather text: across 5.9 hours of library audio,
picture content never exceeds 0.16 in a control band while real tones run
0.68–0.99. Zero false positives; 14 of the 20 recordings turn out to carry
a start tone, and 16 a full phasing interval.

The sixteenth took a while to see. A phasing interval on a badly faded
signal keeps its edge and loses its contrast, so the per-line score that
identifies phasing on a strong signal drops *below* what dark picture
content scores — one library recording's real interval runs 0.34–0.88,
against 0.48–0.62 for content. Nova therefore finds the interval by where
the white edge is rather than by how well each line scores, and a run
survives faded lines instead of ending at the first one. That recording
sends a plain white dead sector, so this interval is the only per-line
phase it has anywhere, and without it the chart came out half a line
rotated (2026-08-12).

Sequencing is wired. The decoder draws the picture and not the control
signals, and on stations sending a plain white dead sector it takes its
line start from the phasing interval [WMO §5.2.3.4] — the only per-line
phase such a station has. That was worth more than it sounds: their
image-derived anchor had been locking onto the chart's blank margin
rather than the dead sector, and VMW's charts were coming out **rotated
by 520 px of 1810**, with the right-hand margin wrapped around to the left
edge. Stations that do send a sync pulse keep tracking it, and decode
byte-for-byte as before; the disagreement between the two anchors is
reported on every decode, and on pulse stations it measures the black
porch — two recordings of the same transmitter agree on it to 3 samples
of 4000.

That agreement is now a test, and chasing the two recordings that broke it
turned up something about the library rather than the code: JSC2 and JSC3
carry ~21-sample steps in their timebase every few lines — present in the
recorded audio, still there at the source's native 44.1 kHz through a
separately written demodulator, and absent from the other eighteen
recordings. Measured across the phasing→image boundary, where no clock
model stands between the two anchors, both files' porch is ordinary. What
their `clock_ppm` reports is the clock plus the mean insertion rate. It is
also the case that decides an open design question: a station sending a
sync pulse keeps its tracked anchor, because a phase measured once and
propagated on a fitted clock cannot survive a timebase that steps, and one
re-measured every line absorbs it without being told.

**The decoder now says when a recording's timebase is not a straight line
(2026-08-12).** Every time figure it reports — the clock, the line period,
the anchor delta — assumes one constant rate, so where that assumption
fails the numbers quietly change meaning. Nova measures it two ways that
share no code: the tracked sync residual, local-median smoothed, whose
step rate runs 0.0–7.0 per 1000 lines on clean recordings against
64.8–339.8 on the affected ones; and the phasing edge's departure from a
straight line, 1.0–3.8 samples against 20.2–25.5. Either is enough on its
own, so a station sending no sync pulse is still covered, and where
neither is available the decoder says "not measurable" instead of
guessing.

A bent phasing edge is not by itself a stepping timebase, and since
2026-08-12 Nova distinguishes the three reasons an edge bends. A faded
interval's edge moves further line to line than the effect being measured,
so it cannot resolve the question either way and says so; a single
time-skip is one jump and not a rate, however large it reads, so
conviction takes more than one *persistent* move. Both cases occur in the
library, and both used to be reported as a stepping timebase — one of them
on evidence that 1922 tracked lines of the same recording contradicted.

Run over the library, that turned the count from two recordings into
**six** — every JSC file, including the three at 60 lpm whose +335 to +458
ppm clocks nobody had questioned. None of the mature decoders reports this
condition: JWX applies one operator-typed constant to every line,
weatherfax_pi hears about lost samples only when PortAudio tells it, and
fldigi builds the histogram that would show it and then keeps only its
peak. Nor does any of them repair one: all four correct a constant clock,
three of them from a single measurement taken before the picture starts.

Since session 11, Nova does repair it, and the reason is that the operator
looked at the pictures. Sara reviewed all twenty decoded charts by eye and
named six that "zig zag" — every one of them a JSC recording, which is the
set the flag had been convicting for two sessions while the decoder drew
them anyway. The tracked sync residual is now cut at the lines where it
persistently moves, a robust line is fitted inside each piece rather than a
level, and a real skip is followed in one line instead of ramped across
seventeen. Ground truth, on a signal generated with insertions at a known
rate: 2.19 px of scatter on the straightness bar before, **0.00** after.
Six library recordings improved, fourteen unchanged, none worse. A station
with no sync pulse still has no per-line phase to correct — VMW 2215Z's
staircase is the open half of that milestone.

Manual override [ISO §4.2.6] works end to end as of session 24: the
operator gives a PHASE — where the dead sector is, as a fraction of the
line — and a SYNC ppm trim, both applying forward from the next row so
that rows already drawn never move, and both carried into the batch
re-decode of that transmission. **They are deliberately asymmetric
there.** PHASE is a *seed*: auto-phasing fails by picking the wrong
candidate for the dead sector, so the click says which feature, and the
decoder refines where. SYNC is a *fallback*: a ppm eyeballed off thirty
seconds of preview loses to one fitted over the whole transmission, so
the measurement wins wherever it has a baseline, and the operator's value
is used only where it has none — a white-only station, a forced start,
too few locked lines. On a healthy recording the typed SYNC is therefore
measured away from, and the file's QA header says so rather than
recording it as the operator's.

Since session 27 the same two corrections reach a chart that has already
been **saved**. Every completed transmission keeps its raw demodulated
stream — the one the picture was decoded from — for exactly as long as it
is the picture on screen, so Apply re-renders from the signal rather than
from the pixels, and writes the result back over the same file: one
transmission, one file, and no Save button, because a chart corrected and
then closed with the good version only in memory is the failure the whole
lifecycle was shaped to remove. Auto puts the automatic decode back byte
for byte, which is only possible because the decode's own settings were
kept beside the stream. A correction that cannot be made says why rather
than being a grey control with no explanation.

Session 28 gave SYNC four steppers — −10 −1 +1 +10 — because nulling a
slant is a judgement made by eye ("a bit more"), not a number anyone can
read off a chart. From a blank box a nudge starts at the clock the
picture was *drawn on* rather than at zero: blank means "as measured",
and on a white-only station the measured clock is −70 to −118 ppm, so
starting at zero would make the operator's first click a jump of the
entire error, away from correct. PHASE has no steppers on purpose — it
is a seed the decoder refines within ±54 columns, so a smaller nudge
would be refined straight back and the picture would not move. Its real
instrument is the click. The steppers sit **directly beneath the SYNC
box**, on a row of their own, with PHASE and SYNC above them as two
matching rows of caption, box and arming button. Session 29 had instead
made the steppers *flank* the SYNC box — a stronger way to say whose they
are, since a control touching a box on both sides cannot be read as
belonging to another box — after the first person to see the panel read
them as PHASE's. Session 30 traded that back for the present shape at
Sara's request, having by then used the window rather than only looked at
it: flanking cost SYNC's caption its place beside its own box, and the
resulting half-empty row was what actually made the block look heavy. The
tie between the steppers and SYNC is now adjacency rather than enclosure,
which is weaker, and `gui_shell` pins the weaker claim as such.

**Both picture gestures are declared.** A small button beside PHASE arms
"click the dead sector"; one beside SYNC arms "click the same feature
twice". A click on the picture with nothing armed does nothing at all —
which is the point, since the alternative is an accidental click silently
moving the operator's data. Arming a gesture puts a crosshair on the
picture and a sentence in the reason line, and the gesture disarms itself
when it completes, so there is no mode to be stuck in. This is hamfax's
shape (it has had both corrections, both armed, since 2001) adapted to
Nova's surface: an arming button and the reason line, not a menu action
and a modal prompt.

Two clicks on one feature measure the slant between them and fill SYNC,
because a slant is the same feature at two rows, and two clicks a thousand
rows apart are a baseline rather than an eyeball. **A short baseline still
measures, and says what it is worth**: one screen pixel of click error is
one column at 100% zoom, so ten rows apart is ±55 ppm — as large as the
30–180 ppm the control exists to remove — and the reason line says so
rather than refusing the measurement. The operator's judgement, with the
error bar attached. The one thing refused is two clicks on the same row,
which has no baseline at all; the gesture stays armed so they can simply
click again.

**A transmission arriving while you are correcting a chart does not take
the screen.** It draws into a buffer behind a compact receiving panel —
state, line count, thumbnail — and comes forward only when that panel is
clicked. Nothing promotes on its own, so pressing Apply cannot replace
your own correction with the incoming page; and the hold is on the pane
and never on the disk, so the new transmission is still saved to its own
file the moment it finishes. Three things travel with a picture and every
one of them would be a defect alone: its pixels, the raw stream a
correction re-decodes from, and the file a re-render overwrites. They are
parked as a unit and handed over as a unit, or an Apply meant for the
chart in front of you would write over the chart that just arrived.

That case needs a receiver and two transmissions to reach, so for one
session it was the best-reasoned and least-tested thing in the program.
Given an instrument — a capture driven from a recording, through the real
engine, with no sound card — it turned out to hold two defects. Apply
asked the *session* which surface it was correcting rather than asking
the *pane*, so with a transmission arriving it re-rendered nothing and
delivered the typed correction to a picture the operator could not see.
And the click that hands the pane over could never have worked at all:
the promotion happens on another thread and announced nothing, so the
window asked whether it had happened before it could have, and never
asked again. Both are fixed and pinned. Neither was a hard problem; both
were simply in the one place nothing had ever run.

**M4 decodes live (2026-08-14): audio in one end, a saved chart out the
other.** The design was settled first, against a survey of sixteen
commercial weather-fax receiver manuals — which is where Nova's two
picture corrections, its named protocol states and its refusal to show a
progress percentage all come from. Since then: a streaming front end that
agrees with the batch path sample for sample, a tone detector that
commits a verdict seconds before a run ends, a forward-only renderer that
draws the picture as it arrives, the session state machine that turns
those into a protocol, and the four-thread wiring that connects a sound
card to a PNG.

Everything on that path lives in a library with no GUI dependencies and
no clock of its own, so all of it is driven in tests by feeding a
recorded fixture through it faster than realtime. The claim that buys is
that **threading changes nothing about the picture**: the same recording
through the live path and through a single-threaded one produces the same
states, the same rows in the same places and the same saved pixels, at
audio block sizes from one sample to 65536. What is left in the GUI
binary is widgets, one audio callback and a timer.

Things the window was built to find, it found. The image pane's ruler is
the surface the operator sets the picture phase on, and it has to name
the image column beneath it exactly; drawn from the mockup it was six
pixels out, and fixing that at one window size did not fix it at any
other, because the toolkit's own resize logic stretched it back off. The
layout is therefore computed rather than scaled, and a window built at a
size is now provably identical to one dragged to it.

And proven on the air the same day: two live broadcasts decoded
unattended through a KiwiSDR browser feed, start tone to saved PNG. The
catches paid for the effort immediately — a Device menu that had never
had a callback, a Start button that was swallowed in SAVED — and one
finding that took three tries to read right: a chart whose dead-sector
edges jogged turned out to be false per-line locks on a faded signal
(dark picture content crowding the sync template's white window), not
the internet drops that were also present. A level move that returns
within the vouching distance is a lock error, not a stream event, and is
no longer followed [docs/01 §5 item 3].

Synthetic tests cover the full {IOC 288, 576} × {60, 90, 120 lpm} matrix
with automatic selection, ±150/±400 Hz deviation, a two-order-of-magnitude
input-amplitude span, dead-sector tolerance edges, the eight-tone gray
scale, clock error to ±250 ppm (including a white-only signal with nothing
to lock onto), heavy noise, and both phasing waveforms. See `ROADMAP.md`
for the milestone map.

## Security posture

Nova decodes audio that arrives from somewhere else — a shared recording,
a receiver nobody controls. A WAV header is a set of *claims* about a
file, and until session 32 every one of them was believed.

**What is assumed about input:** nothing. `read_wav` clamps the declared
data-chunk size to the bytes that actually exist, and refuses a sample
rate that cannot carry the signal (WEFAX white is 2300 Hz, so at or below
4600 Hz Nyquist says it is not a recording of one). The resampler bounds
its ratio independently, because a bound at the file boundary is one new
entry point away from being absent.

**What was found and fixed** (audit Pass D, 2026-08-16): a 144-byte file
declaring a 4 GB data chunk reached ~12.9 GB of footprint and did not
terminate; a *well-formed* file declaring a 1 Hz sample rate hung the
resampler. Both now refuse in ~0.01 s at ~1.4 MB. `tests/test_malformed.cpp`
pins the guards and generates its own inputs, so it runs from any
checkout. Five mutations against it were each killed by the intended
check.

**Fixed after the audit's passes ran:** the sustained-tone case on the
live path (D-PERF-003 / E-RISK-002) — a start tone that never ends, which
none of `preroll_sec`, `phasing_wait_sec` or `max_picture_sec` bounded —
is now bounded by an opening cap (`max_opening_sec`, 300 s): the session
abandons the opening and returns to listening, and the retained store
falls back to its pre-roll bound [session 33; pinned by `live_session`
T14].

**What is not claimed:** Nova has had no coverage-guided fuzzing, no
third-party security review, and no CI. The corrupted-input testing it has
had is a hand-built corpus of ~30 malformed files across two audit passes.

## Platforms

**Built and tested on macOS arm64 only.** That is the whole of it: there
is no CI, and no other platform has been built, so any claim about one
would be a guess. The decoder, the command-line tools and the test suite
are plain C++17 with no external dependencies, which is a reason to
expect other 64-bit platforms to work — not evidence that they do.
Reports from anywhere else are welcome.

No 32-bit build has been attempted, and nothing here is written against
an old-hardware floor.

## Build

Plain C++17 + CMake.

```
cmake -B build -S .
cmake --build build
ctest --test-dir build
```

**What you will see: 8 suites run, 30 report Skipped.** Nova's real-signal
tests need off-air recordings of weather-fax broadcasts, and this
repository does not redistribute them — the copyright of a transmitted
chart varies by issuing meteorological service, and one of the stations in
the set is a commercial news agency. `fixtures/MANIFEST.md` carries what
can be published: every recording's station, provenance, format and
SHA-256, alongside the pass/fail bounds, which live in `CMakeLists.txt`
next to each test. So the claim being made — *this recording, this hash,
decodes within these bounds* — is fully stated and checkable by anyone
holding the same file.

The 8 that run anywhere are not a token: they include the full
{IOC 288, 576} × {60, 90, 120 lpm} synthetic matrix and the
untrusted-input guards, which generate their own malformed files.

**The decoder, the command-line tools and the whole test suite have no
external dependencies.** FLTK and RtAudio are needed by the GUI binary
alone (`nova-gui`), behind `option(NOVA_BUILD_GUI)`, which is ON by
default. If either library is absent the configure step prints
`nova-gui: SKIPPED` and everything else builds and passes as before;
`-DNOVA_BUILD_GUI=OFF` does the same deliberately. FLTK is located
through `fltk-config` and RtAudio through `pkg-config`, since neither
ships a CMake config package.

The GUI decodes live: pick an input device, press Start, and rows appear
in the pane as they are drawn while the chart arrives. A completed
transmission — ended by its stop tone, by Stop, or by closing the window
— is decoded from the retained audio and saved as a greyscale PNG named
by UTC timestamp, with the decode QA in the file's text chunks. Force
Start begins mid-transmission with an operator-supplied IOC and rate; the
PHASE and SYNC controls correct the picture forward while it draws.

Everything between the sound card and the saved file lives in the
dependency-free `nova-live` library and is covered by the test suite; the
GUI binary is widgets, one audio callback and a timer.

It also answers two questions without opening anything — and opens no
audio device when it does:

```
./build/nova-gui --devices
./build/nova-gui --metrics
```

## License

GPLv3+ — see `LICENSE`. Third-party lineage and standards citations in
`NOTICE`. Nova's DSP lineage traces to ACfax → HamFax → yahfax →
weatherfax_pi → KiwiSDR FAX, used under their GPL licences with
attribution.
