# AGENTS.md — Nova

## What this project is
Nova is a cross-platform HF WEFAX (F3C) decoder written from public
standards: WMO-No. 386 Vol. I Part III §5 (signal) and ISO 9876:2015
§4.2 (receiver behaviour, as a design target — no certified-compliance
claim). C++17, FLTK + RtAudio, GPLv3+. No reverse engineering is
involved; DSP lineage is GPL reuse with attribution (see NOTICE).

## Read first, every session
START-HERE.md → top entry of SESSION-LOG.md → state the next step back
to Sara before acting. If the log's last entry has no next step, say so
and stop — that is a broken log, not an invitation to improvise.

## Lane
There are no dirty/clean lanes in this project — no analysis dir, no
decompilation. Instead there is a **provenance rule**: every reused
algorithm or table is recorded in NOTICE and in `docs/00` the day it
enters the tree, with source project and licence.

## Spec status
`docs/01-signal-spec.md` is authoritative for the signal;
`docs/02-compliance-matrix.md` is the acceptance checklist. Changes to
either require a note in SESSION-LOG.md and, once tests exist that pin
the old reading, a committed test update in the same change.

## Reuse before invention (Sara, session 4)
Nova is standards-first, not from-scratch-first. Before writing a new
algorithm, look at how the mature decoders already solved it:
**ACFax, HamFax, fldigi, JWX** (JWX source is in the parent folder),
**weatherfax_pi** and the **KiwiSDR** fax extension — both already good on
clock/slant and phasing-stage alignment — and **Isobar**, which is the
reference for the per-line sync lock approach specifically. If a good solution exists there, reuse it, with the
provenance rule below (NOTICE + `docs/00`, same day). Write something new
only when the prior art genuinely does not cover the case, and say in the
session log which projects you checked and what they did.

This is not only about effort. Their behaviour is the de-facto
compatibility target: a decoder that agrees with fldigi on a signal is
more likely right than one that agrees only with its own reasoning.

## Standing rules
- Claim checklists, not step lists. Always include a "contradictions
  found" section ("none" is a deliverable).
- Every load-bearing claim cites a spec section (e.g. `[WMO §5.2.2.1]`,
  `[ISO §4.2.5]`) and names its screamer test. No screamer = registered
  gap, written down as such.
- Never commit: `recordings/`, `*.m4a`, `*.pdf`, `SESSION-LOG.md`,
  anything under the parent folder. The ISO PDF is a single-user
  licensed document — it is cited, never copied or quoted at length.
- WIP commits on a scratch branch are fine; `main` moves and pushes
  happen only when Sara asks — then don't leave it unpushed.
- No flattery. Disagree when you have grounds. Say when you don't know.
- Finish the small piece or revert; never leave the tree unbuildable.
- Update SESSION-LOG.md (append-only, newest first) before every stop,
  ending with the exact next step.
- Verify before claiming done: run the tests, look at the output image.

## Current risk register (top items)
1. ~~Auto slant/clock correction~~ — proven synthetic (+100 ppm) and
   across the library (session 3 batch: JMH, JSC, XSG, NMC, VMW...).
   Residual: clock wander over long recordings (dead-sector diagonal
   over 61-min JSC4) — bounded, cosmetic at current scope.
2. ~~Per-line resync~~ — session 4: anchor found by across-line
   consistency, pass A re-acquires after 8 misses. 88-99% honest locks on
   every pulse station in the library; time-skips heal (warp fixture
   max_step 54.3 -> 0.75 px). Remaining limit: weak/faded signals (GYA
   2300Z) still break the coarse period fit. White-only stations report
   zero locks BY DESIGN — a measured negative result, not a gap to close
   (docs/01 §5); their per-line phase, if it is recoverable at all, has
   to come from the phasing stage in M3.
3. Start/stop tone detection + auto sequencing (M3) — untouched;
   false-start on text-heavy content is the known trap. Library tone
   survey (session 3): only `jmh sample.wav` carries a start tone.
4. ±150 Hz LF deviation [ISO §4.2.2] — synthetic only; no fixture.
5. ~~Long recordings~~ — session 3: JSC4 61 min, XSG 23 min, Himawari
   17 min all decode end-to-end.

## Registered gaps
- 90 lpm fixture: none in the library (batch survey, session 3).
- IOC 288 fixture: none (no 675 Hz start tone found anywhere).
- ±150 Hz LF mode: no real-world source known; synthetic-only testing.
- Weak/faded signals: GYA 2300Z's period estimate is +3576 ppm off and
  the picture slants. Untouched.
