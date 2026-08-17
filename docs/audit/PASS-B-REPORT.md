# Pass B — Provenance and Licensing (B-COPYLEFT)

```
PASS:          B (variant B-COPYLEFT)
AUDITOR:       Claude Sonnet 5, 2026-08-16, fresh subagent, no authoring context
INPUTS:        Nova tree at git HEAD 067bafdae9e37ed50c82ad6a2ab30eef6230c399
               (2026-08-16 21:00:01 -0500, 66 commits, branch inspected with
               `git log`/`git show`/`git log --all --diff-filter=A`).
               prior-art-corpus/CORPUS-MANIFEST.md as committed 2026-08-16.
               Read in full: NOTICE, LICENSE (head/tail), .gitignore,
               CMakeLists.txt (GUI-gating section, lines ~495-583),
               core/demod.cpp, core/resample.cpp, core/tones.cpp,
               core/phasing.cpp (partial), README.md (full grep pass).
               Read for attribution-site sampling: core/fax.hpp, core/fax.cpp
               (grep + targeted excerpts), core/tones.hpp, core/phasing.hpp,
               live/preview.cpp, tests/test_fixture.cpp, gui/nova-gui.cpp
               (About text).
               Corpus read: acfax/mod_demod.c (full), acfax/COPYING;
               hamfax/src/FaxDemodulator.cpp (full), hamfax COPYING,
               hamfax/src/*.{cpp,hpp} headers (grepped for copyright years);
               weatherfax_pi/src/FaxDecoder.cpp (partial + median()),
               weatherfax_pi LICENSE; Beagle_SDR_GPS/extensions/FAX/
               FaxDecoder.{cpp,h} (partial, incl. UpdateSampleRate);
               fldigi/src/wefax/{wefax.cxx,wefax-pic.cxx,wefax_map.cxx}
               headers + decode_phasing() body + ACfax-derived filter table;
               fldigi/src/include/filters.h header; fldigi root COPYING;
               JWX_source.tar.bz2 extracted and read (JWX.java,
               GoertzelFilter.java, DecodeFax.java headers).
               Isobar: located and read LICENSE + git author log +
               settingsdialog.cpp at ../isobar-dev.zip (extracted to
               /tmp/nova-audit-b/isobar; NOTE the manifest's stated relative
               path was one directory off — see B-GAP-002).
               System-installed FLTK 1.4.5 and RtAudio 6.0.1 COPYING/LICENSE
               files under /opt/homebrew/Cellar, read in full, to check
               NOTICE's characterization against the actual license text
               rather than the corpus (neither library is prior-art-corpus
               content; both are linked runtime dependencies).
```

## Summary of method

For every project named in Gate 0, I read the license grant as printed in
that project's own file headers (not the corpus README, not NOTICE's
characterization, not any website), quoted below. I then compared Nova's
implementation at the highest-probability derivation sites (FIR/resampler
kernels, phasing/sync state machines, tone-detection outlier rejection)
against the corresponding corpus code to test whether NOTICE's "idea-level,
no code taken" claim holds, and followed the fldigi→HamFax→ACfax and
weatherfax_pi→KiwiSDR chains explicitly.

---

## 1. Upstream licence facts, from headers

```
ID:            B-CLAIM-001
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        acfax/mod_demod.c:1-20 (and acfax/COPYING:1-7)
IMPLEMENTED:   n/a (upstream fact-finding)
VERDICT:       CONFORMS
EVIDENCE:      "This program is free software; you can redistribute it
               and/or modify it under the terms of the GNU General Public
               License as published by the Free Software Foundation;
               either version 2 of the License, or (at your option) any
               later version." Copyright (C) 1995-1998 Andreas
               Czechanowski, DL4SDC. -> GPLv2-or-later.
NOTES:         Matches NOTICE's "ACfax ... GPLv2+" claim exactly, including
               the 1995-1998 date range.
```

```
ID:            B-CLAIM-002
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        hamfax/src/FaxDemodulator.cpp:1-17 (and 27 other hamfax/src
               files sampled by grep for "Copyright (C)")
IMPLEMENTED:   n/a
VERDICT:       CONFORMS (with a 1-year date discrepancy, see NOTES)
EVIDENCE:      "This program is free software; you can redistribute it
               and/or modify it under the terms of the GNU General Public
               License as published by the Free Software Foundation;
               either version 2 of the License, or (at your option) any
               later version." Copyright (C) 2001,2002 Christof Schmitt,
               DH1CS. -> GPLv2-or-later. hamfax/src/File.cpp carries the
               latest year found in the tree: "Copyright (C) 2001, 2002,
               2012".
NOTES:         NOTICE states "Christof Schmitt (DH1CS), 2001–2011" for
               HamFax; the latest copyright year actually found in headers
               is 2012, not 2011. One-year discrepancy, not licence-
               affecting, not upgraded in severity.
```

```
ID:            B-CLAIM-003
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        weatherfax_pi/src/FaxDecoder.cpp:1-25, weatherfax_pi/LICENSE:1-4
IMPLEMENTED:   n/a
VERDICT:       CONFORMS
EVIDENCE:      "This program is free software; you can redistribute it
               and/or modify it under the terms of the GNU General Public
               License as published by the Free Software Foundation;
               either version 3 of the License, or (at your option) any
               later version." Copyright (C) 2015 by Sean D'Epagnier.
               -> GPLv3-or-later. Sampled across all 10 weatherfax_pi/src/*.cpp
               top-level files (DecoderOptionsDialog.cpp, AboutDialog.cpp,
               FaxDecoder.cpp, InternetRetrievalDialog.cpp, WeatherFax.cpp,
               SchedulesDialog.cpp, WeatherFaxImage.cpp, WeatherFaxWizard.cpp,
               weatherfax_pi.cpp, wximgkap.cpp) — all identical grant.
NOTES:         Matches NOTICE exactly.
```

```
ID:            B-CLAIM-004
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        Beagle_SDR_GPS/extensions/FAX/FaxDecoder.cpp:1-25,
               FaxDecoder.h:12-13
IMPLEMENTED:   n/a
VERDICT:       CONFORMS
EVIDENCE:      Identical block to weatherfax_pi's (the file is a direct fork:
               "// adapted from github.com/seandepagnier/weatherfax_pi",
               FaxDecoder.cpp line 27): "either version 3 of the License, or
               (at your option) any later version." -> GPLv3-or-later.
NOTES:         NOTICE calls this "file-level GPLv3" rather than
               "GPLv3-or-later"; the header text is unambiguously the
               "or later" grant, so NOTICE's phrasing is imprecise but not
               wrong in the direction that matters (a narrower grant would
               have been the dangerous imprecision; this is the safer
               direction).
```

```
ID:            B-CLAIM-005
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        /tmp/nova-audit-b/jwx/src/jwx/{JWX,GoertzelFilter,DecodeFax}.java,
               headers (all three read in full)
IMPLEMENTED:   n/a
VERDICT:       CONFORMS
EVIDENCE:      "Copyright (C) 2011 by Paul Lutus ... either version 2 of the
               License, or (at your option) any later version." ->
               GPLv2-or-later. No standalone LICENSE/COPYING file ships in
               the JWX tarball; the grant is stated per-file only.
NOTES:         NOTICE's "2011–2013" range could not be corroborated beyond
               2011 — every header found says 2011. This is consistent with
               B-GAP-001 (JWX provenance is the weak link in the corpus per
               the manifest; I have no later-year JWX file to check).
```

```
ID:            B-CLAIM-006 — fldigi, the case the protocol specifically flagged
PASS:          B
SEVERITY:      major
LOAD-BEARING:  yes
SOURCE:        fldigi/COPYING:1-2 (project-level, GPLv3 text);
               fldigi/src/wefax/wefax.cxx:1-23 (GPLv2-or-later);
               fldigi/src/wefax/wefax-pic.cxx:1-22 (GPLv2-or-later);
               fldigi/src/wefax/wefax_map.cxx:1-16 ("version 4" — see below);
               fldigi/src/include/filters.h:1-24 (GPLv3-or-later)
IMPLEMENTED:   n/a
VERDICT:       CONFORMS overall, but the file-level grant is genuinely mixed
EVIDENCE:      fldigi's root COPYING is the plain GPLv3 licence text (no
               "or later" language — that is a property of file headers,
               not COPYING). But the actual WEFAX-module files carry
               DIFFERENT grants from each other:
                 - wefax.cxx / wefax-pic.cxx: "either version 2 of the
                   License, or (at your option) any later version" ->
                   GPLv2-or-later. Both explicitly state:
                   "Adapted from code contained in HAMFAX source code
                   distribution. Hamfax Copyright (C) Christof Schmitt".
                 - filters.h (the generic FIR filter class WEFAX's own
                   filter table sits inside): "either version 3 of the
                   License, or (at your option) any later version" ->
                   GPLv3-or-later.
                 - wefax_map.cxx: "either version 4 of the License, or (at
                   your option) any later version" — this appears to be a
                   drafting error in fldigi's own header (there is no GPL
                   v4); read literally it is not GPLv2 and not GPLv3
                   specifically, but any later-version language attached to
                   a nonexistent version 4 does not create a v2-only trap
                   either. Not exercised by any Nova derivation site found.
NOTES:         This CONFIRMS the protocol's warning that fldigi is
               "documented variously as GPLv2+ and GPLv3 in different
               places" — true even within the single WEFAX module. NOTICE's
               single-line claim "fldigi ... GPLv3+" glosses over this;
               the two files fldigi itself says are adapted from HamFax
               (wefax.cxx, wefax-pic.cxx) are actually GPLv2-or-later, not
               GPLv3+. This does not create an incompatibility with Nova's
               GPL-3.0-or-later (GPLv2-or-later is upgradable), but NOTICE's
               characterization is inaccurate for the specific files in the
               derivation chain, which is why this is `major` rather than
               `informational`: attribution/licence bookkeeping should name
               the grant actually printed in the file it draws from.
```

```
ID:            B-CLAIM-007 — Isobar (own prior work)
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        /tmp/nova-audit-b/isobar/kgfax/LICENSE:1-2 (GPLv3 text);
               kgfax git log (author); gui/settingsdialog.cpp:170
IMPLEMENTED:   n/a
VERDICT:       CONFORMS
EVIDENCE:      LICENSE is the GPLv3 text. `git log --format="%an <%ae>"`
               over the Isobar working copy's own .git history returns a
               single author, `skgsara <skgsara@riseup.net>`. Its own UI
               source literally labels itself `"GPL v3+"`
               (gui/settingsdialog.cpp:170). This corroborates the
               manifest's assertion that Isobar is the project author's own
               prior work rather than taking that assertion on faith.
NOTES:         The corpus manifest's stated relative path
               (`../isobar-dev.zip` from prior-art-corpus/) does not
               resolve to where the files actually are; they are one
               directory further up, at
               `.../isobar-nova/../isobar-dev.zip` i.e.
               `.../2026 Amateur Radio/isobar-dev.zip`. See B-GAP-002.
```

---

## 2. Derivation chain to origin

```
ID:            B-RISK-001
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        hamfax/src/FaxDemodulator.cpp:37-45  |  acfax/mod_demod.c:32-34
IMPLEMENTED:   n/a (chain-of-title finding, not a Nova site)
VERDICT:       CONFORMS
EVIDENCE:      HamFax literally: "// Narrow, middle and wide fir low pass
               filter from ACfax" followed by a 3x17 coefficient table
               whose values (e.g. `-7,-18,-15,11,56,116,177,223,240,...`)
               are the symmetric expansion of ACfax's 9-value
               `firnarrow[]={-7,-18,-15,11,56,116,177,223,240}`
               (mod_demod.c:34). This is a literal, acknowledged carry of
               ACfax's coefficient values into HamFax, confirming the
               manifest's claim and the origin of the "FIR table" hazard
               the protocol asked me to check for.
NOTES:         Both files are GPLv2-or-later (B-CLAIM-001, B-CLAIM-002), so
               even a literal carry-forward here between the two upstream
               projects is licence-compatible with itself. The question
               that matters is whether Nova carries these SAME values —
               see B-RISK-003 below (it does not).
```

```
ID:            B-RISK-002
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        fldigi/src/wefax/wefax.cxx:88-105 (attributed "from ACfax");
               fldigi/src/wefax/wefax.cxx:1-9 ("Adapted from code contained
               in HAMFAX source code distribution")
IMPLEMENTED:   n/a
VERDICT:       CONFORMS (chain confirmed, not identical to either upstream
               table)
EVIDENCE:      fldigi's `input_filters[]` "Narrow" entry is a 65-tap
               floating-point table explicitly commented
               "// Narrow, middle and wide fir low pass filter from ACfax"
               (wefax.cxx:88), but the actual coefficients
               (0.000495, 0.000684, ...) are NOT the ACfax integer table
               values scaled — this is a re-derived/expanded filter that
               keeps the attribution comment but is numerically its own
               design. fldigi's file header separately credits HamFax as
               the code it adapted structurally.
NOTES:         Confirms both legs of the manifest's stated chain
               (fldigi -> HamFax structurally, fldigi -> ACfax for filter
               provenance framing) from fldigi's own comments, independent
               of anything in NOTICE.
```

```
ID:            B-RISK-003 — KiwiSDR FAX extension chain to weatherfax_pi
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        Beagle_SDR_GPS/extensions/FAX/FaxDecoder.cpp:27
IMPLEMENTED:   n/a
VERDICT:       CONFORMS
EVIDENCE:      "// adapted from github.com/seandepagnier/weatherfax_pi"
               (explicit, in-file). The KiwiSDR file additionally extends
               weatherfax_pi's plain `median()` (weatherfax_pi/src/
               FaxDecoder.cpp:230-234, used at line 457) with an explicit
               10%/90% distribution-width outlier rejection
               (Beagle_SDR_GPS/extensions/FAX/FaxDecoder.cpp:236-243:
               "Filter that out by looking at the 10%/90% distribution
               width of the phasing data").
NOTES:         This matters for attribution precision: Nova's NOTICE and
               in-file comments cite "weatherfax_pi/KiwiSDR" jointly for
               the "median-with-spread-rejection" and "10-90% spread"
               ideas (core/tones.cpp:24, core/phasing.hpp:15). The plain
               median is weatherfax_pi's; the 10/90 spread-rejection
               specifically is KiwiSDR's addition on top of it. The joint
               citation is not wrong, just coarser than the actual
               chain — informational, not load-bearing.
```

---

## 3 & 4. Derivation sites in Nova + version-pin compatibility

For every high-probability site (FIR/resampler kernels, sync/phasing state
machines, tone outlier rejection) I read both the Nova implementation and
the corresponding corpus code side by side.

```
ID:            B-RISK-004 — FIR lowpass in the FM discriminator
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        acfax/mod_demod.c:32-34,143-155 (fixed integer coefficient
               tables, hand-tuned, 9 values / filter, selected by name);
               hamfax/src/FaxDemodulator.cpp:37-45 (same tables, 17-tap
               symmetric form); fldigi/src/wefax/wefax.cxx:88-160
               (65-tap floating tables, "from ACfax" but re-derived values)
IMPLEMENTED:   core/demod.cpp:20-40 (`make_lowpass`), 64-92 (`fm_demod`)
VERDICT:       CONFORMS to NOTICE's claim ("ACFax's FIR coefficient tables
               are NOT used; Nova computes its FIR at runtime")
EVIDENCE:      Nova's `make_lowpass()` computes a windowed-sinc lowpass
               (sin(x)/x times a Blackman window) at runtime for a
               caller-supplied cutoff and a fixed 63-tap length, entirely
               from closed-form trigonometric expressions — no table of
               literal numeric coefficients appears anywhere in demod.cpp
               or demod.hpp. This is a different algorithm family from all
               three upstreams (ACfax: 9 hand-picked integer coefficients
               per named filter, exploiting quadrature-mixer symmetry to
               halve multiplies; HamFax: literal copy of ACfax's values;
               fldigi: a larger hand-computed floating table, also
               attributed but numerically independent). No shared
               constants found between core/demod.cpp and any of the three
               upstream tables.
NOTES:         This is the single most important test of NOTICE's central
               claim, and it holds up on direct comparison, not just on
               NOTICE's say-so.
```

```
ID:            B-RISK-005 — Resampler kernel
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        Beagle_SDR_GPS/extensions/FAX/FaxDecoder.cpp:92-97
               (`UpdateSampleRate` — fractional-ratio tracking feeding the
               demodulator's phase increment directly, NOT an interpolation
               kernel)
IMPLEMENTED:   core/resample.cpp:1-65 (`resample_ratio`)
VERDICT:       CONFORMS to NOTICE's claim ("Fractional sample-rate tracking
               was surveyed but NOT taken")
EVIDENCE:      KiwiSDR tracks a fractional sample-rate ratio and folds it
               into the demodulator's running phase, with no interpolating
               filter kernel at all. Nova's resample.cpp is a genuine
               offline windowed-sinc (Blackman-windowed) interpolator with
               a configurable zero-crossing count — a different technique
               entirely, applied at a different point in the pipeline
               (pre-demod resampling to a fixed working rate, not phase
               tracking during demod). No structural or numeric overlap
               found. I did not locate a comparable resampler in
               weatherfax_pi or JWX to check against (JWX is Java/Swing
               desktop code without a comparable resampling path found in
               the files sampled) — see B-GAP-003.
```

```
ID:            B-RISK-006 — Sync/phasing state machine
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        fldigi/src/wefax/wefax.cxx:1656-1745 (`decode_phasing`, a
               threshold-crossing accumulate-and-count state machine using
               member variables `m_curr_phase_high/low/len`,
               `m_phase_lines`, a hardcoded 0.4/0.04/0.94 threshold set,
               and a moving-average pre-filter over 16 samples)
IMPLEMENTED:   core/phasing.cpp:1-120+ (`wedge_fit`, `smooth_local`,
               `unwrap_about`, `spread_10_90`)
VERDICT:       CONFORMS to NOTICE's claim ("Rule shapes reused with
               re-measured constants ... No code taken")
EVIDENCE:      fldigi's algorithm is a level-crossing counter with fixed
               thresholds (188/68 pixel levels, 0.4/0.04/0.94 fractional
               thresholds) that accumulates black/white run lengths across
               calls and decides a phasing line when the run fits an
               expected black-fraction envelope. Nova's phasing.cpp instead
               does a prefix-sum best-fit search for a wedge-shaped white
               run against the rest of the line (`wedge_fit`), with a
               separate local-median smoothing pass over neighbouring LINES
               (not samples) and explicit circular unwrapping
               (`unwrap_about`) for positions that wrap the line period.
               No shared variable names, no shared thresholds, no shared
               data structures. The only shared idea is "reject outliers
               via median/spread," which is attributed in-file
               (core/phasing.hpp:15, :86, :146; core/fax.cpp:77,196-201,293).
NOTES:         Genuinely a different implementation. This is the site the
               protocol flagged as highest-risk for a hidden literal port,
               and on inspection it is not one.
```

```
ID:            B-RISK-007 — Tone/IOC purity detection
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        /tmp/nova-audit-b/jwx/src/jwx/GoertzelFilter.java (Goertzel
               tone detector, cited in core/tones.hpp:6-7 as JWX's approach)
IMPLEMENTED:   core/tones.cpp:42-118 (`tone_purity`, `tone_purity_band`)
VERDICT:       CONFORMS
EVIDENCE:      Both use a Goertzel filter for narrowband tone energy — a
               standard DSP technique, not something JWX invented — but
               Nova's version adds a Hann-windowed normalizer that makes a
               pure sinusoid read exactly 1.0 (core/tones.cpp:70-76,
               explicitly commented as a derived formula, not copied), plus
               a parabolic-interpolation frequency refinement
               (tones.cpp:102-115) that has no counterpart found in
               GoertzelFilter.java. In-file attribution present
               (tones.hpp:6-7).
```

### Version-pin compatibility matrix

| Upstream | File(s) examined | Licence grant per header | vs GPL-3.0-or-later |
|---|---|---|---|
| ACfax | mod_demod.c | GPLv2-or-later | Compatible |
| HamFax | FaxDemodulator.cpp (+ 27 files sampled) | GPLv2-or-later | Compatible |
| weatherfax_pi | FaxDecoder.cpp (+ 9 files sampled) | GPLv3-or-later | Compatible |
| KiwiSDR FAX ext | FaxDecoder.cpp/.h | GPLv3-or-later | Compatible |
| JWX | JWX.java, GoertzelFilter.java, DecodeFax.java | GPLv2-or-later | Compatible |
| fldigi — wefax.cxx, wefax-pic.cxx | (WEFAX core, adapted from HamFax) | GPLv2-or-later | Compatible |
| fldigi — filters.h | (generic FIR class WEFAX's filter sits in) | GPLv3-or-later | Compatible |
| fldigi — wefax_map.cxx | (not used by any Nova site found) | "version 4" (drafting anomaly) | Not exercised; not a v2-only trap either way |
| Isobar | LICENSE, settingsdialog.cpp | GPLv3(+ implied by "GPL v3+" label) | Same author, same licence family — trivially compatible |
| FLTK (runtime dep, `nova-gui` only) | Cellar COPYING (system-installed 1.4.5) | LGPLv2 + FLTK static-link/subclass exceptions | Compatible (exception explicitly permits static link without relicensing) |
| RtAudio (runtime dep, `nova-gui` only) | Cellar LICENSE (system-installed 6.0.1) | MIT-style permissive | Compatible |

**No GPLv2-only (i.e., lacking "or later") grant was found in any file I
read across the derivation-chain sources actually cited by Nova's NOTICE or
in-file comments.** I did find, via an automated corpus-wide grep, files
that matched a naive "version 2 without any later version" pattern —
all of them were in the KiwiSDR repository's unrelated **DRM extension**
(`Beagle_SDR_GPS/extensions/DRM/dream/...`, digital radio mondiale
decoder, ~180 files), which Nova does not draw from at all. On manual
inspection of one such file (`DRM_main.h`), the grep was a **false
positive**: the header does say "any later version," but the phrase is
line-wrapped across a `* ` comment continuation and the automated
substring match missed it. I did not exhaustively re-verify all ~180 DRM
files by eye since they are outside Nova's derivation chain (see
B-GAP-004), but the one spot-check found no genuine GPLv2-only file, in
DRM or elsewhere in the corpus.

```
ID:            B-RISK-008 — no GPLv2-only incompatibility found (the critical question the protocol asked)
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  yes (this is the direct answer to the protocol's central
               concern, even though the answer is negative)
SOURCE:        see matrix above
IMPLEMENTED:   core/demod.cpp, core/resample.cpp, core/phasing.cpp,
               core/tones.cpp, core/fax.cpp
VERDICT:       CONFORMS — no incompatible upstream grant found at any
               confirmed derivation site
EVIDENCE:      Every file in the actual derivation chain (ACfax, HamFax,
               the two fldigi WEFAX files, weatherfax_pi, the KiwiSDR FAX
               extension, JWX, Isobar) carries an "or (at your option) any
               later version" clause. GPL-3.0-or-later publication of
               Nova's re-implementations is therefore not blocked by any
               upstream licence term I was able to read.
NOTES:         This finding would flip to `critical` if a future
               contribution imports code from fldigi's DRM-adjacent or
               other GPLv2-only-headed modules (confirmed to exist in the
               KiwiSDR tree, just not in the FAX extension), or from any
               upstream file I did not personally read. The gap register
               below records exactly what was and was not checked.
```

---

## 5. In-file attribution

```
ID:            B-RISK-009
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        NOTICE:33-75
IMPLEMENTED:   core/fax.hpp:13; core/tones.cpp:24; core/tones.hpp:6-7,62,93-94;
               core/phasing.hpp:15,54,86,146; core/fax.cpp:77,196-201,293,
               1042,1267-1270,1334,1405-1410,1567,1701,1721,1746,1849,1852,
               1870; tests/test_fixture.cpp:7,26-27,31; tests/test_roundtrip.cpp:588;
               live/preview.cpp:191; gui/nova-gui.cpp:2326
VERDICT:       CONFORMS
EVIDENCE:      Every project named in NOTICE (ACfax, HamFax, weatherfax_pi,
               KiwiSDR, JWX, fldigi) has at least one, and generally many,
               in-file comments at the sites that draw on it, naming the
               origin project by name (grep sweep above; 40+ distinct
               attribution comments found across core/ and tests/). This
               is unusually thorough in-file attribution hygiene compared
               to typical GPL projects.
NOTES:         NOTICE itself enumerates all six third-party projects plus
               Isobar with named copyright holders and licences
               (NOTICE:34-75). One nuance: NOTICE's fldigi entry says
               "David Freese (W1HKJ) et al." without naming Remi
               Chateauneu, F4ECW, who is the primary/sole copyright holder
               on the two WEFAX files fldigi itself says came from HamFax
               (wefax.cxx:1-4, wefax-pic.cxx:1-4 both list Chateauneu
               first, Freese only on wefax.cxx as a co-author). "et al."
               is a defensible umbrella but does not name the person whose
               header is actually on the file. Not raised to `major`
               because NOTICE's own text signals it is not a closed list
               ("et al.") and invites correction ("If you believe
               attribution is missing or incorrect, please open an
               issue" — NOTICE:78-80).
```

---

## 6. Third-party dependencies

```
ID:            B-RISK-010
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        /opt/homebrew/Cellar/fltk/1.4.5/COPYING (system-installed,
               matches the version CMakeLists.txt:508 names);
               /opt/homebrew/Cellar/rtaudio/6.0.1/LICENSE (matches
               CMakeLists.txt:508)
IMPLEMENTED:   NOTICE:89-96; CMakeLists.txt:501-583
VERDICT:       CONFORMS
EVIDENCE:      FLTK COPYING item 2: "Widgets that are subclassed from FLTK
               widgets do not constitute a derivative work." Item 3:
               "Static linking of applications and widgets to the FLTK
               library does not constitute a derivative work and does not
               require the author to provide source code for the
               application or widget..." — this is exactly what NOTICE
               claims (NOTICE:90-93). RtAudio LICENSE is a standard
               MIT-style grant with a non-binding courtesy request to
               upstream modifications — matches NOTICE's "MIT-style
               permissive licence" (NOTICE:95-96) precisely.
               Build enforcement: CMakeLists.txt:501-583 gates ALL FLTK/
               RtAudio flags and the `nova-gui` executable itself inside
               `if(NOVA_BUILD_GUI)`, and inside that, behind
               `find_program(NOVA_FLTK_CONFIG fltk-config)` /
               `pkg_check_modules(... rtaudio)` checks that print
               "SKIPPED" and continue rather than failing the configure
               step if either tool is absent (CMakeLists.txt:519-523). No
               other target in the 34K-line CMakeLists.txt references FLTK
               or RtAudio flags (grepped).
NOTES:         The FLTK exception's item 4 asks that programs "still
               identify their use of FLTK" via a suggested credit string.
               NOTICE names FLTK and links fltk.org (NOTICE:89-90), which
               satisfies the substance of that ask even though it does not
               use FLTK's suggested exact wording. Not a compliance gap.
```

---

## B.0 — baseline checks

```
ID:            B-RISK-011 — SPDX headers
PASS:          B
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        n/a
IMPLEMENTED:   entire nova/ tree (53 .c/.cpp/.h/.hpp files under core/,
               cli/, gui/, live/, tests/, excluding build/)
VERDICT:       NOT-IMPLEMENTED
EVIDENCE:      `grep -rl "SPDX-License-Identifier"` across every .cpp/.h/
               .hpp/.c file in the tree (excluding build/) returns zero
               hits. LICENSE (root, GPLv3 text) is present and matches
               LICENSE_DECISION's licence family. Only gui/nova-gui.cpp
               contains an in-source licence statement at all (the About
               dialog's embedded string, GPLv3-or-later, gui/nova-gui.cpp:
               588-608), and that is UI text, not a header comment on the
               file itself. No file in the tree carries a "Copyright ...
               licensed under ..." header of its own.
NOTES:         Because no file carries an SPDX header, there is nothing to
               be inconsistent with — vacuously "consistent." But this
               also means the project relies entirely on NOTICE + LICENSE
               (project-level) rather than per-file declarations to
               establish that Nova's own source is GPL-3.0-or-later. This
               is common practice and not unusual for small projects, but
               it is a real gap in machine-checkable licensing hygiene,
               especially given how much of this project's provenance
               story depends on being able to point at specific files.
               Severity kept at `minor`: no incompatibility results from
               the absence, just reduced auditability.
```

```
ID:            B-RISK-012 — reference documents not committed
PASS:          B
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        .gitignore:19-27; `git log --all --diff-filter=A -- '*.pdf'`
IMPLEMENTED:   n/a
VERDICT:       CONFORMS
EVIDENCE:      `git log --all --diff-filter=A -- '*.pdf'` returns no
               commits — no PDF has ever been added to the repository at
               any point in its 66-commit history, on any ref. .gitignore
               explicitly excludes `recordings/`, `*.m4a`, `*.mp3`,
               `*.flac`, `*.pdf`, `*.tar.bz2` with the comment "Raw
               recordings and licensed standards NEVER enter this repo."
               WMO-No. 386 and ISO 9876:2015 PDFs exist on disk one
               directory above nova/ (386_2023-edition_en.pdf,
               ISO_9876_2015(en).pdf, WMO_386_Vol_I_2009_en.pdf) and were
               never inside the git working tree at all (they sit in
               isobar-nova/, the parent of isobar-nova/nova/, which is the
               git root).
NOTES:         Clean result.
```

```
ID:            B-RISK-013 — fixture redistribution rights (the other candidate for critical severity)
PASS:          B
SEVERITY:      critical
LOAD-BEARING:  yes
SOURCE:        fixtures/*.wav (19 files); tests/test_fixture.cpp:35
               ("kyodo-news-jsc1-60lpm-120s.wav — JSC1 (Kyodo News
               newspaper fax)"); `git ls-files fixtures/`
IMPLEMENTED:   fixtures/kyodo-news-jsc1-60lpm-120s.wav,
               fixtures/kyodo-news-jsc2-steps-120s.wav (and, more broadly,
               all 19 files in fixtures/)
VERDICT:       DEVIATES
EVIDENCE:      `git ls-files fixtures/` confirms all 19 WAV excerpts are
               tracked in git and committed to the public repository (the
               .gitignore's recordings/*.m4a/*.pdf exclusions do not cover
               fixtures/*.wav — they are deliberately exempted, per the
               .gitignore's own comment: "only trimmed WAV excerpts under
               fixtures/ are committed"). I searched docs/00-05, NOTICE,
               README.md, and the fixtures/ directory itself for any
               statement of the copyright status or redistribution basis
               of the underlying off-air transmissions (public-domain
               government-work rationale, an explicit licence, a fair-use
               argument, permission obtained, etc.) and found NONE. There
               is no fixtures/README, no per-file provenance/rights note,
               and no discussion of this question anywhere in the tree.
               tests/test_fixture.cpp:35 itself identifies
               kyodo-news-jsc1/2 as excerpts of "Kyodo News newspaper fax"
               — Kyodo News is a private commercial news agency, not a
               national meteorological service, so the public-domain/
               government-work rationale that might apply to a station
               like NMC (identifiable in the tree as a NOAA/US-government
               call sign by name only, again with no rights statement
               anywhere) does not apply to JSC at all on the facts visible
               in this tree.
NOTES:         Per the protocol: "Unresolvable cases go to the gap
               register; fixtures without a redistribution basis must not
               ship in the repo." All 19 fixtures currently ship with zero
               documented basis, which fails that bar outright — this is
               not a case where I can resolve some and gap others, because
               NONE carry any rationale in-tree. JSC1/JSC2 are the clearest
               case of an entity (a commercial newspaper) for which no
               plausible public-domain argument exists on the facts
               available. The other 17 (JMH/Japan, VMW/Australia,
               XSG/China, GYA/France, NMC/USA, HLL/Korea — government
               meteorological services where a public-domain-government-
               work argument is at least conceivable for some
               jurisdictions) are not resolved either, because the tree
               documents no basis for any of them, and I am not in a
               position to research each national service's copyright
               regime as part of this pass — see B-GAP-005. This is graded
               `critical` and `LOAD-BEARING: yes` because it is
               independent of the GPL-compatibility question entirely: it
               is direct copyright exposure on committed, redistributed
               binary content (audio recordings of copyrighted or
               unclear-status broadcasts), not a code-licence compatibility
               question at all.
```

```
ID:            B-RISK-014 — AI authorship disclosure
PASS:          B
SEVERITY:      major
LOAD-BEARING:  no
SOURCE:        README.md (full text, 346 lines)
IMPLEMENTED:   README.md
VERDICT:       NOT-IMPLEMENTED
EVIDENCE:      Grepped README.md, AGENTS.md, and ROADMAP.md for "AI",
               "Claude", "Anthropic", "large language model", "LLM",
               "written by", "authored", "artificial intelligence" and
               variants. README.md contains no disclosure that the code
               was produced with AI assistance, no mention of an audit
               protocol, and no link to any audit output. The only
               README.md content resembling process history is repeated
               references to numbered "sessions" (e.g. "Since session 11,
               Nova does repair it"), which a reader unfamiliar with this
               project's development process would not recognize as an
               AI-authorship signal. AGENTS.md and SESSION-LOG.md (not
               required to be README-visible per the protocol) do make the
               AI-agent development process explicit, but the protocol's
               requirement is specifically that disclosure live in the
               README with the audit protocol and its outputs linked.
NOTES:         Graded `major` rather than `critical`: this is a disclosure/
               transparency requirement, not a licence-compatibility or
               copyright-infringement exposure — it does not carry the
               same legal risk profile as B-RISK-013. It is squarely a
               required B.0 check that fails as stated. Would flip to
               CONFORMS if README.md gained an AI-authorship statement
               with a working link to the audit protocol/outputs.
```

---

## Gap register

```
ID:            B-GAP-001
PASS:          B
QUESTION:      Is JWX's provenance (the tarball itself, not the code inside
               it) trustworthy enough to rely on for licence-compatibility
               conclusions?
BLOCKED BY:    The corpus manifest itself flags this as unresolved: the
               JWX tarball was already on disk rather than fetched from a
               named upstream, so "its provenance is asserted rather than
               established" (CORPUS-MANIFEST.md:42-46). I read the
               in-tarball file headers (all consistently GPLv2-or-later,
               Paul Lutus, 2011) and they are internally consistent, but I
               have no independent confirmation this tarball matches an
               authentic JWX release.
RESOLVES IF:   The manifest's own suggested remedy is applied: re-fetch
               JWX from a named, checksummed upstream (e.g. the
               arachnoid.com distribution point named in the file headers'
               author email) and diff against this tarball.
```

```
ID:            B-GAP-002
PASS:          B
QUESTION:      Why does the corpus manifest's stated relative path for
               Isobar's sources (`../isobar-dev.zip`, `../isobar-analysis/`,
               `../Isobar-1-8-0.app`, relative to prior-art-corpus/) not
               resolve to where those files actually are (one directory
               further up, in the parent of isobar-nova/)?
BLOCKED BY:    Nothing blocks resolution — I found and read the actual
               files at /Users/sakuragawasara/Documents/2026/2026 Amateur
               Radio/{isobar-dev.zip,isobar-analysis,Isobar-1-8-0.app} and
               confirmed the Isobar licence/authorship claim independently
               (B-CLAIM-007). This is flagged only because the manifest's
               path pointer was wrong and a less thorough pass could have
               reported this as unverifiable when it is not.
RESOLVES IF:   Manifest path corrected; not otherwise load-bearing.
```

```
ID:            B-GAP-003
PASS:          B
QUESTION:      Does weatherfax_pi or JWX contain a resampling/interpolation
               kernel comparable to core/resample.cpp that I did not find?
BLOCKED BY:    Time/scope — I read weatherfax_pi/src/FaxDecoder.cpp and the
               KiwiSDR FAX extension's rate-tracking code, but did not do a
               full-text search of weatherfax_pi's ~30 other source files
               or JWX's AudioProcessor.java/AudioInputReader.java for a
               dedicated resampling routine.
RESOLVES IF:   A targeted grep of weatherfax_pi/src/*.cpp and
               jwx/src/jwx/Audio*.java for "resample"/"interpolat"/"sinc"
               and a read of any hits.
```

```
ID:            B-GAP-004
PASS:          B
QUESTION:      Does the KiwiSDR repository's DRM (Digital Radio Mondiale)
               extension contain any true GPLv2-only (no "or later")
               files, and if so, could any of them plausibly have
               influenced Nova's DSP code even though Nova's own
               attribution comments never mention DRM or the `dream`
               codebase?
BLOCKED BY:    An automated grep flagged ~180 files in
               Beagle_SDR_GPS/extensions/DRM/dream/ as GPLv2-only
               candidates; manual inspection of one (DRM_main.h) showed
               this was a false positive caused by the licence text
               wrapping "any later version" across a comment-continuation
               line boundary that a single-line substring match missed. I
               did not manually re-verify the other ~179 files.
RESOLVES IF:   A whitespace-normalizing check (collapse comment prefixes
               and newlines before matching) run across the DRM directory.
               Not currently load-bearing because no Nova attribution
               comment or code site references the DRM extension at all —
               this gap only matters if that changes.
```

```
ID:            B-GAP-005 — the fixture redistribution question, properly unresolvable from this tree
PASS:          B
QUESTION:      For each of the 19 committed fixtures, what is the actual
               copyright status of the underlying off-air transmission,
               and is there a legitimate basis (public-domain government
               work, an explicit permission, a fair-use/fair-dealing
               argument, or something else) for redistributing a 40-160
               second excerpt of it in a public git repository?
BLOCKED BY:    This tree documents no rights-clearance process for any
               fixture. Determining the actual copyright regime governing
               each issuing service (JMA/Japan for JMH, Bureau of
               Meteorology/Australia for VMW, CMA/China for XSG,
               Météo-France for GYA, NOAA/USA for NMC, KMA/Korea for HLL,
               and Kyodo News — a private company — for JSC) requires
               jurisdiction-by-jurisdiction copyright research (e.g.
               whether each country's government-works doctrine extends to
               broadcast meteorological facsimiles, whether short-excerpt
               fair use/fair dealing applies, whether any of these
               services has published terms covering amateur radio
               reception and republication) that is out of scope for a
               code-provenance audit pass and that I have not attempted.
RESOLVES IF:   Per-fixture, either (a) an explicit rights basis is
               researched and documented (e.g. "NOAA/NMC: US federal
               government work, 17 U.S.C. §105, public domain" — with a
               citation, not an assertion) and added to the tree, or (b)
               the fixture is removed from the repository per the
               protocol's own instruction ("fixtures without a
               redistribution basis must not ship in the repo"). JSC1/JSC2
               are the fixtures least likely to resolve to (a) given
               Kyodo News's status as a commercial entity.
```

---

## Pass summary

**Findings by severity:**
- critical: 1 (B-RISK-013 — fixture redistribution, all 19 fixtures undocumented, JSC1/JSC2 the clearest failure)
- major: 2 (B-CLAIM-006 — fldigi's mixed GPLv2+/GPLv3+ headers misdescribed by NOTICE as flatly "GPLv3+"; B-RISK-014 — no AI-authorship disclosure in README)
- minor: 1 (B-RISK-011 — no SPDX headers anywhere in the tree)
- informational: 15 (B-CLAIM-001 through 005, 007; B-RISK-001 through 010, 012)

**Load-bearing findings: 2** (B-RISK-013, B-RISK-008 — the latter load-bearing as a negative result: it is the direct, evidence-based answer to the protocol's central GPLv2-only-incompatibility concern, and the answer is that no such incompatibility was found at any confirmed derivation site.)

**Gaps: 5** (B-GAP-001 through 005)

**Version-pin compatibility matrix:**

| Upstream | Grant (from header) | vs GPL-3.0-or-later |
|---|---|---|
| ACfax | GPLv2-or-later | Compatible |
| HamFax | GPLv2-or-later | Compatible |
| weatherfax_pi | GPLv3-or-later | Compatible |
| KiwiSDR FAX extension | GPLv3-or-later | Compatible |
| JWX | GPLv2-or-later | Compatible |
| fldigi (wefax.cxx, wefax-pic.cxx) | GPLv2-or-later | Compatible |
| fldigi (filters.h) | GPLv3-or-later | Compatible |
| Isobar | GPLv3(+) | Compatible (same author) |
| FLTK (linked, nova-gui only) | LGPLv2 + FLTK exceptions | Compatible |
| RtAudio (linked, nova-gui only) | MIT-style | Compatible |

**What this pass did NOT cover:**
- I did not exhaustively read every file in any corpus project — HamFax
  (191-commit history, only headers grepped + one file read in full),
  weatherfax_pi (10 of ~35 src files' headers checked, one algorithm file
  read), fldigi (65MB tree; WEFAX module + filters.h read, ~180 DRM
  extension files only spot-checked once), KiwiSDR (FAX extension read,
  the much larger surrounding SDR codebase not touched at all beyond one
  grep). A literal code copy sitting in a file I never opened would not
  have been caught.
- I did not attempt the jurisdiction-by-jurisdiction copyright research
  needed to actually resolve fixture redistribution rights (B-GAP-005) —
  I can confirm the tree documents no basis, not what the true basis is
  or isn't for any given national meteorological service.
- I did not verify JWX's tarball provenance against a live upstream
  (B-GAP-001); the corpus manifest itself flags this as unresolved and I
  did not have live network access to re-fetch it.
- I did not check the HamFax TX-path/IOC-scaling claim (NOTICE's "the TX
  path exists there alone in the lineage") against Nova's code in detail —
  Nova's TX path (core/gen.cpp) was read but not compared line-by-line
  against HamFax's FaxModulator.cpp.
- I did not run any tool to detect near-duplicate/fuzzy code matches
  (e.g. a diffing or clone-detection tool) across the full corpus and the
  full Nova tree; all comparisons were manual, targeted reads guided by
  Nova's own attribution comments and the protocol's named high-risk
  categories (filter tables, sync state machines, resamplers).
- **I cannot certify the provenance of AI-generated output, and
  training-data provenance is not introspectable by me.** Everything
  above is a comparison of two pieces of text I could read (Nova's source,
  the corpus's source); it is not, and cannot be, a guarantee that the
  model(s) that produced Nova's code were not influenced by memorized
  training data resembling any of these projects, fldigi, or any other
  GPL codebase not in this corpus at all. This pass raises the confidence
  floor on the specific claims NOTICE makes; it does not, and structurally
  cannot, produce a clean bill of health.
