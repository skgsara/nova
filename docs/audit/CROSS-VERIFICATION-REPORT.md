# CROSS-VERIFICATION REPORT — Nova audit

Run per `docs/08-cross-verification-handover.md`. One question per finding:

> Does the cited clause exist, and does the paraphrase match what the
> document actually says?

## Verifier identity

| Role | Model |
|---|---|
| Author of the code | Claude Opus 5 |
| Passes A, B, C, D, E | Claude Sonnet 5 |
| **Cross-verifier** | **Kimi (Moonshot AI), via Kimi Code CLI; exact model version not disclosed to the agent** |

Date: 2026-08-19 (CDT). Configuration: interactive CLI agent, working
from the handover packet and local `pdftotext -layout` extractions of
WMO-No. 386 (2023 edition) and ISO 9876:2015. A different vendor from
both prior roles, per §1's stronger option.

## Isolation disclosure (self-reported, mirroring Pass E's precedent)

The packet's §2 restricts the verifier to the packet plus the two
reference PDFs. Before being assigned this task, this verifier read the
repository's `AGENTS.md`, `START-HERE.md`, and the top entry of
`SESSION-LOG.md` — the repository's own mandatory session-startup
procedure. These contain finding outcomes and remediation descriptions
but no verbatim standards text. During sampling, finding IDs and the
citation-identifying fields of sampled findings (`ID:`, `SOURCE:`, and
for two D-GAP entries the gap-register block headers) were read from
the pass reports; no reasoning sections were read. Every verdict below
rests on direct extraction of the reference PDFs, quoted here only as
paraphrase.

## §5a — load-bearing findings citing a standards clause (10 of 10 checked)

| ID | Cited clause | Verdict | Basis (paraphrase, no verbatim standard text) |
|---|---|---|---|
| A-CLAIM-001 | WMO III-5 §5.1.2 | **VERIFIED** | Clause exists; defines IOC by formula and names 576 or 288 as the standard index. |
| A-CLAIM-003 | WMO III-5 §5.1.5 | **VERIFIED** | Clause exists; lists four scanning line frequencies: 60, 90, 120, 240 per minute. |
| A-CLAIM-004 | WMO III-5 §5.2.2 | **VERIFIED** | Clause exists (§5.2.2.1); IOC selected by alternating black/white signals of 5–10 s, 300 Hz for IOC 576, 675 Hz for IOC 288. |
| A-CLAIM-005 | WMO III-5 §5.2.3.1, §5.2.3.2, §5.2.3.4 | **VERIFIED** | All three exist: 30-second phasing transmission with rate-specific alternation frequencies; waveform either symmetrical half/half or asymmetrical 5% white / 95% black; phasing actuated by the leading edge of white, in phase with entry into the dead sector. |
| A-CLAIM-006 | WMO III-5 §5.2.5 | **VERIFIED** | Clause exists (§5.2.5.1); stop signal is five seconds of 450 Hz alternating black/white. Note: the clause adds a following 10 s of continuous black, which the one-line claim omits but does not contradict. |
| A-CLAIM-007 | WMO III-5 §5.3.1.2, §5.5.1, §5.5.2 | **VERIFIED** | All three exist. §5.5.1 covers frequency modulation of the sub-carrier, centred on 1900 Hz with black 1500 / white 2300. §5.5.2 covers direct FSK of the RF carrier about the assigned frequency f₀ — ±400 Hz on HF, ±150 Hz on LF — and makes no reference to 1900 Hz. §5.3.1.2 separately gives the FM sub-carrier characteristics (mean 1900 Hz). The correction the finding asserts is exactly the distinction the document draws. |
| A-CLAIM-009 | WMO III-5 §5.1.1, §5.1.5 | **VERIFIED** | Both exist. Scanning direction left to right, top to bottom. Rate tolerance: within ±5 × 10⁻⁶ of nominal (the 2023 PDF's layout renders the exponent awkwardly in text extraction; the value reads as ±5·10⁻⁶). |
| A-CLAIM-011 | ISO 9876:2015 §4.2.6 | **VERIFIED** | Clause exists; synchronization by stated frequency accuracy (±2 × 10⁻⁶) and stability (±2 × 10⁻⁵); phasing automatic with a facility for manual adjustment. |
| A-CLAIM-011a | ISO 9876:2015 §4.2.5 | **VERIFIED** | Clause exists; requires automatic response to 300 Hz and 675 Hz control signals for start and IOC selection by detection of line synchronization, and to the 450 Hz stop signal. The clause does prescribe a detection method, as the finding states; whether Nova's spectral-purity method is an acceptable declared deviation is outside this verifier's question. |
| A-CLAIM-003 (2nd leg) | ISO 9876:2015 §4.2.4 | **VERIFIED** | Clause exists; the recording unit must select 60, 90 and 120 scans per minute, automatic and manual. 240 is not required of a receiver. |

**A-CLAIM-007, the register's highest priority, verifies cleanly** in
both directions: §5.5.1 is the sub-carrier FM clause about 1900 Hz, and
§5.5.2 is the RF-carrier FSK clause about f₀. The extraction also shows
why the original miscite was easy to make — both clauses carry the same
±400 Hz numbers for HF, in different domains.

## §5b — load-bearing findings with no standards clause (24 of 24 recorded)

Per §4 of the packet, these are recorded without further check:

A-CLAIM-010, A-CLAIM-012, A-CLAIM-013, B-CLAIM-006, B-RISK-008,
B-RISK-013, D-PERF-001, D-PERF-002, D-PERF-003, E-CLAIM-001 through
E-CLAIM-011, E-GAP-001, E-GAP-002, E-RISK-001, E-RISK-002 —
all **UNVERIFIABLE (no clause cited)**. This is the expected outcome,
not a failure.

## Random 10% sample of the remaining findings

Population: 99 finding IDs carry an `ID:` field across the five pass
reports; 33 are in the load-bearing register; 66 remained. Drawn with
Python `random.Random`, **seed 920616308**, 7 of 66 (10% rounded up).
Drawn IDs: **B-RISK-003, B-RISK-004, B-RISK-005, B-RISK-010,
C-MAINT-002, D-GAP-003, D-GAP-005.**

| ID | Verdict | Basis |
|---|---|---|
| B-RISK-003 | **UNVERIFIABLE (no clause cited)** | SOURCE is a prior-art source file (KiwiSDR FAX extension). |
| B-RISK-004 | **UNVERIFIABLE (no clause cited)** | SOURCE is acfax source lines. |
| B-RISK-005 | **UNVERIFIABLE (no clause cited)** | SOURCE is a prior-art source file. |
| B-RISK-010 | **UNVERIFIABLE (no clause cited)** | SOURCE is the system-installed FLTK licence text. |
| C-MAINT-002 | **UNVERIFIABLE (no clause cited)** | SOURCE is a project source line. |
| D-GAP-003 | **UNVERIFIABLE (no clause cited)** | Gap entry: x86-64 runtime behaviour unverified on an arm64-only machine. No standards clause involved. |
| D-GAP-005 | **UNVERIFIABLE (no clause cited)** | Gap entry: cross-reference to a pass that had not yet run when written. Dated-artifact staleness of the kind §6.1 of the packet predicts; not a citation failure. |

The sample happened to draw zero clause-citing findings, so it added no
citation checks beyond the ten above. Reported as drawn, not re-drawn —
re-drawing until a clause appears would defeat the randomness.

## Counts and the trigger question

- **VERIFIED: 10** (all of §5a)
- **CITATION FAILS: 0**
- **UNVERIFIABLE (no clause cited): 31** (24 load-bearing + 7 sampled)

**The citation failure rate is zero**, in both the load-bearing set and
the random sample. The packet's trigger — any failure above zero forcing
full re-verification of a pass — is not met. No finding moves to the gap
register for citation reasons.

## Discrepancy noted for the record

The packet's prose (§4, and the session-log summary) calls the register
"all 27 load-bearing findings". The register's tables list 34 entries
(33 unique IDs): 10 in §5a and 24 in §5b. All 34 were processed as
instructed; the count in the prose does not match the tables. This does
not affect any verdict — every tabled finding was checked or recorded —
but the number 27 could not be reconstructed from the register as
written.

## What this report does not say

Per §3, nothing here judges whether a finding is a good finding or
whether the code is correct — only that every citation a load-bearing
finding leans on exists and is honestly paraphrased. Release remains
gated on the human sign-off (protocol §"Human sign-off gate"), which
this report does not replace.
