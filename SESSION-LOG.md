# SESSION-LOG.md — Nova

Newest entry first. Append-only: correct by adding an entry, never by
rewriting an old one. Every entry ends with the exact next step.
This file is tracked in git (Sara, session 1: "we don't need to hide
anything as our develop history").

---

## 2026-08-13 — Session 16: a second surveyor re-reads all sixteen manuals, and the survey mostly survives

Agent: Kimi Code CLI (Kimi k2). Code changed: none. Files changed:
`docs/04-receiver-ui-survey.md` (independent-verification section and
three decided blocks appended), `docs/03-pre-m4-audit.md` (session-16
status: gate fully clear), `ROADMAP.md` (M4 decisions completed),
`SESSION-LOG.md`.

**Task as accepted:** Sara asked for the commercial-receiver survey to
be redone from Kimi's perspective, working through the manuals in
`../Weather Fax Receiver Manuals/` directly — an independent check on
session 15 rather than a summary of it.

**Method.** Same corpus, fresh tooling: a PDFKit text extractor plus a
Vision-OCR extractor (~60 and ~50 lines of Swift, rebuilt in the
scratchpad — session 15's `pdftool.swift` was not preserved, so this
session's pair now lives in `/tmp/nova-survey/`). Ten manuals had text
layers; the six scans (FAX-207, 208A, 214, SFX-100, Sony, Nagra) went
through Vision OCR, which proved legible throughout the operation
chapters. Sixteen sub-readers each extracted operator-interface facts
from one manual against the seven claims implied by `docs/04`, with
page citations; the synthesis and the comparison against `docs/04` are
this agent's own. Same copyright handling as session 15: facts about
conventions only, extractions stay in the scratchpad.

**Result.** The survey survives a second reader. Confirmed again:
exactly two named picture corrections (PHASE, SYNC) on every receiver
that has controls; the ruler/coordinate phase-entry pattern (now with
the FAX-207/208A/214 and JAX-91 evidence the first pass had skipped);
forced start without exception; zero progress percentages in sixteen
manuals; ring buffers of 12, 20, 30 and 200 images. Four contradictions
and six additions are recorded in the new `docs/04` section; the two
that matter most: Finding 2's "AUTO is a value, never a mode toggle" is
contradicted by the JAX-9B, JAX-91 and SFAX-500 (the JAX-9B evidence
was already in session 15's own bullet list), and Finding 5's pinning
is not universal — the SR-97, the largest store at 200 images, has no
lock. The first does not change the design takeaway (AUTO-as-value
stays, as a choice); the second strengthens Sara's drop-LOCK decision.

**Notable additions for M4.** Slant has no measurement aid in the
entire corpus — every ruler serves PHASE only — so Nova's measured-ppm
SYNC override is a place to beat the industry, not match it. The 1980s
Furunos print `Phase OK` / `Phase NG` on each chart header: precedent
for stamping decode QA onto saved images. The buffered-but-never-
re-rendered 1980s Furunos (corrections "effective only while the
printer is operative") give the live-view-never-revised decision
precedent even where the data to revise existed. Nagra FAXDM's
"minimal" verdict is partly artifact: the scan's right text column is
physically cropped away. The Sony CRF-V21 PDF is confirmed a
principles booklet; its real Operating Instructions are not in the
corpus.

**Validation.** None applicable — no code changed. The verification is
the document comparison itself, done claim by claim with page
citations, recorded in `docs/04`.

**Two decisions taken by Sara after the survey, closing the last open
M4 questions.** (1) The live→saved handoff: **show the transition** —
the live view is labelled provisional from the first row, and the saved
image replaces it in the same pane, once, at end of transmission. (2)
The PNG writer: **hand-rolled encoder with uncompressed deflate
blocks**, no new dependency. Both recorded in `docs/04` and
`ROADMAP.md`. With these, every open `docs/03`/`docs/04` design
question has an answer.

**One more decision the same day**, when Sara asked how manual
correction fits the replace-once handoff: (3) corrections exist at
**both moments** with the same two-number surface — forward-only on the
live preview, non-destructive re-render from retained raw on the saved
image — and an override set during reception **seeds the batch
re-decode as its initial anchor**, with per-station persistence.
Recorded in `docs/04` and `ROADMAP.md`.

**Next step:** design the FLTK/RtAudio shell against the decided
architecture in `docs/04` — the live/saved two-path split, the
streaming tone detector, the PHASE/SYNC override surface, the status
line, the named protocol states, and the provisional→saved transition.
No design question remains open; the gate for GUI code is fully clear.

---

## 2026-08-13 — Session 15: sixteen commercial receivers agree on two knobs, and that settles M4

Agent: Claude Opus 5. Code changed: none. Files changed:
`docs/04-receiver-ui-survey.md` (new), `ROADMAP.md`.

**Task as accepted:** Sara's idea. She had collected 16 operator's
manuals for commercial marine weather-fax receivers and asked to go
through them as a reference for Nova's GUI design, before the M4 shell
is designed.

**Method.** No PDF tooling was installed: poppler is absent, and the
manuals were read with a ~60-line PDFKit tool built in the scratchpad
(`pdftool.swift`, text extraction + page rendering via Swift, already on
the machine). Ten of sixteen had text layers and were mined directly;
the six scans were rendered to PNG and read as images. Extracted text
and page renders live in the session scratchpad, never in the repo —
same handling as the ISO/WMO PDFs in session 13. The manuals are
third-party copyrighted works and were read for *facts about
conventions* only; no text, screen layout or artwork is reproduced in
Nova or in `docs/04`.

**Corpus.** 15 distinct receivers, 7 manufacturers, ~1985–2024: Furuno
FAX-30 (two editions — OME-62600 K1 and L1), FAX-408, FAX-410
(OMC-62610H), FAX-207/208A/210/214; JRC JAX-91 and JAX-9B; Samyung
SFAX-500 and SFX-100; Steamrock SR-97; Taiyo TF-711; Sony CRF-V21;
Nagra FAXDM. The corpus splits into paper recorders (corrections during
reception, destructive) and stored-image receivers (corrections after,
non-destructive). Nova is in the second group, with ACFax.

**What the survey found.** Four results carried the session. (1) Every
receiver has *exactly two* picture corrections, PHASE and SYNC, and
nothing else touches geometry — which maps one-to-one onto Nova's
phasing anchor and fitted timebase, so the manual-override surface is
settled at two numbers. (2) The better designs make the operator report
a measurement rather than guess a correction: Furuno and Samyung print a
ruler along the image and ask for the coordinate of the dead sector; the
SR-97 has the operator touch the dead sector directly. (3) AUTO is a
*value* inside the IOC/rate/frequency controls, never a separate mode
toggle — which is already how `DecodeOptions::ioc` behaves (session 13) —
and every receiver without exception has a forced start, because
automatic detection is expected to fail. (4) The receiver narrates the
protocol state (`WEFAX READY` → `APT DETECTED` → `SYNCHRONIZING` →
drawing → `SAVE?`) rather than showing a percentage, which is a
one-to-one match with the nine stages built in session 14 and answers
the first-minute problem: show the state name, never the +261 ppm that
a short baseline produces.

**Three decisions, all Sara's.** The survey answered five of the eight
open `docs/03` questions from precedent; Sara decided the other three.

- **Streaming model — two paths.** Live view is a provisional forward
  draw, single pass, never revised (the SR-97's model). The saved image
  is a batch re-decode of the retained raw stream once the transmission
  ends, when the long baseline and dropout brackets exist (the FAX-30's
  operator-initiated re-render, made automatic). The consequence that
  earns it: **`decode_fax` does not become incremental** — the tested
  nine-stage core is untouched, its 23 suites keep meaning what they
  meant, and the new incremental code only ever produces a preview.
- **Redraw — the live view is never revised.** No commercial receiver
  redraws on its own initiative. Rows land once; the better rendering
  arrives as the saved image, not as flicker.
- **Storage — unbounded, to a user-set folder, greyscale PNG.** The
  corpus's ring-buffer unanimity (12 to 200 images) answers a 1990s
  memory budget Nova does not have. LOCK/pin disappears with it.

**Two things this creates, both registered in `docs/04`.** Retained raw
is promoted from convenience to load-bearing, since the saved image *is*
a decode of it. And the live view and the saved image are now two
renderings that can differ visibly — no commercial receiver has this
problem, because for all of them the live draw was the final image. The
UI answer is unchosen and registered rather than left to surface as
"the picture changed after it finished".

**One flag raised, not resolved.** Nova has no external dependencies
today (no `find_package`; `core/image.cpp` writes PGM), which is part of
why M5's target matrix is cheap. Greyscale PNG needs libpng, zlib, or a
small hand-rolled encoder using uncompressed deflate blocks. The
hand-rolled writer is recommended and noted in `ROADMAP.md`; the
decision is not taken.

**Validation.** None applicable — no code changed, no tests run. This
was a paper session by design: `docs/03` made answering these questions
the gate for starting M4 GUI code, and that gate is now cleared.

**Registered gaps in the survey itself.** Furuno FAX-207 and FAX-214
operator sections were not read (same paper-recorder generation as the
408/410, which were read in full); the Sony CRF-V21 PDF turned out to be
a facsimile-principles booklet with no operator interface; JAX-91 was
extracted but only JAX-9B was read closely. Screen *layouts* were
deliberately not measured — this survey records conventions, not
geometry.

**Next step:** design the FLTK/RtAudio shell against the decided
architecture in `docs/04` — the live/saved two-path split, the streaming
tone detector, the PHASE/SYNC override surface, the status line, and the
named protocol states. The one design question still open before GUI
code is how the live view gives way to the saved image (`docs/04`, "The
new question the item-1 decision creates").

---

## 2026-08-13 — Session 14: M4 core seams — the monolith is nine stages, and the core no longer prints

Agent: Kimi Code CLI (Kimi k2). Code changed: `core/hooks.hpp`,
`core/hooks.cpp`, `core/fax.cpp`, `core/fax.hpp`, `core/tones.cpp`,
`core/tones.hpp`, `core/phasing.cpp`, `core/phasing.hpp`,
`cli/env_hooks.hpp`, `cli/nova-decode.cpp`, `cli/nova-tones.cpp`,
`tests/test_hooks.cpp`, `CMakeLists.txt`, `ROADMAP.md`, `START-HERE.md`,
`docs/03-pre-m4-audit.md`. Developed on the `m4-seams` scratch branch and
merged to `main` at Sara's request at the end of the session.

**Task as accepted:** the M4 seam work named in `docs/03-pre-m4-audit.md`
("Small core changes worth doing at the start of M4"), before any GUI
design: log/progress callback, cooperative cancellation, structured error
kinds, and a behaviour-preserving stage split of `decode_fax`.

**What was built.** `core/hooks.hpp` is the whole surface: `DecodeHooks`
(log sink, progress callback, cancel predicate — all three null is the
silent batch default), `DecodeError` (a `std::runtime_error` with a
machine-readable `DecodeErrorKind`), and three helpers (`dlog`,
`report`, `throw_if_cancelled`) that each cost one branch when no hook is
installed. The five NOVA_DEBUG* variables became five `LogTopic`s; the
core never reads the environment now — the CLIs map the same variables
onto a stderr sink (`cli/env_hooks.hpp`), so the shell debugging workflow
is unchanged. One deliberate difference, documented there: kDetail stands
alone, so NOVA_DEBUG_FULL=1 without NOVA_DEBUG now shows the per-line
detail it used to suppress.

**The stage split is a seam, not a reorganization.** `decode_fax`'s
numbered sections are nine named functions over a `DecodeState` struct —
onset, dead-sector, phasing, sync-track, period-fit, segmentation,
timebase, change-points, assembly — driven by a table in `decode_fax`.
Only cross-stage values live in the state; stage-locals stayed local.
Every constant, comment and comparison was carried over verbatim;
cancellation checks and progress reports were added at stage boundaries
and every 16–64 iterations of the long loops (comb windows, sync track,
assembly rows), plus inside `detect_tones` and `detect_phasing`, which
gained a trailing `DecodeHooks` parameter (defaulted: tests unchanged).
Errors: empty input / too short / no comb / too few lines are now
`kEmptyInput` / `kTooShort` / `kNoSignal` / `kTooFewLines`, with the same
message strings as before; cancellation throws `kCancelled`.

**Behaviour preservation is measured, not argued.** Before touching
anything: baseline 22/22, PGM hashes of four fixture decodes, and the
five debug streams captured. After the split: all four images
byte-identical, the CLI's stdout byte-identical, and all five
NOVA_DEBUG* streams byte-identical. Final suite **23/23** (100.5 s).

**New screamers** (`hooks`, 13 checks): each error kind by value;
DecodeError still catches as `std::runtime_error`; the sink receives
`dbg:` lines and changes nothing (image and metrics identical with and
without); all nine stages reported in pipeline order with fractions in
[0,1]; cancellation at a stage boundary, mid-decode, and inside
`detect_tones` all end in `kCancelled`, never in a partial image.

**Contradictions found.** None against the audit: docs/03's list mapped
one-to-one onto what was built (item 4, helper consolidation, found no
truly identical variants; item 5, comments moved with their code). The
audit said the split gives "progress, cancellation, and future
incremental execution" seams — the first two are exercised by tests; the
third is a design property and is honestly untested until M4 builds on
it.

**Validation.** Suite: **23/23 pass, 100.52 s**. Baseline comparison as
above: four fixture PGMs, CLI text, and all five debug streams
byte-identical to the pre-split baseline. `git diff --check` clean;
`-Wall -Wextra` build has zero warnings.

**Next step:** design the FLTK/RtAudio shell around these seams, using
Isobar's `LiveScan` single-state-machine/chunking-invariance model and
ACFax's retained-raw non-destructive adjustment architecture (docs/03
"M4 design decisions still open" — streaming model, incremental tone
scan, provisional status, retained raw, memory policy — need answers
first, on paper, before GUI code).

---

## 2026-08-13 — Session 13: pre-M4 audit — the standards were mostly right; the provenance file was not

Agent: Kimi Code CLI (Kimi k2). Code changed: `core/fax.cpp`,
`core/fax.hpp`, `core/phasing.cpp`, `core/phasing.hpp`, `core/gen.cpp`,
`core/gen.hpp`, `cli/nova-decode.cpp`, `tests/test_roundtrip.cpp`,
`tests/test_tones.cpp`, `NOTICE`, `README.md`, `ROADMAP.md`,
`docs/00`–`docs/03`.

**Task as accepted:** Sara reviewed the session-12 pictures ("they're all
right") and asked for a whole-project audit before M4: ISO 9876 as the
highest-level receiver truth, WMO-386 for the signal, prior art checked
before inventing anything, Isobar included, and the codebase kept simple
and readable.

**Method.** The ISO and WMO PDFs were read directly (text extracted only
under ignored `build/audit/`; the licensed ISO text is not in the repo).
The WMO 2009 edition was spot-checked as well as the 2023 edition Sara
provided. ACFax, HamFax, weatherfax_pi, KiwiSDR FAX, JWX, fldigi and the
local Isobar/KG-FAX archive were checked as prior art. Baseline before
touching anything: 22/22 suites, 61.91 s.

**Standards gaps closed.** The decoder already detected 300 vs 675 Hz
start tones but never used the answer: an IOC-288 signal decoded with
default options would have been drawn 1810 px wide. `DecodeOptions::ioc`
is now automatic by default, the first start tone selects IOC 576/288
unless overridden, and `DecodeResult::ioc` reports it [ISO §4.2.5].
`nova-decode --dev 150|400` exposes the LF/HF deviation mode that was in
the core but unreachable from the shipped CLI. The generator gained
`start_sec`, `dead_frac` and `pulse_frac` so the spec edges are tests,
not prose.

**New screamers.** `roundtrip [4]` is now the full ISO §5.4.1 matrix —
{288,576}×{60,90,120}, all with automatic IOC and rate selection; all six
legs measured 0.00 px of bar-edge scatter. The 90 lpm legs read
−62.4/−62.5 ppm because the generator truncates a 5333.33-sample line to
5333; the test pins the generated truth. `[11]` sweeps amplitude over two
orders of magnitude: 0.5, 0.05 and 0.005 decode identically (179 locks,
+0.0 ppm, 0.00 px). `[12]` pins the eight-band gray scale to within 1 LSB.
`[13]` pins a 10 s start tone by the picture boundary (25.5 s, MAD 15.3
from row 0); the naive "50 dropped lines" assertion was wrong because the
onset comb cannot see a long pure tone. `[14]` generates at 44.1 kHz and
resamples through the production path (+0.02 ppm, MAD 16.6, 0.00 px).
`[15]` covers the four permitted WMO §5.1.3.3 corners: dead sector 4.0%
or 5.0%, pulse 1.0% or exactly half the sector.

**The audit found a real LF reporting bug.** A clean generated ±150 Hz
signal decoded straight but its phasing witness said the timebase stepped.
The cause was not the timebase: at LF the generated phasing line contains
a half-cycle carrier-phase alternation, and the wedge-score plateau moved
by 10 samples line to line. The wedge fit is still the right detector,
but WMO §5.2.3.4 names the leading edge of white, so the position is now
refined to the local 50% black→white crossing. Measured on the LF
synthetic: nonlinearity 10.7 samples / 25 fake steps before, 0.0 / linear
after. `roundtrip [6]` now pins that a clean LF signal is not convicted.
All fixture anchor tests still pass.

**One test harness was wrong, and the full suite caught it.** `tones [7]`
used to choose a candidate line rate by the most recovered phasing lines.
After the edge refinement, a 60 lpm signal tested as 120 lpm produced 42
windows against the true rate's 30 — but the true rate's positions agree
to 0.0 samples and the harmonic's spread is 35.5. The harness now prefers
position agreement and uses count only as a tie-break. This was a test
selector bug, not a decoder regression.

**Provenance audit.** NOTICE was wrong, not the code. It claimed ACFax's
FIR coefficient tables were reused; `git log -S firwide` reaches only the
initial scaffold, and the source tree computes its own Blackman
windowed-sinc at runtime. NOTICE now says idea-level reuse, no copied
code/tables/files, and `docs/00`'s ledger names the actual reused rules:
ACFax discriminator architecture, Isobar per-line sync, JWX fold and
Goertzel/domain choice, weatherfax_pi/KiwiSDR wedge/median/spread and
leaky counter, fldigi's run-abandon/coherence/per-line-correlation rule
shapes. HamFax's stale 403 note is corrected; the mirror's
`FaxDemodulator.cpp` confirms the ACFax table lineage. Isobar's local
canonical archive was found at `../isobar-dev.zip`; its `LiveScan` design
(one incremental state machine, batch as one feed, chunking-invariance
test, manual nudge, thread-safe finish) is now the named M4 reference.

**Contradictions found.** Ten, all fixed or registered in
`docs/03-pre-m4-audit.md`: the duplicated NOTICE sentence; the phantom
ACFax FIR reuse; the empty-ledger-vs-NOTICE mismatch; stale HamFax access;
the false 2009-vs-2023 WMO §5.5 restructuring claim (the 2009 PDF has the
same §5.5.1/§5.5.2 split); the nonexistent §4.2.7 assertion; §4.2.3 being
graded as if automatic AND manual were required; detected-but-unused IOC
selection; unreachable ±150 Hz; the LF timebase false positive; and three
WMO clauses missing from the distilled spec (levels, AM scope, optional
recording-level adjustment).

**M4 readiness verdict.** The batch core is ready to build against: no
global mutable state, value results, exceptions at the boundary, and a
strong picture-domain test net. The audit deliberately did NOT rewrite
streaming. The small pre-GUI seam work is named in `docs/03`: replace
`getenv` debug output with a log/progress callback, add cooperative
cancellation and structured errors, split `decode_fax` at its numbered
stages, and only then design the FLTK/RtAudio shell around Isobar's
single-state-machine architecture and ACFax's retained-raw model.

**Validation.** Final suite: **22/22 pass, 83.19 s** (roundtrip 59.46 s).
CLI smoke: generated IOC-288 ±150 Hz signal decodes with automatic IOC
selection and, after the phasing fix, reports a linear timebase. The two
pictures sensitive to the phasing change were inspected:
`vmw-phasing-image-160s` uses the phasing anchor with the title box at
the left margin; `test-chart-jmh-60s` keeps its tracked anchor and a
continuous chart border. `git diff --check` is clean.

**Next step:** begin M4 with the core seams named in
`docs/03-pre-m4-audit.md`: log/progress callback, cooperative
cancellation, structured decode errors, and a behaviour-preserving stage
split of `decode_fax`; then design the FLTK/RtAudio shell around Isobar's
`LiveScan` chunking-invariance model and ACFax's retained-raw
non-destructive adjustment architecture.

---

## 2026-08-13 — Session 12: the dropout rows were findable all along — ask the signal, not the picture

Agent: Kimi Code CLI (Kimi k2). Code changed: `core/fax.cpp`, `core/fax.hpp`,
`cli/nova-decode.cpp`, `tests/test_roundtrip.cpp`, `CMakeLists.txt` comments.

**Task as accepted:** Sara, on the session-11b decodes, with screenshots:
"JMH KiwiSDR Himawari 13986.6 and test chart, they're losing sync in the
middle and the chart misaligns in the middle" — top priority — "then JSC1
and JSC5". And the mechanism, from her: "I recorded those by using KiwiSDR,
so audio drop caused by internet could always be a possibility."

**Root cause, measured.** A KiwiSDR stall drops samples mid-recording:
1269 on Himawari (drawn row 752), 1642 and 3590 on test chart (drawn rows
633 and 651 — the second reads +410 samples mod line). Each drop unlocks
exactly 8 rows — the re-acquisition latency, `kReacqMisses` — and session
11b placed those rows by matching the row above within **±120 px**, when
the true moves are **574 and 743 px**. It could not even reach the right
answer; the torn bands in Sara's screenshots are the spurious minima it
found instead. The row-splitting pass could not help either: its
quarter-line cap (1000 samples) sits below both moves.

**The fix: probe the pulse at both levels.** A dropout run is bracketed by
two known levels — the locked lines before it and after it. The tracker
never locked the rows between because its narrow window sat on the old
prediction until the re-acquire sweep fired, but the pulse is IN the audio:
each row, asked ±20 samples around each extrapolated level, answers
cleanly. Measured on all five library dropouts (Himawari, test chart ×2,
HDSDR ×5 runs, JSC4): the far side scores **0.66–0.96**, the near side
**≤ 0.22** — and exactly one row per run scores nothing at either level:
the row the drop landed in, whose pulse it took. Rows that answer are drawn
where the signal puts them; the one that does not is split over the whole
line (the quarter-line cap is relaxed only next to a re-locked run, where
the move is independently evidenced); the ±120 px picture placement remains
as the fallback for rows the signal cannot place — a faded pulse station,
which is the registered gap it always was.

**Pictures.** Himawari's band: coastline and graticule flow through,
7 rows re-locked + 1 split. Test chart: the checkerboard border is
continuous again, 15 re-locked + 4 splits. JSC4's doubled, ghosted contact
line is a single crisp "【TEL】03−6252−8413 【FAX】03−6252−8805". HDSDR is
the interesting one: session 11b followed its ~0.40–0.50 soft locks and
fixed the strip but tore the text, and reverted. The probe decides nothing
on those scores — it needs ≥ 0.60 with the loser < 0.45 — and finds the
real far-side pulses at 0.8–0.93 against ~0.0: 40 rows re-locked, text
intact, and the right-edge strip p99 improved 13 → 7 px. The 11b conflict
("the strip and the text want different answers") was an artifact of
measuring at positions neither level vouched for.

**Screamers.** 22/22 pass. `fixture_dropout`'s `--expect-rows-in-place 1`
is at its floor: the 1 remaining row is the one the drop destroyed, and its
content is not in the recording — no decoder can draw it.

**JSC1 and JSC5: the decoder was never the thing that failed to move.**
Their 5.0 px rigidity survived session 11b while the 120 lpm files went to
1.0, and "60 lpm" was a suspect, not a measurement. Now measured, against
ground truth: a synthetic 60 lpm signal with JSC1's signature (17 samples
every 3 lines, mid-line, random positions) decodes to place 1.1 px rms /
3.2 worst and matches its own clean decode at MAD 2.5/255 — and reads
rigidity p90 = **5.0 px, the same number the library recordings read**,
while the 120 lpm control reads 1.0. At one step per three lines the steps
are too dense for change points to separate, the rows ride the fitted ramp
to within half a step, and the rigidity statistic reads the step SIZE:
a correctly drawn picture of such a recording genuinely has rows whose two
ends disagree. Pinned in `roundtrip [10]` as the 60 lpm stepped case —
placement asserted, rigidity deliberately not asserted at 60 lpm. JSC1 and
JSC5 are as good as the signal allows, and M2b's last measured gap closes
by explanation.

**Contradictions found.** Session 11b's log claims "JSC6 4.0 → 1.0" for
rigidity; the session-11b decode of JSC6 reads 4.0 today under both the C++
statistic and a reimplementation, and the current decode also reads 4.0 —
so no session-12 regression, but the 1.0 was never true of the full
recording (it was likely measured on a window). FAXSignal reads rigidity
8.0 px with place rms 0.18 — the statistic's edge finder has content
sensitivity (it locks onto other light runs); treat the number as a
screening tool, not a verdict, on new recordings.

**Next step:** Sara reviews `recordings/library-8k/decodes-s12/` (the four
changed pictures: Himawari, test chart, HDSDR, JSC4) — the acceptance test,
per session 11c's rule, is the operator looking at the pictures. Then M4
(GUI + live audio), whose inherited costs are unchanged: incremental
`detect_tones`, the first-minute timebase verdict, and session 11's
assembly needing the next line's correction before it can split a row —
now joined by this session's probe, which needs the run BRACKETED before it
can re-lock, i.e. a live decoder must wait for the far side of a dropout
(or draw provisionally and redraw, which was already the plan).

---

## 2026-08-12 — Session 11c: M2b closes on the operator's judgement, not on a number

Agent: Claude Opus 5. Bookkeeping entry; no code changed.

**Sara, having reviewed the session-11b decodes:** "VMW 2215Z's staircase
is understandable, don't worry about it, others has huge improvement."

**What that settles.** Session 11b left the white-only half of M2b open and
named it the next step. It is not open: it is ACCEPTED. A station that
sends no sync pulse gives the decoder no per-line evidence except the
picture matching its own previous row, and session 11b measured what that
mechanism does when it runs on a whole page — it walks off the phasing
anchor and fails `roundtrip [7]` and `fixture_phasing_anchor`. The
staircase is what a white-only recording with a stepping capture chain
looks like; the decoder reports the condition (`timebase`,
`per_line_sync`) and does not invent a correction for it. M2b is DONE.
Reopen only if a BOUNDED form of the correlation is found — correlate,
then re-anchor to the fitted clock every N lines, with N measured.

**Worth recording as a rule, because it decided a milestone.** The
acceptance test for this milestone was never a threshold; it was the
operator looking at twenty pictures, twice. Session 11 was started by her
review and session 11c closes on her review. Numbers ranked the work and
caught two mechanisms that made things worse, but nothing in the suite
could have told us that a staircase on one white-only recording is
tolerable while a 2 px zig-zag on six newspaper faxes is not.

**Still open and small, carried into whatever comes next:** JSC1 and JSC5
rigidity did not move (5.0 px) while the other four JSC files went to
1.0-4.0 — both are 60 lpm, which is a suspect and not yet a measurement;
HDSDR's right-hand strip is banded where its text is right, and the two
want different answers from the same lines; no screamer exists for a faded
pulse station whose steps sit under its own measurement noise.

**Next step:** M4 — GUI plus live audio — which is where session 10 pointed
before the picture review displaced it, and where the manual override ISO
§4.2.6 asks for lives. The costs it inherits are unchanged and now larger:
session 7's incremental `detect_tones`, session 9's offline timebase test,
and now session 11's assembly, which needs the whole segment before it can
place a line and the NEXT line's correction before it can split a row
(§5b). A live decoder will have to draw provisionally and redraw, which is
ACFax's retained-raw-stream architecture and is already the plan (M4,
non-destructive). If a smaller piece is wanted first, the 60 lpm rigidity
question above is one session's work and would close the last measured gap
in M2b.

---

## 2026-08-12 — Session 11b: Sara looked again, and the rows were stretched, not moved

Agent: Claude Opus 5. Continuation of session 11, same day, same milestone.

**Task as accepted:** Sara reviewed the session-11 decodes and rejected
three of them. "JMH KiwiSDR Himawari 13986.6 ... it lose sync here", "so
does test chart, lose sync at the bottom", "HDSDR ... right black strip is
not consistant", and "for JSCs, small zigzag are still zigzags, still
cause difficulties of reading".

**The JSC verdict was the important one, and it was not what session 11
assumed.** Session 11 corrected where each line STARTS. Measured in its own
decodes: on JSC1 the left end and the right end of the same drawn row move
independently — correlation **+0.12**, and the two disagree by 5 px of 1810
at the 90th percentile, 10 px on JSC2 — against 1 px on XSG FYCI, whose
timebase is linear. **The rows are stretched, not moved.** A capture chain
does not wait for a line boundary to insert samples; when it lands mid-line
everything after that point is displaced and everything before it is not,
and no per-line offset can place such a row. That is why the zig-zag
survived a corrector that measurably fixed the line starts.

**The size of the move was already known; only its position was missing.**
It is the difference between this line's correction and the next line's.
Where inside the line it happened, the picture answers: a weather fax moves
1/1810 of a page between lines, so the break goes where splitting the row
there makes it agree best with the row above. Coarse search over the line,
then refine; the candidates include "at the very start" and "not in this
line at all", so the search cannot choose worse than the un-split row.
**JSC2 fixture: the two ends of a row disagree by 10.0 px without this pass
and 1.0 px with it.** Whole library, against the session-10 baseline: JSC2
edge 5.0 → 1.0 px and rigidity 10.0 → 1.0, JSC4 2.0 → 1.0 and 5.0 → 1.0,
JSC3 1.0 → 0.0 and 2.0 → 1.0, JSC6 4.0 → 1.0, JSC1 and JSC5 4.0 → 2.0
(their rigidity does not move — 60 lpm, and not yet understood).

**Himawari's band: eight rows that nothing could place.** The recording's
phase moves ~1270 samples mid-picture and the eight lines straddling the
move carry no lock, so the ±8-line window placed them where the lines
BEFORE the move are — ~75 px from the rest of the chart. The audio through
those lines is continuous at full amplitude, so the signal is there to be
drawn. They are now placed by matching the row above, which is fldigi's
per-line correlation kept per line instead of collapsed to a histogram mode
(docs/00, session 11b).

**Two mechanisms were built, measured, and NOT taken — and one of them was
mine to want.** (a) *Soft locks.* HDSDR drops ~163 samples at a time and
the ~8 lines bracketing each drop score 0.40-0.50, just under the lock
threshold, while agreeing with each other to a sample: exactly session 10's
"score seeds, position carries" rule, in the image domain. Following those
measurements fixes the broken right-hand strip Sara reported — and tears
the text apart ("JMH", "WARNING" split mid-word). The strip and the text
disagree, and the text is the deliverable. Reverted. (b) *Let the picture
place every unlocked row.* Fails two screamers: the warp fixture's head
drifts 41 px off its body, and on a white-only station, where every row
qualifies, the page wanders off the phasing anchor (`roundtrip [7]` and
`fixture_phasing_anchor`). Narrowed to rows inside a run that the phase
moved across, which is the only case where nothing else can answer.
**The lesson for the next agent: the same evidence can be right on one
recording and wrong on another, and the tie-break is the picture, not the
principle.**

**Also measured and not taken.** Pruning change points that the noise
cannot pay for (merge the cheapest boundary, re-measure, repeat) cut JSC1
from 783 segments to 326 and changed the drawn picture by nothing at all —
p90 unchanged, p99 slightly worse. The remaining JSC zig-zag was never
false steps; it was the intra-line stretch above.

**Contradictions found.** Four. (a) Session 11's implicit claim that
line-start placement is the whole of the problem — false, and the
measurement that shows it (left end vs right end of one row) is one nobody
had made. (b) My own reading of HDSDR: I told Sara the bands were lines
"drawn at the OLD position while the rest moved", and the fix that follows
from that reading makes the picture worse. (c) I expected mid-line
insertions to be permanently harder than boundary ones (session 11 measured
0.66 vs 0.28 px); with the intra-line pass the mid-line case is 0.66 → 0.00
px of bar scatter, so the gap was the missing mechanism, not a limit. (d)
The dead-sector edge cannot judge rows around a dropout — it reads 8 rows
out of place whether they are well placed or badly — which is why the new
check asks what each row MATCHES instead.

**Tests: 22 suites green, zero warnings (was 21).** New fixture
`himawari-kiwisdr-dropout-120s.wav` (JMH KiwiSDR Himawari 340-460 s), which
holds the ~1270-sample move and the eight stranded rows. Two new
picture-domain checks: `--expect-rigid-rows` (do the two ends of a row move
together — JSC2 10.0 px without the intra-line pass, 1.0 with) and
`--expect-rows-in-place` (does any row match the row above best at a large
shift — 2 without picture placement, 1 with, and the one that remains is
the seam itself). The second check runs on the dropout fixture ONLY: it
asks what a row matches, and a resolution grating matches its neighbour at
several shifts by construction (the JMH test chart reads 23 on a picture
with nothing wrong with it).

**Revert checks run; both new mechanisms scream.** Intra-line pass disabled
→ `fixture_timebase_steps` fails (rigidity 10.0 against a bound of 3.0).
Picture placement disabled → `fixture_dropout` fails (2 rows out of place
against a bound of 1). The two mechanisms that failed to earn a screamer
are the two that were removed.

**What did NOT happen.** VMW 2215Z's staircase, GYA, NMC, VMW 2230Z: still
untouched, still the open half of M2b, and now with a measured reason why
the obvious mechanism cannot simply be pointed at them. HDSDR's right-hand
strip is still banded — its text is right and its strip is not, and the two
evidently want different answers; that is registered rather than fixed.
`test chart`'s bottom still carries a real dropout at drawn line 633 whose
743 px seam is followed correctly, and the lines around it are as good as
this session's rules make them.

**Next step:** the white-only half of M2b, with session 11b's warning
attached. VMW 2215Z has no sync pulse, so the only per-line evidence
available is the picture matching its own previous row — the mechanism this
session had to restrict to eight rows because, run on every row, it walks a
page off its anchor. What is missing is a way to bound the walk: the
phasing anchor is an absolute reference at ONE point in the recording, and
a per-row correlation is a relative measurement everywhere, so the honest
form is probably "correlate, then re-anchor to the fitted clock every N
lines" with N measured rather than chosen. VMW 2215Z 0-120 s is the
fixture-in-waiting and Sara's "stair like shape" is the acceptance test.
Tree is green, buildable, and committed.

---

## 2026-08-12 — Session 11: Sara looked at the pictures, and every complaint was the same axis

Agent: Claude Opus 5.

**Task as accepted:** not session 10's next step. Asked to choose between
M4 and two smaller pieces, Sara said "no, before you start, could you
decode all charts and I want to manually go through every charts" — so the
session began by decoding all 20 library recordings with the session-10
binary and handing them over one at a time. Her verdicts are recorded
verbatim in `recordings/library-8k/decodes-s10/_review-notes.md`. She then
chose the milestone this session builds: correct the timebase, not just
report it.

**Twelve complaints, one axis.** "The black strip, it's actually zig
zagging, not solid at all" on all six JSC recordings; "not 100% smooth" on
both JMH test charts; "sync lose at the top part" on JMH KiwiSDR Himawari
and "at the bottom" on `test chart`; "stair like shape" on VMW 2215Z. Not
one word about grey scale, geometry, aspect, crop or rotation, which is
worth as much as the complaints. Measured afterwards in the drawn pixels —
the dead sector is a fixed-width band, so the column where it ends is a
straight-line witness — the recordings she named carried 3.0-4.6 px of
row-to-row edge scatter and the ones she passed over carried 0.3-1.9.
**The gap her eye found: sessions 9 and 10 built two statistics that DETECT
and REPORT a bad timebase, and nothing CORRECTED one.** The decoder said
"NOT LINEAR: 199.7 steps per 1000" about JSC4 and then drew the picture
anyway.

**Prior art first, as the reuse rule requires, sources read rather than
recalled** (docs/00, session 11): fldigi `wefax.cxx`, KiwiSDR
`extensions/FAX/FaxDecoder.cpp`, weatherfax_pi `src/FaxDecoder.cpp`, JWX
`DecodeFax.java`. **None of the four repairs a timebase.** All four correct
a CLOCK, three of them from one measurement taken before the picture
starts (`phasingSkipData` → `m_skip`; `clock_correct_line`). fldigi is the
painful one: `correlation_shift` computes the per-line shift against the
previous line and keeps only the MODE of a histogram of them, while
`decode_image` places every row from sample count alone. The number exists
and is thrown away. Nothing to reuse; ledger unchanged. fldigi's discarded
per-line correlation IS the idea to take for the white-only case, which
this session does not solve, and it is written down as that.

**Three properties of the sync residual, each measured before it was
written, and each one is a mechanism.**

(a) *It MOVES.* A ±8-line median through a step is wrong on both sides of
it. The window is now cut at every change point — a move the 4 locked
lines on each side agree about, by more than `kNonlinSec` (10 samples),
which is the resolution the timebase test already claims. The timebase
TEST keeps its flat window on purpose: it is a calibrated instrument and
session 9's library thresholds were measured through it. The comment
saying the two smooth identically is corrected rather than left to rot.

(b) *Between steps it RAMPS.* This was the surprise. The period fit
absorbs the MEAN insertion rate, so inside a segment the residual walks
back down at 1.9 samples a line — 19 samples of tilt across an 11-line
segment, twice the step it sits between. A median through a slope is wrong
at both ends by half of it. A robust line (Theil-Sen, median of pairwise
slopes) evaluated at the drawn line fixes it, and this single change took
the ground-truth synthetic from 2.05 px of place error to 0.66 and its
drawn straightness bar from 2.19 px of scatter to **0.00**.

(c) *One large move is a real skip.* A change point is exempt from the
per-line clamp, so JMH KiwiSDR Himawari's ~1270 lost samples produce a
one-line seam instead of a twelve-line diagonal tear. Seams are counted
separately from `max_step_px`, which keeps its old meaning.

**Two more faults found while measuring, both older than this session.**
The assembly dropped any residual further than `2*search` from the fitted
line as bogus — a statement about the fit, not the line. Himawari's first
720 drawn lines sit 1100-1390 samples off a line fitted through the other
1200, so every one of them was thrown away, the correction froze at zero,
and the top of the chart was drawn half a page across. That is exactly
"sync lose at the top part". And a coasting line COUNTED as a correction
level, so a picture whose first lines do not lock started from zero and
the clamp walked it up at 0.03 lines each: on the warp fixture the first
two drawn lines came out 80.7 and 26.3 px from where the signal put them,
with ten clamped corrections. Neither had a test. Both do now.

**The suite had no picture-domain check at all, and now has two.**
`--expect-straight-strip` measures the dead sector's edge in the finished
pixels and shares no code with the decoder; `place_rms_px` is the
decoder's own account of the same quantity and is held to the same bound,
so a disagreement between them is a failure rather than a matter of taste.
Six fixtures carry it. The second check exists because of a defect that no
number the decoder produces can see: the smoothing window could reach back
into the phasing region, where the template anchors half a dead sector
away, and the first drawn lines were placed at the control signal's phase —
88.6 px from the rest of the chart on XSG FYCI. The first lines of a
picture usually do not lock, so the place error reads 0.13 px either way,
and one step does not move a 90th percentile. Only the picture shows it.
New fixture `xsg-fyci-phasing-head-120s.wav` (XSG FYCI 120-240 s) and a
check that the top of the picture sits on the same page as its body.

**Contradictions found.** Seven.
(a) My own first acceptance metric — rms deviation from a 31-row local
median — called SIX recordings WORSE after the change, including
FAXSignal 0.33 → 1.53. Every one of them had gained a correct one-line
seam, which a local median smears across 31 rows. The statistic was wrong,
not the code; the 90th percentile of the row-to-row move replaced it.
Had I trusted the first number I would have reverted a change that
improved every picture in the library.
(b) Session 9's `roundtrip [10]` asserted that the stepping picture
"visibly wobbles, which is why the flag exists". True of a decoder that
only reported. Now false — the assertion is replaced, not relaxed.
(c) I predicted mid-line insertions would be permanently harder than
insertions at the line boundary, because the sync template of the line
they land in straddles them. Measured: 0.66 px against 0.28. The
ambiguity costs one line and nothing else. Both cases are now pinned.
(d) "One skip is one line" — thinning runs of adjacent change points to
their largest member is the obvious tidy-up, and it is wrong: it merges
the genuinely separate steps of a recording that inserts samples every few
lines. Measured worse everywhere (synthetic 2.05 → 2.39, its LINEAR
control 0.19 → 1.71, JSC2 fixture edge p90 2.0 → 5.0). Not taken, and the
reason is in the code so nobody re-derives it.
(e) I told Sara that HDSDR and VMW 2215Z were worth watching because their
ROTATION was unanchored. The actual defect in both is line-start
placement; rotation is fine. Corrected to her in the same session.
(f) The `2*search` gate read as a safety mechanism and was the cause of
the library's largest single misplacement.
(g) `opt.search_frac` had become dead configuration — the tracker's window
was hard-coded at the same 0.03 — while still gating the assembly. It now
means the one thing it names.

**Tests: 21 suites green, zero warnings (was 20).** One new fixture, one
new flag on six of them, one new ground-truth case in `roundtrip [10]`
(the same insertions at the line boundary). **Revert checks run; all six
surviving mechanisms scream.** Change points off → `roundtrip`,
`fixture_warp`, `fixture_timebase_steps` fail. Theil-Sen off → `roundtrip`
and `fixture_timebase_steps` fail. Window not held inside the picture →
`fixture_picture_head` and `fixture_anchor_delta_xsg` fail. `2*search`
gate restored → `fixture_warp` fails. Coasting sets a level →
`fixture_warp` fails. Seams clamped like any other move → `fixture_warp`
fails. Nothing in this session survives without a test that would catch
its removal.

**The library-wide effect, measured against the session-10 baseline.**
Dead-sector edge, 90th percentile of the row-to-row move, in px of 1810:
**7 recordings better, 13 unchanged, 0 worse.** All six JSC (5.0 → 1.0,
4.0 → 2.0, 1.0 → 0.0, 2.0 → 1.0, 4.0 → 2.0, 4.0 → 2.0) and JMH KiwiSDR
Himawari (6.0 → 1.0, and its 99th percentile 94.4 → 5.0). Every pulse
recording in the library now reports 0.18-1.11 px of place error; the
worst before was 4.37.

**What did NOT happen.** VMW 2215Z's staircase is untouched, and so are
GYA 2300Z, GYA 2324Z, NMC 2204Z and VMW 2230Z: a white-only station sends
no sync pulse, so there is no per-line residual to segment and nothing in
this session applies to it. That is the open half of M2b and it is the
half Sara can see — "stair like shape, the chart is not constant" was one
of her twelve verdicts. HDSDR's complaint ("unreadable, no sync") is also
not this defect: its place error was 0.40 px before the session. Its
picture carries the start tone and phasing drawn into the top, because the
recording holds no control signals the segmenter can find, and its
rotation is unanchored for the same reason. Neither is registered as
fixed.

**Next step:** the white-only half of M2b — give a station with no sync
pulse a per-line line-start measurement, from the picture itself.
fldigi's `correlation_shift` computes exactly this and keeps only the
histogram mode (docs/00, session 11); kept per line, with only persistent
moves accepted, it is the same corrector this session built with a
different measurement feeding it. VMW 2215Z 0-120 s is the
fixture-in-waiting, and Sara's "stair like shape" is the acceptance test.
The risk to watch is that picture content moves for real reasons, so the
correlation must be trusted only where it is sharp and only where the move
persists. Tree is green, buildable, and committed.

---

## 2026-08-12 — Session 10: the rejected candidate was real, and finding it broke three things that were passing by luck

Agent: Claude Opus 5.

**Task as accepted:** session 9's next step, first candidate — decide the
registered GYA 2300Z phasing candidate either way. Sara chose it over M4.
Prior art checked first, as the reuse rule requires (docs/00, session 10
section).

**The candidate is real, and the registered gap described it wrongly.** It
is not "an 18-line candidate at 15.5–24.5 s scoring 0.77". It is a **real
40-line phasing interval at 4.5–24.5 s** — the identical interval that GYA
2324Z, the same station 24 minutes later, puts its clean 40-line phasing
in. Lines 9–48 sit at position 872–968 of a 4000-sample line with a sharp
onset (0.16 → 0.83 in one line) and a sharp end; lines 0–8 and 49+ scatter
across the whole line. The "18 lines" was an artifact of the detector, not
a property of the signal: it grew runs from CONSECUTIVE qualifying lines,
and a faded interval breaks into fragments of one to six.

**Score cannot be the membership test, at any threshold.** `phasing.hpp`
said `min_score` "sits in that gap" between true phasing at 0.88–0.97 and
dark content at 0.48–0.62 (session 6). GYA 2300Z's REAL phasing lines score
**0.34–0.88** — through that gap and out the far side — because fading
takes the contrast the score measures and leaves the edge exactly where it
was. Runs are now SEEDED by score and CARRIED by position agreement, which
fading does not touch. Prior art has the shape of this and not the
substance: fldigi's `decode_phasing` abandons phasing only after 5
consecutive failed lines, KiwiSDR's start/stop counter leaks rather than
resets ("can deal with noisy input if we had a miss"), and JWX does not
judge lines at all — it folds 20 s of them and finds the edge on the sum,
which would work here and is recorded as a live option for M4. Carrying on
POSITION is new and is written down as such.

**Then the failures started, and all three were things passing by luck.**

(a) *The timebase witness convicted GYA immediately.* Having recovered the
interval, the decoder reported `NOT LINEAR: 46.2 smp off straight` — worse
than JSC2's 25.5, on a recording with no steps in it. Session 9 calibrated
that statistic only on intervals whose per-line edge is good to about a
sample; GYA's is good to ~14. The image half of the same test has always
local-median smoothed for exactly this reason and the phasing half never
did. Smoothing alone does not save it (GYA 14.6 against JSC4's 18.4 is not
a gap to put a threshold in). What separates them is the residual's SHAPE:
an inserted sample stays inserted, so its residual is a staircase whose
neighbouring lines agree; noise redraws every line.

(b) *A single skip read worse than any stepping recording.* JMH KiwiSDR
Himawari's phasing interval straddles one ~95-sample jump with textbook
linear edge either side — two segments at position ~1055 and ~1150 — and
reads **96.1** off straight. Its 1922 tracked lines say 1.6 steps per 1000,
i.e. linear, and a 60-line phasing interval was over-ruling them. Session 9
had already settled that one skip is not a rate, in the image domain, which
COUNTS steps; this domain measured a spread and could not tell one jump
from fifty. The old code missed it only because the run happened to start
after the jump.

(c) *`jmh sample` lost its head crop, and the obvious fix broke FAXSignal.*
`jmh sample` carries two whole transmissions (start tones at 6.25 s and
424.88 s), each with a real phasing interval, of 59 and 60 lines. The
detector took the LONGEST qualifying run; the day the second grew by one
line the head crop fell from 62 lines to 3 and 59 phasing lines were drawn
into the chart. Segmentation three sections below in the same file already
says "the first one after the previous boundary, never the last or the
largest" and names `jmh sample` as the reason, so I made the phasing
detector take the first — and the final library sweep caught what that did
to FAXSignal, which I had flagged as a risk mid-session and then lost track
of. FAXSignal holds two OPENINGS before ONE picture (start tone 0–7 s,
phasing 7–22 s, a second 300 Hz burst 22–30.5 s, phasing again 32–64.5 s):
taking the first drew 68 lines of the second opening into the chart and
took `max_step` from 14.83 px to 54.23. Neither rule is right alone. The
control tones decide it: inside a known transmission the LAST opening wins,
because the picture begins after it; with no bounds known the FIRST wins,
because a later run may belong to a transmission this decode is not
drawing. `jmh sample`'s second transmission is past its first stop tone and
FAXSignal's second opening is not, which is exactly the distinction. The
tone scan moved up to §2b and is now shared with segmentation rather than
run twice. **The lesson for the next agent: two recordings can want
opposite answers from the same rule, and the one you did not re-measure is
the one that changed.** FAXSignal is back to its baseline decode exactly.

**One threshold now does three jobs, so there is one number and not three.**
`kNonlinSec` is the resolution the test claims (10 samples). An interval
whose own roughness exceeds it cannot resolve it in either direction and
abstains; below it the edge is straight; above it, conviction needs two
counted persistent moves. Measured: roughness 0.1–1.8 on every clean and
every stepping recording against **15.2** on GYA 2300Z; counted steps 16
and 17 on JSC2/JSC3, **0** on Himawari's single jump. `PhasingWitness` is
reported in words, so `kUnknown` names which witness is missing instead of
implying none exists.

**Two mechanisms I added were inert, and I removed them.** Both had been
justified in a comment before being measured. A white-run count to reject
control tones: I read FAXSignal's 30-line candidate "@line 14, spread 0.0,
score 0.99" as a start tone, when line 14 is 7.00 s — exactly where the
start tone ENDS and the phasing begins. It was always the real phasing.
Disabling the filter changes nothing on any of the 20 recordings. A
run-level median score floor, added to kill XSG ASPN's 26-line false run
(score 0.24): once the run's lease is renewed by score rather than by
agreement, that run no longer forms. Also inert across the library, also
removed. **The lesson for the next agent: a mechanism that survives because
no test fails when you disable it is not defensive, it is unmeasured.**

**Contradictions found.** Six. (f) My own mid-session claim that "first
qualifying" was the right selection rule, contradicted by FAXSignal in the
final sweep — see (c). (a) `phasing.hpp`'s score-separation claim,
above — true of strong signals, false of faded ones. (b) The registered
gap's own description of the candidate (18 lines at 15.5–24.5 s; it is 40
at 4.5–24.5). (c) docs/01's "15 of 20 recordings carry a detectable phasing
interval" is now 16 of 20. (d) My own white-run diagnosis, corrected by
measurement rather than by argument. (e) FAXSignal's phasing was being
reported as 32.00–64.50 s when the start tone ends at 7.00 s and the
phasing begins there — the "11 of 14 recordings where phasing begins where
the start tone ends" was 11 of 14 partly because this one disagreed for a
reason that was a bug.

**VMW 2215Z is settled the other way, and permanently.** The other
`kUnknown` recording has no phasing interval to find: three isolated single
lines in the whole file, at positions 2657, 1638 and 0. The recording
begins mid-transmission. That is a measured negative, not a gap.

**Verified against the picture, which is where this was decided.** GYA is
white-only, so the phasing anchor IS the rotation. Decoded both ways: with
the phasing anchor the chart's title box sits at the left margin, exactly
as it does on GYA 2324Z, whose phasing is unambiguous; with the image
anchor it sits half a line across. The automated form of that check
(`--expect-phasing-anchor`) does NOT apply here — it needs a column that is
white on 90% of rows, and this recording's whitest column reaches 0.90 only
at column 429 — so the verification is by eye plus the 2324Z corroboration,
and that limitation is registered.

**Tests: 20 suites green, zero warnings (was 17).** Three new fixtures, each
cut from a real recording and each isolating one claim.
`fixture_faded_phasing` (GYA 2300Z 0–120 s) pins both halves at once and
they pull opposite ways: the faded interval must be FOUND, and having been
found must NOT then be convicted. `fixture_phasing_one_skip` (JMH KiwiSDR
Himawari 0–120 s, cut at 120 s so the image witness clears its 128-line
floor and can be the one that answers). `fixture_phasing_two_openings`
(FAXSignal 0–70 s) pins not-the-longest, corroborated by the tone
detector, which shares no code: the start tone ends at 7.00 s and the
phasing begins there. `roundtrip [10]` gains a synthetic one-skip case with
ground truth — a single insertion inside the phasing interval, which reads
steps=1 and so is the thing that actually pins the constant at 2, because
the real Himawari case reads 0 (its jump falls across a gap in the run).
`tones [13]` builds both shapes with ground truth — two openings where the
second is deliberately LONGER — and pins all three branches: no window →
first, window → last, and a run past the stop tone is not eligible at all.

**Revert checks run; every surviving mechanism is pinned by at least one.**
Gap bridging off → three fail. Noise gate off → `fixture_faded_phasing`
fails. One-skip rule off → `roundtrip` fails. Window ignored → `tones`
fails. Longest-wins → `fixture_phasing_two_openings` and `tones` fail. The
two that failed to break anything are the two I deleted.

**What did NOT happen: `kUnknown` did not get rarer.** That was the stated
goal of session 9's first candidate and it is not met, for a reason worth
having found. Both recordings still report `kUnknown` — VMW 2215Z because
there is genuinely nothing to measure, GYA 2300Z because its interval is
too noisy to resolve a step and saying otherwise would be a guess. What
changed is that neither is unexplained now, and GYA 2300Z gained its
anchor, which is the thing that actually affects its picture.

**Registered gaps added / narrowed.** A faded interval that IS stepping is
detectable by neither statistic (no library recording is both, so this is
unexercised). The picture-based anchor check (`--expect-phasing-anchor`)
has no faded-signal form.

**The library-wide effect, measured against the pre-session baseline:
5 recordings of 32 entries changed, and every change is intended.** GYA
2300Z gains its interval and its anchor. NMC 2204Z 19 s → 32.5 s, XSG ASPN
26.5 s → 31 s, JMH KiwiSDR Himawari 23.5 s → 30.5 s — all growing TOWARD
the ~30 s of WMO §5.2.3, having previously been cut short at the first
faded line. **Every anchor in the library moved by at most 0.2 samples**,
which is the corroboration that matters: the runs got longer, not
different, so the extra lines are more of the same edge and not something
else being swept in. Everything else — FAXSignal, `jmh sample`, VMW 2230Z,
GYA 2324Z, all six JSC — is byte-identical apart from the timebase wording.
`fixture_anchor_delta_xsg`'s drawn-line band moved 138–148 → 128–140
because nine more phasing lines are now correctly cropped rather than
drawn; the anchor delta it exists to pin did not move (−107.2).

**Next step:** M4 — the GUI plus live audio, which is where the manual
override lives and where session 7's incremental-`detect_tones` cost comes
due. Session 9's note for whoever takes it still stands: the timebase test
is offline (it needs the drawn segment and the whole phasing interval), so
a live decoder needs an incremental form and the prior art has nothing to
offer. Add session 10's: the phasing run now grows forward across gaps and
ends `max_gap` lines after its last strong line, which is incremental
already, but the noise/steps verdict needs the finished run. If a smaller
piece is wanted first, JWX's fold (docs/00, session 10) is the untaken idea
that would decide a faded interval without any per-line vote at all. Tree
is green, buildable, and committed.

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
