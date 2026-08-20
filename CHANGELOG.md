# Changelog

Nova has not been released. There are no version tags, and
`CMakeLists.txt` still carries `VERSION 0.0.0` — deliberately, because a
version number is a release decision and no release has been made.

Until then this file records what changed and when, newest first. The full
narrative lives in `SESSION-LOG.md`; this is the short form.

## Unreleased

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
