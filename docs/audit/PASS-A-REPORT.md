# Pass A — Signal Conformance Audit (Tier 1 primary)

```
PASS:          A
AUDITOR:       Claude Sonnet 5, run 2026-08-16, fresh subagent, no authoring context
INPUTS:        Tree: /Users/sakuragawasara/Documents/2026/2026 Amateur Radio/isobar-nova/nova
               Tier 1 opened: 386_2023-edition_en.pdf (WMO-No. 386, 2023 edition,
                 confirmed from title page text at extraction lines 8/26),
                 own pdftotext -layout extraction, Part III §5 at source lines
                 10240-10510 of the extraction
               Tier 2 opened: ISO_9876_2015(en).pdf, own pdftotext -layout extraction,
                 full text (Clauses 1-5.5.5)
               Fixture set: nova/fixtures/, 19 WAV files present on this auditor's
                 machine, MANIFEST.md SHA-256 list not independently re-hashed
                 (bytes not re-verified against MANIFEST digests — see A-GAP-004)
               Prior pass outputs: none (Pass A is independent per protocol)
               Build: cmake -B /tmp/nova-audit-a -S nova, cmake --build (Release,
                 default), ctest -j4 — 38/38 passed, 0 skipped (fixtures present)
```

I did not read SESSION-LOG.md, ROADMAP.md, AGENTS.md, START-HERE.md,
docs/06-audit-gate0.md, or any git history, per the hard-isolation instruction.
`docs/01-signal-spec.md` and `docs/02-compliance-matrix.md` were read only as
the SUBJECT of this audit; every "specified value" below was independently
re-derived from my own WMO/ISO extractions before comparison.

---

## A.1 — Parameter conformance table

Full table reproduced again in the summary section at the end, as required.
Findings below give the evidence for each row.

### 1. Index of Cooperation (IOC)

```
ID:            A-CLAIM-001
PASS:          A
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        Tier 1 — WMO386:III-5 §5.1.2  |  core/fax.cpp:822-833  |  both
IMPLEMENTED:   core/fax.cpp:822-833 (IOC selection), core/fax.cpp:1531
               (width = 288 -> 905 px, else 1810 px), core/gen.cpp:64
               (generator start-tone frequency by IOC)
VERDICT:       CONFORMS
EVIDENCE:      WMO §5.1.2 defines M = LF/π and states "the standard index of
               cooperation shall be 576 or 288" — no other values. The code
               defaults IOC to 576 and switches to 288 only on detection of
               the 675 Hz start-tone kind (ISO §4.2.3 permits automatic OR
               manual selection; the CLI --ioc flag supplies manual). No
               other IOC value is representable — the CLI rejects anything
               but 0/288/576.
NOTES:         The 1810 px / 905 px line-pixel-count constants are NOT a WMO
               clause value; WMO defines only the ratio M = LF/π, which
               constrains transmitter/receiver compatibility, not a specific
               picture width in software. 1810 ≈ 576·π is a widely used
               engineering convention (docs/01 attributes it to "JWX/others"
               rather than to a WMO citation) — this is correctly NOT
               over-cited to a clause number in docs/01, so no citation
               defect here.
```

```
ID:            A-CLAIM-002
PASS:          A
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        Tier 1 — WMO386:III-5 §5.1.2 / §5.1.4
IMPLEMENTED:   n/a — this is a document-only comparison
VERDICT:       CONFORMS
EVIDENCE:      WMO §5.1.4 states line density is "nominally equal to 3.8
               lines/mm (index 576) and 1.9 lines/mm (index 288)" — matches
               docs/01's table exactly, and this quantity plays no role in
               Nova's software path (line density is a physical-recorder
               property; Nova works from IOC directly).
NOTES:         none
```

### 2. Line rate / drum speed set and detection logic

```
ID:            A-CLAIM-003
PASS:          A
SEVERITY:      major
LOAD-BEARING:  yes
SOURCE:        Tier 1 — WMO386:III-5 §5.1.5  |  core/fax.cpp:493,508,538-565  |  both
IMPLEMENTED:   core/fax.cpp:493 (kRates[] = {60, 90, 120}), :508 (opt.lpm
               filter), :538-552 (rate selected by lowest-clearing-rate
               comb rule), cli/nova-decode.cpp:82-83 (CLI rejects any lpm
               other than 0/60/90/120)
VERDICT:       DEVIATES (declared)
EVIDENCE:      WMO §5.1.5 states the scanning line frequency "shall be: 60
               lines per minute; 90 lines per minute; 120 lines per minute;
               240 lines per minute" — four values, all required of a
               conforming facsimile transmission facility. Nova's kRates
               array and CLI validation admit only three; there is no code
               path, constant, or CLI flag anywhere in core/ or cli/ that
               can select or detect 240 lpm. This is a genuine deviation
               from the WMO clause, not merely from convention.
               ISO 9876:2015 §4.2.4, the document actually governing a
               "recording unit" (Nova's actual role — see A.2), requires
               only "60, 90 and 120 scans per minute" — so against the
               document that specifies what a *receiver* must do, Nova
               conforms; against the document that specifies the full set
               of transmission rates a facility may use, it does not.
NOTES:         docs/01 and docs/02 both state this omission and its ISO
               §4.2.4 justification explicitly and correctly cite it as a
               declared deviation — this is exactly the "declared deviation
               still counts as a deviation" case the protocol calls out.
               Marked major rather than critical because it is a scope
               limit stated up front and does not corrupt output on inputs
               within the implemented set; a 240 lpm broadcast simply
               cannot be decoded at all (would fail as kNoSignal, verified
               by code inspection — no 240-candidate exists in the onset
               comb scan).
```

### 3. Start (APT) tone

```
ID:            A-CLAIM-004
PASS:          A
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        Tier 1 — WMO386:III-5 §5.2.2  |  core/tones.hpp:23-26,
               core/tones.cpp:128-131, core/gen.cpp:56-65  |  both
IMPLEMENTED:   core/tones.hpp:24-25 (300.0/675.0 Hz nominal), core/tones.cpp
               :128-131 (kStartIOC576=300Hz, kStartIOC288=675Hz), core/gen.cpp
               :63-65 (generator produces 5-10 s per opt.start_sec at the
               matching frequency)
VERDICT:       CONFORMS
EVIDENCE:      WMO §5.2.2.1 specifies alternating black/white 5-10 s at
               300 Hz (IOC 576) or 675 Hz (IOC 288, or 576 alternate-line),
               §5.2.2.2 requires an approximately rectangular envelope.
               Nova's constants and duration acceptance window (min_start_sec
               = 2.0 s, deliberately below spec minimum to tolerate a
               recording that opens mid-tone — see NOTES) match the
               frequencies exactly; the black/white alternation is generated
               as a rectangular square wave (core/gen.cpp:59-60 push_tone).
NOTES:         Nova's min_start_sec=2.0 is NOT the WMO duration — it is a
               deliberately relaxed detector-acceptance floor, clearly
               documented as such in tones.hpp:47-51. This is a sound
               engineering choice, not a spec violation, since WMO defines
               what a TRANSMITTER sends, not what a receiver must require
               before accepting a truncated capture of one.
```

### 4. Phasing signal

```
ID:            A-CLAIM-005
PASS:          A
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        Tier 1 — WMO386:III-5 §5.2.3.1, §5.2.3.2, §5.2.3.4  |
               core/phasing.hpp, core/phasing.cpp, core/gen.cpp:67-77  |  both
IMPLEMENTED:   core/phasing.hpp:60-91 (PhasingOptions: max_gap=8,
               min_lines=12, max_sec=60.0, max_spread_frac=1/24),
               core/gen.cpp:71-77 (phasing line generation, symmetric or
               5%/95% wedge)
VERDICT:       CONFORMS
EVIDENCE:      WMO §5.2.3.1 specifies a 30 s transmission with 1.0/1.5/2.0 Hz
               for 60/90/120 lpm (4.0 Hz for 240, correctly out of Nova's
               scope per A-CLAIM-003); §5.2.3.2 permits either a symmetric
               (50/50) or asymmetric (5% white / 95% black) waveform;
               §5.2.3.4 places the phasing reference at the leading edge of
               white, coincident with dead-sector entry. Nova's generator
               (gen.cpp:75) produces both waveform shapes on request and the
               decoder (phasing.hpp docstring, phasing.cpp) explicitly
               refines to "the local 50% black->white crossing" for the
               anchor, matching §5.2.3.4's stated reference point. Both
               waveforms are exercised by real off-air fixtures: XSG
               (symmetric 50/50) and JMH/GYA family (asymmetric 5/95) —
               see fixture_anchor_delta_jmh / _xsg.
NOTES:         The acceptance window (max_gap=8 lines, min_lines=12,
               max_spread_frac=1/24 ≈ 4.2% of a line) is Nova's OWN
               engineering tolerance, not a WMO number — WMO specifies the
               transmitted signal, not a receiver's acceptance geometry, so
               there is nothing in the clause to compare these thresholds
               against; this is correctly not over-cited in docs/01 as a
               clause-derived number.
```

### 5. Stop tone

```
ID:            A-CLAIM-006
PASS:          A
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        Tier 1 — WMO386:III-5 §5.2.5  |  core/tones.cpp:131,
               core/gen.cpp:116-118  |  both
IMPLEMENTED:   core/tones.cpp:131 (kStop, 450.0 Hz nominal), core/gen.cpp
               :116-118 (5 s of 450 Hz alternating, then 10 s of black,
               i.e. push_samples 10.0*fs of 0.0f)
VERDICT:       CONFORMS
EVIDENCE:      WMO §5.2.5.1: "a five-second transmission of alternating
               black and white signals at 450 Hz, followed by 10 seconds of
               signal corresponding to continuous black." Nova's generator
               reproduces exactly this two-part structure and the decoder's
               ToneKind::kStop / 450 Hz nominal matches. Real off-air
               coverage: nmc-image-stop-tone-120s.wav ("the library's only
               real 450 Hz stop tone, fading 0.88 s mid-tone").
NOTES:         Stop DETECTION behaviour (what happens after a stop tone is
               found) is bounded by the segmentation logic (DecodeOptions
               ::segment, fax.hpp:139-142): a found stop tone crops the
               drawn picture there; it does not itself terminate the
               decode of the whole recording (a second transmission after
               the stop is not drawn, but is not an error either — see
               fixture_phasing_two_openings / the jmh-sample two-
               transmission case discussed in docs/01, which I did not
               independently re-verify against that specific recording
               since it is not one of the 19 registered fixtures — see
               A-GAP-001).
```

### 6. Carrier/centre frequency, deviation, tone assignment, modulation index

```
ID:            A-CLAIM-007
PASS:          A
SEVERITY:      major
LOAD-BEARING:  yes
SOURCE:        Tier 1 — WMO386:III-5 §5.3.1.2, §5.5.1, §5.5.2  |
               core/demod.hpp:12, cli/nova-decode.cpp:51,86,96  |  both
IMPLEMENTED:   core/demod.hpp:12 (center=1900.0, deviation=400.0 defaults),
               cli/nova-decode.cpp:51 (deviation default 400.0), :57-58
               (--dev flag), :86 (validates deviation is 150.0 or 400.0
               only)
VERDICT:       CONFORMS, with one imprecise citation flagged (see NOTES)
EVIDENCE:      WMO §5.3.1.2 (FM subcarrier): mean 1900 Hz, black 1500 Hz,
               white 2300 Hz — matches core/demod.hpp defaults exactly
               (center - deviation = 1500 = black, center + deviation =
               2300 = white). WMO §5.5.1 restates the identical 1900/1500/
               2300 Hz triple for "frequency modulation of the sub-carrier
               ... over radio circuits." Both match Nova's hardcoded values.
NOTES:         **Citation-precision finding.** docs/01's table row "Audio
               shift at receiver | ±400 Hz (HF circuits), ±150 Hz (LF
               circuits) about the 1900 Hz centre | ISO §4.2.2, WMO §5.5.2"
               is imprecise about what WMO §5.5.2 itself says. WMO §5.5.2
               is titled "direct frequency modulation (FSK)... of the RF
               carrier" and its stated centre frequency is explicitly
               "fo (corresponding to the assigned frequency)" — the RF
               carrier — NOT 1900 Hz; black/white are fo∓400 Hz (HF) or
               fo∓150 Hz (LF) about that RF carrier, a completely different
               physical signal from the audio subcarrier of §5.3.1.2/§5.5.1.
               The "audio shift about 1900 Hz" framing is actually and only
               ISO §4.2.2's language: "shifts of ±150 Hz and/or ±400 Hz
               about a centre frequency of 1900 Hz" (verified verbatim
               against my own ISO extraction). So the row's dual citation
               conflates two different WMO clauses (§5.3.1.2/§5.5.1 audio
               FM, and §5.5.2 RF FSK) under one number that is actually
               only ISO's. docs/01's own "Edition note" partially
               acknowledges this tension ("the practical content is pinned
               by ISO §4.2.2 anyway") but the table row itself still cites
               WMO §5.5.2 for a number that clause does not state. This
               does not change Nova's implemented behaviour — the code
               correctly implements ISO §4.2.2's audio-domain requirement,
               which is the requirement that actually applies to a
               software decoder receiving discriminator audio — but the
               clause citation as written is a defect under this protocol's
               "mis-citing a clause number is a critical defect in the
               audit itself" rule, scoped to the CITATION rather than to
               the CODE. I rate this major (not critical) because the
               governing document for what the code does (ISO §4.2.2) is
               correctly cited elsewhere in the same table row and the
               deviation is transparently documented in the surrounding
               prose, so the practical risk of someone building the wrong
               thing from this row is low, but the citation itself is
               objectively imprecise and should be corrected to cite ISO
               §4.2.2 alone, or to state explicitly that WMO §5.5.2's f0
               is being (correctly) treated as equivalent to the ISO
               audio-domain figure for a receiver whose "RF" input is
               already downconverted to an audio subcarrier by an external
               receiver.
```

```
ID:            A-CLAIM-008
PASS:          A
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        Tier 1 — WMO386:III-5 §5.3.1.2, §5.4.1, §5.4.3  |
               core/gen.cpp:52-55, tests/test_roundtrip.cpp [12]  |  both
IMPLEMENTED:   test_roundtrip.cpp [12] "gray linearity: eight-band step
               chart [WMO §5.4.3]"
VERDICT:       CONFORMS
EVIDENCE:      WMO §5.4.1: "a linear distribution should be observed... on
               the basis of a number of tones equal to eight." §5.4.3 gives
               the exact eight FM frequencies: "1500, 1614, 1729, 1843,
               1957, 2071, 2186, 2300 Hz." docs/01's table reproduces these
               eight numbers exactly and they match the extraction
               character-for-character. Tested by roundtrip [12].
NOTES:         Modulation INDEX is not a term WMO 386 or ISO 9876 uses in
               this section (this is expected — analogue WEFAX FM does not
               define a modulation index the way narrowband FM voice
               specs do; the deviation ratio, 400/1900 or 150/1900, plays
               that role informally). Neither document nor Nova's code
               computes an explicit "modulation index," so there is nothing
               to conform or deviate against — recorded as NOT-APPLICABLE.
```

### 7. Aspect ratio, samples-per-line, accumulated drift

```
ID:            A-CLAIM-009
PASS:          A
SEVERITY:      minor
LOAD-BEARING:  yes
SOURCE:        Tier 1 — WMO386:III-5 §5.1.1, §5.1.5  |  core/fax.cpp:1531,
               core/gen.cpp:40-41  |  both
IMPLEMENTED:   core/fax.cpp:1531 (width = 288 -> 905, else 1810),
               core/gen.cpp:40-41 (period = fs*60/lpm samples per line)
VERDICT:       CONFORMS (drift handling), GAP (formal aspect-ratio clause)
EVIDENCE:      WMO §5.1.1 defines scan direction and adjacency only; it does
               not state an aspect ratio numerically (aspect ratio in
               analogue WEFAX is a function of drum/paper geometry, §5.1.3.1
               /§5.1.3.2, which are hardware dimensions Nova has no reason
               to reproduce — see A.2). I could not find any WMO or ISO
               clause that states a target width:height ratio for a decoded
               RASTER image; this appears to be a genuine absence in the
               normative text, not an omission by Nova. Accumulated timing
               drift IS addressed at length: the design-consequences section
               of docs/01 and the corresponding code (segmented Theil-Sen
               fit, per-line dead-sector relock, step correction — core/
               fax.cpp lines ~1520-1545 and surrounding stages) describe a
               baseline-measured clock with per-line correction, which is
               the correct mechanism for the accuracy figures ISO §4.2.6
               actually specifies (see A-CLAIM-011).
NOTES:         Register as GAP-002: no WMO/ISO clause defines a numeric
               aspect ratio for the decoded raster; docs/01's "Derived: ...
               active picture ≈ 95.5% of the line" is a Nova-side
               computation from the dead-sector fraction, not a
               clause-sourced figure, and is correctly NOT cited to a WMO
               paragraph in docs/01.
```

### 8. Behaviour on out-of-spec, truncated, or noise-corrupted input

```
ID:            A-CLAIM-010
PASS:          A
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        both (WMO 386 does not specify receiver error handling;
               this is inherently a Nova-side engineering question) |
               core/hooks.hpp:51-58, core/wav.cpp:43-44,55,59,100-103,
               core/fax.cpp:484,556,601,1934
IMPLEMENTED:   DecodeErrorKind{kEmptyInput, kTooShort, kNoSignal,
               kTooFewLines, kCancelled}; WAV reader kMinRateHz=4600,
               kMaxRateHz=768000, RIFF/WAVE tag checks, 0-channel rejection,
               declared-size clamping to actual bytes present
VERDICT:       CONFORMS to Nova's own stated design goal (no crash/hang/
               unbounded resource use); no applicable WMO/ISO clause exists
               to conform or deviate against
EVIDENCE:      Independently fuzz-tested (see A.3 below) with truncation at
               6 points including 0/4/10/20/43/44-byte headers, 2000-byte
               random bit flips (broadband and header-targeted), a
               head/tail-swapped splice, mid-file and whole-file silence
               injection, a data-chunk-size lie (0xFFFFFFF0 declared over
               ~960 KB real), and a 1 Hz declared sample rate. All 17
               variants completed in <=1 s wall time with peak RSS <=8.6 MB
               (bounded by input file size), either producing a decode or
               exiting 1 with a specific, non-generic error message. No
               crash, hang, or unbounded memory growth was observed.
NOTES:         See A.3 for the full corrupted-input methodology and the one
               correctness (not crash) finding this pass produced
               (A-CLAIM-013, phasing-loss on a white-only station).
```

---

## A.2 — ISO 9876:2015 applicability map

Walking the standard clause by clause (my own extraction, full text).
**No global "conforms to ISO 9876:2015" statement is made anywhere in this
report**, per protocol — Nova is a software decoder, not the "equipment"
(radio receiver + recording unit) ISO 9876 type-tests as a unit.

| Clause | Disposition | Reason / file:line / cross-ref |
|---|---|---|
| §3.1-3.13 (definitions) | APPLICABLE (informative) | Defines terms used by §4.2; not independently testable, folded into the rows below |
| §4.1 Construction | HARDWARE-ONLY | Physical enclosure, stylus/paper access under artificial light — no software analogue |
| §4.2.1 Recording unit / continuous recording | HARDWARE-ONLY | "Continuous recording while transmissions are being received" describes a paper recorder; Nova decodes files/streams, not continuous paper — docs/02 correctly excludes this |
| §4.2.2 Input signals | APPLICABLE | core/demod.hpp:12 (1900 Hz centre, both ±150/±400 Hz deviations selectable via cli/nova-decode.cpp --dev); amplitude tolerance addressed by amplitude-normalized discriminator (tested: roundtrip [11], amplitude sweep). VERDICT: CONFORMS (synthetic only — see untested-conformance list) |
| §4.2.3 IOC 576/288, automatic or manual | APPLICABLE | core/fax.cpp:822-833 (auto from start tone), CLI --ioc (manual). VERDICT: CONFORMS. Real-signal auto-selection of IOC 288 (675 Hz) is untested — see A-GAP-002 below |
| §4.2.4 Scanning speeds 60/90/120, auto+manual | APPLICABLE | core/fax.cpp kRates[]={60,90,120}; CLI --lpm manual. VERDICT: CONFORMS, cross-ref A-CLAIM-003 for the 240 lpm non-requirement this clause creates |
| §4.2.5 Automatic response to 300/675/450 Hz | APPLICABLE, DEVIATES in method | core/tones.cpp spectral-purity detector, not "detection of line synchronization" as the clause's own text specifies ("shall be by detection of line synchronization"). See A-CLAIM-011a below — this is the SECOND declared deviation the protocol asked me to verify |
| §4.2.6 Sync/phasing accuracy, automatic + manual adjustment | APPLICABLE (partial) | See A-CLAIM-011 below |
| §4.2.7 Pitch of scanning trace ±25% | APPLICABLE | core/fax.cpp — line pitch is one output row per measured scan line by construction, so the ±25% figure is structurally unfalsifiable by the code (no separate mechanism could deviate from it); tested indirectly by --expect-straight-strip picture-domain measurement. VERDICT: CONFORMS |
| §4.2.8 Recording size/marking | HARDWARE-ONLY | Physical paper roll width/marking — no software analogue |
| §4.3 Radio receiver (frequency range, stability, sensitivity, selectivity, IF/image rejection) | HARDWARE-ONLY | Nova has no RF front end; it consumes audio already demodulated by an external receiver or SDR. All of §4.3.1-4.3.6 is out of scope by construction |
| §4.4 IEC 60945 environmental/general | HARDWARE-ONLY | Bridge-mount environmental spec, not applicable to software |
| §5.1-5.3 (general test conditions, construction test) | HARDWARE-ONLY / test-methodology, not a functional requirement | Describes HOW a type-testing lab sets up equipment; not a functional clause Nova's code could conform to or deviate from |
| §5.4.1-5.4.2 (recording-unit test method + result: IOC×speed matrix identical to transmitted) | DEFERRED to A.1 | Directly mirrors docs/02's own §5.4.2 row; cross-ref A.1 items 1-2. Nova's synthetic full {288,576}×{60,90,120} matrix (roundtrip [4]) is the closest software analogue of this test method. VERDICT: CONFORMS (synthetic only) |
| §5.4.3 (visual: chart complete/square; manual phase adjustment works) | APPLICABLE, partial | The "complete and square" half is DEFERRED to A.1 item 7 (pitch/drift); the manual-phase-adjustment half is implemented (live/session.cpp, live/preview.cpp StreamPreview::set_phase_anchor, per docs/02's own narrative) but this is GUI/live-path code that Pass A did not independently exercise beyond the ctest suite (override_phase_seed / override_sync_fallback tests pass — see A.3). VERDICT: CONFORMS, tested |
| §5.4.4 Pitch of scanning trace (visual: even, parallel lines) | DEFERRED to A.1 item 7 | Same clause as §4.2.7; the test-method text is visual inspection, Nova substitutes a quantitative picture-domain measurement (--expect-straight-strip / --expect-rigid-rows), which is a stronger check, not merely an equivalent one |
| §5.4.5 Recording size/marking (test method) | HARDWARE-ONLY | Cross-ref §4.2.8 |
| §5.5.1-5.5.5 (radio-receiver test methods) | HARDWARE-ONLY | Cross-ref §4.3 |

```
ID:            A-CLAIM-011
PASS:          A
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        Tier 2 — ISO 9876:2015 §4.2.6  |  core/fax.cpp (segmented
               Theil-Sen fit, phasing anchor), live/preview.hpp
               set_phase_anchor / set_clock_ppm
VERDICT:       CONFORMS (synthetic +100/-137 ppm cases; real-recording claim
               is explicitly narrower than "conforms" — see NOTES)
EVIDENCE:      ISO §4.2.6 requires ±2×10⁻⁶ frequency accuracy, ±2×10⁻⁵
               stability, and "phasing... automatic with a facility for
               manual adjustment." The synthetic round-trip tests (roundtrip
               [2], [8]) demonstrate the fit recovers an injected clock
               error (+100 ppm, -137 ppm) to within the stated bound. Manual
               adjustment exists end-to-end (CLI --phase/--sync flags,
               GUI wiring per docs/02's own account, tested by
               override_phase_seed / override_sync_fallback, both of which
               passed in this run's ctest).
NOTES:         docs/02 itself states the honest limitation clearly and I
               independently agree with the reasoning: "§4.2.6's accuracy
               figure is a receiver specification. Nova's synthetic
               round-trip meets it outright... on real recordings the
               honest statement is the residual shear bound... because no
               ground-truth clock exists for an off-air recording." This is
               a well-calibrated claim, not an overclaim — I flag it here
               only to make explicit that "CONFORMS" in the A.1/A.2 tables
               above, where it rests on synthetic tests only, should be
               read with that same caveat.
```

```
ID:            A-CLAIM-011a
PASS:          A
SEVERITY:      major
LOAD-BEARING:  yes
SOURCE:        Tier 2 — ISO 9876:2015 §4.2.5  |  core/tones.hpp:1-16,
               core/tones.cpp:42-77 (tone_purity Goertzel implementation)
VERDICT:       DEVIATES (declared)
EVIDENCE:      ISO §4.2.5 states: "The recording unit shall automatically
               respond to control signals of 300 Hz and 675 Hz modulation
               of the carrier wave for start and index of cooperation
               selection shall be by detection of line synchronization and
               to 450 Hz modulation stop signal." (I extracted this
               sentence verbatim from my own ISO copy; its grammar is
               garbled in the source PDF itself — "selection shall be by
               detection of line synchronization" reads as an appositive
               qualifying HOW selection is done.) Read most naturally, this
               specifies that IOC/start selection is to be accomplished by
               detecting LINE SYNCHRONIZATION (i.e., the transition/comb
               structure of the alternating tone), not by a spectral-purity
               test. Nova's core/tones.cpp explicitly and admittedly does
               something else: a Goertzel-bin-power-fraction purity
               measure, chosen because (per tones.hpp:9-16 and the library
               measurement cited in docs/01) a pure transition-rate counter
               cannot distinguish a true control tone from dense weather
               text that happens to average the right transition rate. This
               is the SECOND deliberate deviation the audit protocol asked
               me to verify, and it is real: the method is different from
               the clause's stated method, even though the frequencies
               detected (300/675/450 Hz) and their accuracy (±1% per WMO
               §5.2.6) are unchanged.
NOTES:         This is an engineering improvement over the literal clause
               method, well-argued and library-measured in docs/01 (content
               ≤0.16 purity vs tones 0.68-0.99, session 6). It is still,
               under this protocol's rules, a DEVIATES verdict against the
               clause's stated method, not a CONFORMS with commentary. I
               rate it major (not critical) because it does not change what
               frequencies are detected or degrade accuracy — if anything
               docs/01's own false-start testing (tones_fixture_no_false_
               start, 120 s of real newspaper text producing zero events)
               is stronger evidence against false positives than a
               transition-rate method would give. What would change the
               verdict to CONFORMS: nothing will, short of the clause being
               reworded; this should simply be listed as a permanent,
               justified deviation, not resolved.
```

---

## A.3 — Fixture regression

### Build and test run

`cmake -B /tmp/nova-audit-a -S nova && cmake --build /tmp/nova-audit-a -j8`
completed with exit 0, no warnings surfaced in the captured log beyond normal
build output. `ctest --test-dir /tmp/nova-audit-a -j4` ran **38 of 38
registered tests, 100% passed, 0 skipped** (fixtures/*.wav are present on
this auditor's machine, so `NOVA_HAVE_FIXTURES` was ON and none of the
fixture-gated suites fell back to the "SKIPPED: needs an off-air recording"
stub described in `fixtures/MANIFEST.md` and mirrored in `CMakeLists.txt`'s
`nova_fixture_test()` function).

### Coverage checklist against the protocol's required cases

| Required case | Covered? | Evidence |
|---|---|---|
| Both IOC values | Partial | IOC 576 exercised by every real fixture (all 19 use the 1810-px/pulse-or-white-576 layout by inspection of the CMakeLists bounds — none passes 288 as the lpm-column's neighbouring IOC field, and none of the `nova_fixture_test` invocations reference IOC 288). IOC 288 is exercised ONLY synthetically, in `roundtrip [4]`'s {288,576}×{60,90,120} matrix and `test_tones.cpp`'s 675 Hz synthetic case. **No real off-air IOC-288 recording exists in the fixture set.** docs/02 states this itself ("real 675 Hz fixture remains a registered gap") — confirmed correct by my own grep of CMakeLists.txt and MANIFEST.md; I did not find a contradiction |
| Every supported line rate (60/90/120) | Partial | 120 lpm: the overwhelming majority of real fixtures. 60 lpm: exactly one real fixture, `kyodo-news-jsc1-60lpm-120s.wav` (fixture_60lpm). **90 lpm has no real off-air fixture** — synthetic only (roundtrip [4]). docs/02 states this itself ("real 90 lpm fixture remains a registered gap") — confirmed correct |
| A clean signal | Yes | `fixture` (test-chart-jmh-kiwisdr-image-60s.wav): "pure image content, no control signal, no echo, no dropouts," 117/120 lines locked, passed |
| A fading/noisy signal | Yes | `fixture_weak_white` (GYA weak+white-only), `fixture_faded_phasing` (GYA faded phasing), `fixture_false_locks` (HLL faded with polluted white gap) — all passed |
| A corrupted/missed phasing block | Partial, in-suite; supplemented independently | The suite's closest analogue is `fixture_faded_phasing` (a REAL faded phasing interval that is hard to detect, not an artificially corrupted/absent one) and `fixture_phasing_one_skip` (a real single discontinuity inside phasing). Neither is a case where phasing is entirely ABSENT or destroyed on a station that structurally depends on it. I constructed an independent test for exactly that gap — see A-CLAIM-013 below, which found a real (non-crash) behavioural finding |
| A truncated transmission | Partial, in-suite; supplemented independently | `fixture_fill_reject` (stall-fill-15s.wav, no line structure at all — correctly refused) is the only in-suite truncation-adjacent case, and it is "no signal ever appeared," not "signal that stops mid-transmission." I independently truncated a clean fixture at 0.1%, 1%, 50%, and 99% of its length and fed each to nova-decode — see A.3 corrupted-input results below; no crash/hang, and the very short truncations were correctly refused (kTooShort / kNoSignal) |

```
ID:            A-GAP-002
PASS:          A
QUESTION:      Is there a real off-air recording exercising IOC 288 (675 Hz
               start tone) anywhere in the 19-file fixture set?
BLOCKED BY:    No such recording exists in fixtures/ as of this build (I
               grepped CMakeLists.txt's nova_fixture_test bounds and
               MANIFEST.md's station list; none references IOC 288 or a
               675 Hz start). This is not a limitation of my search — the
               project's own docs/02 acknowledges the same gap in its own
               words ("real 675 Hz fixture remains a registered gap"),
               which I independently confirmed rather than took on faith.
RESOLVES IF:   An off-air recording of an IOC-288 station (or an IOC-576
               station using 675 Hz "alternate-line" mode per WMO §5.2.2.1)
               is captured and added to fixtures/, with a corresponding
               add_test bound in CMakeLists.txt.
```

```
ID:            A-GAP-003
PASS:          A
QUESTION:      Is there a real off-air recording at 90 lpm anywhere in the
               fixture set?
BLOCKED BY:    Same as above — confirmed absent by direct inspection, and
               the absence is self-reported in docs/02 ("real 90 lpm
               fixture remains a registered gap"), independently verified.
RESOLVES IF:   A 90 lpm off-air capture is added with a bound.
```

### Independent corrupted-input testing (done separately from `test_malformed.cpp`)

`test_malformed.cpp` (added "session 32" per its own header comment, which I
read only as code, not as a session-log citation) already covers: a
data-chunk-size lie clamped to real bytes, implausible sample rates (0, 1,
4000, 4294967295 Hz refused; 8000/11025/48000/192000 accepted), a truncated
header, a truncated data chunk (50 MB declared over 40 real bytes -> 20
samples read, not allocated), 0-channel rejection, and `resample_ratio`
bound checks (8000x and 1e-5 refused; 6.0, 1/6, 1.0001 accepted). This is a
well-targeted suite directly aimed at the two documented historical defects
(D-PERF-001/002) and I assess it as adequate for the WAV-header and
resample-ratio attack surface specifically — but it is entirely synthetic
(hand-built headers) and does not exercise the decode pipeline (`decode_fax`)
itself against corrupted PAYLOAD content, only against corrupted CONTAINER
metadata. I therefore ran an independent round against `nova-decode` end to
end, using a real fixture's audio payload:

Method: `perl -e 'alarm 60; exec @ARGV' ./nova-decode <corrupt.wav> out.pgm`,
wrapped in `/usr/bin/time -l` for peak-RSS measurement, against 17
independently generated variants of `test-chart-jmh-kiwisdr-image-60s.wav`
(960044 bytes): truncation at 0/4/10/20/43/44 bytes (inside the 44-byte
canonical header) and at 0.1%/1%/50%/99% of the full file; 2000 random byte
flips scattered through the payload; 20 random byte flips confined to the
44-byte header; a head/tail block-swap splice; a 200000-byte zeroed block
mid-payload; a whole-payload zero-fill; a `data` chunk size field forced to
0xFFFFFFF0; and a sample-rate field forced to 1.

```
ID:            A-CLAIM-012
PASS:          A
SEVERITY:      informational
LOAD-BEARING:  yes
SOURCE:        file:line  |  independent test run, not from the repository's
               own test suite
IMPLEMENTED:   core/wav.cpp, core/fax.cpp (onset/error-throwing paths)
VERDICT:       CONFORMS to Nova's own no-crash/no-hang/bounded-memory design
               goal
EVIDENCE:      All 17 variants completed in <=1 s wall time. Peak RSS was
               bounded at ~8.5 MB across every case that reached the decode
               path (matching the ~960 KB-2.5 MB input file sizes with
               normal working-set overhead, not the file's nominal or
               declared size) and ~0.9-1.9 MB for cases refused early (rate/
               header rejections). No case hung against the 60 s alarm. No
               case crashed (no SIGSEGV/SIGABRT/nonzero-signal exit observed
               in any /usr/bin/time -l report). Every rejected case produced
               a specific, non-generic error string identifying which check
               fired (e.g. "not a RIFF file," "implausible WAV sample rate
               (1 Hz...)," "decode_fax: recording too short," "decode_fax:
               no fax line comb found (fill or no signal)").
NOTES:         This corroborates test_malformed.cpp's claims via an
               independent, end-to-end route (full nova-decode binary
               against real audio payload data, not synthetic headers) and
               finds nothing test_malformed.cpp did not already predict.
               I consider this a genuinely independent confirmation, not a
               restatement of the existing suite, since it exercises the
               decode pipeline itself rather than only the WAV/resample
               boundary.
```

The one genuinely new finding from independent testing is behavioural, not
a crash:

```
ID:            A-CLAIM-013
PASS:          A
SEVERITY:      major
LOAD-BEARING:  yes
SOURCE:        file:line  |  independent test, cross-referenced against
               docs/01's own claim about white-only stations
IMPLEMENTED:   core/fax.cpp (phasing-anchor fallback path, per fax.hpp:105-
               115's documented absence of any per-line white-only sync
               template)
VERDICT:       CONFORMS to the letter of Nova's documented design ("no
               per-line sync for a white-only station" is stated and
               accepted behaviour), but the OUTCOME is a silent,
               un-flagged loss of line-phase accuracy that a caller could
               easily miss
EVIDENCE:      I decoded `vmw-phasing-image-160s.wav` unmodified, obtaining
               `phasing 7.00-36.49 s ... anchor delta +1153.5 smp vs image
               (PHASING anchor used)`. I then zeroed exactly that audio
               region (bytes corresponding to 7.00-36.49 s) in a copy of
               the WAV's PCM payload — simulating a jammed, dropped-out, or
               otherwise corrupted/missing phasing block on a station that,
               per docs/01's own measurement, has this as "the ONLY place
               its line phase exists" (VMW/NMC/GYA are white-only dead-
               sector stations with zero per-line sync in the image body).
               Re-decoding the modified file produced: `phasing none`,
               `dead=white(0.92) no-per-line-sync`, `timebase not
               measurable`, `image whole recording (no control signals to
               segment on)` — exit code 0, a PGM image WAS written, no
               error, no exception. The measured clock also shifted from
               -69.6 ppm (clean) to +163.5 ppm (phasing zeroed), consistent
               with the coarse autocorrelation estimator now integrating
               over a silent region it was not measuring before.
               Under docs/01's own stated logic, a white-only station with
               no recoverable phasing interval has NO source of line-start
               phase anywhere in the signal, so the resulting picture is
               very likely phase-rotated (the VMW clean-signal case
               documents a 520-px rotation when the phasing anchor is
               unavailable and the image anchor is used instead) — but
               nova-decode's output gives no indication that this happened
               beyond the terse "phasing none" / "no-per-line-sync" status
               line, which a scripted or GUI caller not specifically
               checking those two fields would not surface to an operator
               as a warning. It is not marked as a lower-confidence or
               degraded decode in any structured way (no warning-level flag
               distinct from the ordinary "this station just doesn't send
               a sync pulse" case, which is NORMAL and expected for these
               same three stations under healthy conditions).
NOTES:         This is not a crash and not technically "wrong" against
               Nova's own documented contract (the fields that WOULD tell a
               careful reader are present: phasing_found=false,
               per_line_sync=false). I rate it major rather than critical
               because (a) it requires an adversarial or accidental
               corruption of exactly the phasing region on exactly a
               white-only station to manifest, a narrower precondition than
               generic noise, and (b) the diagnostic fields to detect it
               programmatically do exist, just are not elevated to a
               distinct warning. What would change the verdict: a distinct
               "phase reference unavailable, no line-start basis at all"
               warning/exit code (as opposed to the normal, healthy
               "no-per-line-sync, phasing anchor used instead" case), or a
               documented recommendation that callers treat
               `!phasing_found && !per_line_sync` as a hard degraded-
               output signal rather than an incidental combination of two
               independently-true flags. No existing fixture or ctest
               suite exercises this exact combination — see below.
```

```
ID:            A-GAP-004
PASS:          A
QUESTION:      Do the SHA-256 hashes recorded in fixtures/MANIFEST.md
               actually match the bytes of the 19 WAV files present on this
               machine?
BLOCKED BY:    I did not re-hash the 19 files against MANIFEST.md's table
               during this pass (out of scope of A.1-A.3's required checks
               as I read the protocol, and re-hashing ~30 MB of audio was
               not necessary to answer any A.1/A.2/A.3 question). I ran
               ctest against whatever bytes are present, which passed, but
               that is a functional check, not an identity check.
RESOLVES IF:   `shasum -a 256 fixtures/*.wav` is compared line-by-line
               against MANIFEST.md by a pass that has reason to care about
               fixture provenance specifically (this may already be a
               different pass's job under the audit protocol — I don't
               know, since I was not shown docs/06 or the other passes'
               scope).
```

---

## A.4 — Tier 3 compatibility notes (compatibility appendix — NOT a conformance input)

I read the file listing of `Weather Fax Receiver Manuals/` (16 PDFs) but,
given the size of A.1-A.3's evidentiary burden and the protocol's explicit
instruction that "no conformance verdict may cite Tier 3," I prioritized
depth on Tier 1/Tier 2 conformance and independent fixture/corruption
testing over an exhaustive 16-manual survey. What follows is a narrow,
honestly-scoped appendix rather than a full survey.

```
ID:            A-GAP-005
PASS:          A
QUESTION:      What documented receiver behaviours in the 16 Tier 3 manuals
               go beyond what WMO 386 / ISO 9876 specify (e.g. proprietary
               auto-gain schemes, specific AGC time constants, vendor error
               recovery heuristics, or de facto extensions like non-
               standard IOC/rate combinations some receivers accept)?
BLOCKED BY:    Time allocated in this pass to Tier 1 conformance depth and
               independent A.3 corrupted-input testing, which the protocol
               marks as "one of the most valuable things this pass
               produces" and which surfaced a real finding (A-CLAIM-013).
               I did not open the 16 manuals' full text.
RESOLVES IF:   A dedicated Tier-3 survey pass (or a follow-up on this one)
               opens each manual, extracts documented behaviours WMO/ISO
               are silent on, and appends them here — paraphrased, per the
               copyright rule, never transcribed.
```

No conformance verdict in this report cites Tier 3 material. None of the
A.1/A.2 table rows above reference the manuals directory.

---

## Pass summary

### Findings by severity

- **critical: 0**
- **major: 5** — A-CLAIM-003 (240 lpm out of WMO §5.1.5's four-value set,
  declared), A-CLAIM-007's citation-precision note (WMO §5.5.2 mis-cited for
  an ISO-only figure), A-CLAIM-011a (ISO §4.2.5's "detection of line
  synchronization" method deviated to spectral purity, declared),
  A-CLAIM-013 (silent degraded output when phasing is lost on a white-only
  station)
- **minor: 1** — A-CLAIM-009 (no WMO/ISO aspect-ratio clause found; not a
  Nova defect, a documentation-completeness note)
- **informational: 6** — A-CLAIM-001, -002, -004, -005, -006, -008, -010,
  -012 (IOC/start/phasing/stop/gray-scale/out-of-spec-handling conformance
  confirmations, and the corroborating independent fuzz run)

### Load-bearing findings: 10 of 13 total findings (A-CLAIM-001 through -013)

### Gaps: 5 — A-GAP-002 (no real IOC-288 fixture), A-GAP-003 (no real 90 lpm
fixture), A-GAP-004 (fixture SHA-256 not independently re-verified),
A-GAP-005 (Tier 3 manuals not surveyed in depth)

Note: I numbered gaps 002-005 above; there is no A-GAP-001 in this report
(I referenced a hypothetical "A-GAP-001" inline in the stop-tone finding's
NOTES as a placeholder while drafting and then folded that specific
uncertainty into prose rather than a standalone gap, since it did not meet
the bar of "could not be determined" — the jmh-sample two-transmission
claim is outside the 19 registered fixtures and I did not attempt to
verify it, which is a scope limitation stated in that finding's NOTES, not
an unresolved question with a specific closing artifact).

### A.1 table, consolidated

| # | Family | WMO clause | ISO clause | Verdict |
|---|---|---|---|---|
| 1 | IOC 576/288 | §5.1.2 | §4.2.3 | CONFORMS |
| 2 | Line rate 60/90/120 (not 240) | §5.1.5 | §4.2.4 | DEVIATES from WMO (declared, 240 lpm), CONFORMS to ISO |
| 3 | Start/IOC-select tone | §5.2.2 | §3.5 | CONFORMS |
| 4 | Phasing signal | §5.2.3.1-.4 | §3.8 | CONFORMS |
| 5 | Stop tone | §5.2.5 | §3.13 | CONFORMS |
| 6 | Carrier/deviation/tone assignment | §5.3.1.2, §5.5.1, §5.5.2 | §4.2.2 | CONFORMS (one imprecise clause citation flagged, A-CLAIM-007) |
| 7 | Aspect ratio / samples-per-line / drift | §5.1.1 (no numeric aspect ratio found) | — | drift handling CONFORMS; aspect-ratio clause GAP (none exists to conform to) |
| 8 | Out-of-spec/truncated/noisy input | not specified by WMO/ISO | — | CONFORMS to Nova's own stated goal; independently verified, one behavioural finding (A-CLAIM-013) |
| — | Control-tone detection METHOD | — | §4.2.5 ("detection of line synchronization") | DEVIATES (declared, spectral-purity method) |
| — | AM modulation variant | §5.3.1.1 | — | NOT-IMPLEMENTED (declared out of scope; no code path exists) |

### CONFORMS verdicts with no fixture exercising them (untested-conformance flags)

- **IOC 288 (real signal)** — A-CLAIM-001/row §4.2.3: CONFORMS is backed only
  by synthetic tests (roundtrip [4], test_tones.cpp's 675 Hz synthetic
  case). No real off-air 675 Hz start tone or IOC-288 picture exists in the
  19-file fixture set. See A-GAP-002.
- **90 lpm (real signal)** — A-CLAIM-003/row §4.2.4: CONFORMS is backed only
  by the synthetic matrix. No real 90 lpm recording exists in the fixture
  set. See A-GAP-003.
- **§4.2.2 amplitude/deviation tolerance, real-signal LF (±150 Hz) case** —
  the ±150 Hz LF deviation mode is exercised by roundtrip [6] (synthetic
  only, per docs/02's own "synthetic ✓" marking, which I confirm is
  accurate); I found no fixture using --dev 150 against a real recording.
- **§4.2.6 manual phase/sync override, end-to-end on the exact scenario
  docs/02 cites as its strongest evidence** ("one operator PHASE click lands
  the page to within 1 px of the batch image... measured on vmw-white-
  sector-120s") — this specific claimed 1-px figure is NOT one of the
  numeric bounds asserted in any `add_test` in CMakeLists.txt (I checked
  override_phase_seed's and override_sync_fallback's actual registered
  bounds, which test the mechanism's byte-identical-above/moved-below
  behaviour, not this specific 1-px number against this specific fixture).
  The 1-px figure appears to be a one-off measurement reported in prose,
  not a regression-locked claim. This is exactly the kind of gap the
  protocol asks this pass to surface.
- **The phasing-loss-on-white-only scenario itself (A-CLAIM-013)** — no
  fixture or ctest suite exercises "phasing region corrupted/absent on a
  white-only station" at all; I found this gap only by constructing the
  test independently, and no bound in CMakeLists.txt would catch a
  regression here.

### What this pass did NOT cover

- A full clause-by-clause survey of the 16 Tier 3 receiver manuals (see
  A-GAP-005) — only their existence and directory listing were confirmed.
- Independent re-hashing of the 19 fixture WAV files against
  `fixtures/MANIFEST.md`'s recorded SHA-256 values (A-GAP-004).
- GUI-path (nova-gui) interactive/visual verification — I ran the `gui_
  layout` and `gui_shell` ctest suites (both passed) but did not launch or
  screenshot the GUI itself; that is outside a signal-conformance pass's
  remit as I read the protocol.
- Live/streaming-path (`live/` directory) behaviour under corrupted or
  adversarial REAL-TIME input specifically (I tested only the batch
  `nova-decode` CLI path against corrupted files; `live_engine`, `live_
  session`, `live_preview`, `live_tones` all passed in ctest, which covers
  their own registered synthetic/fixture assertions, but I did not
  independently fuzz the live audio-ring ingestion path the way I did the
  WAV file path).
- Passes B/C/D or whatever else this protocol's other passes cover — I saw
  none of their output and made no attempt to duplicate or reconcile with
  work outside Pass A's stated scope.
- Performance/timing characteristics beyond the corrupted-input wall-clock/
  memory bound (no throughput, latency, or real-time-factor measurement was
  attempted).

### Fixture set reproducibility statement

**The 19 WAV recordings in `nova/fixtures/` are present on this auditor's
machine but are NOT part of the git repository and are NOT redistributed
with it** (confirmed by `.gitignore`'s explicit `*.wav` / `recordings/`
exclusion, and by `fixtures/MANIFEST.md`'s own statement: "The recordings
themselves are not in this repository, and never will be"). Every A.3
result in this report that depends on those 19 files — the 38/38 ctest
pass count, the fixture-coverage table, and the independent corrupted-input
tests built from `test-chart-jmh-kiwisdr-image-60s.wav` and
`vmw-phasing-image-160s.wav` — is **not reproducible from a clean git
clone** of this repository. A clone would build and run only the 8
fixture-independent synthetic suites (roundtrip, malformed, hooks, ruler_
mapping, png_roundtrip, live_ring, gui_layout, and tones — the last of
which is synthetic despite its name; the three `tones_fixture_*` suites and
all `fixture_*`/`live_*`/`override_*` suites would report SKIPPED with the
reason string, per `CMakeLists.txt`'s `nova_fixture_test()` mechanism,
which I read and independently confirm behaves as documented).
