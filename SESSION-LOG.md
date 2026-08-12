# SESSION-LOG.md — Nova

Newest entry first. Append-only: correct by adding an entry, never by
rewriting an old one. Every entry ends with the exact next step.
This file is tracked in git (Sara, session 1: "we don't need to hide
anything as our develop history").

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
