# Changelog

Nova has not been released. There are no version tags, and
`CMakeLists.txt` still carries `VERSION 0.0.0` — deliberately, because a
version number is a release decision and no release has been made.

Until then this file records what changed and when, newest first. The full
narrative lives in `SESSION-LOG.md`; this is the short form.

## Unreleased

### 2026-08-16 — audited against `docs/07-audit-protocol.md`

All five passes run by an agent that did not author the code, under a
different model. Reports in `docs/audit/`. Cross-verification by a third
model is **still outstanding** — see `docs/08-cross-verification-handover.md`.

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
