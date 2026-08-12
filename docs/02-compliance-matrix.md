# 02 — Compliance matrix: ISO 9876:2015, software-applicable requirements

Nova's acceptance checklist. One row per ISO 9876:2015 clause that a
*software decoder* can satisfy; each row gets a test (screamer) or an
explicit waiver. Wording: Nova is "designed to satisfy" these — no
certified-compliance claim (no type testing).

Clauses deliberately out of scope (hardware-only, recorded here so the
omission is a decision, not an oversight): §4.1 construction; §4.2.1
continuous *paper* recording; §4.2.8 recording size/marking; §4.3
radio-receiver RF performance (sensitivity, selectivity, IF/image
rejection); §4.4 IEC 60945 environmental; §6 marking; §7 crew
information.

| Clause | Requirement (paraphrase) | Software translation | Test | Status |
|---|---|---|---|---|
| §4.2.2 | Accept AF input −10…+10 dBm, shifts ±150 and/or ±400 Hz about 1900 Hz | Wide usable input-level range (AGC/normalization); both deviation modes supported | synthetic: both deviations decode (roundtrip [6]); level-range test pending | partial |
| §4.2.3 | IOC 576 and 288, automatic or manual | IOC auto from 300/675 Hz tone + manual override | synthetic decode both IOCs (roundtrip [4]); auto-from-tone pending (M3) | partial |
| §4.2.4 | Scan speeds 60/90/120 spm, automatic and manual | lpm auto from phasing rate + manual override | synthetic 3 rates, auto-detected (roundtrip [1][4][5]) | synthetic ✓ |
| §4.2.5 | Auto respond to 300/675 Hz start (via line-sync detection) and 450 Hz stop | tone detectors + state machine; hysteresis; false-start rejection | pending (M3) | pending |
| §4.2.6 | Sync accuracy ±2×10⁻⁶, stability ±2×10⁻⁵; phasing automatic with manual adjustment | clock-rate from a median line-period fit over pairs of locked lines an eighth of the recording apart, segmented at every step in sync position; per-line dead-sector template lock with whole-line re-acquisition after a run of misses; manual phase nudge pending GUI | synthetic +100 ppm corrected (roundtrip [2]); −137 ppm recovered as −137.00 (roundtrip [8]); residual shear of the decoded image ≤10 ppm on all 20 library recordings, measured against the picture, not the metric (session 5); `fixture_warp` re-acquires across a stream time-skip (max_step 5.2 px) | synthetic+fixture ✓, manual adj. pending |
| §4.2.6 (cont.) | …on stations that send no sync pulse | Dead-sector style measured per recording [WMO §5.1.3.3]; white-only stations decode on the measured clock with **no** per-line lock — the sector carries no phase information (session 4 measurement, docs/01 §5). The clock they ride on is measured by folded-block phase drift, which needs no locks at all (session 5) | `fixture_white_sector` (VMW): style detected, zero locks claimed. `fixture_weak_white` (GYA, weak *and* white-only): clock bound is the picture screamer, and fails at −51.6 ppm if the fold is removed. Roundtrip [7]: white-only generated at +250 ppm, decoded +250.0, zero locks | met, with the limit stated |
| §4.2.7 | Pitch of scanning trace within ±25% | line rate held by design (resampled) | trivially met; assertion in line assembly | met |
| §5.4.2 | Recorder produces chart identical to transmitted for all IOC/speed combos | decode synthetic full matrix {288,576}×{60,90,120} | 576/120, 288/60, 576/90 covered; full matrix pending | partial |

Notes:
- §4.2.4 lists 60/90/120 only — this is why 240 lpm is out of scope.
- §4.2.2's "±150 Hz and/or ±400 Hz" is why LF deviation is a
  first-class mode, not an afterthought.
- WMO §5.2.3.2's two phasing waveforms (50/50 and 5/95) are covered
  under the §4.2.6 phasing row.
- WMO §5.1.3.3 makes the dead sector's black pulse *permitted*, not
  required. Five of twenty library recordings send none. That is a
  property of the signal, not a decoder shortfall, and §4.2.6's sync
  figures are met for those stations by clock accuracy alone — which,
  as of session 5, is measured rather than asserted: their residual image
  shear is ≤10 ppm, and two recordings of the same station agree on the
  clock to ~1 ppm with nothing in the code enforcing it.
- §4.2.6's accuracy figure is a *receiver* specification. Nova's
  synthetic round-trip meets it outright (−137.00 against −137); on real
  recordings the honest statement is the residual shear bound above,
  because no ground-truth clock exists for an off-air recording.
