# HUMAN SIGN-OFF — Nova audit

Protocol: `docs/07-audit-protocol.md` §"Human sign-off gate", items 0–5.
Signer: Sara. Walkthrough assisted by Kimi (Moonshot AI), 2026-08-19.
Decisions are recorded here as they are made; an item is not signed
until Sara has said so in words.

Status legend: **PENDING** / **SIGNED** / **ADJUSTED** (with note) /
**REJECTED** (finding moves to gap register for resolution).

---

## Item 0 — Gate 0 values (docs/06)

**COMPLETE — all values SIGNED 2026-08-19** (details below)

- **SIGNED** (2026-08-19): WMO_386_EDITION = 2023 edition; "Part III-5"
  = Vol. I Part III §5 location question CLOSED on the cross-verifier's
  extraction of the 2023 PDF (facsimile spec confirmed at Part III §5);
  ISO_9876_EDITION = 2015 (Edition 3); LICENSE_DECISION =
  GPL-3.0-or-later; PRIOR_ART_LANE = B-COPYLEFT (derived).
- **SIGNED** (2026-08-19): AUDIO_INPUT_RATE = 8000 Hz mono 16-bit
  (fixture rate only; live RtAudio capture rate deliberately unpinned);
  FIXTURE_SET = 19 committed excerpts, distinct from the 20-recording
  private library; RECEIVER_MANUALS = 16 manuals, **revisions recorded
  this day** in docs/06 §1 (12 with explicit edition/revision marks,
  4 with none printed — row now complete).
- **SIGNED** (2026-08-19): TARGET_FLOOR_ARCH = 64-bit LE, x86-64
  baseline (no AVX) + AArch64 (x86-64 side run-unverified — D-GAP-003
  stands); TARGET_FLOOR_CPU = Intel Core 2 Duo / Apple M1 (names only,
  no benchmark); TARGET_FLOOR_RAM = 2 GB (worst measured peak 553 MB on
  JSC4's 61-min chart, 2026-08-19); REALTIME_BUDGET = ≤25% of one core
  at 120 lpm IOC 576 on the host (measured ~1.1% on JSC4);
  BENCH_METHOD = not required (moot).
- **SIGNED** (2026-08-19): FIXTURE_COMPARE = screamer-bound policy, no
  golden images, including the recorded knowing trade (a pixel change
  inside every bound passes undetected); coverage gaps stand as
  registered (IOC 288 and 90 lpm synthetic-only; the "truncated
  transmission" row resolved by Pass A — in-suite gap, independently
  fuzz-tested, A-CLAIM-013 stands); MAX_FUNCTION_LINES = 80,
  MAX_NESTING_DEPTH = 4; pass auditor model = Sonnet 5; the §4b
  history-rewrite record reviewed and confirmed accurate (backup bundle
  location and sha256 noted there).

**Item 0 complete.**

## Item 1 — Load-bearing findings (docs/08 §5 register, 33 unique IDs)

IN PROGRESS

- **SIGNED** (2026-08-19) — Pass A, all 12: A-CLAIM-001, 003 (both
  legs), 004, 005, 006, 007, 009, 010, 011, 011a, 012, 013. Majors
  reviewed individually: 003 and 011a stand as declared, documented
  deviations; 007 and 013 remediated (013's fix independently confirmed
  by E-CLAIM-010).
- **SIGNED** (2026-08-19) — Pass B, all 3: B-CLAIM-006 (fldigi headers
  carry no licence grant — idea-level reuse is clean), B-RISK-008 (no
  GPLv2-only upstream — the protocol's critical question answered in the
  clear), B-RISK-013 (critical; remediated by the history rewrite,
  independently confirmed by E-CLAIM-011).
- **SIGNED** (2026-08-19) — Pass D, all 3 criticals: D-PERF-001 and
  D-PERF-002 accepted as remediated in session 32. **D-PERF-003 decided
  and FIXED this day, not just signed**: Sara chose "abandon at 300 s"
  (option A) — `max_opening_sec` in `SessionOptions`; on expiry the
  session leaves START TONE/PHASING for READY and the retained store is
  bounded by the pre-roll again. Pinned by `live_session` T14; full
  suite 38/38 green after the change. Documented in docs/05 beside the
  page cap.
- **SIGNED** (2026-08-19) — Pass E, all 15: E-CLAIM-001…011 (eleven
  CONFORMS / remediation confirmations), E-CLAIM-003 and E-RISK-001
  remediated in session 32, E-GAP-001 (version/tags) and E-GAP-002 (CI)
  stand as release blockers by choice, E-RISK-002 resolved this day by
  the D-PERF-003 fix.

**Item 1 complete — all 33 load-bearing findings signed.**

## Item 2 — Gap register (entire)

**SIGNED** (2026-08-19) — all 19 entries reviewed: A-GAP-002…005,
B-GAP-001…005, C-GAP-002…004, D-GAP-001…006, E-GAP-001…002.
Resolved/moot since writing: A-GAP-004 (E-CLAIM-012), B-GAP-005 (recordings
removed — nothing ships), C-GAP-003 (E-CLAIM-009), D-GAP-001 and D-GAP-002
(resolved by the TARGET_FLOOR_RAM / REALTIME_BUDGET values set this day),
D-GAP-004 (moot — D-PERF-003 fixed this day), D-GAP-005 (Pass A ran).
Standing open and accepted as registered: A-GAP-002, A-GAP-003, A-GAP-005,
B-GAP-001…004, C-GAP-002, C-GAP-004, D-GAP-003, D-GAP-006, E-GAP-001,
E-GAP-002.

## Item 3 — Pass B, in full

**SIGNED** (2026-08-19) — all 26 findings (B-CLAIM-001…007,
B-RISK-001…014, B-GAP-001…005) plus the pass summary, the version-pin
compatibility matrix, and the "what this pass did not cover" section,
including the standing caveat that AI training-data provenance is not
introspectable and the pass structurally cannot produce a clean bill of
health. Remediated since the report: B-CLAIM-006 (NOTICE now records
fldigi's per-file mixed grants), B-RISK-013 (history rewrite),
B-RISK-014 (README AI-authorship disclosure, confirmed by E-CLAIM-008).
Standing by choice: B-RISK-011 (no SPDX headers), B-CLAIM-002 (HamFax
year range one year off, not licence-affecting).

## Item 4 — README / release-note conformance claims

**SIGNED** (2026-08-19) — all nine: the standards framing (WMO §5 /
ISO §4.2, no certified-compliance claim); the decodes-real-stations
claim; the AI-authorship disclosure; the audit description (its "is
being audited" wording to be updated to past tense at close-out, when
that becomes true); the features list including the declared 240 lpm
omission and WAV-only input; "8 run / 30 Skipped"; the security posture
section including the D-PERF-003 paragraph, which was stale as written
and was updated this day to record the opening-cap fix; the honest
Platforms section; the no-external-dependencies claim.

## Item 5 — Cross-verification report (docs/audit/CROSS-VERIFICATION-REPORT.md)

**SIGNED** (2026-08-19) — verdicts (10 VERIFIED / 0 CITATION FAILS /
31 UNVERIFIABLE, failure rate zero), the verifier's isolation
disclosure, the random sample as drawn (seed 920616308), and the
27-vs-34 register-count discrepancy all accepted as recorded. No
finding was invalidated, so nothing moves to the gap register for
citation reasons.

---

## Gate complete

**All six items signed, 2026-08-19, Sara Sakuragawa.** The human
sign-off gate required by `docs/07-audit-protocol.md` is satisfied:
every Gate 0 value, every load-bearing finding (33), the entire gap
register (19), all of Pass B, every README conformance claim, and the
cross-verification report.

One audit finding was not merely signed but resolved at the gate:
D-PERF-003, decided by Sara (abandon at 300 s) and implemented, tested
(`live_session` T14), and documented the same day.

**Release blockers remaining after this gate, by choice or by missing
resource, none of them audit findings awaiting review:** no CI
(E-GAP-002); no version number or tags (E-GAP-001); Pass C's 20
maintainability findings deferred by choice; session 31's two by-hand
GUI runs still outstanding; the registered gaps that need hardware or
fixtures the project does not have (x86-64, big-endian, IOC 288 and
90 lpm recordings).

> An AI audit of AI-authored code is a filter, not an assurance.
> Anything published under your name and callsign carries your
> judgement, not the auditor's.
