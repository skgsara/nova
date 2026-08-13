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
| §4.2.2 | Accept AF input −10…+10 dBm, shifts ±150 and/or ±400 Hz about 1900 Hz | Wide usable input-level range by an amplitude-normalized discriminator; both deviation modes supported and selectable | synthetic: both deviations decode and a clean LF signal reports a linear timebase (`roundtrip [6]`); amplitude sweep (`roundtrip [11]`); CLI `--dev 150|400` | synthetic ✓ |
| §4.2.3 | IOC 576 and 288, automatic or manual | Both; the clause is OR. Manual `--ioc`; automatic selection from the 300/675 Hz start tone when IOC is not overridden | synthetic full-rate matrix with auto IOC (`roundtrip [4]`) | synthetic ✓ (real 675 Hz fixture remains a registered gap) |
| §4.2.4 | Scan speeds 60/90/120 spm, automatic and manual | lpm auto-detected by the line comb; manual `--lpm` | synthetic 3 rates, auto-detected (`roundtrip [1][4]`) | synthetic ✓ (real 90 lpm fixture remains a registered gap) |
| §4.2.5 | Auto respond to 300/675 Hz start and IOC selection, and to 450 Hz stop | Purity-based detection on demodulated video (`core/tones.cpp`): Goertzel power in the tone's own bin as a fraction of the window's AC power, run coherence by 10–90% frequency spread. This deliberately deviates from the clause's line-sync method because purity is stronger against false starts: library content ≤0.16, tones 0.68–0.99. The detected start kind now drives automatic IOC selection | `tones` [1][2][3] start 576/288 and stop, each within the ±1% of WMO §5.2.6; [4][5] no event from picture content; [6] survives noise. `tones_fixture_vmw`: real 300 Hz start in an off-air recording. `tones_fixture_no_false_start`: zero events on 120 s of real newspaper text. **Sequencing (session 7):** tone events bound the drawn picture; `roundtrip [9]` asserts row 0 is image line 0. `roundtrip [4]` asserts IOC selection from the tone | ✓ (675 Hz real-signal fixture remains a registered gap) |
| §4.2.3 / §4.2.4 (auto) | IOC and scan speed selected automatically | IOC from 300 vs 675 Hz start tone; lpm from the line comb, with phasing-rate recovery cross-checked separately [WMO §5.2.3.1] | `roundtrip [4]` covers {288,576}×{60,90,120} with automatic selection; `tones` [7] recovers 60/90/120 from phasing alone; [8] accepts the symmetric waveform [WMO §5.2.3.2]; `tones_fixture_vmw` recovers 120 lpm from a real phasing interval | synthetic+fixture ✓ |
| §4.2.6 | Sync accuracy ±2×10⁻⁶, stability ±2×10⁻⁵; phasing automatic with manual adjustment | clock-rate from a median line-period fit over pairs of locked lines an eighth of the recording apart, segmented at every step in sync position; per-line dead-sector template lock with whole-line re-acquisition after a run of misses; manual phase nudge pending GUI | synthetic +100 ppm corrected (roundtrip [2]); −137 ppm recovered as −137.00 (roundtrip [8]); residual shear of the decoded image ≤10 ppm on all 20 library recordings, measured against the picture, not the metric (session 5); `fixture_warp` re-acquires across a stream time-skip (max_step 5.2 px). ISO §5.4.3 also makes manual phase adjustment a test item | synthetic+fixture ✓, manual adj. pending M4 |
| §4.2.6 (cont.) | …on stations that send no sync pulse | Dead-sector style measured per recording [WMO §5.1.3.3]; white-only stations decode on the measured clock with **no** per-line lock — the sector carries no phase information (session 4 measurement, docs/01 §5). The clock they ride on is measured by folded-block phase drift, which needs no locks at all (session 5) | `fixture_white_sector` (VMW): style detected, zero locks claimed. `fixture_weak_white` (GYA, weak *and* white-only): clock bound is the picture screamer, and fails at −51.6 ppm if the fold is removed. Roundtrip [7]: white-only generated at +250 ppm, decoded +250.0, zero locks | met, with the limit stated |
| §4.2.7 | Pitch of scanning trace within ±25% | line pitch held by construction (one output row per measured scan line); ISO §5.4.4's visual even/parallel-line check is exceeded by picture-domain measurement | `--expect-straight-strip` measures the dead-sector edge in finished pixels on six fixtures, with bounds below the pre-repair pictures | met |
| §5.4.2 | Recorder produces chart identical to transmitted for all IOC/speed combos | decode the full synthetic matrix {288,576}×{60,90,120}, selecting IOC and speed automatically | `roundtrip [4]` | synthetic ✓ |

Notes:
- §4.2.4 lists 60/90/120 only — this is why 240 lpm is out of scope.
- §4.2.2's "±150 Hz and/or ±400 Hz" is why LF deviation is a
  first-class mode, not an afterthought.
- WMO signal-side checks added in the same audit: dead-sector tolerance
  edges (`roundtrip [15]`, WMO §5.1.3.3), 10 s start tone (`roundtrip [13]`,
  WMO §5.2.2), and eight-tone gray linearity (`roundtrip [12]`, WMO §5.4.3).
- **Session 10, §4.2.6 "phasing automatic":** automatic phasing now also
  covers a FADED interval, which is the case where it matters most —
  a station sending a plain white dead sector has no other source of line
  phase, and the one such recording in the library was reporting no
  phasing at all. The interval is identified by where the white edge is
  rather than by each line's contrast, because fading destroys the second
  and not the first (real phasing lines at 0.34–0.88, below the 0.48–0.62
  that dark picture content scores). Count of library recordings with a
  detected phasing interval: 15 → **16 of 20**.
  [`fixture_faded_phasing`, `tones [12]`]
- WMO §5.2.3.2's two phasing waveforms (50/50 and 5/95) are covered
  under the §4.2.6 phasing row. Both are now detected and distinguished
  (`tones` [7][8]); the library's XSG recordings are the real 50/50 case.
  §4.2.6's "phasing automatic" is **met for automatic phasing** as of
  session 7: `decode_fax` consumes the line-start reference [WMO §5.2.3.4]
  on stations whose image lines give no per-line sync, which is exactly the
  set that needs it, and reports the phasing-vs-image anchor delta on every
  decode so the two are always cross-checked. Manual phase adjustment is
  still pending the GUI, so the row stays "automatic ✓, manual pending".
  The edge convention behind it — which edge of the dead sector the phasing
  white marks — was a registered gap from session 6 and is now settled by
  measurement (docs/01 §5): it marks dead-sector ENTRY, on both dead-sector
  styles.
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
- §4.2.6's stability figure (±2×10⁻⁵ = ±20 ppm) presumes the recording has
  ONE rate to be stable about. Six of twenty library recordings do not:
  their capture chain inserts samples, so the measured clock is the
  transmitter's clock plus an insertion rate and no single figure describes
  the file (session 9, docs/01 §5). Nova reports this
  (`DecodeResult::timebase`), so the clock figure is never read as a
  stability measurement when it is not one, and since session 11 it also
  CORRECTS the picture wherever the station sends a sync pulse: the drawn
  line starts are placed segment by segment rather than through a fixed
  smoothing window, rows a mid-line insertion stretched are drawn in two
  pieces, and rows a stream dropout unlocked are re-locked by probing the
  sync pulse at the level the lines after the drop vouch for (session 12 —
  far side scores 0.66–0.96 against ≤ 0.22 at the near one, on all five
  library dropouts). How straight they came out is reported per decode
  (`place_rms_px`), and six fixtures assert it against the finished pixels
  (`--expect-straight-strip`). On a station with no sync pulse nothing can
  be corrected per line and the paper still moves (M2b, open). Compliance is
  therefore claimed against the recording's timebase where that timebase is
  linear, and the non-linear case is *reported* rather than quietly
  averaged. `roundtrip [10]`, `fixture_timebase_steps`,
  `fixture_timebase_linear`, `fixture_warp`, `fixture_picture_head`.
