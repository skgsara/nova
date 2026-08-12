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
| §4.2.2 | Accept AF input −10…+10 dBm, shifts ±150 and/or ±400 Hz about 1900 Hz | Wide usable input-level range (AGC/normalization); both deviation modes supported | synthetic: decode at min/max level, both deviations | pending |
| §4.2.3 | IOC 576 and 288, automatic or manual | IOC auto from 300/675 Hz tone + manual override | synthetic both IOCs; fixture if found | pending |
| §4.2.4 | Scan speeds 60/90/120 spm, automatic and manual | lpm auto from phasing rate + manual override | synthetic 3 rates | pending |
| §4.2.5 | Auto respond to 300/675 Hz start (via line-sync detection) and 450 Hz stop | tone detectors + state machine; hysteresis; false-start rejection | synthetic + noisy fixture; no false start on text-heavy image | pending |
| §4.2.6 | Sync accuracy ±2×10⁻⁶, stability ±2×10⁻⁵; phasing automatic with manual adjustment | clock-rate estimate from phasing + per-line dead-sector relock; manual phase nudge in GUI | long fixture decodes straight; injected clock error corrected | pending |
| §4.2.7 | Pitch of scanning trace within ±25% | line rate held by design (resampled) | trivially met; assertion in line assembly | pending |
| §5.4.2 | Recorder produces chart identical to transmitted for all IOC/speed combos | decode synthetic full matrix {288,576}×{60,90,120} | generator→decode round-trip, all 6 combos | pending |

Notes:
- §4.2.4 lists 60/90/120 only — this is why 240 lpm is out of scope.
- §4.2.2's "±150 Hz and/or ±400 Hz" is why LF deviation is a
  first-class mode, not an afterthought.
- WMO §5.2.3.2's two phasing waveforms (50/50 and 5/95) are covered
  under the §4.2.6 phasing row.
