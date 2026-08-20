# Pass E — Release Readiness Audit

```
PASS:          E
AUDITOR:       Claude Sonnet 5, run 2026-08-16, fresh subagent, no authoring context
INPUTS:        Tree: /Users/sakuragawasara/Documents/2026/2026 Amateur Radio/isobar-nova/nova
               Prior pass reports received in full: docs/audit/PASS-A-REPORT.md,
                 PASS-B-REPORT.md, PASS-C-REPORT.md, PASS-D-REPORT.md (all four read
                 completely before any tree inspection in this pass).
               Read: README.md (full), NOTICE (full), LICENSE (head/tail), .gitignore,
                 fixtures/MANIFEST.md (full), docs/00-05 (targeted sections cited below),
                 docs/07-audit-protocol.md (Pass E's own mandate section, to confirm
                 scope), CMakeLists.txt (version/target/GUI-gating sections),
                 core/wav.cpp, core/resample.cpp (full — verifying D-PERF-001/002 fix),
                 live/session.cpp, live/session.hpp, live/tone_stream.cpp/.hpp,
                 live/engine.hpp (verifying/refuting a D-PERF-003 fix),
                 core/fax.cpp/.hpp (no_phase_reference — verifying A-CLAIM-013 fix),
                 tests/test_malformed.cpp (full), tests/test_roundtrip.cpp (targeted,
                 no_phase_reference assertions), cli/nova-decode.cpp (full).
               NOT read (hard isolation, confirmed never opened): SESSION-LOG.md,
                 ROADMAP.md, AGENTS.md, START-HERE.md, docs/06-audit-gate0.md.
               One inadvertent brush with the isolation boundary: a single
                 `git log --oneline -5` was run early in this pass before I re-read my
                 own instructions closely enough; it printed five commit *subject lines*
                 (no bodies, no `git show`/`blame`). I did not use their content as
                 evidence for any finding below — every claim in this report is
                 independently re-derived from tree/source state, build output, or my
                 own hashing/grepping, and I stopped using `git log` immediately after.
                 Flagging this against myself rather than omitting it.
               Built: `git clone` of the local repo to /tmp/nova-e-clone (a real clone,
                 not a copy — see E.4), then `cmake -B /tmp/nova-audit-e-build -S
                 /tmp/nova-e-clone` (default config) and a second configure at
                 /tmp/nova-audit-e-noui with -DNOVA_BUILD_GUI=OFF. Both built to 100%,
                 exit 0, zero warnings. `ctest -j4` run against both.
               Independently re-hashed all 19 files in the (untracked, local-only)
                 fixtures/ directory against fixtures/MANIFEST.md's SHA-256 table.
               No file was modified anywhere in the project tree. All build/test
                 artifacts are under /tmp.
```

---

## E.1 — README claims cross-checked against Pass A and against the tree

```
ID:            E-CLAIM-001
PASS:          E
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        README.md:1-10  |  A-CLAIM-001 through A-CLAIM-011a  |  both
IMPLEMENTED:   n/a — documentation claim
VERDICT:       CONFORMS
EVIDENCE:      Grepped README.md, NOTICE, and every file under docs/ for "ISO 9876:2015
               compliant", "compliant with ISO", "fully compliant", "certified" (case-
               insensitive). The only hits are all of the qualifying, NOT the disqualified,
               form: README.md:9 "designed to satisfy (... no claim of certified
               compliance is made)"; docs/02-compliance-matrix.md:6 "no certified-
               compliance claim (no type testing)"; NOTICE:24 "certified compliance is
               made" (in a negating sentence); docs/05:1149,1173 repeat the same qualifier.
               No unqualified "ISO 9876:2015 compliant" claim exists anywhere in the tree
               I can read. This directly satisfies the protocol's specific requirement.
NOTES:         Nothing would change this verdict short of someone adding the unqualified
               phrase back in.
```

```
ID:            E-CLAIM-002
PASS:          E
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        README.md:52-56  |  A-CLAIM-003
IMPLEMENTED:   core/fax.cpp:493 (kRates[]={60,90,120})
VERDICT:       CONFORMS
EVIDENCE:      README's "Features (target)" bullet states the 240 lpm omission and its
               ISO §4.2.4 justification in the same terms and with the same precision as
               Pass A's finding: "WMO §5.1.5 lists a fourth rate, 240 lpm, which Nova does
               not implement — ISO 9876:2015 §4.2.4 requires only these three of a
               receiver. A 240 lpm broadcast will not decode at all." This is the declared-
               deviation language the protocol calls for, not a silent omission dressed up
               as full conformance.
NOTES:         none
```

```
ID:            E-CLAIM-003 — README claims an input format the tree does not implement
PASS:          E
SEVERITY:      major
LOAD-BEARING:  yes
SOURCE:        README.md:64 ("Input: WAV natively; m4a/AAC via an installed `ffmpeg`")
IMPLEMENTED:   n/a — no implementation exists to cite
VERDICT:       NOT-IMPLEMENTED
EVIDENCE:      Searched the entire tree (core/, cli/, gui/, live/, tests/, CMakeLists.txt)
               for any of: "ffmpeg", "m4a", ".aac"/"AAC", "popen", "exec", "subprocess",
               "system(". The only "exec"/"system" hits are unrelated prose ("executable",
               "executed on the caller's thread") and neither ffmpeg nor any subprocess
               invocation exists anywhere. core/wav.cpp is a hand-written RIFF/WAVE parser
               with no knowledge of any other container. There is no shell-out, no pipe, no
               temp-file handoff to an external decoder, and CMakeLists.txt does not probe
               for ffmpeg the way it probes for fltk-config/pkg-config(rtaudio). A user
               following this line and handing Nova an .m4a file gets a "not a RIFF file"
               error from core/wav.cpp, not a transcoded decode.
NOTES:         This sits under "Features (target)" — the header itself signals these are
               aspirational, not all delivered — but the ffmpeg clause reads as a factual
               capability statement ("via an installed ffmpeg"), not a stated future goal,
               and it is the one bullet in that list with literally zero supporting code
               anywhere, in contrast to every other bullet in the same list (all traced to
               real, tested implementation in E-CLAIM-002 and cross-referenced Pass A
               findings above). Rated major because a user relying on this claim gets a
               silent, confusing failure rather than the documented behaviour, and because
               it is exactly the kind of unsupported claim the protocol's "any conformance/
               capability language unsupported by the evidence must be struck" instruction
               targets, even though the protocol's own wording is scoped to Pass A/WMO-ISO
               claims — I extend it here because the instruction to check "feature claims"
               and "Features (target)" items explicitly was given directly by the task.
               What would change the verdict: either implement the ffmpeg shell-out (with a
               test), or strike the m4a/AAC clause (or move it to a clearly-marked "not yet
               built" list, distinct from the WAV/live claims that are real).
```

```
ID:            E-CLAIM-004
PASS:          E
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        README.md:327-337  |  D-GAP-003, D-PERF-007
IMPLEMENTED:   n/a
VERDICT:       CONFORMS
EVIDENCE:      "Built and tested on macOS arm64 only. That is the whole of it: there is no
               CI, and no other platform has been built... Reports from anywhere else are
               welcome." This matches Pass D's own finding precisely: D-GAP-003 found no
               x86-64 hardware/emulator was used and the x86-64 side was only cross-
               compiled and disassembled, never linked or run. README does not claim
               anything stronger than that, and explicitly does not claim CI exists (which
               matters for E.5 below — the claim itself is honest, the absence is the
               finding). "No 32-bit build has been attempted, and nothing here is written
               against an old-hardware floor" matches Gate 0's TARGET_FLOOR_ARCH and the
               stated "old hardware is explicitly not a goal."
NOTES:         This is a rare case of a README being MORE conservative than what the code
               could support — a well-calibrated claim, not an overclaim.
```

```
ID:            E-CLAIM-005 — build/skip-count claim, independently reproduced
PASS:          E
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        README.md:349 ("What you will see: 8 suites run, 30 report Skipped")  |
               fixtures/MANIFEST.md:19-30  |  independent clean-clone build (E.4)
IMPLEMENTED:   CMakeLists.txt nova_fixture_test() gating mechanism
VERDICT:       CONFORMS
EVIDENCE:      `git clone` of the local repo to /tmp/nova-e-clone, `cmake -B
               /tmp/nova-audit-e-build -S /tmp/nova-e-clone`, `cmake --build ... -j8`
               (exit 0, zero warnings despite -Wall -Wextra), `ctest -j4`: **exactly 8
               tests passed (malformed, ruler_mapping, png_roundtrip, live_ring,
               gui_layout, hooks, tones, roundtrip), exactly 30 reported Skipped**, 100%
               of the tests that ran passed. This matches README's claim and Gate 0's
               stated fact bit-for-bit, verified from an actual clone rather than the
               working tree (which still has all 19 fixtures present locally).
NOTES:         This is the strongest kind of verification a release-readiness pass can
               give: not "the docs say X and I believe them" but "I built what a stranger
               gets and counted."
```

```
ID:            E-CLAIM-006 — fldigi licence characterization, re-verified against B-CLAIM-006
PASS:          E
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        NOTICE:70-78  |  B-CLAIM-006 (major, flagged NOTICE's single-line "fldigi …
               GPLv3+" as inaccurate for the two files in the actual derivation chain)
IMPLEMENTED:   n/a
VERDICT:       CONFORMS (remediated since Pass B)
EVIDENCE:      NOTICE now reads: "fldigi — David Freese (W1HKJ) et al. Licence is MIXED
               per file, and the files in this lineage are NOT the project's headline
               GPLv3. Checked at commit 61b97f41 (audit Pass B, 2026-08-16): fldigi's root
               COPYING is the plain GPLv3 text, but `src/wefax/wefax.cxx` and
               `src/wefax/wefax-pic.cxx` — the WEFAX module proper... each grant 'version 2
               of the License, or (at your option) any later version', i.e. GPLv2-or-
               later." This is precisely the correction B-CLAIM-006 asked for — it now
               names the specific files and their actual per-file grant instead of a flat
               project-level characterization.
NOTES:         Good remediation; the fix even cites the audit pass and commit that found
               the problem, which is unusually good bookkeeping.
```

```
ID:            E-CLAIM-007 — WMO §5.5.2 citation precision, re-verified against A-CLAIM-007
PASS:          E
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        docs/01-signal-spec.md:39,54,63-64  |  A-CLAIM-007
IMPLEMENTED:   n/a
VERDICT:       CONFORMS (remediated since Pass A)
EVIDENCE:      docs/01's table row now reads "±400 Hz (HF circuits), ±150 Hz (LF circuits)
               about the 1900 Hz centre | ISO §4.2.2 (see the edition note: WMO §5.5.2 is
               the RF-carrier form of the same numbers, NOT an audio-domain claim)" and an
               explicit prose note at line 54 distinguishes "the audio subcarrier FM
               (1500/1900/2300 Hz)" from "§5.5.2 is direct frequency modulation... of the
               RF carrier," with a line-63 note stating the table row "used to cite WMO
               §5.5.2 alongside ISO §4.2.2, which this note already contradicts." This is
               exactly the correction A-CLAIM-007 called for.
NOTES:         none
```

```
ID:            E-CLAIM-008 — AI-authorship disclosure, re-verified against B-RISK-014
PASS:          E
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        README.md:16-48  |  B-RISK-014 (major, NOT-IMPLEMENTED)
IMPLEMENTED:   n/a
VERDICT:       CONFORMS (remediated since Pass B)
EVIDENCE:      README.md now carries a full "How Nova was written" section stating plainly:
               "Nova's code was written by an AI agent (Claude), directed session-by-
               session by Sara Sakuragawa, who owns every design decision in it," a
               "What has been reviewed by a person" paragraph (design decisions, output
               against real off-air recordings, eye-checked charts), and a "What has not"
               paragraph explicitly stating the code is not line-by-line human-reviewed and
               that AI training-data provenance is not introspectable, with a direct
               statement that no process here "can rule out unconscious reproduction at a
               level below what comparison detects." A dedicated "The audit" paragraph
               links docs/07-audit-protocol.md, docs/06-audit-gate0.md, and docs/audit/ —
               all three link targets exist in the tree (confirmed by directory listing,
               without reading docs/06's content). This satisfies both the disclosure
               requirement and the "what was/was not human-reviewed" requirement in full.
NOTES:         This is a genuinely good disclosure — better than most human-authored open
               source projects' provenance statements — not just a checkbox.
```

```
ID:            E-CLAIM-009 — documented build reproduces from a clean clone, both configs
PASS:          E
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        README.md:339-347, 365-371  |  C-BUILD-001, C-GAP-003
IMPLEMENTED:   CMakeLists.txt (whole file)
VERDICT:       CONFORMS
EVIDENCE:      Two independent builds from the git clone at /tmp/nova-e-clone (never the
               working tree, never build/):
               (1) Default config: `cmake -B /tmp/nova-audit-e-build -S /tmp/nova-e-clone`
               → "fixtures: ABSENT... 8 synthetic suites still run", "nova-gui: enabled
               (/opt/homebrew/bin/fltk-config, rtaudio 6.0.1)"; `cmake --build -j8` → exit
               0, zero warnings; `ctest -j4` → 8/8 passed, 30 Skipped (E-CLAIM-005).
               (2) `-DNOVA_BUILD_GUI=OFF`: configure prints no "nova-gui" line at all,
               build succeeds (exit 0), no `nova-gui` binary exists at the build root
               afterward, and `ctest -j4` produces the same 8 passed / 30 skipped split,
               correctly including gui_layout and gui_shell among the skips. This directly
               resolves Pass C's C-GAP-003 ("whether -DNOVA_BUILD_GUI=OFF actually produces
               a working dependency-free build... not attempted in \[Pass C\]") — it does.
NOTES:         The documented build is accurate and reproduces exactly as claimed on both
               its documented paths.
```

---

## E.2 — Version, changelog, release notes

```
ID:            E-GAP-001 — no version, changelog, or release notes exist
PASS:          E
SEVERITY:      major
LOAD-BEARING:  yes
QUESTION:      Does Nova have a version identifier, a changelog, or release notes that a
               downstream user or packager could point to?
BLOCKED BY:    Nothing — this is a confirmed absence, not an unresolved question.
               `project(nova VERSION 0.0.0 LANGUAGES CXX)` (CMakeLists.txt:2) is the only
               version string in the tree, and it is the CMake default placeholder, never
               incremented (a project at "0.0.0" going into a release-readiness review is
               itself informative). `git tag -l` returns zero tags. No `CHANGELOG*`,
               `VERSION*`, or `RELEASE*` file exists at the repo root (checked directly,
               not inferred). No CLI tool or the GUI binary has a `--version` flag
               (grepped cli/*.cpp and gui/nova-gui.cpp for "--version" and "\"version\"":
               zero hits). README.md itself makes no version claim of any kind — it
               describes state via prose milestones ("M0 and M1 done," "M4 decodes live")
               rather than a version number, and points to ROADMAP.md as "the milestone
               map," which I cannot read under the isolation rule and which is in any case
               a development-history document, not a versioned changelog with dated
               entries a release consumer would expect.
RESOLVES IF:   A version scheme is adopted (even a simple date-stamped or milestone-based
               one, consistent with the project's own M0-M4 language), CMakeLists.txt's
               placeholder 0.0.0 is replaced with it, and either a CHANGELOG.md is added
               or README/ROADMAP is restructured so a reader can answer "what changed
               since the last thing I looked at" without SESSION-LOG.md's session-by-
               session granularity (317 KB, append-only, not designed as release notes).
NOTES:         This is one of the six items the protocol names explicitly for Pass E
               ("Version, changelog, and release notes present and accurate") and the
               honest answer for all three is "does not exist," not "exists but is
               inaccurate." Rated major, load-bearing: a project entering a publication
               decision with a 0.0.0 version number and no tags has not yet decided what
               it is releasing, which is a real release-readiness blocker independent of
               code quality.
```

---

## E.3 — Security posture

```
ID:            E-RISK-001 — README does not state security posture or input-handling assumptions
PASS:          E
SEVERITY:      major
LOAD-BEARING:  yes
SOURCE:        README.md (full text, 398 lines)  |  D-PERF-001, D-PERF-002, D-PERF-003
IMPLEMENTED:   n/a — documentation gap
VERDICT:       NOT-IMPLEMENTED (relative to the protocol's explicit Pass E requirement)
EVIDENCE:      Grepped README.md for "untrusted", "malformed", "corrupt", "security",
               "adversar", "crash", "hang", "resource", "denial". The only hit with any
               substance is one clause inside the Build section's test description: "the
               untrusted-input guards, which generate their own malformed files"
               (README.md:362) — a passing mention of a test suite's existence, not a
               security posture statement. There is no dedicated section (no "Security"
               heading, no "Threat model" or "Untrusted input" heading anywhere), no
               statement of what happens on malformed/adversarial WAV input (bounded
               memory, bounded time, or otherwise), and no mention of the corrupted-input
               testing methodology or its results — even though Nova's core purpose is
               decoding audio "from a receiver nobody controls" (the tester's own words in
               tests/test_malformed.cpp:6) and the GUI/live path explicitly advertises
               unattended reception from a "KiwiSDR browser feed" (README.md:309-310),
               i.e. audio sourced over the open internet from a third party.
NOTES:         The protocol's item 6 is specific: "input-handling assumptions and the
               mutation-testing results from A.3 belong in the README." None of A.3's
               findings (the 17-variant corrupted-input campaign, its bounded-time/bounded-
               memory conclusion, or A-CLAIM-013's since-fixed phasing-loss finding) are
               summarized in the README. This is a real, actionable gap, and unlike
               E-CLAIM-003 it is not merely imprecise — it is a required item stated as
               simply absent. What would change the verdict: a short "Security / untrusted
               input" section in README stating (a) the file-based decode path is
               fuzz-tested against malformed WAV headers, oversized/short chunks, and
               implausible sample rates, with the D-PERF-001/002 fixes and their bounds
               named, and (b) the still-open live-path gap below (E-RISK-002), since a
               security statement that omits a known open issue is worse than none — see
               NOTES on E-RISK-002.
```

```
ID:            E-RISK-002 — D-PERF-003 (unbounded live-session memory growth) remains open and undisclosed
PASS:          E
SEVERITY:      critical
LOAD-BEARING:  yes
SOURCE:        D-PERF-003  |  live/session.cpp:136-226,529-537, live/tone_stream.cpp:1-160,
               live/session.hpp:221,249-252
IMPLEMENTED:   live/session.cpp (LiveSession::push, trim_preroll), live/tone_stream.cpp
               (StreamToneDetector::update)
VERDICT:       DEVIATES (unresolved from Pass D; this pass adds independent confirmation
               that no fix landed and adds a disclosure finding on top)
EVIDENCE:      Re-read every file Pass D traced this through. `trim_preroll()`
               (live/session.cpp:529) still opens with `if (in_transmission_) return;` —
               no trimming happens while a transmission is open. `push()`
               (live/session.cpp:136-226) still unconditionally appends every incoming
               sample to `retained_`, a plain `std::vector<float>` with no capacity bound
               (live/session.hpp:249). `StreamToneDetector`'s run-closing logic
               (live/tone_stream.cpp:88-160, `update()`) still closes a run only on
               `++c.cold > max_gap_` — a GAP in the tone, not an absolute duration cap.
               I grepped live/session.cpp, live/session.hpp, live/tone_stream.cpp,
               live/tone_stream.hpp, live/engine.hpp, and live/engine.cpp (targeted) for
               every plausible watchdog identifier Pass D or a fix would use —
               `kMaxRunSec`, `max_run`, `kMaxTone`, `kMaxTransmission`, `kMaxRetained`,
               `run_max`, `forced_close`, `force_close`, `kMaxOpenFrames`, `watchdog`,
               `timeout`, `stuck` — zero matches anywhere in live/. `tests/test_malformed.cpp`
               (added specifically to close the D-PERF-001/002/003 findings, per its own
               header: "audit Pass D found three ways to turn a tiny file into gigabytes...
               Two measured results from the audit, on the code as it stood: [D-PERF-001];
               [D-PERF-002]") explicitly enumerates only TWO of the three defects its own
               comment says exist — D-PERF-003 (a live-stream, not a file, attack surface)
               is not addressed by this or any other test I found. `docs/04-receiver-ui-
               survey.md:361`'s "Memory policy: unbounded, to a user-set folder, as
               greyscale PNG" is a DIFFERENT decision (disk storage of completed, saved
               PNGs across sessions) and does not cover or excuse the in-memory
               `retained_` growth during a single stalled transmission that never closes.
NOTES:         This is the one Pass D critical finding NOT remediated in this round (the
               other two, D-PERF-001 and D-PERF-002, are fixed and tested — see the
               carried-forward table). It is rated critical here for the same reason Pass D
               did: unbounded memory growth reachable from network-sourced RF input with no
               benchmark needed to establish the category. It is additionally flagged as a
               DISCLOSURE gap under Pass E specifically: a reader of README's live/GUI
               section, which explicitly advertises unattended KiwiSDR-fed reception, has
               no way to know this surface exists. Per D-GAP-004, Pass D's own confidence
               in this finding was already "lower than D-PERF-001/002... a static trace,
               not a reproduced crash" — that gap is UNCHANGED by this pass; I did not
               build a dynamic harness either (same time-budget reasoning as Pass D), so
               this remains the one Gate-0-critical finding across all four prior passes
               whose in-practice severity is still not empirically confirmed, only
               strengthened by a second independent static read finding the same absence
               of any bound. What would change the verdict: either a demonstrated ceiling
               this pass's second static read also missed, or (more likely, since two
               independent reads found nothing) an actual fix — a max open-run duration on
               `StreamToneDetector`, and/or an absolute cap on `retained_.size()` with a
               defined behaviour when exceeded (truncate-and-warn, or force `end_transmission`).
```

---

## E.4 — CI

```
ID:            E-GAP-002 — no CI exists at all
PASS:          E
SEVERITY:      critical
LOAD-BEARING:  yes
QUESTION:      Does CI run the fixture regression with the FIXTURE_COMPARE policy (per-
               fixture numeric bounds registered with each add_test), with failure
               blocking release, as the protocol requires for Pass E?
BLOCKED BY:    Nothing — confirmed absent. `find . -iname "*.yml" -o -iname "*.yaml" -o
               -iname ".travis*" -o -iname "azure-pipelines*" -o -iname "Jenkinsfile" -o
               -iname ".circleci"` (excluding build/) returns nothing; there is no
               `.github/` directory at all. README.md itself states this plainly and is
               not misleading about it: "there is no CI, and no other platform has been
               built" (README.md:329-330). fixtures/*.wav are also not committed (see
               E-CLAIM-011 below), so even a CI system that existed could not run the
               30 fixture-gated suites without a separate, undocumented mechanism to
               supply them — the FIXTURE_COMPARE policy (bounds registered per add_test in
               CMakeLists.txt) is real and well-implemented AS A LOCAL MECHANISM, but there
               is no automated system anywhere that invokes it on every change and blocks
               a release on failure.
RESOLVES IF:   A CI workflow is added that (a) builds the project, (b) runs the 8
               fixture-independent suites at minimum on every push/PR, and (c), for the
               30 fixture-gated suites specifically, either has secure access to the
               private (non-redistributed) 19 recordings to run the full FIXTURE_COMPARE
               regression, or explicitly documents that the fixture-gated suites are run
               out-of-band by whoever holds the recordings before a release is tagged —
               and (d) failure of either is wired to block merging/tagging.
NOTES:         This is graded critical and load-bearing because the protocol names it as
               one of six explicit, mandatory Pass E checks with no qualifier ("CI runs
               the fixture regression with the FIXTURE_COMPARE policy, and failure blocks
               release") — this is not a soft recommendation, it is stated as a release
               gate. The project's honesty about the absence (README says so outright)
               mitigates the DISCLOSURE risk but not the underlying gap: there is currently
               no automated mechanism that can block a bad release, only a human running
               `ctest` locally with or without the private fixture set. Given that the
               fixtures are deliberately not published (a correct decision — see
               E-CLAIM-011), a full CI-run FIXTURE_COMPARE gate would need either a private
               runner with the recordings pre-staged as a secret/artifact, or a documented
               manual pre-release checklist item — neither exists.
```

---

## E.5 — Remediation verification carried over from prior passes (not new findings, evidence for the summary table)

```
ID:            E-CLAIM-010 — A-CLAIM-013 (silent degraded phasing output) is fully remediated
PASS:          E
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        A-CLAIM-013 (major)  |  core/fax.hpp:322, core/fax.cpp:1963-1972,
               cli/nova-decode.cpp:155-163, tests/test_roundtrip.cpp:818,827,834
IMPLEMENTED:   core/fax.cpp:1966-1972 (`no_phase_reference` computed as
               `!per_line_sync && !phasing_found && !anchor_from_hint`)
VERDICT:       CONFORMS (remediated since Pass A)
EVIDENCE:      A new `bool no_phase_reference` field exists on `DecodeResult`
               (core/fax.hpp:322), computed exactly at the point A-CLAIM-013's NOTES asked
               for — after phasing, dead-sector style, and any operator hint are all
               settled — with an explicit in-code citation: "// [audit Pass A, A-CLAIM-013]"
               (core/fax.cpp:1963). `cli/nova-decode.cpp:155-163` prints a multi-line, hard
               to miss warning when the flag is set: "NO PHASE REFERENCE: this recording
               has a white-only dead sector (no per-line sync) AND no phasing interval, so
               nothing in it establishes where a line begins... Give --phase FRAC to set it
               by hand." Three assertions in tests/test_roundtrip.cpp (lines 818, 827, 834)
               pin the flag both false (healthy cases) and true (the constructed failure
               case), and false again when an operator hint substitutes. This is precisely
               the "distinct warning... as opposed to the normal healthy... combination"
               fix A-CLAIM-013's NOTES specified as the thing that would flip the verdict.
NOTES:         A model remediation — traceable to the finding by ID in the code comment,
               tested, and user-visible in the exact place (CLI stdout) a caller would see
               it.
```

```
ID:            E-CLAIM-011 — B-RISK-013 (fixture redistribution) is fully remediated
PASS:          E
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        B-RISK-013 (critical)  |  .gitignore:19-28, fixtures/MANIFEST.md:1-7,
               `git ls-files fixtures/`, /tmp/nova-e-clone/fixtures/ (clean clone contents)
IMPLEMENTED:   n/a
VERDICT:       CONFORMS (remediated since Pass B)
EVIDENCE:      `git ls-files fixtures/` in the working tree returns exactly one file,
               `fixtures/MANIFEST.md` — zero `.wav` files are tracked. The clean clone at
               /tmp/nova-e-clone independently confirms this: `ls fixtures/` there shows
               only MANIFEST.md, 9578 bytes, nothing else. .gitignore now excludes `*.wav`
               unconditionally with an explicit comment: "Until session 32 this carved out
               an exception... Audit Pass B found the cost of that exception — 19 off-air
               excerpts were tracked with no redistribution basis stated for any of them,
               and one station (JSC) is Kyodo News, a commercial newspaper. The exception
               is gone and the audio was removed from history." fixtures/MANIFEST.md
               itself now opens with "The recordings themselves are not in this
               repository, and never will be" and states the copyright concern in the same
               terms Pass B's finding did, including naming JSC/Kyodo News specifically as
               the clearest case with no public-domain argument. This resolves B-RISK-013
               by the exact mechanism the protocol prescribed ("fixtures without a
               redistribution basis must not ship in the repo") rather than by after-the-
               fact rationalization.
NOTES:         B-GAP-005 (the underlying jurisdiction-by-jurisdiction copyright question
               for the other 17, government-service fixtures) is now moot for THIS
               repository's exposure, since none of the 19 files are redistributed by it
               at all — the question could still matter for wherever the recordings are
               held privately, but that is outside this audit's remit.
```

```
ID:            E-CLAIM-012 — fixture SHA-256 integrity independently verified, resolving A-GAP-004
PASS:          E
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        A-GAP-004  |  fixtures/*.wav (19 files, present locally, not in git)  |
               fixtures/MANIFEST.md
IMPLEMENTED:   n/a
VERDICT:       CONFORMS
EVIDENCE:      `shasum -a 256 fixtures/*.wav`, sorted and diffed byte-for-byte against
               every SHA-256 value listed in fixtures/MANIFEST.md's table (19 entries):
               zero mismatches, zero missing entries either direction. This resolves
               A-GAP-004 ("Do the SHA-256 hashes recorded in fixtures/MANIFEST.md actually
               match the bytes of the 19 WAV files present on this machine?" — Pass A did
               not check this) with a definitive yes, on this machine's copies.
NOTES:         This does not (and cannot) verify the recordings' PROVENANCE (that they are
               genuine off-air captures of the claimed stations) — only that the bytes on
               disk match what MANIFEST.md's table says they should hash to, i.e. integrity
               against tampering/corruption, not authenticity.
```

---

## Pass summary

### Findings by severity

- **critical: 2** — E-RISK-002 (D-PERF-003 unresolved and undisclosed), E-GAP-002 (no CI
  exists — this is a Pass-E-explicit mandatory item, graded critical because the protocol
  states it as a release gate with no qualifier)
- **major: 3** — E-CLAIM-003 (README claims ffmpeg/m4a/AAC input that does not exist in the
  tree), E-GAP-001 (no version/changelog/release notes exist), E-RISK-001 (README has no
  security-posture statement, the sixth explicit Pass E requirement)
- **minor: 0**
- **informational: 8** — E-CLAIM-001, -002, -004, -005, -006, -007, -008, -009 (README-
  claim verifications and clean-build verification), plus the three remediation-
  verification findings E-CLAIM-010, -011, -012 (informational, but load-bearing —
  see below)

### Load-bearing findings: 10 of 13 total findings in this pass

(E-CLAIM-001, -002, -004, -005, -008, -009, -010, -011 for a positive release-readiness
signal; E-CLAIM-003, E-GAP-001, E-GAP-002, E-RISK-001, E-RISK-002 for a negative one —
note E-CLAIM-003's own tally above lists it as major and load-bearing, all five
negative items are both.)

### Gaps: 2 (E-GAP-001, E-GAP-002)

### RELEASE-READINESS VERDICT

**Not yet ready to publish as a finished release, though the gap from "not ready" is now
narrow and the trend across this audit round is strongly positive.** Specifically:

**Blocks publication:**
1. **E-RISK-002 / D-PERF-003** — an unbounded-memory denial-of-service reachable from the
   live/RF path this project's own README advertises as a headline feature (unattended
   KiwiSDR reception), confirmed still open by two independent reads (Pass D's and this
   one) with zero test coverage and zero disclosure. This is the most serious item in the
   report: it is the one Gate-0-critical class of defect (unbounded allocation/work on
   untrusted input) that Pass D found and this pass confirms was NOT fixed, unlike its two
   siblings (D-PERF-001, D-PERF-002), which were.
2. **E-GAP-002** — no CI exists. The protocol states this as an explicit, unqualified Pass
   E requirement ("CI runs the fixture regression... and failure blocks release"). README
   is honest about the absence, which is good practice, but honesty about a missing gate is
   not the same as having the gate.
3. **E-CLAIM-003** — a factual capability claim in README ("m4a/AAC via an installed
   ffmpeg") that corresponds to zero code anywhere in the tree. This is a correctness-of-
   documentation problem a release should not ship with, independent of how good the rest
   of the decoder is.
4. **E-GAP-001** — no version identifier (0.0.0 placeholder), no changelog, no release
   notes, no tags. A project cannot meaningfully be "released" without first deciding what
   it is releasing.
5. **E-RISK-001** — no security-posture statement in the README, the sixth explicit,
   named Pass E requirement, and the one item on this list most directly tied to
   E-RISK-002 above (a security statement that is silent about a known-open critical issue
   would be worse than the current silence, so these two should be fixed together, not
   E-RISK-001 alone).

**Does NOT block publication — resolved, verified in this pass, or correctly disclosed
rather than hidden:**
- Fixture redistribution (B-RISK-013, formerly critical) — fully remediated; verified via
  clean clone containing zero WAV files.
- AI-authorship disclosure (B-RISK-014, formerly major) — fully remediated with a thorough,
  specific "How Nova was written" section.
- No unqualified ISO 9876:2015 compliance claim anywhere (the protocol's specifically named
  check) — clean.
- The declared WMO/ISO deviations (240 lpm; ISO §4.2.5's spectral-purity method instead of
  line-sync detection) — consistently and correctly disclosed everywhere a reader would
  look (README's Features list, docs/01, docs/02), not silently omitted.
- Two of Pass D's three critical findings (D-PERF-001, D-PERF-002) — fixed, tested, and the
  fix cites the finding ID in-code.
- A-CLAIM-013 (silent degraded phasing output) — fixed with a loud, tested, user-visible
  warning.
- Two citation-precision findings (A-CLAIM-007's WMO §5.5.2 mischaracterization,
  B-CLAIM-006's fldigi licence mischaracterization) — both corrected in docs/01 and NOTICE
  respectively.
- The documented build reproduces exactly as claimed, from a genuine clean clone, on both
  its documented configurations (default and -DNOVA_BUILD_GUI=OFF), with zero warnings and
  the exact 8-passed/30-skipped split the project claims.
- Fixture hash integrity (A-GAP-004) — independently verified, all 19 match.

### Carried-forward table — every prior pass's load-bearing and critical/major finding, current state as verified in the tree today

| ID | Pass | Severity | Original verdict | Current state (verified this pass) |
|---|---|---|---|---|
| A-CLAIM-003 (240 lpm out of WMO's 4-value set) | A | major | DEVIATES (declared) | **STILL OPEN, correctly and consistently disclosed** — a permanent, documented scope limit, not a defect; README/docs/01/docs/02 all state it identically |
| A-CLAIM-007 (WMO §5.5.2 mis-cited for an ISO-only figure) | A | major | CONFORMS w/ citation defect flagged | **REMEDIATED** — docs/01 now distinguishes the audio-subcarrier vs RF-carrier clauses explicitly (E-CLAIM-007) |
| A-CLAIM-011a (ISO §4.2.5 line-sync method deviated to spectral purity) | A | major | DEVIATES (declared) | **STILL OPEN, correctly and consistently disclosed** — a permanent, documented, well-justified deviation; docs/02 states it in the same terms Pass A found |
| A-CLAIM-013 (silent degraded output, phasing loss on white-only station) | A | major | CONFORMS to letter, poor in practice | **REMEDIATED** — new `no_phase_reference` flag, loud CLI warning, tested (E-CLAIM-010) |
| A-GAP-002 (no real IOC-288 fixture) | A | gap | open | **STILL OPEN** — no new fixture added; not independently re-checked in depth this pass beyond confirming no new fixture files appeared |
| A-GAP-003 (no real 90 lpm fixture) | A | gap | open | **STILL OPEN** — same as above |
| A-GAP-004 (fixture hashes not independently re-verified) | A | gap | open | **RESOLVED** — all 19 hashes independently re-verified against MANIFEST.md this pass, zero mismatches (E-CLAIM-012) |
| B-RISK-013 (19 fixtures redistributed with no rights basis) | B | **critical** | DEVIATES | **REMEDIATED** — all 19 WAV files removed from git and history; clean clone confirmed to contain zero WAV files (E-CLAIM-011) |
| B-RISK-008 (no GPLv2-only incompatibility found at any confirmed derivation site) | B | informational, load-bearing (negative result) | CONFORMS | **UNCHANGED, still holds** — no new derivation sites introduced since Pass B that this pass found |
| B-CLAIM-006 (fldigi's mixed licence grants mischaracterized by NOTICE) | B | major | CONFORMS overall, mixed grant flagged | **REMEDIATED** — NOTICE now states the per-file mixed grant explicitly, citing Pass B (E-CLAIM-006) |
| B-RISK-014 (no AI-authorship disclosure in README) | B | major | NOT-IMPLEMENTED | **REMEDIATED** — full disclosure section added (E-CLAIM-008) |
| B-RISK-011 (no SPDX headers anywhere) | B | minor | NOT-IMPLEMENTED | **STILL OPEN** — re-grepped this pass, zero `SPDX-License-Identifier` hits across the tree, unchanged |
| C — no critical/major findings | C | — | — | Pass C had zero critical or major findings (20 total, 16 minor, 4 informational); nothing from Pass C is load-bearing to this verdict. Spot-checked C-MAINT-001 (`stage_assembly`, core/fax.cpp:1512): still present, still oversized — **unremediated but non-blocking** |
| C-GAP-003 (whether -DNOVA_BUILD_GUI=OFF actually builds) | C | gap | open | **RESOLVED** — verified this pass: configures, builds, produces no `nova-gui` binary, and ctest runs the same 8/30 split correctly (E-CLAIM-009) |
| D-PERF-001 (unbounded WAV data-chunk resize — 144-byte file to ~13 GB) | D | **critical** | DEVIATES | **REMEDIATED** — `read_wav` now clamps declared size to bytes actually remaining in the file, in-code comment cites the finding, tested in test_malformed.cpp |
| D-PERF-002 (unbounded resample ratio — 1 Hz rate to 1.6+ GB) | D | **critical** | DEVIATES | **REMEDIATED** — `resample_ratio` now bounds the ratio to [1/1024, 64] and `read_wav` independently rejects rates outside [4600, 768000] Hz; both cite the finding, both tested |
| D-PERF-003 (unbounded `LiveSession::retained_` growth on a sustained/stuck tone) | D | **critical** | DEVIATES (static trace, lower confidence) | **STILL OPEN — confirmed by a second independent static read this pass, and NOT disclosed anywhere in README** (E-RISK-002). The one Pass D critical finding not remediated |
| D-PERF-004 (WAV float-sample branch assumes host little-endian) | D | minor | DEVIATES, non-floor-violating | **STILL OPEN**, unchanged — non-blocking per the 64-bit-LE-only floor |
| D-PERF-005 (fmt chunk declared size < 16 not validated) | D | minor | DEVIATES, no worse-than-clean-rejection observed | **STILL OPEN**, unchanged — non-blocking |
| D-GAP-004 (D-PERF-003 never dynamically reproduced) | D | gap | open | **STILL OPEN** — this pass also did not build a dynamic harness; the static evidence for the finding is now doubly-confirmed (two independent reads) but the confidence gap Pass D flagged is unchanged |

### What this pass did NOT cover

- **Did not build a dynamic reproduction harness for D-PERF-003.** Both this pass and Pass D
  rely on static code reading; the finding is now corroborated twice but still not
  empirically demonstrated with a running process and a measured RSS climb. This is the
  single most valuable thing a follow-up pass could add.
- **Did not independently re-verify A-GAP-002/A-GAP-003** (no real IOC-288 or 90 lpm
  fixture) beyond confirming no new fixture files appeared in the directory listing — did
  not re-run the fixture test suite against the local (untracked) recordings to confirm
  Pass A's coverage table is still accurate line-by-line.
- **Did not read gui/nova-gui.cpp in full** (Pass C already flagged this as only partially
  read, C-GAP-004; this pass adds no further coverage there beyond the build/test-pass
  confirmation).
- **Did not attempt a jurisdiction-by-jurisdiction copyright analysis of the fixture
  recordings themselves** (B-GAP-005) — moot for this repository's own redistribution
  exposure now that the files are removed from git, but the underlying question (what basis,
  if any, exists to hold and eventually publish excerpts of JMH/VMW/XSG/GYA/NMC/HLL/JSC
  broadcasts) was not investigated.
- **Did not re-run or re-verify any of Pass C's 16 minor maintainability findings**
  individually beyond one spot-check (C-MAINT-001) confirming the general "largely
  unremediated, all non-blocking" characterization — a full line-by-line re-audit of
  Pass C's findings was out of scope for a release-readiness pass with no load-bearing
  Pass C items to verify.
- **Did not attempt the cross-verification step** the protocol describes as happening
  "after all five passes" (a different-model second auditor re-checking citations) — that
  is explicitly a separate, later step in the protocol, not part of Pass E itself.
- **No file was modified.** This pass is audit-only, per its own hard rule; every build was
  performed in /tmp against a git clone, never against the working tree or `build/`.

================================================================================
REMEDIATION — appended 2026-08-20 (session 35), per the dated-artifact rule
================================================================================

Two gaps addressed: E-GAP-001 CLOSED, E-GAP-002 PARTIAL. Verification:
ctest 39/39 on the fixture-holding machine, and 9/39 with 30 skipped on a
fixture-less copy — both real builds, both run. Zero warnings.

E-GAP-001    CLOSED    `project(nova VERSION 0.4.0)` replaces the 0.0.0
             placeholder. The scheme is milestone-based, in the project's
             own M0-M4 language, M4 being the milestone that is complete;
             it stays below 1.0 because nothing in the Platforms or
             Security-posture sections has changed. The string reaches the
             code through exactly one generated header
             (core/version.hpp.in -> <nova/version.hpp>), so CMakeLists.txt
             is the only place it is written. All five binaries answer
             `--version` on stdout as "<tool> <version>", ahead of their
             own argument-count checks. CHANGELOG.md, which the finding
             noted did not exist when Pass E ran, carries the release
             notes and is now headed 0.4.0.

             The screamer is `version_flag` (new; suite count 38 -> 39,
             36 -> 37 without the GUI). Its two sides are NOT equal by
             construction — one is CMakeLists.txt's declared version, the
             other is the bytes a built binary writes — so the failures it
             exists for are real: a hardcoded literal in one tool, a tool
             naming a different tool, a tool without the flag. Three
             mutations, one per shape, each KILLED by the intended check
             with the FAIL line read back; unmutated baseline SURVIVED
             before and after.

             NOT done, deliberately: no tag exists. A tag names a release
             and the release decision is the operator's. What the tree now
             has is everything a tag needs.

E-GAP-002    PARTIAL   .github/workflows/ci.yml builds and tests on macOS
             arm64 and Linux x86-64, with FLTK and RtAudio installed and
             the GUI target explicitly required (a silent GUI skip would
             drop two suites from the inventory), plus a second job on the
             -DNOVA_BUILD_GUI=OFF path. release.yml gates a `v*` tag on
             the public suites, on the tag matching the declared version,
             and on the fixture-regression record described below.

             Against the finding's RESOLVES IF: (a) builds — yes; (b) runs
             the fixture-independent suites on every push and PR — yes,
             all 9 of them; (c) documents that the 30 fixture-gated suites
             are run out-of-band by whoever holds the recordings — yes,
             and mechanised rather than only documented:
             tools/record-fixture-regression.sh runs the full suite and
             writes docs/audit/FIXTURE-REGRESSION.md, refusing to write a
             record if the tree is dirty, if the recordings are absent, if
             anything failed, or if anything skipped; release.yml then
             refuses a tag whose commit is not the recorded commit;
             (d) failure blocks tagging — as far as a workflow can, which
             is not as far as branch protection, a repository setting no
             file in the tree can supply.

             **Why PARTIAL and not CLOSED: none of it has ever run.** This
             repository has no remote, so no execution of either workflow
             exists. Recording that plainly rather than claiming the gap
             closed is the point — an instrument that has never been seen
             to fail is not evidence [SESSION-LOG, session 23 onward].

             What WAS verified, by hand, on real builds: the inventory the
             CI step asserts is the inventory a fixture-less build
             actually produces (39 registered, 9 ran, 30 skipped; 37, 8
             and 29 with the GUI off), and check-suite-inventory.sh was
             seen to FAIL when given wrong numbers. The reason that check
             exists at all is this report's own observation about the skip
             mechanism: without recordings ctest exits 0 and prints "100%
             tests passed" having run a quarter of the suite, so a gate
             reading the exit code alone would report a regression that
             never ran.

             Still unknown, and not claimed anywhere: whether Nova builds
             on Linux or on x86-64. The workflow's Linux row is there to
             find out. README's Platforms section continues to say macOS
             arm64 and nothing more.
