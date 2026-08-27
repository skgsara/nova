# 06 — Audit protocol, Gate 0

Prepared 2026-08-16 against tree revision `893bdd9` (session 31, clean).
**Status: COMPLETE. Gate 0 is fully filled, all five passes have run,
cross-verification is done (2026-08-19, zero citation failures), and the
human sign-off gate is complete (2026-08-19 — see
`audit/HUMAN-SIGNOFF.md`).** Reports are in `audit/`.

Nothing in this file is an audit finding; Gate 0 preparation is not a
pass. It is kept as the record of what the auditors were given as
declared fact, which is what makes the audit reproducible.

The protocol requires the human to verify every value against the
physical documents and real hardware before any pass runs. The rows
below marked **NEEDS SARA** cannot be closed from the source tree at
all; the rows marked *decided* were decided by Sara on 2026-08-16 and
still want her eye on the values themselves.

---

## 0. Auditor identity — decided

The protocol states the auditing agent MUST NOT be the agent that
authored the code. Claude authored all of it across 31 sessions, which
disqualifies it from Passes A, C, D and E.

**Decision (Sara, 2026-08-16): passes run in fresh sessions under a
different model.** Claude stays out of A, C, D and E entirely, and picks
the work back up at remediation, which the protocol already defines as a
separate, later, human-directed activity. Gate 0 authoring — this file —
is not a pass and is not covered by the constraint.

Each pass session gets: the source tree, the reference documents, the
fixture set, the protocol, and the structured outputs of its
prerequisite passes. It does **not** get `SESSION-LOG.md`, `AGENTS.md`,
commit messages, or any authoring transcript.

**Pass B is the sole exception to that document restriction** and may
read all of them, for provenance evidence only. It is **not** an
exception to the authorship rule: the protocol's bar on the agent that
authored the code has no exception anywhere, so Pass B also needs a
non-authoring agent. Knowledge Pass B gains from transcripts must not
be imported into any other pass.

Note for whoever runs the passes: the tree contains a great deal of the
authoring agent's own prose — `docs/01` holds the WMO clause citations,
`docs/02` holds the ISO applicability map. Those are the *subject* of
Pass A, not evidence for it. A.1's "specified" column must come from the
WMO document itself, never from `docs/01`.

---

## 1. Values the tree settles

| Parameter | Value | Evidence |
|---|---|---|
| LICENSE_DECISION | **GPL-3.0-or-later** | `LICENSE` is the GPLv3 text; `NOTICE` and `README.md:336` both state GPLv3+. Already a fact in the tree. |
| PRIOR_ART_LANE | **B-COPYLEFT** | Derived from the above per the protocol, not chosen. Prior-art reading is permitted; the obligation is attribution hygiene. |
| ISO_9876_EDITION | **2015 (Edition 3)** | `ISO_9876_2015(en).pdf` held outside the repo; `docs/01` and `docs/02` cite 2015 throughout. |
| AUDIO_INPUT_RATE | **8000 Hz, mono, 16-bit** | Read from the fixture WAV headers (offset 22 = 1 channel, offset 24 = 8000). All 19 fixtures share this shape. This is the *fixture* rate; the live capture rate through RtAudio is a separate number and is not pinned anywhere. |
| FIXTURE_SET | **`nova/fixtures/`, 19 trimmed WAV excerpts** | Committed in-tree. Distinct from the 20-recording ground-truth library in the parent directory, which is gitignored. Both numbers appear in the docs — an auditor must not conflate them. |
| RECEIVER_MANUALS | **16 manuals; revisions recorded 2026-08-19** | `../Weather Fax Receiver Manuals/`: Furuno FAX-30 (OME-62600-K1: 1st ed. Sep 2002, rev. K1 Aug 2022), FAX-207 (OME-62580-F1: 1st ed. Mar 1994, rev. F1 Jan 2003), FAX-208A (OME-62430-T: 1st ed. Oct 1986, rev. T Jul 1994), FAX-210 (OME-62490-M1: 1st ed. Nov 1991, rev. M1 Aug 2004), FAX-214 (OME-62460-X: 1st ed. Apr 1988, rev. X Jan 2002), FAX-408 (OME-62620-B: 1st ed. Sep 2006, rev. B Jun 2009); JRC JAX-91 (code 7ZPNA4002, Sep 2006, Edition 1), JAX-9B (code 7ZPNA4036D, no date line printed); Nagra FAX-DM (Feb 1982, Kudelski SA); OMC 62610H/FAX-410 (pub. OMC-62610-H, no date line printed); OME 62600L1/FAX-30 (1st ed. Sep 2002, rev. L1 Oct 2025); Samyung SFAX-500 (no edition mark found), SFX-100 (1st ed. 2008-04-23); Sony CRF-V21 (©1988 Sony); Steamrock SR-97 (no edition mark found); Taiyo TF-711 (No. MPP0446, May 2003, version 1.3). Read off the title/imprint pages with pdftotext, and pdftoppm + visual read for the four scanned-only PDFs. |

---

## 2. TARGET_FLOOR — decided in shape, needs values

**Decision (Sara, 2026-08-16): old hardware is not a goal.** The floor is
set to what Nova actually supports, and the unsupported platform tiers
come out of the README.

The finding that drove it: **there is no old-hardware target anywhere in
this project.** The build sets no `-march` or `-mtune`;
`CMAKE_CXX_STANDARD 17` is the only constraint. Everything has only ever
been built and run on macOS arm64 (`build/nova-decode: Mach-O 64-bit
executable arm64`). No 32-bit build has ever been attempted. Filling a
Pentium III floor would not have described Nova — it would have imposed
a new requirement, and Pass D would have measured against a floor
nothing was written for.

**Values confirmed by Sara, 2026-08-19, at the human sign-off gate
(recorded in `audit/HUMAN-SIGNOFF.md`), grounded in a fresh measurement
on JSC4 (61-min chart: 41.6 s wall, 553 MB peak RSS, ~1.1% of one core
at live rate):**

| Parameter | Value | Note |
|---|---|---|
| TARGET_FLOOR_ARCH | 64-bit little-endian: x86-64 (baseline, no AVX) and AArch64 | No sub-baseline extensions are relied on today because none are requested. The x86-64 side is cross-compile-inspected only — D-GAP-003 stands until it runs on real x86-64. |
| TARGET_FLOOR_CPU | Intel Core 2 Duo (x86-64) / Apple M1 (AArch64) | Names only; no benchmark is run. |
| TARGET_FLOOR_RAM | **2 GB** | 3.6× headroom over the worst measured peak (553 MB, JSC4, 2026-08-19). This is the bound A.3's malformed-input testing measures against. |
| REALTIME_BUDGET | **≤ 25% of one core** at 120 LPM, IOC 576, on the machine Nova runs on | Measured reality ~1.1%; the ceiling exists to fail a real regression, not to justify breaking KISS. |
| BENCH_METHOD | **Not required** | Moot under this decision. Pass D reports no PERF figures and says so in its "what this pass did not cover" statement. |

**Pending remediation, independent of the values above:** `README.md`
claims Tier 1 *"release-tested: Windows 64-bit, macOS (universal), Linux
x86_64"* and Tier 2 *"CI-built, community-tested: 32-bit Windows/Linux,
ARM, FreeBSD"*. There is no CI and no git remote (`git remote -v` is
empty), and no platform other than macOS arm64 has ever been built. Those
claims are unsupported under every option and are struck.

---

## 3. FIXTURE_COMPARE — decided, and rewritten around the suite

**Decision (Sara, 2026-08-16): state the policy in terms of the property
assertions the suite actually makes.** No golden-image set is built.

The protocol as written assumes each fixture has an expected output
image, hashed. **No such artifact exists** — there are no stored golden
images and no hashes anywhere in `tests/` or `CMakeLists.txt`. Nova's
suite is screamers: numeric bounds on measured properties of the decoded
picture, several of which share no code with the decoder.

### The policy

For each fixture, the comparison is the bound set registered with its
`add_test` in `CMakeLists.txt`, in the form

```
nova-test-fixture <path> <lpm> <min_lines> <max_lines>
                  <clock_lo_ppm> <clock_hi_ppm> <min_locked_frac>
                  [--expect-* ...]
```

A fixture PASSES when the decode reports a line rate equal to `<lpm>`, a
drawn-line count within `[min_lines, max_lines]`, a clock within
`[clock_lo_ppm, clock_hi_ppm]`, a locked-line fraction at or above
`<min_locked_frac>`, and every `--expect-*` predicate satisfied. Those
predicates are the picture-domain checks and carry their own numeric
bounds — `--expect-straight-strip N` (90th percentile row-to-row move of
the dead-sector edge, in finished pixels), `--expect-straight-porch N`,
`--expect-rigid-rows N`, `--expect-rows-in-place N`,
`--expect-anchor-delta LO HI`, `--expect-phasing-window LO HI`,
`--expect-phasing-lines N`, `--expect-timebase linear|steps|noisy`,
`--expect-white-only`, `--expect-phasing-anchor`, `--expect-reject`.

**This policy is architecture-independent by construction**, which
resolves the problem the protocol's FIXTURE_COMPARE row was written to
solve. The bounds are tolerances on measured physical quantities, not
byte patterns, so x87/SSE divergence cannot produce a spurious failure
and no separate canonical-versus-floor comparison rule is needed. The
protocol's warning that a strict-hash requirement on an x87 build is a CI
defect does not apply, because no strict hash exists.

**What this policy gives up, recorded because it is a knowing trade:** a
change that alters output pixels while keeping every measured property
inside its bounds will not be caught. The bounds were each set below the
measured value and above what the previous code achieved, so a
regression to a known-worse behaviour fails — but an unintended change
that is merely *different* can pass. A golden-image tripwire was the
option that would have caught it and was declined.

### Coverage against A.3's required list

A.3 requires the fixture set to cover, at minimum: both IOC values,
every supported line rate, a clean signal, a fading/noisy signal, a
signal with a missed or corrupted phasing block, and a truncated
transmission. From the registrations, with the gaps the project has
already registered itself:

| Required | Covered by | Status |
|---|---|---|
| IOC 576 | most fixtures | yes |
| IOC 288 | — | **gap** — `docs/02` registers "real 675 Hz fixture remains a registered gap"; synthetic only |
| 60 lpm | `fixture_60lpm` (JSC1) | yes |
| 90 lpm | — | **gap** — `docs/02` registers "real 90 lpm fixture remains a registered gap"; synthetic only |
| 120 lpm | many | yes |
| clean signal | `fixture` (JMH KiwiSDR test chart) | yes |
| fading / noisy | `fixture_weak_white` (GYA), `fixture_faded_phasing` | yes |
| corrupted phasing | `fixture_faded_phasing`, `himawari-kiwisdr-phasing-jump`, `fixture_phasing_two_openings` | yes |
| truncated transmission | — | **unclear** — `fixture_fill_reject` covers a stall, not a truncation. Pass A should determine whether any fixture opens or closes mid-transmission in the sense A.3 means. |

These are Pass A's to verify and re-register; they are listed here only
so Gate 0 is not filled in ignorance of them.

### A naming collision that will derail a pass if it is not flagged

A.3's "mutation testing" means **corrupting the fixture audio** — bit
flips, truncation, head/tail splicing, silence injection — and requiring
the decoder not to crash, hang, or grow unboundedly.

Nova's own long-standing discipline, referred to throughout
`SESSION-LOG.md` and `ROADMAP.md` as "mutation testing", means the
opposite direction: **mutating the source** to prove a test fails when
the code is broken.

These are two different activities with the same name. An auditor
reading the project's language will find extensive evidence of "mutation
testing" and may credit it against A.3, which it does not satisfy at
all. **A.3's input-corruption testing does not exist in this project.**
The nearest thing is `fixture_fill_reject`, a single hand-made stall
case. Building it is real, unstarted work.

---

## 4. NEEDS SARA — the remaining rows

### WMO_386_EDITION — half decided, half still open

**WMO_386_EDITION = the 2023 edition** (`386_2023-edition_en.pdf`).
Decided by Sara, 2026-08-16: pin the latest. The 2009 edition
(`WMO_386_Vol_I_2009_en.pdf`) stays on disk as the corroborating check
that `docs/01` and `NOTICE` already describe, but 2023 is normative and
is what Pass A must cite.

**The second half of this row is NOT closed.** The protocol warns that if
Part III-5 is not where the facsimile specification lives, every Tier 1
citation degrades — and that check has not been made. The project cites
in the form `WMO §5.1.2`, meaning section 5 of Vol. I Part III; the
protocol's form is `WMO386:III-5 §x.y`. Almost certainly the same thing
said two ways, and "almost certainly" is exactly what this row exists to
eliminate.

Attempted 2026-08-16 and failed for want of tooling: the 2023 PDF is 233
pages of subset-encoded fonts, and this machine has no `pdftotext`,
`mutool`, `qpdf`, or any Python PDF library, so a raw stream extraction
returned glyph indices rather than text (zero hits for "facsimile" in
3.8 MB of extracted bytes — an extraction failure, not a finding about
the document). Installing poppler would close it, as would Sara simply
opening the PDF and reading the contents page.

**Pass A must not run until this is closed.** Everything else in Gate 0
is now filled.

The two checks, restated in full so neither is lost:

1. ~~**Which edition is normative.**~~ **Decided: 2023.** The project
   does not pin one on its own —
   `docs/01` and `NOTICE` both say the citations were verified against
   the 2009 *and* 2023 editions and found identical in the checked
   sections. Both PDFs are on disk (`386_2023-edition_en.pdf`,
   `WMO_386_Vol_I_2009_en.pdf`), both gitignored via `*.pdf`. Keeping
   2009 as a corroborating check is stronger than the protocol asks and
   matches what was actually done, but the protocol wants one pin and
   2023 is it.
2. **Whether "Part III-5" and the project's "Vol. I, Part III, §5" are
   the same location.** The project cites as `WMO §5.1.2`, meaning
   section 5 of Part III; the protocol's form is `WMO386:III-5 §x.y`.
   Probably the same thing said two ways — and "probably" is exactly
   what this row exists to eliminate. If they diverge, every citation in
   `docs/01`, `docs/02` and the README needs re-anchoring.

### MAX_FUNCTION_LINES / MAX_NESTING_DEPTH — decided

**MAX_FUNCTION_LINES = 80. MAX_NESTING_DEPTH = 4.** (Sara, 2026-08-16.)
The protocol's own example values, chosen against the measured
distribution rather than by default.

**Counting convention** (recorded 2026-08-27, PR-013): both numbers are
measured the way the scanner below measures them — nesting depth is
BRACE depth relative to the function body; a braceless control-flow
statement does not add a level. The alternative convention (counting
control-flow statements, braces or not) reads one chain in
`core/fax.cpp`'s `relock_dropout_runs` as depth 5 — while > if > if >
for > a braceless `if (relock_row(...))` — where the brace count is 4.
That chain is ACCEPTED under this convention: the braceless `if` adds
no scope, and it is the convention the Pass C remediation's "none
deeper than 4" was measured against.

Distribution across 168 functions in `core/ live/ cli/ gui/`, measured
2026-08-16 by brace-depth scan (heuristic — it keys on a signature
pattern at column 0, so it will miss some definitions and may merge
others; good enough to set a threshold against, not a census):

| | lines |
|---|---|
| median | 13 |
| p75 | 36 |
| p90 | 102 |
| max | 416 |

Functions over each candidate threshold: >60 → 28 (17%), **>80 → 24
(14%)**, >100 → 17 (10%), >150 → 6 (4%).

The longest, which Pass C will certainly reach: `stage_assembly`
(`core/fax.cpp:1512`, 416), `main` (`gui/nova-gui.cpp:3174`, 397),
`detect_phasing` (`core/phasing.cpp:180`, 297), `LiveEngine::collect_batch`
(`live/engine.cpp:661`, 186), `main` (`cli/nova-decode.cpp:45`, 178),
`stage_dead_sector` (`core/fax.cpp:607`, 175).

### Pass auditor model — decided

**Sonnet 5** runs Passes A, C, D and E, each in a fresh session with no
rationale, no session log, no commit messages. Cross-verification
afterwards must use a third model different from both Opus 5 and
Sonnet 5.

### RECEIVER_MANUALS revisions

**Closed 2026-08-19** — all sixteen title/imprint pages read; the
revisions are recorded in the §1 row above. (This task also appeared
twice in this file by a copy error; collapsed to one entry when
closed.)

---

## 4b. History rewrite, 2026-08-16 — recorded because it invalidates SHAs

Pass B's B-RISK-013 (critical, load-bearing) found 19 off-air recordings
tracked in git with no redistribution basis stated for any of them, one of
them from a commercial news agency. **Sara's decision: the library stays
private.** The audio was removed from all history.

| | before | after |
|---|---|---|
| HEAD | `330d408` | `aeff6a3` |
| commits | 69 | 69 |
| `.git` | 38 MB | 1.1 MB |
| `.wav` across all refs | 19 | **0** |

Command: `git filter-repo --path-glob 'fixtures/*.wav' --invert-paths`.
The narrower glob rather than `--path fixtures/`, so `fixtures/MANIFEST.md`
survives. All **seven** branches were rewritten, `main` included — it
carried 17 of the recordings and would have kept them had only the working
branch been filtered.

Backup before the rewrite:
`../nova-prerewrite-backup/nova-prerewrite-20260816-330d408.bundle`,
`git bundle verify` reports a complete history, sha256
`033d54148f908b080fab539a41c851fd91b37ade656b720aac2886b399f10873`.
The recordings themselves are preserved outside the repository at
`../fixtures-private/`, hash-checked against `fixtures/MANIFEST.md`.

**Two consequences worth stating.**

1. **The audit reports keep their original citations and are not edited.**
   B-RISK-013 says `git ls-files fixtures/` returns 19; it now returns 1.
   The report is a dated artifact and correcting it in place would destroy
   the record of what was found. Correct by appending — the same rule
   `SESSION-LOG.md` runs on.
2. **Every SHA in the reports and in commit messages before `aeff6a3` no
   longer resolves.** The first three commits keep their identity (they
   predate the first recording); everything from M0 onward was rewritten.
   The bundle above is the only way back to the old hashes.

`filter-repo` also checks out the rewritten tree, which deleted the
working-tree copies as well — not only the history. They were restored
from `../fixtures-private/` and are now untracked and ignored, so local
runs still see all 38 suites.

## 5. Things already visible that the passes will hit

Not findings — no pass has run, and this file's author is not the
auditor. Recorded because they bear on how Gate 0 is filled.

1. **No CI, no remote.** A.3 requires confirming the fixture set runs in
   CI with failures blocking the build; Pass E requires the same. The
   repo is local-only. There is nothing to audit, and both passes will
   register it.
2. **The README's Platforms section claims testing that has not
   happened.** See §2. Likely load-bearing for Pass E as a public claim
   about verification.
3. **`docs/02` is already in the shape A.2 demands.** A.2 forbids a
   global "conforms to ISO 9876:2015" statement; `docs/02` is a
   clause-by-clause applicability map with hardware-only exclusions
   listed as decisions, and both it and the README say "designed to
   satisfy … no claim of certified compliance". Whether each row's
   verdict is *correct* is Pass A's job.
4. **`NOTICE` carries a lineage ledger but no version pins.** It names
   ACfax, HamFax, weatherfax_pi, KiwiSDR FAX, JWX, Isobar and fldigi
   with stated licences and an explicit "idea-level reuse, no code
   taken" claim. B-COPYLEFT step 1 asks for the exact upstream
   version/commit examined and the licence **as stated in that version's
   file headers**. `NOTICE` asserts licences instead, and records no
   version or commit for any of the seven. The protocol's specific
   concern is a **GPLv2-only** upstream, which would be incompatible
   with GPLv3+ publication of a derivation; the ledger records GPLv2+
   for ACfax, HamFax and JWX, but from assertion rather than headers.
5. **No fixture has a stated redistribution basis.** B.0 requires one per
   fixture, and says fixtures without a basis must not ship. The 19
   committed excerpts derive from JMH (Japan), VMW (Australia), XSG
   (China), GYA (France), NMC (USA), HLL (Korea) and JSC (Kyodo News —
   a commercial newspaper facsimile, the most likely of the set to have
   no basis at all). Nothing in the tree states a basis for any of them.

---

## 6. Order of work

1. ~~Floor decision~~ — decided, §2. Values still to confirm.
2. ~~FIXTURE_COMPARE~~ — decided and written, §3.
3. Verify the WMO pin against the paper: edition, and Part III-5.
4. Read the 16 manual revisions off their title pages.
5. Pick the two Pass C thresholds.
6. **Pass B carries the critical-severity legal outcome, but it cannot
   run today — its corpus is missing.** B-COPYLEFT step 1 requires the
   exact upstream version/commit of each prior-art project and the
   licence **as stated in that version's file headers**. Of the six
   external projects `NOTICE` names, exactly one is on disk:
   `../JWX_source.tar.bz2`. ACfax, HamFax, weatherfax_pi, the KiwiSDR
   FAX extension and fldigi are all absent, as is Isobar, Sara's own
   prior work. Headers cannot be read from sources that are not there,
   and `NOTICE`'s licence claims are precisely what Pass B exists to
   check rather than to trust.
   **Corpus assembly is a prerequisite, not part of the pass** — and it
   is work the authoring agent may do, since fetching sources is not
   auditing them.
   *(Corrects an earlier draft of this file, which said all of Pass B's
   inputs existed today. They do not.)*
   **Assembled 2026-08-16**, all six projects, pinned and checksummed:
   `../prior-art-corpus/`, with `CORPUS-MANIFEST.md` recording source
   URL, commit or version, and size for each. It sits OUTSIDE the git
   repository so it cannot be committed by accident. Isobar was left
   unextracted and its location noted instead. Pass B is unblocked.

7. **Pass C is the one that can start immediately** — it needs only the
   two Gate 0 thresholds and the source tree. Nothing else blocks it.
   **Correction to an earlier draft of this file:** Pass B is *not*
   exempt from the authorship constraint. The protocol's exception is to
   the DOCUMENT-ACCESS clause only — Pass B may read authoring
   transcripts, session logs and full git history, for provenance
   evidence. The clause barring the agent that authored the code has no
   exception anywhere in the protocol, so Pass B needs a non-authoring
   agent exactly as A, C, D and E do. It is first because its inputs are
   ready, not because it is easier to staff.
