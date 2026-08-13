# 04 — Commercial receiver operator-interface survey (M4 design input)

Date: 2026-08-13. Surveyor: Claude Opus 5 + Sara.
Corpus: 16 operator's manuals covering 15 distinct receivers from 7
manufacturers, spanning roughly 1985–2024. Sara collected them.

**What this document is.** A record of *how commercial WEFAX receivers
present the job to their operator* — controls, status, adjustments,
scheduling, storage — gathered before designing Nova's GUI, in the same
reuse-first spirit as `docs/00`. It answers the eight open M4 design
questions in `docs/03` from precedent instead of invention.

**What it is not.** Nothing here is copied. The manuals are third-party
copyrighted works; they are read for *facts about conventions* — that
every receiver has two picture corrections, what the status line
carries, how AUTO is offered — and none of their text, screen layouts or
artwork is reproduced in Nova or in this file. The PDFs stay outside the
repo, like the standards and recordings (`docs/00`, session 13). The
convergence across seven competing manufacturers over four decades is
itself the finding: these are industry conventions, not any one firm's
expression, and a WEFAX receiver that ignored them would be the odd one
out.

---

## The corpus

| Receiver | Class | Contribution |
|---|---|---|
| Furuno FAX-30 (OME-62600, eds. K1/L1) | black box + PC/MFD software | **The closest precedent to Nova**: the whole operator interface is software |
| Furuno FAX-408 | thermal paper recorder | Full control-by-control panel description |
| Furuno FAX-410 (OMC-62610H) | thermal paper recorder | Confirms 408 conventions unchanged |
| Furuno FAX-207 / 208A / 210 / 214 | paper recorders, 1980s–90s | Same generation; sampled only |
| JRC JAX-91 / JAX-9B | paper recorder, LCD status | 40-character status line, AUTO/MANU/forced |
| Samyung SFAX-500 | LCD, stored images | Image toolset with numeric PHASE/SYNC |
| Samyung SFX-100 | full GUI on a screen | **Persistent status bar; AUTO as a value in every field** |
| Steamrock SR-97 | portable e-paper touchscreen | **Live receive states; touch-the-dead-sector phasing** |
| Taiyo TF-711 | paper recorder | Phase step expressed as % of paper width |
| Sony CRF-V21 | consumer receiver | Principles booklet; little operator UI |
| Nagra FAXDM | short document | Minimal |

Two generations, and the split matters more than any other distinction:

- **Paper recorders** (408, 410, TF-711, JAX-9B, 207–214). Corrections
  are made *during* reception and are destructive — the paper is already
  printed. Every adjustment is therefore a nudge applied to the rest of
  the picture.
- **Stored-image receivers** (FAX-30, SFAX-500, SFX-100, SR-97). The
  image is kept and corrections are applied *after* reception,
  non-destructively, and can be redone.

**Nova is unambiguously in the second group**, which is also the group
ACFax's retained-raw architecture belongs to (`docs/00` reuse ledger).

---

## Finding 1 — every receiver has exactly two picture corrections

Not one, not five. Across all seven manufacturers and both generations,
the operator is given **PHASE** (where the line starts — moves the dead
sector sideways) and **SYNC** (line-rate trim — makes a slanted dead
sector vertical). Nothing else touches geometry.

| Receiver | PHASE | SYNC |
|---|---|---|
| Furuno FAX-30 | number 00–40, read off a scale drawn along the image | −50 … +50 |
| Samyung SFAX-500 | number 0–31, read off a graduated ruler | −200 … +200 |
| Furuno FAX-408 / FAX-410 | ◀ / ▶ keys, ~5.0 mm of paper per press | analog knob |
| Taiyo TF-711 | ◀ / ▶ keys, 2.5 % of paper width (~6.4 mm) per press | analog knob |
| JRC JAX-9B | PH key, horizontal start position | dial, centre-detented |
| Steamrock SR-97 | **touch the dead sector itself** | two increment buttons |

Three things fall out of this table.

**It maps exactly onto Nova's two measured quantities.** PHASE is the
phasing anchor (session 7); SYNC is the fitted line period and timebase
(sessions 5, 8, 9, 11). Nova measures automatically what these receivers
asked the operator to dial in — so the manual controls are the
*override* surface, and there is a precedent-backed answer to what the
override must expose: exactly two numbers.

**The good designs make the operator read the picture, not guess.** The
Furuno and Samyung both print a *ruler along the image* and ask the
operator to type the coordinate where the dead sector sits. The operator
never estimates a correction — they report a measurement, and the
receiver computes the shift. That is a far better interaction than a
nudge key, and it is directly implementable.

**The SR-97 improves on it again**: the operator touches the dead sector
and the device re-centres from there. Same idea — report the position,
don't guess the delta — with the ruler removed. Its manual adds two
cautions worth keeping: it centres *the remainder* of the image (it is a
forward correction, not a retroactive one), and the operator is told to
touch once and wait several lines before judging.

**Both are per-station, and persist.** The SR-97 stores the slant
setting independently per station. The JAX-9B warns that SYNC, once set
for a station, holds for that station but usually needs re-adjustment
when the station changes. Nova measures this per transmission, so it
inherits the benefit for free — but it confirms per-station memory is
the natural unit for anything the operator does adjust.

---

## Finding 2 — AUTO is a value, not a mode

This is the single most transferable pattern in the corpus.

- **Samyung SFX-100 channel setup** presents ZONE / STATION / CHANNEL /
  IOC / SPEED / FREQUENCY as six fields, and `AUTO` is simply one of the
  selectable values *inside* the IOC, SPEED, CHANNEL and FREQUENCY
  fields. There is no separate "automatic mode" switch to get out of sync
  with the fields.
- **Furuno FAX-30 timer programs** do the same: a program reads
  `IOC AUTO`, `SPEED AUTO` alongside explicit zone, station and times.
- **Steamrock SR-97** sets LPM, IOC and RCV=AUTO|MAN per station.
- **JRC JAX-9B** toggles AUTO/MANU, and notes that in MANU the operator
  *must* supply IOC and scan speed — the mode and the fields are coupled.

Nova already works this way in the core: `DecodeOptions::ioc` defaults to
automatic and the first start tone selects 576/288, with the CLI able to
override (session 13). The GUI should surface that as `IOC: [Auto ▾]`
with 288 and 576 as siblings — not a checkbox plus a disabled field.

**And every one of them has a forced start.** Without exception:
JAX-9B `#`→REC ("Forced recording … used when recording does not start
in AUTO and MANU mode"), FAX-30 `START RX` with an explicit IOC/rpm pair
chosen from ten combinations, SFX-100 `MANUAL RCV`, SR-97's start button
("if you have missed the start signal or it was not properly detected,
you can still initiate reception"), FAX-408 `RCD`. The industry's
unanimous verdict is that **automatic detection will fail and the
operator must be able to say "start now, with these parameters, I don't
care what you detected."** Nova has the same failure mode registered
already — faded signals, the GYA 2300Z gap.

---

## Finding 3 — the receiver narrates which part of the transmission it is in

The operator is never shown a bare progress bar. They are shown *the
state of the protocol*:

- **SR-97**: `WEFAX READY` → `APT DETECTED` → `SYNCHRONIZING` → drawing,
  with a small `RCV` indicator → on stop, `SAVE?`.
- **JAX-9B**: an operating-status field reading `RECORD`, `PHASE`, or
  blank.
- **FAX-408 / TF-711**: the RCD lamp *flashes* while the start signal is
  being received and goes *steady* once recording.

This is a one-to-one match with Nova's nine decode stages (session 14) —
onset, dead-sector, phasing, sync-track, period-fit, segmentation,
timebase, change-points, assembly — and with the control tones
`nova-tones` already detects. The progress callback built in session 14
is exactly the seam this needs; what the corpus adds is that the
*names*, not the fractions, are what belongs on screen.

---

## Finding 4 — the persistent status line, and what is on it

The SFX-100 carries a two-row status bar across the bottom of every
screen; the JAX-9B carries the same information on a 2×20-character LCD;
the FAX-30 writes it as a header line above each stored image. Pooling
them, the fields that recur are:

| Field | Seen on |
|---|---|
| Receive mode (AUTO / MANU / forced) | SFX-100, JAX-9B, SR-97 |
| IOC (or `IOC-AUTO`) | SFX-100, JAX-9B, FAX-30, SR-97 |
| Line rate / rpm (or `RPM-AUTO`) | SFX-100, JAX-9B, FAX-30, SR-97 |
| Operating state | SFX-100 (`STANDBY`), JAX-9B, SR-97 |
| Frequency | all |
| Channel number and station call sign | all |
| Date and time of reception | SFX-100, FAX-30, SR-97 |
| Normal / reverse | SFX-100, JAX-9B (`R`), FAX-30, SR-97 |
| Half-tone (photo) mode | JAX-9B (`H`), SFAX-500 |
| External receiver / attenuator | JAX-9B (`E`, `A`) |
| **Signal strength and S/N** | FAX-30 (`SS xxx  SN xx`) |

The last row is the one Nova has no equivalent of yet, and it is worth
noting: the FAX-30 shows a live signal-strength and signal-to-noise
readout *while receiving*. Nova's honest lock metric (session 3) and its
phasing/timebase statistics are the natural analogues.

---

## Finding 5 — storage is a ring buffer with pinning

Every stored-image receiver uses the same policy, and none of them asks
the operator to manage memory:

- **FAX-30**: 12 images across two pages of thumbnails; when full, the
  oldest is deleted automatically to make room; `LOCK` marks an image
  exempt from erasure.
- **SFAX-500 / SFX-100**: thumbnail grid, `LOCK` with a padlock icon on
  the thumbnail, delete-all as a separate destructive command.
- **SR-97**: 200 images, reverse-chronological list, oldest deleted when
  full; a received image longer than the screen is stored as two or more
  pages.

So: **bounded ring buffer, newest first, oldest silently evicted, with an
explicit pin to opt out** — and a confirmation prompt on anything that
destroys many at once. That is the memory policy for an all-day capture,
answered by unanimous precedent.

---

## Finding 6 — a transmission ends in exactly three ways

The SR-97 documents this most completely, and the others agree:

1. **The operator stops it.** (All receivers.) On the SR-97 the image is
   then held with a `SAVE?` prompt — stop does not mean discard.
2. **The stop tone is detected.** In AUTO the receiver saves, clears and
   returns to ready; in MAN the SR-97 holds the image for a save/delete
   decision.
3. **A page limit is reached.** The SR-97 lets the operator preset the
   maximum pages per transmission, and stops there "even when the device
   is set in MAN mode … This feature ensures that the device does not
   keep receiving an unlimited number of pages if a station's stop signal
   is not detected."

The third is the interesting one: it exists purely as a guard against
failure of the second. Nova's segmentation currently takes the first
transmission and drops the rest (registered gap, session 7); for live
operation the page cap is the precedent-backed safety valve.

---

## Finding 7 — the operator declares the content type

The JAX-9B has a `HALF` key for half-tone, and tells the operator to use
it for satellite cloud pictures and to leave it off for charts and text,
because half-tone makes charts *less* legible. The SFAX-500 has a
half-tone setup menu; the FAX-30 offers monochrome / 16-level gray /
three false-colour palettes as a post-reception choice.

Two consequences for Nova. First, photo-vs-chart is an operator-declared
setting in every commercial receiver — nobody tries to detect it.
Second, Nova has a registered gap that is exactly a photo-content
problem: picture content that mimics the sync pulse, which the synthetic
test pattern triggers with 629 false locks. A content-type declaration is
the precedent-backed place to hang a defensive path if that gap is ever
hit in the field.

---

## Finding 8 — scheduling is a first-class feature, and it is modest

- **FAX-30**: 30 timer programs. Fields per program: zone, station,
  channel, IOC, speed, start time, end time, frequency. Programs can be
  individually enabled/disabled, listed in time order, and cleared en
  masse behind a confirmation. Only the nearest 10 are shown at once.
- **FAX-408**: 16 programs, plus a sleep timer and a key-lock while the
  timer is armed.
- **SFAX-500 / SFX-100**: timer setup as a top-level menu item next to
  channel setup.

Note what is *not* there: no recurrence rules, no calendars, no
per-weekday logic. A program is a start time, an end time and a channel.

---

## What Nova takes, and what it leaves

| Take | Leave |
|---|---|
| Exactly two geometry overrides, PHASE and SYNC, as the entire manual-correction surface | Nudge-key phasing (`◀`/`▶` by 5 mm) — a stored-image receiver can do better |
| Ruler-along-the-image phasing: the operator reports where the dead sector *is* | Typing a number when the picture can be clicked |
| Click/drag the dead sector directly (SR-97), correcting forward, with a "wait a few lines" affordance | |
| `AUTO` as a value inside the IOC / rate / frequency controls | A separate "auto mode" toggle |
| An always-available forced start with explicit IOC + rate | |
| Named protocol states (ready → tone detected → phasing → drawing → stopped) instead of a bare percentage | |
| A persistent status line: mode, IOC, rate, state, frequency, station, time, reverse, quality | Attenuator / external-receiver flags (hardware-specific) |
| Bounded newest-first image ring with an explicit lock/pin | Operator-managed storage |
| Three stop conditions including a page cap as the stop-tone failsafe | |
| Operator-declared photo/chart content type | Auto-detection of content type |
| Timer programs as {channel, start, end}, individually toggleable | Recurrence rules |
| Per-station persistence of anything the operator adjusts | |

---

## Answers to the eight open M4 questions (docs/03 §"M4 design decisions still open")

**All eight now have answers** — five from precedent, three decided by
Sara on 2026-08-13 during this survey. `docs/03` made answering them the
gate for starting M4 GUI design ("on paper first, before GUI code"); that
gate is cleared.

1. **Streaming model.** **DECIDED 2026-08-13 (Sara): two paths.**
   Precedent is unanimous for *drawing as it arrives*: every receiver in
   the corpus, both generations, paints the picture line by line while
   receiving, and the SR-97 explicitly warns the operator to wait for
   lines to be drawn before judging a correction.
   Monitor-then-decode-on-stop-tone has no precedent and would break the
   states in Finding 3. But Nova's long-baseline period fit and bracketed
   dropout repair genuinely cannot work on line 30 — which is precisely
   why the commercial sets had a SYNC knob: they made the operator supply
   what the machine could not yet measure.

   Nova does not have to. The decided design combines the two commercial
   behaviours (Findings 1 and 6, and the "re-render the stored image
   afterwards" of the FAX-30 and SFAX-500):

   - **Live view** — provisional forward draw from the best current
     estimate, single pass, *never revised*. This is the SR-97's model
     exactly. Being wrong here costs a preview.
   - **Saved image** — when the transmission ends, the retained raw
     stream is decoded through the existing batch path, which by then has
     the long baseline, the dropout brackets and the change-point fit.
     This is the FAX-30's operator-initiated re-render, made automatic.

   The consequence that makes this worth doing: **`decode_fax` does not
   become incremental.** The tested nine-stage core (session 14) is
   untouched and its 23 suites keep meaning what they meant. The new
   incremental code is confined to a provisional renderer whose output is
   never the artifact the user keeps.
2. **Multi-transmission live sessions.** Answered: one transmission →
   one image, ended by operator stop, stop tone, or a preset page cap
   (Finding 6), then saved and the receiver returns to ready.
3. **Incremental tone scan.** **DECIDED 2026-08-13 (Sara): follows from
   item 1.** The live path needs a streaming tone detector to know when
   to start drawing; the batch path keeps its existing full scan (~9 s on
   JSC4) because it runs once, at the end, on a recording that is already
   complete. The industry accepts the standby cost of continuous
   monitoring as the price of automatic start — the SR-97 notes AUTO
   standby "uses slightly more battery power" and recommends it anyway.
4. **Early-window status.** Answered: show the *state name*, not the
   number, until the measurement has a baseline (Finding 3). The +261 ppm
   from a short baseline should never be displayed as a figure; the UI
   says "phasing" / "drawing", and clock and timebase readouts stay
   blank or marked provisional until the long baseline exists.
5. **Provisional draw/redraw.** **DECIDED 2026-08-13 (Sara): the live
   view is never revised.** No commercial receiver redraws on its own
   initiative — paper recorders physically cannot, and of the
   stored-image sets the SR-97 corrects only "the remainder of the
   image" and states that stored images cannot be modified, while the
   FAX-30 and SFAX-500 re-render the whole image only when the *operator*
   asks. Nova follows: rows land once, nothing rearranges itself
   mid-picture, and the improved rendering arrives as the saved image
   under item 1 rather than as flicker in the live view.
6. **Retained raw + post-decode adjustment.** Answered by the whole
   stored-image generation and by ACFax in `docs/00`: keep the raw
   stream, apply PHASE and SYNC non-destructively, allow redo. Note that
   the item-1 decision promotes this from a convenience to a
   **load-bearing requirement** — the saved image *is* a decode of the
   retained raw stream, so the retention policy is now on the critical
   path, not an optional nicety.
7. **Memory policy.** **DECIDED 2026-08-13 (Sara): unbounded, to a
   user-set folder, as greyscale PNG.** The corpus's ring-buffer
   unanimity (Finding 5) is answering a constraint Nova does not have —
   12 to 200 images was a 1990s memory budget, and greyscale weather
   charts on a modern disk are not a scarce resource. Two consequences:
   the LOCK/pin control disappears, since it existed only to protect
   images from automatic eviction; and the image list becomes a view of a
   folder rather than a managed slot table, so images survive a crash.

   One open item this creates: Nova has **no external dependencies**
   today (no `find_package` in `CMakeLists.txt`; `core/image.cpp` writes
   PGM), which is part of why M5's tier-1/tier-2 target matrix is cheap.
   Greyscale PNG needs libpng, or zlib, or a small hand-rolled encoder
   using uncompressed deflate blocks. The hand-rolled writer is
   recommended: no new dependency, and the size penalty is small because
   chart images compress poorly regardless.
8. **Manual phase adjustment UX** (the remaining ISO §4.2.6 / §5.4.3
   compliance item). Answered, and this is the strongest result in the
   survey: a ruler along the image plus a click on the dead sector, with
   a numeric field showing the same value for keyboard entry and for
   reproducibility. Every receiver in the corpus implements some
   degraded version of this; Nova can implement the best version of it.

## The new question the item-1 decision creates

No commercial receiver has this problem, because for all of them the
live draw *was* the final image. Nova's live view and its saved image are
two different renderings of the same transmission, and **they can differ
visibly** — the user watches a picture build, and then the saved version
replaces it looking better, or slanted differently, or with dropout rows
repaired that were ragged a moment ago.

That is not a defect; it is the decision working. But it needs a UI
answer, and there is no precedent to borrow: the honest options are to
show the transition explicitly (the live view is labelled provisional and
is visibly superseded), to hide it (the live view closes and the saved
image opens as a separate artifact), or to show both. Unregistered, this
would surface as a bug report — "the picture changed after it finished" —
so it is registered here instead.

## Registered gaps in this survey

- Furuno FAX-207, FAX-214 and the Sony CRF-V21 operator sections were
  not read. 207/214 are the same paper-recorder generation as the 408
  and 410, which were read in full; the Sony PDF is a facsimile-
  principles booklet with no operator interface. If a design question
  turns on the 1980s Furuno panel specifically, they are unread.
- The JAX-91 was extracted but only the JAX-9B was read closely.
- Screen *layouts* were deliberately not measured or transcribed. This
  survey records conventions, not geometry — unlike Isobar's `docs/05`,
  which measured the original's form resources because interop demanded
  it. Nothing here demands it.
