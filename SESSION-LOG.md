# SESSION-LOG.md — Nova

Newest entry first. Append-only: correct by adding an entry, never by
rewriting an old one. Every entry ends with the exact next step.
This file is tracked in git (Sara, session 1: "we don't need to hide
anything as our develop history").

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
