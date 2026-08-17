# 07 — Audit protocol (v2)

Author: Sara Sakuragawa. This is the protocol Nova is audited against.
Its filled parameters live in `06-audit-gate0.md`; the pass reports live
in `audit/`.

**Status: template. Fill Gate 0 before running any pass.**

**Execution model:** five independent passes, each in a fresh session.
Each pass receives: the source tree, the reference documents, the fixture
set, this protocol, and the structured outputs of the prerequisite passes
(Pass D receives Pass C's output; Pass E receives all prior outputs).
Independence means the auditor does not see a prior pass's reasoning —
only its findings register.

**Auditor constraint:** the auditing agent MUST NOT be the agent that
authored the code, and MUST NOT be given the design rationale, commit
messages, or authoring-session transcripts — with one exception: Pass B
is the provenance lane and may inspect authoring transcripts, retained
session logs, and full git history (including commit messages) solely to
establish prior-art exposure. Knowledge gained there must not be imported
into any other pass's reasoning. Every pass report header records the
auditor's model, version, date, and configuration, so the audit itself is
reproducible.

## Gate 0 — Parameters (fill before running)

These are not suggestions to the agent. They are declared facts the agent
must treat as given. The auditor has no authority to question Gate 0 —
therefore the human must verify every Gate 0 value against the physical
documents and real hardware before running anything. This verification is
the first item of the human sign-off gate.

| Parameter | Value | Notes |
|---|---|---|
| LICENSE_DECISION | GPL-3.0-or-later \| MIT \| Apache-2.0 \| UNDECIDED | If UNDECIDED, Pass B cannot run. Select before proceeding. |
| PRIOR_ART_LANE | derived — see Pass B | Determined by LICENSE_DECISION, not chosen independently. |
| WMO_386_EDITION | e.g. 2023 update, Part III-5 | Pin edition + update year. Cite as WMO386:III-5 §x.y. Verify the pin against the physical document before running — if Part III-5 is not where the facsimile signal specification lives, every Tier 1 citation degrades. |
| ISO_9876_EDITION | 2015 (Edition 3) | Confirmed current as of last review. |
| TARGET_FLOOR_ARCH | e.g. i686, no SSE2 or i686 + SSE2 | SSE2 is not guaranteed on pre-2003 i686. Decide. |
| TARGET_FLOOR_CPU | e.g. Pentium III 800 MHz | Named part, not a vibe. |
| TARGET_FLOOR_RAM | e.g. 256 MB total, 64 MB process ceiling | |
| AUDIO_INPUT_RATE | e.g. 11025 Hz mono | The rate the real-time budget is computed against. |
| REALTIME_BUDGET | e.g. ≤ 40% of one core at 120 LPM, IOC 576 | Must be measurable. This is the only thing that justifies breaking KISS. |
| FIXTURE_SET | path | Off-air recordings + expected outputs. See Pass A.3. Provenance and redistribution rights are checked in B.0. |
| FIXTURE_COMPARE | e.g. exact SHA-256 on canonical build; pixel-diff ≤ 0.5% on floor-target build | Strict hash is required on the canonical architecture only. Cross-architecture comparison uses the stated tolerance — x87/SSE divergence makes strict hash on the floor target a spurious-failure generator. |
| MAX_FUNCTION_LINES | e.g. 80 | Pass C threshold. "Reasonable length" is not auditable; a number is. |
| MAX_NESTING_DEPTH | e.g. 4 | Pass C threshold. |
| BENCH_METHOD | e.g. 86Box, Pentium III 800, gcc 12.2 -march=pentium3 -O2, 3 runs, median | Emulator/hardware, CPU model, compiler + version + flags, repetition count, aggregation. Unspecified method = unreproducible PERF findings. |
| RECEIVER_MANUALS | enumerated list, model + revision | Vague "commercial receivers" is not auditable. |

**Reference document handling.** WMO-No. 386 and ISO 9876:2015 are
copyrighted and are working references only. They MUST be gitignored and
MUST NOT appear in the published repository. The audit reports are
publishable artifacts, so they MUST paraphrase clause content and cite by
number — no verbatim reproduction of standard text in any finding,
comment, docstring, or test name.

## Source authority hierarchy

Findings must cite a tier. Higher tier wins on conflict; a conflict
between tiers is itself a finding, not something the agent resolves
silently.

- **Tier 1 — WMO-No. 386, Part III-5.** Normative for everything the
  decoder does: IOC, line rate, tone frequencies and durations, phasing,
  deviation, aspect ratio. This is the signal specification.
- **Tier 2 — ISO 9876:2015.** Contextual. It is a shipborne-receiver
  hardware type-approval standard (construction, performance, type
  testing, inspection) and it defers the transmission format to
  WMO-No. 386 Part III-5. Most clauses are hardware-only and inapplicable
  to a software decoder. Pass A must produce an explicit applicability map
  (see A.2) rather than asserting conformance.
- **Tier 3 — Enumerated receiver operating manuals.** Evidence of observed
  field behaviour, not specification. Where a manual documents behaviour
  absent from Tier 1, that is a compatibility note, never a conformance
  claim. Manuals are copyrighted: describe behaviour, do not transcribe.
- **Tier 4 — Prior-art source code.** See Pass B for whether the agent may
  read this at all.

## Global rules for every pass

1. **No unsourced claims.** Every finding carries either a Tier 1/2/3
   citation or a file:line reference. A finding with neither is invalid
   and must be moved to the gap register.
2. **Cite only what you read.** If the agent cannot locate a clause in the
   provided documents, it MUST register a gap. Inventing a clause number
   is the single worst failure mode for this audit and is treated as a
   critical defect in the audit itself.
3. **Audit, do not fix.** No source modifications during any pass.
   Findings only. Remediation is a separate, later, human-directed
   activity.
4. **Prior expectation is not evidence.** The agent's trained-in knowledge
   of WEFAX parameters is a hypothesis generator only. Where the agent's
   expectation disagrees with the document, the document wins and the
   disagreement is logged as a finding against the agent's assumption, not
   against the document.
5. **Gap register.** Anything the agent cannot resolve with available
   evidence goes to GAP-nnn with a statement of what evidence would
   resolve it. An honest gap register is a success criterion, not a
   failure.
6. **Traceability.** Every finding gets a stable ID: CLAIM-nnn
   (conformance assertion), RISK-nnn (legal/provenance), MAINT-nnn
   (readability), PERF-nnn (portability/performance), GAP-nnn. Numbering
   is per-pass-namespaced (e.g. A-CLAIM-014) so independent sessions
   cannot collide.
7. **Load-bearing flag.** A finding is load-bearing if a downstream
   decision (license selection, release/no-release, a conformance
   statement in the README) depends on it. Load-bearing findings require
   human sign-off and may not be auto-accepted.

## Severity definitions

| Severity | Meaning |
|---|---|
| critical | Wrong decoder output on valid input; legal/compliance exposure (license incompatibility, missing attribution on derived code); crash, hang, or unbounded resource use on untrusted input. |
| major | Spec deviation with a workaround; portability or real-time failure on the floor target; missing required attribution where derivation is confirmed but licensed compatibly. |
| minor | Maintainability issues with no behavioural effect (threshold violations under MAX_FUNCTION_LINES / MAX_NESTING_DEPTH, comment problems, duplication). |
| informational | Observations and Tier 3 compatibility notes. No action required. |

## Pass A — Signal conformance (Tier 1 primary)

### A.1 Parameter conformance table

For each parameter family below, locate the governing clause in
WMO-No. 386 Part III-5, record the document's stated value, record the
implemented value with file:line, and mark CONFORMS / DEVIATES /
NOT-IMPLEMENTED / GAP.

- Index of Cooperation values and the code paths selecting between them
- Line rate / drum speed set, and rate detection or selection logic
- Start (APT) tone: frequency per IOC, duration, black/white alternation
  rate
- Phasing signal: line count, pulse geometry, and the sync-lock
  algorithm's acceptance window
- Stop tone: frequency, duration, and stop detection behaviour
- Carrier / centre frequency, deviation, black and white frequency
  assignment, and modulation index
- Aspect ratio, samples-per-line derivation, and accumulated timing drift
  over a full chart
- Behaviour on out-of-spec, truncated, or noise-corrupted input

The agent is expected to arrive at this table with prior expectations
about several of these values. Those expectations MUST NOT be written
into the table. Only document-sourced values go in the "specified"
column.

### A.2 ISO 9876:2015 applicability map

Walk the standard clause by clause. For each clause emit exactly one of:

- **APPLICABLE** — with the implementing file:line and a conformance
  verdict
- **HARDWARE-ONLY** — with a one-line reason (RF front end, recording
  unit, mechanical, environmental, type-test procedure)
- **DEFERRED** — clause hands off to WMO-No. 386; cross-reference the A.1
  row

Deliverable is the map itself. Do not emit a global "conforms to
ISO 9876:2015" statement; for a software decoder that claim is not
available and must not appear in the README, the repo description, or
release notes.

### A.3 Fixture regression (the real conformance test)

Prose conformance is secondary to correct output on known-good input.

- Each fixture: an off-air recording, provenance (station, frequency,
  UTC, receiver, sample rate), and an expected output image hashed.
- Verify the fixture set covers, at minimum: both IOC values, every
  supported line rate, a clean signal, a fading/noisy signal, a signal
  with a missed or corrupted phasing block, and a truncated transmission.
- Comparison follows FIXTURE_COMPARE: exact hash on the canonical build;
  tolerance-based image comparison on floor-target and other
  non-canonical builds. A strict-hash requirement on an x87 build is a
  defect in the CI configuration, not in the decoder — flag it as such.
- **Mutation testing.** Derive corrupted variants of each fixture (bit
  flips, truncation, head/tail splicing, silence injection) and feed them
  to the decoder. Any crash, hang, or memory growth beyond
  TARGET_FLOOR_RAM is a finding (critical severity). This decoder
  processes untrusted RF input; historical vulnerabilities in this class
  of software cluster in malformed-input handling.
- Report coverage gaps as GAP-nnn. A parameter marked CONFORMS in A.1
  with no fixture exercising it is an untested conformance claim and must
  be flagged as such.
- Confirm the fixture set runs in CI and that comparison failures fail the
  build.

### A.4 Tier 3 compatibility notes

For each enumerated receiver manual, record documented behaviours that
Tier 1 does not specify (autostart thresholds, tolerance windows,
unsupported-mode handling). Output is a compatibility appendix, clearly
separated from conformance. No conformance verdict may cite Tier 3.

## Pass B — Provenance and licensing

Run one variant. The variant is determined by LICENSE_DECISION, not
chosen by the agent.

This pass is exempt from the auditor isolation constraint to the extent
stated in the header: it may inspect authoring transcripts, session logs,
and full git history solely to establish prior-art exposure.

### Variant B-COPYLEFT — if LICENSE_DECISION is GPL-family

Prior-art reading is permitted. The obligation is attribution hygiene,
not avoidance.

1. For each of ACfax, HamFax, JWX, fldigi, KiwiSDR FAX extension: record
   the exact upstream version/commit examined, the license as stated in
   the file headers of that version (not as stated on a website — headers
   govern, and fldigi is documented variously as GPLv2+ and GPLv3), and
   the copyright holders named.
2. Note the known derivation chain before starting: fldigi's WEFAX source
   states its core was taken from HamFax and its FIR low-pass filter
   coefficient sets from ACfax. Material pulled from fldigi may therefore
   carry ACfax and HamFax copyright. Attribution must follow the chain to
   its origin, not stop at the proximate source.
3. Identify every location in the project that is derived from, adapted
   from, or structurally follows prior-art code. Filter coefficient
   tables, sync detection state machines, and resampler kernels are the
   highest-probability sites.
4. **Version-pin compatibility matrix.** For every derivation site,
   classify the upstream grant as GPLv2-only / GPLv2-or-later / GPLv3+
   from the actual header text, and tabulate against LICENSE_DECISION. A
   GPLv2-only upstream is incompatible with GPL-3.0-or-later publication
   of that derivation. Do not assume "GPL-family upstream + GPL-family
   project = fine"; the version pin is where GPL derivations actually
   break. Any incompatibility is a critical, load-bearing finding.
5. Verify each such location carries an in-file attribution comment naming
   the origin project, and that NOTICE / THIRD-PARTY enumerates all
   upstream copyright holders and licenses.
6. Verify license compatibility of every third-party dependency (not just
   prior art) against the selected license.

### Variant B-PERMISSIVE — if LICENSE_DECISION is MIT / Apache-2.0

Prior-art source MUST NOT be read by the auditing agent. Reading it
converts this session to a dirty lane and invalidates the pass. The task
is contamination detection, and the tools are static, not conversational.

1. Establish whether the authoring sessions had prior-art source in
   context. Check authoring transcripts, retained session logs, and any
   vendored or cached copies in the working tree or its history
   (`git log --all --diff-filter=A`, including deleted files and stashes).
2. Run automated similarity and license scanning — scancode-toolkit,
   fossology, or equivalent — against the known prior-art corpus. Record
   tool, version, corpus revision, and raw output. Note in the report that
   these tools catch near-verbatim copying only; paraphrased or
   algorithmically-equivalent reuse is below their floor.
3. Flag high-risk constructs for human review regardless of scanner
   result: hardcoded filter coefficient tables, magic constants not
   traceable to a Tier 1 clause, unusually specific variable naming, and
   any function whose structure has no derivation in the project's own
   documentation.
4. The agent must state plainly that it cannot certify the provenance of
   its own generated output. Training-data provenance is not
   introspectable. This pass can raise the confidence floor; it cannot
   produce a clean bill of health. If the contamination question is
   genuinely load-bearing for the release, the honest outcomes are:
   relicense to GPL, or have a human rewrite the flagged constructs from
   the Tier 1 document alone.

### B.0 Applies to both variants

1. Confirm LICENSE file present, matching LICENSE_DECISION, and
   consistent with every SPDX header in the tree.
2. Confirm no reference document (WMO-No. 386, ISO 9876:2015, receiver
   manuals) or extract thereof is committed or tracked.
3. **Fixture redistribution rights.** The fixture recordings contain
   transmitted meteorological chart products, whose copyright status
   varies by issuing national meteorological service (some are public
   domain, many are not). For each fixture, record the issuing service and
   the basis on which redistribution in a public repository is
   permissible. Unresolvable cases go to the gap register; fixtures
   without a redistribution basis must not ship in the repo.
4. Confirm AI authorship is disclosed in the README, with the audit
   protocol and its outputs linked.

## Pass C — Readability and maintainability (KISS)

Optimisation is out of scope for this pass. Where C and D conflict, C wins
by default; D may only override with a benchmark on TARGET_FLOOR_CPU
demonstrating a REALTIME_BUDGET miss. Record every such override as a
PERF-nnn linked to the MAINT-nnn it overrules.

- Identify functions exceeding MAX_FUNCTION_LINES or MAX_NESTING_DEPTH;
  identify duplicated logic. Thresholds are Gate 0 facts, not auditor
  discretion.
- Identify abstractions with a single implementation, speculative
  extension points, and indirection layers that a maintainer must traverse
  without benefit — these are a characteristic AI-authorship artifact and
  should be looked for specifically.
- Identify comments that restate the code rather than explain intent.
  Every DSP constant should have a comment citing either its Tier 1 clause
  or its derivation; a bare numeric literal in signal-path code is a
  finding.
- Assess whether a competent C/C++ maintainer with no project history
  could locate the sync lock, the demodulator, and the image writer within
  a few minutes of opening the tree.
- Assess build simplicity: dependency count, and whether the documented
  build actually reproduces from a clean checkout.

## Pass D — Portability and 32-bit performance

Everything here is measured against the Gate 0 floor. "Optimised enough
for old machines" is not an acceptance criterion; REALTIME_BUDGET is.

- Verify no reliance on instruction set extensions above
  TARGET_FLOOR_ARCH, including compiler-generated ones — check effective
  `-march`/`-mtune`, and whether auto-vectorisation emits above-floor
  instructions.
- Verify 32-bit correctness: no assumption that `long` or pointers are
  64-bit, no `size_t`/`int` conflation, no 2 GB+ allocation paths, LFS
  handling for file offsets.
- Verify peak memory against TARGET_FLOOR_RAM, specifically full-chart
  image buffering at the largest supported IOC and line count.
- Verify floating-point strategy is sound on the floor CPU; identify any
  x87/SSE result divergence. The acceptance mechanism is the tolerance
  comparison in FIXTURE_COMPARE run on both an x87 build and an SSE build
  — not a requirement that the two builds produce identical bits.
- Benchmark per BENCH_METHOD on real or emulated floor hardware. Report
  measured figures against REALTIME_BUDGET, with the full method recorded
  so the numbers are reproducible. Estimates are not acceptable — an
  unmeasured performance claim is a GAP-nnn.
- Verify no unbounded allocation or unbounded work driven by input, since
  a malformed signal on a 256 MB machine is a denial-of-service surface.
  Cross-reference A.3 mutation-testing results.

## Pass E — Release readiness

Receives all prior pass outputs as input.

- README claims cross-checked against Pass A findings. Any conformance
  language unsupported by A.1/A.2 must be struck. Specifically: no
  unqualified "ISO 9876:2015 compliant" claim.
- Version, changelog, and release notes present and accurate.
- AI-authorship disclosure present, plus a statement of what was and was
  not human-reviewed.
- Documented build reproduces from clean checkout on the floor target.
- CI runs the fixture regression with the FIXTURE_COMPARE policy, and
  failure blocks release.
- Security posture stated: this decodes untrusted RF input, so
  input-handling assumptions and the mutation-testing results from A.3
  belong in the README.

## Cross-verification (after all five passes)

The worst failure mode of this audit is a hallucinated citation, so give
it a detector:

- A second auditor, a different model from all five pass auditors,
  receives only the findings register and the reference documents — not
  the source tree, not the prior reasoning.
- It re-verifies every load-bearing finding plus a random 10% sample of
  the rest, checking one thing only: does the cited clause exist, and does
  the paraphrase match what the document actually says?
- Any citation that fails verification invalidates the finding, which
  moves to the gap register for human resolution. A citation failure rate
  above zero in the sample triggers full re-verification of that pass's
  findings.
- Record the cross-verifier's model, version, and date alongside the pass
  auditor identities.

## Output schema (every pass)

Pass report header:

```
PASS:          A
AUDITOR:       <model, version, date, config>
INPUTS:        <source tree revision, document editions, fixture set revision, prior pass outputs received>
```

Each finding:

```
ID:            A-CLAIM-014
PASS:          A
SEVERITY:      critical | major | minor | informational   (per Global Rules definitions)
LOAD-BEARING:  yes | no
SOURCE:        Tier 1 — WMO386:III-5 §<n>   |   file:line   |   both
IMPLEMENTED:   src/demod/sync.c:212–248
VERDICT:       CONFORMS | DEVIATES | NOT-IMPLEMENTED | NOT-APPLICABLE
EVIDENCE:      <paraphrase of the clause; no verbatim standard text>
NOTES:         <what would change the verdict>
```

Each gap:

```
ID:            B-GAP-007
PASS:          B
QUESTION:      <what could not be determined>
BLOCKED BY:    <missing evidence, tool, fixture, or document access>
RESOLVES IF:   <specific artifact that would close it>
```

Pass summary must state: findings by severity, count of load-bearing
findings, count of gaps, and an explicit statement of what the pass did
not cover.

## Human sign-off gate

The audit does not authorise release. Before publication, a human must
review and sign off on, at minimum:

0. **Every Gate 0 value** — confirmed against the physical documents and
   real hardware before the passes ran. In particular the
   WMO_386_EDITION pin, which the auditors were required to treat as
   unquestionable fact.
1. Every load-bearing finding.
2. The entire gap register.
3. All of Pass B, in full, regardless of variant.
4. Every README or release-note conformance claim.
5. The cross-verification report, including any invalidated findings.

An AI audit of AI-authored code is a filter, not an assurance. Anything
published under your name and callsign carries your judgement, not the
auditor's.

## Changelog v1 → v2

- Resolved contradiction: Pass B is now explicitly exempt from the auditor
  isolation constraint (it must inspect authoring transcripts and git
  history for provenance), with the knowledge confined to Pass B.
- Resolved contradiction: "fresh session, no prior context" redefined —
  passes receive prior passes' structured outputs as inputs; independence
  now means no access to prior reasoning, not no access to prior
  conclusions. Cross-pass links (PERF-nnn ↔ MAINT-nnn, Pass E ↔ Pass A)
  are now possible.
- Added B-COPYLEFT step 4: explicit GPLv2-only / v2+ / v3+ classification
  per derivation site with a compatibility matrix — the version pin is
  where GPL derivations actually break against GPL-3.0-or-later.
- Added FIXTURE_COMPARE to Gate 0 and rewrote A.3/D accordingly: strict
  hash only on the canonical build; tolerance-based comparison on
  x87/floor builds, eliminating the spurious-failure collision between
  fixture CI and floating-point divergence.
- Added MAX_FUNCTION_LINES / MAX_NESTING_DEPTH to Gate 0 — Pass C
  thresholds are now measurable, same standard the protocol already
  applied to performance.
- Added severity definitions table — five independent sessions now produce
  comparable severity distributions.
- Added BENCH_METHOD to Gate 0 — emulator, CPU model, compiler/flags,
  repetitions, aggregation pinned so PERF figures are reproducible.
- Added mutation testing to A.3 — corrupted-fixture variants;
  crash/hang/memory blowup = critical finding. This is where decoders of
  untrusted RF input historically fail.
- Added Cross-verification section — a second, different model re-verifies
  all load-bearing findings + 10% sample, checking citations only; auditor
  identity now recorded per pass for reproducibility.
- Added fixture redistribution rights to B.0 — transmitted chart products
  are not uniformly public domain; fixtures need a redistribution basis to
  ship in the repo.
- Gate 0 pre-verification added to the human sign-off gate (item 0) —
  including the WMO Part III-5 pin, which auditors cannot challenge.
- Finding IDs namespaced per pass (A-CLAIM-014) to prevent collisions
  across independent sessions.
