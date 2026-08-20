# Changelog

Nova has not been released: there is no tag, because making one is the
release decision itself. Since session 35 it does carry a version —
milestone-based in the project's own M0-M4 language, reported by every
binary's `--version`.

This file records what changed and when, newest first. The full narrative
lives in `SESSION-LOG.md`; this is the short form.

## Unreleased — 0.4.5

### 2026-08-20 — M4.5: the tuning strip

- **The spectrum/waterfall display, cut from M4 on 2026-08-13 and parked
  in M4.5, is built.** M4.5 is complete; it was the last milestone before
  M5 (packaging & release). A waterfall with the instantaneous spectrum
  along its top edge, over **800–3000 Hz**, marking the two WMO tones at
  1500 Hz black and 2300 Hz white [WMO §5.2.1] — 72 px directly above the
  level meter, extending the full-width meter strip rather than taking
  sidebar space. Band, contents and placement decided by Sara, session 36.
- **`View → Tuning strip`** toggles it, default on, remembered in prefs
  beside the device and the image folder. Hiding it returns all 72 px to
  the picture and moves nothing else; the window does not resize.
- **It reads the RAW capture audio, before the resampler**, and it is live
  from the moment the window opens rather than from Start. Both are the
  point rather than details: a spectrum taken after the demodulator shows
  the tuning error already removed, and tuning is what an operator does
  *before* a transmission arrives.
- **New module `live/spectrum.{hpp,cpp}`** — Hann-windowed 4096-point FFT,
  one column per 50 ms, dB scale referenced so a full-scale sine reads
  0 dB. Dependency-free, in `nova-live` rather than in the GUI, because a
  column that names the wrong frequency is a thing that can be wrong about
  a signal. The band mapping is a pair of free functions, so the widget's
  marker lines and the analyser's columns are one implementation and not
  two — the mistake `live/ruler.hpp` exists to prevent.
- **New suite `tuning_spectrum`** (40 total, 38 without the GUI;
  fixture-free, so it runs from a bare clone). 33 checks, stated in Hz
  rather than pixels: a generated tone peaks where the mapping says at
  nine frequencies across the band and names its frequency to within
  0.75 of an FFT bin; 44100 and 48000 put the same tone in the same
  column; both WMO tones read as two peaks with a valley between; the dB
  scale is checked as a difference between two levels off the clamp; an
  out-of-band tone leaves the band clean; the waterfall scrolls, wraps and
  reports nothing-measured as nothing-measured rather than as silence.
- **Wiring and layout screamers too**: `live_engine`'s `test_tuning_strip`
  pins that the strip is fed raw audio (a 2300 Hz tone captured at 48 kHz
  where the internal rate is 8 kHz) and that it fills with no capture
  started; `gui_layout` pins the region, the toggle's 72 px, and the
  marker columns (81 and 174) at every window size.
- **17 mutations, all killed** — 10 against the DSP, 7 against the layout
  — each attributed to the check it was aimed at, with unmutated baselines
  surviving both passes. Two findings from the pass itself: the Hz
  tolerance was tightened from 1.5 bins to 0.75 after two mutations
  survived the looser one, and one bundled layout assertion was split into
  three so a kill names the rule it actually broke.
- **`SpectrumAnalyzer::reset()` was written and then deleted**: a device
  change destroys the engine, so it had no caller. A principled method
  defended only by a test that calls it is not evidence.
- **Version 0.4.0 → 0.4.5**, mechanically: the scheme is milestone-based
  and M4.5 is the milestone that is now complete. Still below 1.0 —
  nothing in Platforms or Security posture has changed.
- CI inventory gates updated **and re-measured** on real fixture-less
  builds rather than carried forward: 40 registered / 10 ran / 30 skipped,
  and 38 / 9 / 29 with `NOVA_BUILD_GUI=OFF`. The check was again seen to
  FAIL when given the old numbers.
- **Not looked at by a person.** The strip has only been driven headlessly.
  It joins session 31's two by-hand GUI runs as a release blocker, and it
  is the surface where that gap matters most, since its entire job is to
  be looked at.

## Earlier in 0.4.0

### 2026-08-20 — a version, a `--version`, and CI that has never run

- **`project(nova VERSION 0.4.0)`** [E-GAP-001, closed]. The version
  reaches the code through one generated header (`core/version.hpp.in` →
  `<nova/version.hpp>`), so CMakeLists.txt is the only place it is
  written. `nova-decode`, `nova-gen`, `nova-tones`, `nova-preview` and
  `nova-gui` all answer `--version`, ahead of their argument-count checks.
- **New suite `version_flag`** (39 total, 37 without the GUI). It compares
  what each binary PRINTS with what CMake was told, and fails if a tool
  hardcodes a version, names a different tool, or lacks the flag — three
  mutations, each killed by the intended check. It also refuses to pass
  with fewer than four tools checked, because a loop over an empty list is
  green by default.
- **`.github/workflows/ci.yml`** — build and test on macOS arm64 and Linux
  x86-64, with and without the GUI. **It has never executed: there is no
  remote.** Verified by hand instead against a fixture-less build, which
  is what a runner sees: 39 registered, 9 ran, 30 skipped (37/8/29 with
  `NOVA_BUILD_GUI=OFF`), and the check seen to fail on wrong numbers.
- **`tools/check-suite-inventory.sh`** — CI does not read ctest's exit
  code alone. Without recordings 30 suites skip and ctest still prints
  "100% tests passed", so the gate asserts the registered and skipped
  counts and a vanished suite fails instead of passing as a smaller green
  run.
- **`.github/workflows/release.yml` + `tools/record-fixture-regression.sh`**
  [E-GAP-002, partial]. The 30 fixture-gated suites cannot run on a public
  runner, so a `v*` tag is gated on a record of a full-suite run of the
  same CODE — the recorded commit an ancestor of the tag with nothing but
  the record differing, since committing the record moves HEAD — written
  by a script that refuses a dirty tree, a fixture-less machine, any
  failure, or any skip. The first record is in the tree: 39/39, nothing
  skipped. The tag is also
  required to match the declared version.
- Suite counts brought level in `CMakeLists.txt`, `START-HERE.md`,
  `README.md` and `ROADMAP.md` (38 → 39); ROADMAP's blocker list corrected
  — item 12 still said Pass C was unaddressed, which session 34 had
  already closed.
- README gained a **Version** section and a **Continuous integration**
  section, both stating what is and is not established; `--version` is
  documented beside `--devices` and `--metrics`.
  `docs/audit/HUMAN-SIGNOFF.md` gained a dated appendix recording where
  its five listed blockers now stand — appended, not edited, because the
  gate record is signed and dated.

### 2026-08-20 — Pass C remediated: all 16 minor findings, all 3 gaps

Maintainability only; zero behaviour change, no test changes, suite
green (38/38; 36/36 with `-DNOVA_BUILD_GUI=OFF`) at every commit.

- Every function in `core/`, `live/`, `cli/`, `gui/` now meets Gate 0's
  MAX_FUNCTION_LINES = 80 and MAX_NESTING_DEPTH = 4 — including
  `stage_assembly` (416 → 64) and the GUI's `main` (397 → 68) — verified
  by a whole-tree scan, which also caught one straggler the refactor
  itself missed (`print_result`).
- `core/constants.hpp` holds `kPi` and `blackman()` (were duplicated
  five and two times). `kInternalRate` deduplicated into
  `cli/internal_rate.hpp`. The bare `1900.0` subcarrier literals now
  cite `[WMO §5.5.1]`; the 63-tap FIR and `zero_crossings = 16` say what
  they are.
- `core/fax.cpp`'s header now signposts where the per-line sync lock
  lives (C-MAINT-018, by signpost not file split — Sara's call).
- Gaps closed: every session number cited in code resolves against
  `SESSION-LOG.md` (C-GAP-002); the GUI-less build is verified and now
  announces `nova-gui: SKIPPED - NOVA_BUILD_GUI=OFF` (C-GAP-003);
  `gui/nova-gui.cpp` read end-to-end, nothing further found (C-GAP-004).
- Full per-finding mapping appended to `docs/audit/PASS-C-REPORT.md`.

### 2026-08-19 — cross-verified, sign-off gate complete

Cross-verification run by a third model from a different vendor (Kimi,
Moonshot AI): all 10 clause-citing load-bearing findings verified against
WMO-No. 386 (2023) and ISO 9876:2015, zero citation failures
(`docs/audit/CROSS-VERIFICATION-REPORT.md`). The human sign-off gate is
complete: every Gate 0 value, all 33 load-bearing findings, the entire
gap register, all of Pass B, every README conformance claim, and the
cross-verification report, signed by Sara (`docs/audit/HUMAN-SIGNOFF.md`).

**Fixed — unbounded live-session memory (D-PERF-003, the audit's last
open critical):**
- `SessionOptions::max_opening_sec` (300 s). A start tone that never ends
  met none of the existing bounds, so the session sat in START TONE with
  the retained store growing without limit. The opening is now abandoned
  and the session returns to listening. Pinned by `live_session` T14.
- Gate 0's last open values filled: TARGET_FLOOR_RAM = 2 GB and
  REALTIME_BUDGET = ≤25% of one core (measured against JSC4's 61-minute
  chart: 553 MB peak, ~1.1% of one core); the 16 receiver-manual
  revisions recorded.

### 2026-08-16 — audited against `docs/07-audit-protocol.md`

All five passes run by an agent that did not author the code, under a
different model. Reports in `docs/audit/`.

**Fixed — untrusted input (all three found by audit Pass D):**
- `read_wav` clamped the declared data-chunk size to the bytes that exist.
  A 144-byte file declaring 4 GB previously reached ~12.9 GB of footprint
  and did not terminate.
- `read_wav` now refuses a sample rate that cannot carry WEFAX. A
  *well-formed* file declaring 1 Hz previously hung the resampler.
- `resample_ratio` bounds its ratio independently of the file path.
- New suite `tests/test_malformed.cpp`, which generates its own inputs and
  therefore runs from any checkout.

**Fixed — silent degraded output (audit Pass A):**
- `DecodeResult::no_phase_reference`. A white-only station that loses its
  phasing interval has no line-start phase anywhere; Nova drew a picture
  rotated by ~1091 px of 1810 and said nothing distinguishable from a
  healthy decode. `nova-decode` now warns loudly; `roundtrip [16]` pins
  that the flag separates that case from the healthy one.

**Fixed — provenance and claims:**
- The 19 off-air fixture recordings were removed from the repository and
  from all git history. They had been tracked with no stated
  redistribution basis, and one station is a commercial news agency
  [audit Pass B, critical]. `fixtures/MANIFEST.md` carries their identity,
  provenance and SHA-256 instead; the bounds remain in `CMakeLists.txt`.
  **30 of 38 test suites now report Skipped from a clean checkout.**
- `NOTICE` named the wrong licence for fldigi. The two files in Nova's
  lineage grant GPLv2-or-later, not the project's headline GPLv3.
- `docs/01` cited WMO §5.5.2 for an audio-domain claim; that clause
  specifies RF-carrier FSK about f₀.
- README: AI-authorship disclosure added; platform claims corrected to
  macOS arm64 only; 240 lpm scope stated; the unimplemented m4a/ffmpeg
  input claim corrected; a security-posture section added.

**Known open at the close of the audit:**
- `D-PERF-003` — unbounded live-session retention on a sustained tone.
  Confirmed by code reading, **not reproduced**.
- No CI of any kind.
- No version, no tags, no release notes.
- Pass C's 20 maintainability findings, unaddressed by choice.
- Two by-hand runs outstanding since session 31.
