PASS:          C
AUDITOR:       Claude Sonnet 5, 2026-08-16, fresh subagent, no authoring context
INPUTS:        Source tree at /Users/sakuragawasara/Documents/2026/2026 Amateur Radio/isobar-nova/nova.
               Read: CMakeLists.txt, README.md; all of core/*.cpp, core/*.hpp; all of
               live/*.cpp, live/*.hpp; cli/nova-decode.cpp, cli/nova-preview.cpp,
               cli/nova-tones.cpp, cli/nova-gen.cpp, cli/env_hooks.hpp; gui/nova-gui.cpp
               (sampled in full-file passes, not every line re-read in this report).
               Not read: SESSION-LOG.md, ROADMAP.md, AGENTS.md, START-HERE.md,
               docs/06-audit-gate0.md, any git history (per isolation rules — confirmed
               not accessed). docs/00-05 were read only where cited below, and only as
               the subject of audit, never as evidence a claim is true.
               No prior pass output was provided to this pass.
               Tooling: a hand-written Python brace/heuristic scanner (kept in scratchpad,
               not part of the repo) was used to triage function length and nesting
               candidates; every finding below whose exact boundaries matter was then
               verified by direct Read of the cited file:line range. A full clean build
               was run per the task's instructions (see C-BUILD-001).

================================================================================
1. THRESHOLD VIOLATIONS — MAX_FUNCTION_LINES (80)
================================================================================

ID:            C-MAINT-001
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        core/fax.cpp:1512
IMPLEMENTED:   core/fax.cpp:1512-1927
VERDICT:       DEVIATES
EVIDENCE:      `stage_assembly(DecodeState&)` is 416 lines, 5.2x the 80-line limit —
               the single largest function in the codebase. Verified by direct read of
               the full range (braces open/close confirmed at 1512 and 1927). It performs
               per-line placement, intra-line break search, dropout re-lock via signal
               probing, and final pixel fill, all in one function body with no
               sub-function extraction. See C-MAINT-002 for the nesting-depth violation
               this same function also carries.
NOTES:         Splitting into the sub-phases the file's own comments already name
               (§5a per-line correction / §5b intra-line break / picture-placement
               fallback / pixel fill) would bring each piece under the limit and would
               not require behavioural changes. Verdict would flip to CONFORMS only if
               the function were decomposed.

ID:            C-MAINT-002
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        core/fax.cpp:1727
IMPLEMENTED:   core/fax.cpp:1727-1791 and core/fax.cpp:1545-1665
VERDICT:       DEVIATES
EVIDENCE:      Two independent nesting-depth-5 chains inside stage_assembly, both
               exceeding MAX_NESTING_DEPTH (4). Traced by direct line read, not the
               heuristic scanner:
               Chain A — core/fax.cpp:1727 `while (i < out_lines)` [depth 1] ->
               1732 `if (i > 0 && j + 1 < out_lines && ...)` [2] ->
               1740 `if (moved > kNonlinSec * fs)` [3] ->
               1757 `for (int m = i; m <= j; m++)` [4] ->
               1771/1776/1781 `if (sn >= lock ...) / else if (so >= lock ...) / else`
               [5].
               Chain B — core/fax.cpp:1545 `for (int l = line_lo; l < line_hi; l++)`
               [1] -> 1547 `if (opt.autolock)` [2] -> 1604 `if (measured)` [3] ->
               1606 `if (r.size() >= 4)` [4] -> 1614 `if (!slopes.empty())` [5].
               Both chains are 5 levels deep relative to the function body, one level
               over the stated Gate 0 limit.
NOTES:         This is the only nesting-depth violation found anywhere in the codebase
               (an automated scan of core/, live/, cli/, gui/ for depth >= 5 found no
               other function-relative depth-5+ chain — everywhere else the codebase
               holds to <=4). Extracting the innermost `if/else if/else` in Chain A and
               the `if (r.size() >= 4)` block in Chain B into named helper functions
               would each remove one level and bring the function into conformance.

ID:            C-MAINT-003
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        core/fax.cpp:466
IMPLEMENTED:   core/fax.cpp:260-350, 466-604, 607-781, 784-889, 896-997, 1000-1148,
               1151-1243, 1250-1374, 1377-1505
VERDICT:       DEVIATES
EVIDENCE:      Every "stage_*" function in the decode pipeline exceeds 80 lines, i.e.
               this is systemic to the file, not an isolated outlier (stage_assembly in
               C-MAINT-001 is simply the worst case). Measured directly:
                 refine_period            260-350   (91 lines)
                 stage_onset              466-604   (139 lines)
                 stage_dead_sector        607-781   (175 lines, verified by direct read)
                 stage_phasing            784-889   (106 lines)
                 stage_track              896-997   (102 lines)
                 stage_fit                1000-1148 (149 lines)
                 stage_segment            1151-1243 (93 lines)
                 stage_timebase           1250-1374 (125 lines)
                 stage_change_points      1377-1505 (129 lines)
               None of these (apart from stage_assembly, C-MAINT-002) were found to
               exceed the depth-4 nesting limit — the length violation is length alone.
NOTES:         The file's own comment at core/fax.cpp:421 ("incremental pipeline can
               drive stages rather than one monolith") shows the stage-function split
               was a deliberate decomposition already; the remaining problem is that
               each stage itself was not decomposed further. Extracting the profiling
               loop, the hint-refinement block, and the logging tail of stage_dead_sector
               (for example) into named helpers would bring it under 80 without touching
               behaviour.

ID:            C-MAINT-004
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        core/phasing.cpp:180
IMPLEMENTED:   core/phasing.cpp:180-476
VERDICT:       DEVIATES
EVIDENCE:      `detect_phasing(...)` is 297 lines (3.7x the limit), verified by direct
               read of the full range. Nesting stays within bounds (max relative depth
               ~3 by inspection: outer `while` -> inner `for` -> occasional `if`), so
               this is a pure length violation, not a nesting one.
NOTES:         The function already has visually marked sub-sections in its own
               comments (run growth, run-end trim, non-linearity measurement, absolute
               anchor) that map cleanly onto candidate helper functions.

ID:            C-MAINT-005
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        core/tones.cpp:122
IMPLEMENTED:   core/tones.cpp:122-214
VERDICT:       DEVIATES
EVIDENCE:      `detect_tones(...)` is 93 lines, 13 over the limit.
NOTES:         Smallest overage in the set; a single extraction (e.g. the per-window
               scoring loop) would bring it into conformance.

ID:            C-MAINT-006
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        core/wav.cpp:37
IMPLEMENTED:   core/wav.cpp:37-145
VERDICT:       DEVIATES
EVIDENCE:      `read_wav(...)` is 109 lines, verified by direct read. The overage is
               almost entirely the five near-identical `if (fmt==1 && bits==N)` sample-
               decode branches (lines 86-138), which are structurally repetitive rather
               than complex — see C-MAINT-011 for the duplication angle on the same
               code.
NOTES:         A `template<typename Sample> float read_one(const unsigned char*)` or a
               small per-bit-depth reader table would collapse the five branches and the
               function would drop well under 80 lines.

ID:            C-MAINT-007
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        core/gen.cpp:36
IMPLEMENTED:   core/gen.cpp:36-142
VERDICT:       DEVIATES
EVIDENCE:      `gen_fax_signal(...)` is 107 lines. Not signal-path per the audit's
               definition (it is the test-signal generator, not part of decode), so no
               bare-literal citation requirement applies here, but the length limit
               still applies project-wide per Gate 0's stated parameters.
NOTES:         n/a.

ID:            C-MAINT-008
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        live/engine.cpp:661
IMPLEMENTED:   live/engine.cpp:661-846
VERDICT:       DEVIATES
EVIDENCE:      `LiveEngine::collect_batch()` is 186 lines, verified by direct read of
               the full range. Nesting is shallow (mostly single-level `{}` scoping
               blocks for lock guards, max relative depth ~3), so this is a length
               violation, not a nesting one. The function body is dominated by four
               nearly-sequential concerns: read the batch result, decide whether to
               park it, save the image, and hand it to the pane — each already delimited
               by the function's own paragraph comments.
NOTES:         n/a.

ID:            C-MAINT-009
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        live/preview.cpp:64
IMPLEMENTED:   live/preview.cpp:64-166, live/preview.cpp:181-268
VERDICT:       DEVIATES
EVIDENCE:      `StreamPreview::try_acquire(int)` is 103 lines (64-166); 
               `StreamPreview::draw_row(...)` is 88 lines (181-268). Both boundary lines
               verified directly.
NOTES:         n/a.

ID:            C-MAINT-010
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        live/session.cpp:136
IMPLEMENTED:   live/session.cpp:136-226, live/session.cpp:406-493
VERDICT:       DEVIATES
EVIDENCE:      `LiveSession::push(...)` is 91 lines (136-226); `LiveSession::watch_step
               (...)` is 88 lines (406-493). Both boundary lines verified directly.
NOTES:         n/a.

ID:            C-MAINT-011
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        cli/nova-decode.cpp:45
IMPLEMENTED:   cli/nova-decode.cpp:45-222 (178 lines), cli/nova-preview.cpp:37-150
               (114 lines), cli/nova-tones.cpp:20-119 (100 lines)
VERDICT:       DEVIATES
EVIDENCE:      All three CLI tools' `main()` exceed 80 lines. `nova-decode.cpp`'s main
               was read in full: argument parsing (lines 52-89) and result-formatting
               printf logic (99-216, including a `switch` over `Timebase` with nested
               `if`s reaching relative depth 4 — at the limit, not over it) are both
               inlined into main rather than factored into a formatter function.
NOTES:         These are CLI presentation functions, not signal-path logic; behavioural
               risk of the length violation is low, but the limit as stated has no
               carve-out for CLI code.

ID:            C-MAINT-012
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        gui/nova-gui.cpp:3174
IMPLEMENTED:   gui/nova-gui.cpp:3174-3570 (397 lines), gui/nova-gui.cpp:1345-1597
               (create(), 253 lines), gui/nova-gui.cpp:1805-2054 (apply_state(), 250
               lines), gui/nova-gui.cpp:2938-3070 (print_metrics(), 133 lines),
               gui/nova-gui.cpp:2719-2823 (click_image(), 105 lines),
               gui/nova-gui.cpp:1602-1722 (layout(), 121 lines)
VERDICT:       DEVIATES
EVIDENCE:      `main()` at gui/nova-gui.cpp:3174 is 397 lines — the largest function in
               the codebase after stage_assembly and nearly 5x the limit — verified by
               direct read start-to-end. It combines four largely independent concerns
               in one body: (1) a ~40-branch CLI flag parser (3188-3355) building an
               `Action` list; (2) definition of a local `struct Action` that is itself a
               small scripted-test-driver DSL (kArm/kClick/kFeed/kStopCapture/kType/
               kApply/kAuto/kRecvClick/kMark) embedded directly in the production binary
               (3189-3206); (3) an interpreter loop executing that action list against a
               live `Shell` (3455-3520); (4) GUI bootstrap and the `Fl::run()` event loop
               (3403-3570). Automated nesting scan of this file found no depth
               violations (max relative depth ~3), so this is a pure length /
               single-responsibility violation, not a nesting one. `create()` and
               `apply_state()` similarly mix many independent per-widget setup/update
               blocks in one function each.
NOTES:         The Action-kind dispatch (3455-3518) is a natural extraction boundary
               (`run_actions(Shell&, const std::vector<Action>&)`), as is the flag
               parser (`parse_args(...) -> ParsedArgs`). Doing so would also address
               C-MAINT-013 (duplicated Action-construction boilerplate) at the same time.

================================================================================
2. AI-AUTHORSHIP ARTIFACTS
================================================================================

ID:            C-MAINT-013
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        gui/nova-gui.cpp:3260-3335
IMPLEMENTED:   gui/nova-gui.cpp:3260-3335
VERDICT:       DEVIATES
EVIDENCE:      Rather than unused abstraction, the artifact found under this heading is
               the opposite failure mode: repeated boilerplate instead of a shared
               helper. Nine `else if (!std::strcmp(argv[i], "--X") ...)` branches in
               main()'s flag parser each independently write out `Action a; a.kind =
               Action::kY; ...; actions.push_back(a);` (verified at lines 3260-3267,
               3272-3282, 3298-3302, 3303-3316, 3317-3321, 3322-3326, 3327-3331,
               3332-3336). A one-line helper `push(Action::Kind)` or a lambda would
               remove ~30 lines of repetition. This is duplicated logic (protocol item
               1), not an unused-abstraction artifact (protocol item 2).
NOTES:         Low behavioural risk; purely a maintainability trim.

ID:            C-MAINT-014
PASS:          C
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        core/hooks.hpp:36, live/stream.hpp:1-149
IMPLEMENTED:   core/hooks.hpp (whole file), live/stream.hpp (whole file)
VERDICT:       NOT-APPLICABLE
EVIDENCE:      Searched the full core/ and live/ header set for the classic AI-artifact
               shapes the protocol names: `grep -rn virtual` across core/, live/, cli/,
               gui/ returned zero matches (no inheritance-based polymorphism anywhere in
               the codebase); there are no `template<class T>` generic container/
               strategy classes with a single instantiation; `DecodeHooks` (core/
               hooks.hpp:36, a `std::function`-based callback struct) is used by both
               CLI tools and the GUI engine with genuinely different bodies, not a
               speculative seam; `StreamResampler`/`StreamDemod` (live/stream.hpp) look
               at first glance like a duplicate of core/resample.cpp and core/demod.cpp
               but their header comment (live/stream.hpp:1-37) cites a specific,
               falsifiable equivalence requirement (docs/05 §2.2, and a named test,
               tests/test_live_equiv.cpp) with measured tolerances (bit-identical for
               the demodulator, 5e-13 for the resampler) — this is a load-bearing
               wrapper with a stated reason to exist, not indirection without benefit.
NOTES:         This codebase's characteristic AI-authorship artifact, on the evidence
               gathered in this pass, is NOT speculative architecture — it is oversized
               single functions (section 1) and a comment style unusually dense with
               narrative justification (section 3). No unused interface, no dead
               extension point, and no polymorphism-for-its-own-sake was found in the
               files read.

================================================================================
3. COMMENTS
================================================================================

ID:            C-MAINT-015
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        cli/nova-decode.cpp:96
IMPLEMENTED:   core/demod.hpp:12, core/demod.cpp:67, core/resample.hpp:9,13,
               cli/nova-decode.cpp:14,96, cli/nova-preview.cpp:24,74,
               cli/nova-tones.cpp:17,60, live/engine.hpp:122-123, live/stream.hpp:127
VERDICT:       DEVIATES
EVIDENCE:      Bare numeric literals in signal-path code with no adjacent comment
               citing a standard clause or a derivation:
               - `1900.0` (WEFAX audio subcarrier centre frequency) appears as an
                 uncommented literal or uncommented default parameter at
                 core/demod.hpp:12, cli/nova-decode.cpp:96, cli/nova-preview.cpp:74,
                 cli/nova-tones.cpp:60, live/engine.hpp:123, live/stream.hpp:127 — six
                 occurrences, none citing WMO/ISO or explaining why 1900 Hz.
               - `kInternalRate = 8000` is independently declared, identically, in
                 three CLI files (cli/nova-decode.cpp:14, cli/nova-preview.cpp:24,
                 cli/nova-tones.cpp:17) with no comment on why 8000 Hz was chosen as the
                 internal processing rate.
               - `63` (FIR tap count for the I/Q lowpass) at core/demod.cpp:67
                 (`make_lowpass(fs, iq_lowpass_hz, 63)`) has no comment at the call
                 site. Notably, a *different* file (live/stream.hpp:45-61) contains an
                 extensive, measured justification for `kDemodOverlap`, a constant
                 *derived from* this same tap count — the derived value is rigorously
                 justified while the literal it depends on is not.
               - `zero_crossings = 16`, the resampler's default kernel width
                 (core/resample.hpp:9,13), carries only "quality set by number of zero
                 crossings per side" — a description of what the parameter does, not a
                 derivation of why 16.
               This contrasts with the majority of DSP constants in core/fax.cpp and
               core/phasing.cpp, which are well justified (e.g. core/fax.hpp:40-48,
               kFaxDeadFrac/kFaxPulseFrac/kFaxDarkLevel/kFaxWhiteLevel each carry a WMO
               clause citation or a measured-tolerance rationale; core/fax.cpp:52-55
               justifies kSegHalf=4 by name).
NOTES:         Every one of these five literals is a place where a maintainer, six
               months from now, cannot tell from the code alone whether the number is a
               standards requirement, a tuned constant, or an arbitrary placeholder.
               Adding one comment per site (or centralizing into a single
               `kWefaxSubcarrierHz`/`kInternalRate` constant, see C-MAINT-016) would
               close this.

ID:            C-MAINT-016
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        core/demod.cpp:17
IMPLEMENTED:   core/demod.cpp:17, core/gen.cpp:10, core/fax.cpp:30, core/tones.cpp:8,
               core/resample.cpp:9
VERDICT:       DEVIATES
EVIDENCE:      `constexpr double kPi = 3.14159265358979323846;` is defined identically
               five separate times, once per .cpp file, each inside its own anonymous
               namespace (verified by grep across all five files — all five lines are
               byte-identical). In addition, the Blackman-window formula `0.42 + 0.5 *
               std::cos(kPi * u) + 0.08 * std::cos(2.0 * kPi * u)` is independently
               written out in full in two places: core/demod.cpp:33-34 and
               core/resample.cpp:44-45.
NOTES:         C++17 (the declared standard) has no `<numbers>` header (that is C++20),
               so a small shared `core/constants.hpp` (or a private header shared only
               by the .cpp files) with `kPi` and, if desired, a `blackman(double u)`
               helper would remove five duplicate definitions and one duplicated
               formula without changing any output.

ID:            C-MAINT-017
PASS:          C
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        core/fax.cpp:1512-1927, live/engine.cpp:661-846, core/phasing.cpp:180-476
IMPLEMENTED:   (same ranges)
VERDICT:       NOT-APPLICABLE
EVIDENCE:      Sampled multiple files across the tree (core/wav.cpp in full,
               live/ring.hpp in full, gui/nova-gui.cpp:1345-1420, core/fax.hpp:38-57)
               specifically looking for comments that restate the code rather than
               explain intent (the protocol's stated concern). None were found in the
               sampled material — even terse comments like "// riff size" and "// PCM"
               (core/wav.cpp:44,157) name what a discarded/magic field IS rather than
               re-describing the statement next to them, which is the useful case, not
               the restating one.
               The opposite pattern is what's actually present at scale: many functions
               (see the three cited above) carry dense, multi-paragraph comments that
               narrate *how a bug was found and fixed*, frequently citing "session N"
               and, in several places, a named individual's verdict verbatim (e.g. core/
               fax.cpp:1682-1684 quotes "Sara's verdict on the session-11 decodes was
               'for JSCs, small zigzag are still zigzags, still cause difficulties of
               reading'"; live/engine.cpp:733 cites "[§8.2, Sara, session 30]"; gui/
               nova-gui.cpp:3182 cites "Session 28 learned..."). These comments are
               intent-rich, not restating — but they are only fully verifiable by a
               maintainer who also has SESSION-LOG.md and ROADMAP.md open, which this
               audit pass was barred from reading (see C-GAP-002). A maintainer with
               full repository access would not have this problem; this auditor,
               working under the isolation rule, could not confirm what "session 11",
               "session 26", "session 28", "session 30", etc. actually established, only
               that the code cites them as authority.
NOTES:         Not a defect in the ordinary sense — the practice explains WHY far better
               than average commented code does — but it does mean code comprehension in
               this codebase is coupled to a narrative document that lives outside the
               source tree, rather than being self-contained. Recorded as informational
               because it does not affect decoder correctness or license status.

================================================================================
4. NAVIGABILITY
================================================================================

ID:            C-MAINT-018
PASS:          C
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        core/fax.cpp (whole file, 1965 lines)
IMPLEMENTED:   core/fax.cpp:896-997 (stage_track), core/fax.cpp:372 (fax_best_sync)
VERDICT:       DEVIATES
EVIDENCE:      Task performed: locate "the sync lock" with no prior knowledge of the
               tree, using only `ls core/` and grep.
               `ls core/` lists demod.cpp, fax.cpp, gen.cpp, hooks.cpp, image.cpp,
               phasing.cpp, resample.cpp, tones.cpp, wav.cpp — no filename contains
               "sync" or "lock". `phasing.cpp` is a plausible but wrong first guess (it
               is the ~30-second alternating-tone phasing interval detector, not the
               per-line sync-pulse tracker). The actual per-line sync lock
               (`fax_best_sync`, the `lock` threshold, `stage_track`) lives inside
               core/fax.cpp, a 1965-line file that also contains dead-sector detection,
               onset detection, segmentation, timebase measurement, and final assembly.
               It was found in under a minute via `grep -rn fax_best_sync core/` —
               fast, but only because grep was used; directory/filename browsing alone
               does not lead there.
NOTES:         This is a real but mild finding: the fix is not necessarily to split
               fax.cpp (that has its own costs, see section 1), but a maintainer new to
               the tree has no filename-level signpost to the sync-lock code the way
               they do for the demodulator or the image writer (below).

ID:            C-MAINT-019
PASS:          C
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        core/demod.cpp:1, core/image.cpp:1
IMPLEMENTED:   core/demod.cpp (whole file), core/image.cpp (whole file)
VERDICT:       CONFORMS
EVIDENCE:      Task performed: locate "the demodulator" and "the image writer" cold.
               Demodulator: `ls core/` shows `demod.cpp`/`demod.hpp` immediately;
               `fm_demod` is the only public entry point in the file (94 lines) — found
               in seconds, no grep needed.
               Image writer: `ls core/` shows `image.cpp`/`image.hpp`; `write_pgm` is
               the only function in a 16-line .cpp file — found in seconds. Note there
               is a second image writer for the live/GUI path, live/png.cpp (writes
               PNG, not PGM) — a maintainer asking generically "the image writer"
               without knowing which of the two pipelines (batch CLI vs. live GUI) they
               mean would need one extra step to discover the second one via `ls live/`,
               but each individual writer is itself trivially locatable once the
               relevant pipeline is known.
NOTES:         No deviation. Recorded to give the navigability assessment required by
               the protocol a complete positive/negative contrast against C-MAINT-018.

================================================================================
5. BUILD SIMPLICITY
================================================================================

ID:            C-BUILD-001
PASS:          C
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        CMakeLists.txt:1-90, README.md:302-320
IMPLEMENTED:   CMakeLists.txt (whole file)
VERDICT:       CONFORMS
EVIDENCE:      Ran the exact documented build from a clean, isolated directory (not the
               existing build/ tree): `rm -rf /tmp/nova-audit-c && cmake -B
               /tmp/nova-audit-c -S . && cmake --build /tmp/nova-audit-c -j8`. Configure
               succeeded (`-- nova-gui: enabled (/opt/homebrew/bin/fltk-config, rtaudio
               6.0.1)`), and the full build completed to 100% with exit code 0 and zero
               lines matching `grep -i warning` in the captured build log, despite
               `add_compile_options(-Wall -Wextra)` (CMakeLists.txt:12) being active
               project-wide.
               Dependency count: only `Threads` (system pthread, CMakeLists.txt:18) is a
               hard `find_package(... REQUIRED)`. FLTK and RtAudio (CMakeLists.txt:507-
               539) are looked up by the project's own tooling (fltk-config, pkg-config)
               and gate only the optional `nova-gui` target behind
               `NOVA_BUILD_GUI` (README.md:313-317 confirms the project's own claim that
               the rest of the suite builds and passes without them) — this audit did
               not verify the NOVA_BUILD_GUI=OFF path directly (see C-GAP-003).
               `nova-core` and `nova-live` (the decode/live engine libraries) link no
               external dependency beyond Threads.
NOTES:         No deviation found. The documented build reproduces from a clean
               checkout on this machine.

================================================================================
GAP REGISTER
================================================================================

ID:            C-GAP-002
PASS:          C
QUESTION:      Whether the "session N" / named-individual comment citations
               (core/fax.cpp:1682, live/engine.cpp:733, gui/nova-gui.cpp:3182, and
               similar) are in fact resolvable and accurate against SESSION-LOG.md, and
               whether that coupling is a real navigability cost for a maintainer who
               (unlike this audit pass) has full repository access.
BLOCKED BY:    SESSION-LOG.md and ROADMAP.md are excluded from this pass by the audit's
               isolation rules.
RESOLVES IF:   A future pass with access to SESSION-LOG.md/ROADMAP.md confirms the
               session numbers cited in-code correspond to real, findable entries, and
               assesses whether a maintainer without that history could still follow
               the reasoning from the in-code comment alone.

ID:            C-GAP-003
PASS:          C
QUESTION:      Whether `cmake -B ... -S . -DNOVA_BUILD_GUI=OFF` actually produces a
               working dependency-free build, as README.md:313-317 and
               CMakeLists.txt:507-539 claim.
BLOCKED BY:    Not attempted in this pass; only the default (GUI-enabled) configuration
               was built, since FLTK and RtAudio were both present on this machine and
               the default path was sufficient to answer the build-simplicity question
               the protocol asks for.
RESOLVES IF:   A build run with `-DNOVA_BUILD_GUI=OFF` on this or another machine,
               confirming it configures, builds, and that `nova-gui: SKIPPED` (or
               equivalent) is reported as CMakeLists.txt's comments claim.

ID:            C-GAP-004
PASS:          C
QUESTION:      Whether functions in gui/nova-gui.cpp beyond the ones explicitly cited
               in C-MAINT-012/013 (the file is 3570 lines, of which only a few hundred
               were read closely) contain further length/nesting/comment issues.
BLOCKED BY:    Time budget for this pass; the file was triaged by the automated brace
               scanner (which found the functions cited in section 1 and confirmed no
               depth>=5 nesting anywhere in the file) and sampled by direct read at
               several points, but not read end-to-end line by line the way core/fax.cpp
               was.
RESOLVES IF:   A dedicated pass reads gui/nova-gui.cpp in full; given the automated
               scanner's negative result on nesting depth for this file, any further
               findings there are expected to be length/duplication issues similar to
               C-MAINT-012/013 rather than new categories.

================================================================================
PASS SUMMARY
================================================================================

Findings by severity:
  critical:        0
  major:            0
  minor:           16   (C-MAINT-001, 002, 003, 004, 005, 006, 007, 008, 009, 010,
                          011, 012, 013, 015, 016, 018)
  informational:    4   (C-MAINT-014, C-MAINT-017, C-MAINT-019, C-BUILD-001)

  Total findings:  20   (C-MAINT-001 through C-MAINT-019 [19 items] + C-BUILD-001)

Load-bearing findings: 0. Nothing in this pass affects license selection, the
release/no-release decision, or a conformance statement in the README — every finding
is a maintainability observation with no behavioural effect, consistent with the
protocol's expectation that most Pass C findings land there.

Gaps: 3 (C-GAP-002, C-GAP-003, C-GAP-004).

What this pass did NOT cover:
  - Correctness of the DSP algorithms themselves (Pass A/B territory — this pass
    assumes the code does what its comments say and evaluates only how hard that would
    be for a new maintainer to verify or change).
  - The test suite was not run, per the task's explicit instruction (build only).
  - License/attribution completeness (NOTICE, LICENSE headers) — out of scope for
    Pass C per the protocol; Gate 0 already declares LICENSE_DECISION as fact.
  - gui/nova-gui.cpp was not read in its entirety (see C-GAP-004); tests/*.cpp were not
    reviewed for maintainability at all (only used to understand what nova-test-fixture
    links against, via CMakeLists.txt).
  - fixtures/*.wav were not inspected (binary audio data, not source).
  - `-DNOVA_BUILD_GUI=OFF` build path was not exercised (C-GAP-003).
  - No file in the forbidden list (SESSION-LOG.md, ROADMAP.md, AGENTS.md, START-HERE.md,
    docs/06-audit-gate0.md, git history) was read at any point in this pass — isolation
    was maintained throughout.

================================================================================
REMEDIATION — appended 2026-08-20 (session 34), per the dated-artifact rule
================================================================================

All 16 minor findings remediated; all 3 gaps closed. Behaviour-preserving
throughout: no logic changes, no test changes. Verification: full build
-Wall -Wextra with zero warnings; ctest 38/38 (GUI config) and 36/36
(-DNOVA_BUILD_GUI=OFF); a whole-tree function-length scan (core/, live/,
cli/, gui/) reports zero functions over 80 lines and none deeper than 4.
Commits on branch m4-gui-surfaces: 7238e41 (constants), e033d71 (group B),
eda1f71 (group D), 181793f (group C), d3081f5 (group E), 0aeec53 (group A),
plus two follow-ups noted below.

C-MAINT-001  CONFORMS  stage_assembly 416 -> 64 (correct_line_starts,
             line_correction, segment_residual_line, relock_dropout_runs,
             relock_row, intra_line_break, place_adrift_row, draw_rows).
C-MAINT-002  CONFORMS  Chain A: innermost if/else-if/else -> relock_row.
             Chain B: the r.size()>=4 block -> segment_residual_line.
C-MAINT-003  CONFORMS  refine_period 91->54, stage_onset 139->48,
             stage_dead_sector 175->69, stage_phasing 106->33,
             stage_track 102->71, stage_fit 149->65, stage_segment 93->38,
             stage_timebase 125->56, stage_change_points 129->47.
C-MAINT-004  CONFORMS  detect_phasing 297->55 (grow_run, trim_run_ends,
             measure_nonlin, absolute_anchor, judge_run).
C-MAINT-005  CONFORMS  detect_tones 93->23 (scan_tone).
C-MAINT-006  CONFORMS  read_wav 109->80 (pcm_traits<Sample> +
             decode_frames<Sample> collapse the five sample branches).
C-MAINT-007  CONFORMS  gen_fax_signal 107->67 (gen_line).
C-MAINT-008  CONFORMS  LiveEngine::collect_batch 186->80 (six file-local
             helpers along the audit's four seams).
C-MAINT-009  CONFORMS  try_acquire 103->50, draw_row 88->73.
C-MAINT-010  CONFORMS  push 106->67 (had grown from the audited 91 when
             D-PERF-003's opening cap landed), watch_step 88->67.
C-MAINT-011  CONFORMS  mains 178/114/100 -> 23/73/57 (parse_args +
             formatter in each). Follow-up: the extracted print_result in
             nova-decode.cpp was itself still 138 lines and was missed by
             the group (it checked the mains, not the helpers); the
             whole-tree scan caught it and print_timebase was extracted
             (print_result now 79).
C-MAINT-012  CONFORMS  gui main 397->68 (parse_args, run_actions,
             configure_shell, print_usage), create 253->9, apply_state
             250->64, print_metrics 133->10, click_image 105->54,
             layout 121->44. --metrics/--mark output verified
             byte-identical; load-bearing orderings preserved and listed
             in commit d3081f5.
C-MAINT-013  CONFORMS  the nine Action blocks go through a push() lambda
             inside parse_action_flag.
C-MAINT-015  CONFORMS  1900.0 cited [WMO §5.5.1] at all six sites;
             kInternalRate deduplicated into cli/internal_rate.hpp with
             the fixture-rate rationale; the 63-tap FIR noted in
             demod.cpp; zero_crossings=16 marked a tuned constant in
             resample.hpp.
C-MAINT-016  CONFORMS  core/constants.hpp holds kPi and blackman(); five
             duplicate definitions and two duplicated formulas removed.
C-MAINT-018  CONFORMS  signpost, per Sara's decision (not a file split):
             core/fax.cpp's header comment now maps where the per-line
             sync lock lives and disambiguates phasing.cpp.
C-MAINT-014, C-MAINT-017, C-MAINT-019, C-BUILD-001: informational, no
             action required; C-MAINT-017's session-citation coupling is
             addressed by C-GAP-002's outcome below.

C-GAP-002    CLOSED    Every session number cited in code (3-12, 16, 17,
             20-23, 25-31) resolves against SESSION-LOG.md. Wrinkle
             recorded: sessions 24 and 25 have no dated headings of their
             own — their work is narrated inside the neighbouring entries
             (session 25's live run inside Session 26's entry), so they
             are findable by grep but not by heading scan.
C-GAP-003    CLOSED    -DNOVA_BUILD_GUI=OFF configures, builds with zero
             warnings, and passes 36/36 (the two GUI suites absent by
             design). The path printed nothing at configure time — the
             SKIPPED messages only covered missing deps — so a one-line
             "nova-gui: SKIPPED - NOVA_BUILD_GUI=OFF" was added.
C-GAP-004    CLOSED    gui/nova-gui.cpp read end-to-end during the group
             E refactor: no further length, nesting, duplication or
             comment issues beyond those remediated.

One non-observable ordering note, disclosed by the group A worker:
res.place_rms_px is now computed inside correct_line_starts instead of
after the draw loop; nothing reads it in between except the final
summary line, which is unchanged.
