# 08 — Cross-verification handover

**Status: NOT STARTED. This is the last step of the audit and it has not
been run.**

The protocol's cross-verification exists because *"the worst failure mode
of this audit is a hallucinated citation."* This file is the packet to
hand a cross-verifier. Everything it needs is here; nothing it must not
see is.

---

## 1. Who may run this

The cross-verifier MUST be **a different model from all five pass
auditors**. Recorded so the constraint is checkable:

| Role | Model |
|---|---|
| Author of the code (disqualified from auditing entirely) | Claude Opus 5 |
| Passes A, B, C, D, E | Claude Sonnet 5 |
| **Cross-verifier — must be neither of the above** | **unassigned** |

Within this toolchain that leaves Fable 5 or Haiku 4.5. A model from a
different vendor would be stronger, because the failure being hunted —
a confidently wrong citation — is a failure mode the Claude family may
share with itself.

## 2. What the cross-verifier receives, and what it must NOT

**Receives:**
- this file, including the register in §5
- the reference documents: WMO-No. 386 (2023 edition), ISO 9876:2015

**Must NOT receive:**
- the source tree
- any pass report's reasoning
- `SESSION-LOG.md`, `ROADMAP.md`, `AGENTS.md`, git history

That restriction is the instrument. A verifier holding the source tree
would start re-auditing the code, which is not the job and would let a
plausible-sounding finding carry a bad citation through on the strength
of the code agreeing with it.

**Note on the documents:** they are copyrighted working references,
gitignored, and NOT in this repository. They are held locally at
`../386_2023-edition_en.pdf` and `../ISO_9876_2015(en).pdf`. `pdftotext
-layout` extracts both cleanly; a raw extraction of the WMO PDF without
poppler returns glyph indices and zero hits for "facsimile", which is an
extraction failure and must not be read as a fact about the document.

## 3. The one question to answer

For each finding below, exactly one question:

> **Does the cited clause exist, and does the paraphrase match what the
> document actually says?**

Not whether the finding is a good finding. Not whether the code is
correct. Only whether the citation is real and the paraphrase is honest.

Verdicts: **VERIFIED** / **CITATION FAILS** / **UNVERIFIABLE**.

Any citation that fails verification **invalidates the finding**, which
moves to the gap register for human resolution. **A citation failure rate
above zero in the sample triggers full re-verification of that pass's
findings.**

## 4. Scope

- **Every load-bearing finding** in §5 — all 27.
- **Plus a random 10% sample** of the remaining findings. The full set is
  in `docs/audit/PASS-{A,B,C,D,E}-REPORT.md`; the sample must be drawn at
  random, not chosen, and the drawn IDs recorded in the report.

Findings whose SOURCE is a `file:line` rather than a standards clause
cannot be citation-checked without the tree. Mark those **UNVERIFIABLE
(no clause cited)** — that is a correct outcome, not a failure, and it is
why the table below separates the two kinds.

## 5. The register

### 5a. Load-bearing findings citing a STANDARDS CLAUSE — these are the real work

| ID | Sev | Cited clause | Claim in one line |
|---|---|---|---|
| A-CLAIM-001 | info | WMO386:III-5 §5.1.2 | IOC values 576 and 288 as specified |
| A-CLAIM-003 | major | WMO386:III-5 §5.1.5 | Four scan rates specified (60/90/120/240); Nova implements three |
| A-CLAIM-004 | info | WMO386:III-5 §5.2.2 | Start tone: IOC selection by alternating black/white |
| A-CLAIM-005 | info | WMO386:III-5 §5.2.3.1, §5.2.3.2, §5.2.3.4 | Phasing: rate selection, both waveforms, white leading edge |
| A-CLAIM-006 | info | WMO386:III-5 §5.2.5 | Stop signal: 450 Hz, five seconds |
| A-CLAIM-007 | major | WMO386:III-5 §5.3.1.2, §5.5.1, §5.5.2 | §5.5.1 is subcarrier FM about 1900 Hz; §5.5.2 is RF-carrier FSK about f₀ — the docs cited §5.5.2 for an audio-domain claim |
| A-CLAIM-009 | minor | WMO386:III-5 §5.1.1, §5.1.5 | Scan direction and rate tolerance |
| A-CLAIM-011 | info | ISO 9876:2015 §4.2.6 | Sync accuracy and stability; phasing automatic with manual adjustment |
| A-CLAIM-011a | major | ISO 9876:2015 §4.2.5 | The clause's detection method vs Nova's spectral-purity method — a declared deviation |
| A-CLAIM-003 (2nd leg) | major | ISO 9876:2015 §4.2.4 | ISO requires only 60/90/120 of a receiver — the justification for the 240 lpm omission |

**Highest priority in this table: A-CLAIM-007.** It is the finding that
corrected a citation in the project's own specification document, so a
wrong verification here propagates in both directions.

### 5b. Load-bearing findings with NO standards clause — expected UNVERIFIABLE

These cite source lines, upstream licence headers, or measurements. Record
them as UNVERIFIABLE (no clause cited) and do not attempt to check them
without the tree.

| ID | Sev | Basis |
|---|---|---|
| A-CLAIM-010, A-CLAIM-012, A-CLAIM-013 | info / info / major | independent test runs |
| B-CLAIM-006 | major | fldigi file headers at commit 61b97f41 |
| B-RISK-008 | info | version-pin compatibility matrix |
| B-RISK-013 | **critical** | git tracking state of 19 recordings |
| D-PERF-001, D-PERF-002, D-PERF-003 | **critical** ×3 | source lines + measured memory |
| E-CLAIM-001…011, E-GAP-001, E-GAP-002, E-RISK-001, E-RISK-002 | mixed | README lines, prior finding IDs, build runs |

## 6. Two things the cross-verifier must be told plainly

**1. Remediation has already happened. The reports were deliberately not
edited.** They are dated artifacts. Several findings describe a tree that
no longer exists — B-RISK-013 says 19 recordings are tracked in git; they
were removed from all history on 2026-08-16. This is expected and is not a
citation failure. Judge the citation as of the report's date.

**2. One isolation slip is already on the record.** Pass E ran
`git log --oneline -5` and saw five commit subjects, and reported it
against itself. It is disclosed here so the cross-verifier does not have
to discover it, and so its own report can note whether it considers Pass
E's remediation verdicts affected.

## 7. Report format

Record model, version, date, and configuration alongside the pass auditor
identities in §1. Then, per finding: ID, verdict, and — for any CITATION
FAILS — what the document actually says, paraphrased. **No verbatim
reproduction of standard text**; these reports are publishable artifacts
and the standards are copyrighted.

Close with: counts by verdict, the randomly drawn 10% sample IDs, and an
explicit statement of whether the failure rate was zero.

## 8. After cross-verification

The audit still does not authorise release. The human sign-off gate
(protocol §"Human sign-off gate") requires Sara to review every Gate 0
value, every load-bearing finding, the entire gap register, all of Pass B,
every README conformance claim, and the cross-verification report itself.

> An AI audit of AI-authored code is a filter, not an assurance. Anything
> published under your name and callsign carries your judgement, not the
> auditor's.
