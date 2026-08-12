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
1. Auto slant/clock correction (unsolved by every surveyed tool —
   all punt to manual calibration). Screamer: decode of a long
   uncorrected-clock fixture must measure straight within tolerance.
2. Per-line resync from the dead-sector sync pulse [WMO §5.1.3.3].
3. ±150 Hz LF deviation mode [ISO §4.2.2] — no known fixture; needs
   the harness signal generator.
4. Start/stop tone robustness on noisy HF (false-start on text-heavy
   image content is the known failure mode — KiwiSDR filters this via
   phasing-spread sanity check; adapt it).

## Registered gaps
- 90 lpm fixture: not yet identified in the recording library.
- IOC 288 fixture: not yet identified (675 Hz start tone).
- ±150 Hz LF mode: no real-world source known; synthetic-only testing.
