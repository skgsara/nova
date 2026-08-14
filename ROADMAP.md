# ROADMAP.md — Nova

Milestone map. Done vs pending. Screamer tests owed by the risk
register are milestone entries, not intentions (SOP P1.5).

## M0 — headless core decodes one real recording  [DONE 2026-08-12]
- Dependency-free DSP core: resample-to-internal-rate, FM demod
  (quadrature + phase-difference discriminator), sync, line assembly.
- First fixture: `test chart.m4a` (JMH Tokyo 13988.5 kHz) — decodes
  straight and readable, incl. the portrait, unattended.
- Harness: `nova-gen` signal generator (start/phasing/image/stop,
  injectable ppm and noise). Round-trip test suite: 6 groups green.
- Screamers live: `ctest` = roundtrip (synthetic matrix) + fixture
  (real JMH excerpt, measured bounds).
- Key bring-up findings in SESSION-LOG 2026-08-12 session 2.

## M1 — mode generality  [DONE 2026-08-12, sessions 3 + 4]
- Rate auto-detect: odd-harmonic comb scan, 60/90/120 from one code
  path (no hardcoded 576/120). Real-signal proof: JSC1/4/5/6 at 60 lpm
  (93-99% locks), everything else at 120.
- Signal onset gate: recordings opening with leader tones / stall-fill
  are detected and anchored (or refused) — session 3.
- Full-line image mapping (measured JMH line layout; no cropping).
- Fixtures: `kyodo-news-jsc1-60lpm-120s` (only 60 lpm signal),
  `himawari-jmh-warp-120s` (stream time-skip), `stall-fill-15s`
  (rejection screamer), KiwiSDR test chart (new primary).
- ±400 Hz proven on real signals; ±150 Hz synthetic only (gap).

## M1b — dead-sector style + re-acquisition  [DONE 2026-08-12, session 4]
- Line-start anchor now measured by across-line consistency instead of a
  fold-average: the dead sector is the only part of a line that looks the
  same on every line. Scores the black->white *shape*, so the black space
  margin of a full-disk satellite image cannot fake a pulse.
- Dead-sector style (black pulse / white only) detected per recording,
  reported, and pinned by a screamer. Library separation: 0.48-0.94 vs
  0.14-0.34.
- Pass A re-acquires: after 8 unlocked lines it sweeps the whole line at
  a coarse step. This also healed the Himawari stream time-skip — the
  warp fixture's max_step fell from 54.3 px to 0.75 px — so "wide
  re-acquisition after time-skips" is done, not M2 work.
- Library effect (honest locks, before -> after): FAXSignal 65 -> 2170,
  XSG ASPN 116 -> 2566, JSC2 103 -> 2192, jmh sample 71 -> 1023, test
  chart 62 -> 711, HDSDR 105 -> 1790, JMH Himawari 740 -> 1953.
- NEGATIVE RESULT, recorded: the "VMW white-sector sync template" this
  roadmap asked for does not exist. Two were built and measured; both
  raise lock counts and make the picture worse. White-only stations are
  decoded on the measured clock and report zero locks. See docs/01 §5.
- REMAINING: weak-signal period estimation (GYA 2300Z, still slanted).

## M2 — automatic slant correction  [DONE 2026-08-12, session 5]
- Done in M0/M1b: clock-rate estimate from the whole signal, per-line
  dead-sector re-lock [WMO §5.1.3.3], whole-line re-acquisition.
- Session 5 — **the period was being measured over too short a baseline,
  in both estimators.** Accuracy comes from drift accumulated across the
  recording, not from lag resolution or averaging (docs/01 §5).
  - No locks (white-only): folded-block phase tracking replaces the bare
    200 Hz autocorrelation, whose lag step is 10 000 ppm and which was
    wrong by 30–180 ppm on real recordings.
  - With locks: pass B now pairs locked lines an eighth of a recording
    apart instead of neighbours ≤10 lines.
  - Both segment at discontinuities first (phasing↔image step, stream
    time-skip, chart restart) — a long baseline is only meaningful
    inside one regime.
- The claim "the library decodes straight" was FALSE when it was written.
  Measured residual shear, before → after: JSC2 −157 → +6 ppm, JSC3 −182
  → −5, JSC4 −172 → +2 (a third of a page of drift, on the newspaper
  faxes where it is hardest to see by eye), GYA 2300Z +50 → +4, VMW 2215Z
  −399 → +0.2, NMC −79 → −9. Nothing regressed.
- GYA 2300Z's frame line — a feature of the chart, not a statistic — is
  now straight to +1.7 ppm over 1358 lines (was −23.4).
- Screamers: `fixture_weak_white` (GYA 2324Z, fails at −51.6 ppm if the
  fold is removed — verified by removing it), roundtrip [7] white-only at
  a known +250 ppm with zero locks, roundtrip [8] −137.00 ppm measured
  against −137 true.
- OPEN: fractional resampling (KiwiSDR approach) — not needed so far,
  the fitted-line + local-median correction has been sufficient.

## M2b — timebase CORRECTION, not just detection  [DONE 2026-08-12, sessions 11 + 11b; dropout bands + 60 lpm verdict session 12]

Sara reviewed all 20 decoded charts by eye and every complaint she made was
the same axis — where each line starts, horizontally. Six recordings "zig
zag"; two "lose sync" at one end; one is a staircase. Sessions 9 and 10
built two statistics that detect and report a bad timebase, and nothing
corrected one: the decoder told the truth about JSC and then drew the
picture anyway. Measured against the drawn pixels, the recordings she named
carried 3.0–4.6 px of dead-sector-edge scatter and the ones she passed over
carried 0.3–1.9.

Done:
- **Segment the tracked sync residual at change points.** A move is real
  when the kSegHalf=4 locked lines each side agree it happened, by more
  than kNonlinSec (10 samples) — the resolution the timebase test already
  claims. The assembly's ±8-line median window is truncated there, so it
  never averages across a step; the test's window is NOT, because it is a
  calibrated instrument (session 9's library thresholds were measured
  through it).
- **Fit a robust line, not a level, inside each segment.** The residual
  ramps between steps, because the period fit absorbs the mean insertion
  rate — 1.9 samples/line on the synthetic, 19 samples of tilt across an
  11-line segment. Theil-Sen (median of pairwise slopes); the median only
  where a segment is too short to see a slope.
- **Follow a real skip in one line.** A change point is exempt from the
  per-line clamp, so a recording that loses 1270 samples gets a one-line
  seam instead of a 12-line diagonal tear.
- **Believe a residual that disagrees with the FIT.** The old ±2*search
  gate dropped exactly the evidence a big skip produces; JMH KiwiSDR
  Himawari's whole first half was drawn 574 px across because of it.
- **Coasting no longer sets a correction level**, so a picture whose first
  lines do not lock starts at its first real measurement instead of
  clamping up to it from zero.
- **A picture-domain screamer at last** (`--expect-straight-strip`): the
  dead sector's edge measured in the finished pixels, sharing no code with
  the decoder, on five fixtures. `place_rms_px` is the decoder's own
  account of the same quantity and is held to the same bound.

Measured, whole library: 6 recordings better, 14 unchanged, 0 worse.
Ground truth (roundtrip [10]): bar scatter 2.19 px → 0.00 with the same
insertions, and 0.28 px of place error when they land on the line boundary.

Session 11b, after Sara reviewed the session-11 decodes and found three
things still wrong ("for JSCs, small zigzag are still zigzags, still cause
difficulties of reading"):
- **The rows were stretched, not moved.** A capture chain does not wait
  for a line boundary to insert samples, and when it lands mid-line
  everything after that point moves while everything before it stays. No
  per-line offset can place such a row. Measured in the session-11
  decodes: the two ends of a JSC row moved with correlation +0.12 and
  disagreed by 5-10 px of 1810, against 1 px on a linear recording. The
  size of the move is already known (it is the difference between this
  line's correction and the next line's); only its POSITION was missing,
  and the picture supplies it — the break goes where splitting the row
  makes it agree best with the row above. JSC2 fixture: the two ends
  disagree by 10.0 px without this pass and 1.0 px with it.
- **Rows a dropout stranded are placed by the picture.** Eight lines
  straddling JMH KiwiSDR Himawari's ~1270-sample move carry no lock, so
  the window placed them where the lines before the move are — ~75 px from
  the rest of the chart, the band Sara found still there. Their own sync
  template is not the answer (following HDSDR's equivalent bands tears the
  text apart, measured), and neither is a general "let the picture place
  every unlocked row" rule, which fails two screamers. Only rows inside a
  run that the phase moved across qualify.
- New fixture `himawari-kiwisdr-dropout-120s.wav` and two new picture-
  domain checks: `--expect-rigid-rows` (do the two ends of a row move
  together) and `--expect-rows-in-place` (does any row match the row above
  best at a large shift). 22 suites.

NOT A DEFECT — the white-only staircase, by operator decision. Sara,
session 11b, having reviewed the decodes: "VMW 2215Z's staircase is
understandable, don't worry about it, others has huge improvement." A
station that sends no sync pulse gives the decoder no per-line evidence
except the picture matching its own previous row, and session 11b measured
what happens when that mechanism runs on a whole page: it walks off the
phasing anchor and fails two screamers. The staircase is therefore
ACCEPTED, not deferred — it is what a white-only recording with a stepping
capture chain looks like, and the decoder reports the condition
(`timebase`, `per_line_sync`) rather than inventing a correction for it.
Reopen only if a bounded form is found — the shape it would have to take is
in SESSION-LOG session 11b's next-step note.

Session 12, after Sara's review of the 11b decodes ("losing sync in the
middle", with screenshots, on JMH KiwiSDR Himawari and test chart):
- **Dropout runs are re-locked on the SIGNAL, not the picture.** Sara
  recorded both via KiwiSDR, where an internet stall drops samples
  mid-recording (1269 and 1642 samples on those two). The 11b picture
  placement searches ±120 px and the moves are 574/743 px — it could not
  even reach the right answer. But the run is bracketed by two known
  levels, and the sync pulse is in the audio at the far one: probing each
  row ±20 samples around each level, the far side scores 0.66–0.96 and the
  near side ≤ 0.22 on all five library dropouts, and exactly one row per
  run scores nothing at either level — the row the drop landed in, which
  is then split over the whole line (the usual quarter-line cap exists to
  stop the search inventing breaks; next to a re-locked run the move is
  independently evidenced). Also fires on HDSDR (40 rows, 5 drops of ~163
  samples: strip AND text both read now, where 11b's soft-lock follow tore
  the text — the probe needs ≥ 0.60 with the loser < 0.45, so the mid-way
  0.40–0.50 measurements that misled 11b decide nothing) and on JSC4 (8
  rows; the doubled, ghosted contact line is a single crisp row now).
- **JSC1/JSC5's 5 px rigidity is the statistic, not the decoder.** A
  ground-truth synthetic at 60 lpm with JSC1's signature (17 samples
  every 3 lines) reads the same p90 = 5.0 px while placing rows to
  1.1 rms / 3.2 worst and matching its own clean decode at MAD 2.5/255;
  the 120 lpm control reads 1.0. At one step per three lines a correctly
  drawn picture genuinely has rows whose two ends disagree, and 17
  samples is 3.8 px at 60 lpm. Pinned in roundtrip [10] (placement
  asserted; rigidity deliberately not — at 60 lpm it measures the
  recording, not the decode).

STILL OPEN, and small:
- No screamer for a faded pulse station whose steps are below its own
  measurement noise — the probe needs a pulse that scores over the lock
  threshold at the far level; below that, rows fall back to the ±120 px
  picture placement, which cannot reach a large move.

## M3 — full auto sequencing  [done except manual override, which needs M4]

Done (session 13) — the pre-M4 standards audit closed the last silent
automatic-selection gap: the 300/675 Hz start tone now selects IOC 576/288
when the operator did not override it [ISO §4.2.5], and the synthetic
{288,576}×{60,90,120} matrix runs with automatic IOC and rate selection.
A real 675 Hz recording remains a registered fixture gap, not a code gap.

Done (session 6) — detection, measured against the library:
- Start/stop tones [ISO §4.2.5]: `core/tones.cpp`. Accept test is spectral
  **purity** in the tone's own bin, not a transition rate, because a rate
  test cannot tell a 300 Hz square wave from text that averages 300
  transitions/s. Library separation: content ≤ 0.16, tones 0.68–0.99,
  threshold 0.35. Zero false positives in 5.9 hours; 14 of 20 recordings
  carry a start tone, all measuring 299.8 Hz, all stops 450.5 Hz — inside
  the ±1% of WMO §5.2.6.
- Phasing [WMO §5.2.3]: `core/phasing.cpp`. Wedge fit, median, 10–90%
  spread rejection (KiwiSDR shape) plus a duration cap from the spec.
  Found on 15 of 20 recordings, 30.0 s long, spread 4–73 samples of 4000.
  Recovers the line rate (60/90/120) and both waveforms (5/95 and 50/50 —
  XSG is the real symmetric case).
- **This corrected session 3's tone survey.** That survey searched the raw
  audio, where these signals only appear as incidental envelope ripple,
  and concluded one recording in twenty carried a start tone. The correct
  domain is demodulated video, and the answer is 14 of 20.
- Screamers: `tones` [1]–[11], `tones_fixture_vmw` (real start tone +
  phasing in an off-air white-only recording), `tones_fixture_no_false_start`
  (zero events on 120 s of real newspaper text).

Done (session 7) — sequencing, wired and verified against pictures:
- **The edge convention, settled.** The phasing white leading edge marks
  dead-sector ENTRY [WMO §5.2.3.4], on both dead-sector styles. Measured
  by folding the video over the phasing region and the image region on one
  common grid: on JMH the phasing edge is at −73 samples of 4000 and the
  image's dead-sector black run starts at −67. Across seven pulse-station
  recordings the offset is the black porch, −1.65% to −2.86% of a line,
  and two recordings of one transmitter agree to 3 samples of 4000.
- **The phasing anchor drives the decoder on white-only stations.** Their
  image-derived anchor was catching the chart's blank margin, not the dead
  sector: VMW 2230Z decoded rotated by 520 px of 1810, NMC's caption torn
  across the line boundary, GYA 2324Z with a 130 px strip of its right edge
  on the left. All three correct now. Pulse stations keep their tracked
  anchor and are byte-identical (checked on all 10). The anchor delta is
  reported on every decode either way.
- **Segmentation.** Only the picture is drawn: start tone and phasing
  cropped from the head, stop tone from the tail. Output framing only —
  onset, period, anchor and both tracking passes still see the whole
  recording, so nothing session 5 measured moves.
- Screamers: `tones` [11] (absolute anchor, both run parities and a −137
  ppm clock), `roundtrip [9]` (row 0 of the output is image line 0),
  `fixture_phasing_anchor` (the VMW picture: content begins one dead
  sector into the line, 4.97% — reads 0.00% with the old anchor),
  `fixture_phasing_boundary` (a real phasing→image transition).

Done (session 8) — the anchor question closed on evidence:
- **A pulse station keeps its tracked anchor.** Session 7 argued it; JSC2
  decides it. That recording's phasing anchor sits 234 samples (106 px of
  1810) from the tracked one, and the tracked one draws the correct picture,
  because its timebase steps and a fixed reference propagated on a fitted
  clock cannot survive that. Now asserted, not assumed.
- **The two anchors are pinned against each other** on pulse stations, where
  the phasing anchor is measured but never used and so nothing else in the
  suite corroborates either. Bands from the whole library: −66.1 to −114.3
  samples of 4000, repeats of one transmitter within ~7.
- **Why two recordings disagreed.** JSC2 and JSC3 carry ~21-sample timebase
  steps every few lines — in the audio, still there at 44.1 kHz through a
  separate demodulator, absent from all 18 other recordings. Their true
  porch, measured across the phasing→image boundary with no clock model in
  between, is normal (−46 and −3). `clock_ppm` on those two files is the
  clock plus the mean insertion rate. [docs/01 §5]
- New fixture `xsg-phasing-image-100s.wav`: the library's only SYMMETRIC
  50/50 phasing station, whose anchor no fixture had covered.
- Screamers: `fixture_anchor_delta_jmh` (5/95), `fixture_anchor_delta_xsg`
  (50/50). Both fixtures have an even number of phasing lines on purpose —
  reverting session 7's integer-line anchor fix moves them half a line out
  and both fail.

Pending:
- Manual override for everything [ISO §4.2.6 "facility for manual
  adjustment"] — still untouched, needs the GUI (M4).

## M4 — GUI + live audio  [pending; core seams done session 14, design decided session 15, shell designed session 17, skeleton built session 18]
- Core seams (session 14, done): log/progress callback, cooperative
  cancellation, structured DecodeError kinds, decode_fax split into nine
  named stages (core/hooks.hpp; screamers in `hooks`).
- Design settled session 15 against a 16-manual survey of commercial
  receivers [docs/04]. All eight open design questions answered; the
  docs/03 "answer on paper before GUI code" gate is cleared.
  - **Two decode paths.** Live view: provisional forward draw, single
    pass, never revised. Saved image: batch re-decode of the retained
    raw stream at end of transmission. `decode_fax` stays batch and
    untouched; only the provisional renderer is incremental.
  - Streaming tone detector for the live path; batch keeps its full scan.
  - Two manual geometry overrides only — PHASE and SYNC — surfaced as a
    ruler along the image plus click-the-dead-sector (ISO §4.2.6/§5.4.3).
  - AUTO as a value inside the IOC / rate / frequency controls, never a
    separate mode toggle; always-available forced start with explicit
    IOC + rate.
  - Named protocol states, not a percentage; clock/timebase figures
    withheld until the long baseline exists.
  - Images saved unbounded to a user-set folder as greyscale PNG; no
    ring buffer, no LOCK/pin.
  - Live→saved handoff: the live view is labelled provisional from the
    first row; the saved image replaces it in the same pane, once, at
    end of transmission [docs/04, decided session 16].
  - Manual PHASE/SYNC corrections at both moments [docs/04, decided
    session 16]: forward-only on the live preview; non-destructive
    re-render from retained raw on the saved image; live-path overrides
    seed the batch re-decode as its initial anchor — PHASE as a seed,
    SYNC only as a fallback [docs/05 §7.1]. ~~values persist per
    station~~ **removed session 20**: neither value persists between
    transmissions, because no key the operator can supply names the
    (transmitter, receive chain) pair a clock trim belongs to, and a
    remembered PHASE seeds the anchor search at last week's line phase
    [docs/05 §8.5 item 6].
- FLTK GUI, RtAudio capture, input level meter, image tools,
  post-decode realign / line-start adjust on retained raw stream
  (ACFax architecture: non-destructive; now load-bearing, since the
  saved image is a decode of the retained stream).
- Greyscale PNG writer [DECIDED session 16, Sara]: hand-rolled encoder
  (uncompressed deflate), no new dependency — Nova has no external
  dependencies today and libpng/zlib would change that.
- Shell design [docs/05, session 17 — PROPOSAL, not yet decided by Sara]:
  - Three layers: `nova-core` (untouched, no deps) → `nova-live` (no
    FLTK, no RtAudio, no real clock — streaming tones, provisional
    renderer, session state machine, retained store, PNG writer) →
    `nova-gui` (thin: devices, widgets, queues, no DSP). The rule exists
    so M4 can have screamers at all.
  - Four threads: RtAudio callback → lock-free SPSC ring → live decode
    thread → GUI queue drained on a 50 ms `Fl::add_timeout`; batch decode
    on a frozen `shared_ptr<const vector<float>>` snapshot per
    transmission. No worker thread touches a widget.
  - Streaming resample/demod by block-with-overlap, so `core/` gains no
    stateful entry point; equality with the whole-file result is a
    screamer.
  - Eight named live states (IDLE, READY, START TONE, PHASING,
    DRAWING — PREVIEW, STOP TONE, DECODING, SAVED). The nine decode
    stages are sub-progress inside DECODING only — see the contradiction
    in docs/05 §10 against docs/04 Finding 3.
  - **The only core change M4 asks for**: two `DecodeOptions` fields,
    `phase_anchor_hint` (negative = none) and `clock_ppm_fallback`
    (NaN = none; zero cannot mean auto, because a perfect clock is
    zero ppm). Both follow the existing `lpm = 0` / `ioc = 0` idiom.
  - **The two do NOT behave the same way** [DECIDED 2026-08-13, Sara,
    session 17; docs/05 §7.1]. PHASE is a **seed**: the anchor search
    starts at the operator's click and refines locally — their judgement
    about *which* feature is the dead sector, the decoder's precision
    about *where* it is. SYNC is a **fallback**: the batch fit wins
    wherever it has a baseline (sessions 5/8/9 — long baselines beat
    short ones), and the operator's ppm is used only where it does not
    (white-only station, forced start, too few locked lines). Named for
    those semantics so neither reads as a plain override. Consequence
    accepted: on a healthy recording the operator's SYNC value is
    measured away from, in the direction of correct.
  - Retention [DECIDED 2026-08-13, Sara]: no raw sidecar on disk, and an
    image from three hours ago cannot be re-phased (matches the SR-97's
    "stored images cannot be modified"). **Two** snapshots are held, by
    role — the transmission being received, and the image being
    displayed — so a live edit is never cut off by the next
    transmission starting. Bounded at two however long the edit lasts;
    ~76 MB worst case. Older images open with PHASE/SYNC visibly
    disabled and the reason shown, never silently inert.
  - Capture rate [DECIDED 2026-08-13, Sara]: accept whatever the device
    offers, resample to 8 kHz, as the file path already does.
  - Page cap [DECIDED 2026-08-13, Sara]: 1 page — stop at the first stop
    tone, cap is purely the guard for when that tone is missed.
  - Station identity [DECIDED 2026-08-13, Sara]: an **operator-typed
    label**, blank by default. Frequency is not available to Nova at
    all — it is fed audio from a sound card and never sees the radio,
    which retired the question as posed and corrected the status panel
    (docs/05 §8.1). Every receiver in the docs/04 corpus contains its
    own radio; Nova is one component of one, and Finding 4's field list
    had to be re-sorted by what an audio-only decoder can source.
  - Six screamers planned, none needing an audio device or a window:
    `live_demod_equiv`, `live_tones`, `live_preview` (bit-identical
    whatever the block size), `png_roundtrip`, `override_phase_seed`,
    `override_sync_fallback`.
  - Pane ownership during an edit [DECIDED 2026-08-13, Sara]: **the edit
    holds the pane.** The incoming transmission draws into a background
    buffer behind a compact receiving indicator (state, line count,
    thumbnail) that switches on click. Costs nothing architecturally —
    the provisional renderer already pushes rows through the GUI queue
    instead of drawing to a widget, and the entire edit state is two
    numbers. Rejected: the new transmission taking the pane, which would
    interrupt the operator during the one interaction ISO §4.2.6 exists
    to guarantee.
  - **No design question remains open** (docs/05 §12, all six decided
    2026-08-13). The next step is code.
- **Walking skeleton [DONE session 18]** — `option(NOVA_BUILD_GUI)`,
  `gui/nova-gui.cpp`, the §8 window in real FLTK widgets, RtAudio device
  enumeration in the Device menu. No decode, no threads, no DSP, no audio
  stream: the point was the dependency wiring and the layout, both proven.
  Verified: 23/23 suites pass with `NOVA_BUILD_GUI=OFF` and no `nova-gui`
  is produced; a missing FLTK or RtAudio skips the target and configures
  successfully. Four §8 corrections came out of it (docs/05 §8.0), of
  which one is load-bearing: the ruler must be aligned to the image
  pane's *interior*, and FLTK's resizable-group scaling breaks that at
  every size but the built one, so the shell computes its layout on
  resize instead of scaling children.
  - **Layout gap closed (session 19):** `gui_layout` pins the ruler/pane
    interior alignment at five window sizes and built-at == dragged-to,
    from `nova-gui --metrics` — the first test in the project to cover
    FLTK. The alignment had been wrong twice before it got a screamer.
- **Picture area, transport and chrome [DECIDED 2026-08-13, Sara,
  session 18; docs/05 §8.3]** — eight questions Sara asked on seeing the
  skeleton, five of them about surfaces docs/05 had never specified.
  - **Ruler reads image columns**: 0–1809 at IOC 576, 0–904 at IOC 288
    (width = round(IOC × π); 1810 and 905 px, both measured on real
    decoder output). Tick step chosen from the displayed scale; blank and
    disabled while IOC is unknown, as the clock readouts already are.
  - **Zoom**: Fit (default), 25%, 50%, 100%, 200%, with Fit as a value in
    the list. An IOC 576 chart does not fit at 100% — it fits at ~43% in
    a 772 px pane — so the range runs below Fit and above 100%, the
    latter because PHASE placement is a per-column judgement.
  - **Scrollbars** in both axes when the image exceeds the pane, and the
    **ruler tracks zoom and horizontal scroll**. This upgrades the
    alignment invariant into a mapping invariant — the column under the
    cursor is the column the ruler names — testable with no window.
  - **Start becomes Stop while receiving.** Stop runs the full
    end-of-transmission path and saves; it does not discard [docs/04
    Finding 6: operator stop is the first of three ways a transmission
    ends, and the SR-97 holds the image at a SAVE? prompt].
  - **No sidebar waterfall reservation** — wrong shape for a 200 px
    column, and that space is already §8.2's receiving indicator. M4.5's
    waterfall extends the full-width meter strip.
  - **No autosave toggle**: every completed transmission is saved, which
    is the retention decision already taken (answer 7).
  - **Settings sets the folder; format stays greyscale PNG only.** BMP
    rejected: a second writer, larger files, and no metadata — where PNG
    text chunks are the home for Nova's decode QA, with precedent in the
    Furunos' printed Phase OK/NG header.
  - **About**, made load-bearing by GPLv3+, with Settings in a
    File / Settings / Help menu bar. No receiver in the corpus has a menu
    bar; the survey constrains the picture-correction surface, not
    whether a desktop application has desktop chrome. The About text is
    approved verbatim (session 19) and lives in docs/05 §8.3 item 8 —
    the coding session copies it, it does not invent one.
  - Metric consequence: the Zoom control costs ~120 px in the control
    row, so the minimum window width rises from 740 px to ~880. The
    control row still sets the floor, not the picture.
  - **Behaviour over time [DECIDED 2026-08-13, Sara, session 19; docs/05
    §8.4]** — five questions from preparing the §8.3 coding session, four
    decided: settings persist in a preference file next to the executable
    (a non-writable directory means no persistence, never a failure);
    a zoom change keeps the pane's left edge; Force Start is insensitive
    until IOC and rate are both explicit; the ruler appears suddenly when
    AUTO resolves the IOC; and during DECODING the button is insensitive,
    reading "Start", active again at SAVED — the first GUI is serialized,
    with an overlapping next reception architecturally available but
    unbought.
  - The two screamers from this **[built session 19]**: `gui_layout`
    (ruler/pane alignment, built-size == dragged-size; guarded by
    `NOVA_BUILD_GUI`) and `ruler_mapping` (the column under a screen x is
    the column named there, at every zoom, scroll offset and both IOCs;
    the mapping is a pure function in `live/ruler.*`, the first
    `nova-live` code, tested unguarded). Suite count is **"24 (+1 with
    the GUI)"** — session 18's wording said "23 (+1)", but `ruler_mapping`
    tests dependency-free code and runs in every build, so the base count
    moved to 24 and the GUI conditional stayed +1.
- **The §8.3 + §8.4 surfaces, in code [built session 20]** — the menu bar
  (File / Settings / Help) with the folder chooser and the verbatim About
  text; the preference file beside the executable; Zoom with left-edge
  retention through `nova::rezoomed`; `Fl_Scroll` around the pane; the
  ruler consuming `live/ruler.hpp` and blank until the image width is
  known; and the transport rules — one button relabelled by state,
  insensitive during DECODING, Force Start gated on explicit IOC + rate.
  Still no decode behind any of it: the buttons are inert on a plain run,
  and `--state NAME` drives the shell as nova-live will so the rules are
  inspectable. Third GUI screamer built with it — `gui_shell` — which
  moves the count to **"24 (+2 with the GUI)"**.
- **The streaming front end [built session 20]** — `live/stream.{hpp,cpp}`:
  `StreamResampler` and `StreamDemod`, block-with-overlap wrappers over
  core's batch `resample`/`fm_demod`, so the live preview and the saved
  image cannot differ from the call pattern. `core/` unchanged, as §2.2
  required. `live_demod_equiv` measures it at eleven block sizes from one
  sample to 44100, on a real recording and on generated signals at 44100
  and 48000 Hz: **the demod is bit-identical, the resampler agrees to
  5e-13, and output counts match everywhere.** The demod overlap is
  measured rather than assumed at **62 samples** — one less than the
  63-tap filter predicts, because the Blackman window's endpoint taps are
  exactly zero — and ships at 64. The resampler consumes input in whole
  blocks of the reduced ratio's denominator (441 samples at 44100, 6 at
  48000), because a segment boundary that is not a whole number of output
  periods would shift the rest of the stream. Suite count 25 (+2 GUI).
- **The streaming tone detector [built session 20]** —
  `live/tone_stream.{hpp,cpp}`: `StreamToneDetector`, the same per-frame
  purity computation as `detect_tones` over the same frame grid, with
  the retrospective run assembly replaced by an incremental one that
  emits **at the earliest qualifying moment**. `core/tones.cpp` changed
  only by exposing its median and 10–90% spread helpers, so the two
  paths compute the run statistics with one implementation rather than
  two. `live_tones` measures it across all 17 fixtures: same kinds and
  counts, `t_start` identical to **0.0000 s** (§9 asked only for one
  hop), the event list bit-identical at every block size from 1 sample
  to 65536, and **12/12 events committed early — 2.6 to 7.1 s each,
  48.75 s of lead in total**, which is the whole point of the exercise.
  Two findings worth carrying: the streaming path commits on the
  *weakest* frames of a tone (purity 0.391 where the batch path reports
  0.849 for the same tone), so its reported purity is not a quality
  bar; and **no fixture in the library carries a stop tone or a tone
  that fades mid-run**, both registered in docs/05 §13, both covered
  meanwhile by generated signals, and both closable by cutting one new
  fixture from VMW 2230Z, NMC 2204Z or GYA 2300Z. Suite count 26 (+2
  GUI).
- **The provisional renderer [built session 21]** —
  `live/preview.{hpp,cpp}`: `StreamPreview`, forward-only, single pass,
  never revised. Rate from a short EMA over the last N locked lines,
  phase from the same per-line dead-sector relock the batch path uses —
  `fax_best_sync`, promoted out of `core/fax.cpp` into `core/fax.hpp`
  with the layout constants and the re-acquisition rule, so the preview
  and the saved image lock onto the same feature rather than onto two
  implementations of one template. `core/fax.cpp` changed by nothing
  else. **A picture appears in the pane for the first time**, and the
  first one out was the JMH test chart, readable.
  `live_preview` renders all 16 fixtures that hold a picture: the dead
  sector lands within **1 px** of the batch image's column on all eleven
  pulse fixtures and both phasing-anchored white-only ones, and the
  image and every row's placement are **bit-identical at seven block
  sizes from 1 sample to 65536**. Three real bugs on its first run, all
  fixed: the retained buffer was released per push rather than per row,
  which made the picture depend on the block size through the last bits
  of the sync search's accumulated probe positions; the forward tracker
  had no whole-line re-acquisition sweep, so a dropout tore the page
  from there to the end (140 locks of 238 against the batch's 232 of
  240, now 230 of 239); and `docs/05 §6`'s seed list named IOC and rate
  but not PHASE, which on a white-only station is the only seed that
  works — the preview drew `vmw-phasing-image-160s` **524 px around**
  from the saved image until the phasing anchor was passed in.
  Registered gap: two library fixtures are white-only AND carry no
  phasing interval, so nothing in the signal says where their dead
  sector is; the preview may draw them rotated (563 px measured), and
  the screamer demonstrates that one operator PHASE click lands the page
  to 1 px. Suite count 28 (+2 GUI).
- **The stop-tone gap, closed [session 21]** — `fixtures/
  nmc-image-stop-tone-120s.wav`, NMC 2204Z 340–462 s: a real chart
  ending in a real 450 Hz stop tone [WMO §5.2.5] that **fades to nothing
  for 0.88 s in its middle**. One fixture closes both gaps session 20
  registered — the library had no stop tone at all and nothing that
  faded mid-tone — and it adds NMC to the fixture library and exercises
  the tail half of segmentation, which nothing else did (22 lines of
  stop tone dropped). `tones_fixture_nmc_stop` pins that the fade is
  bridged into ONE run rather than two bursts; the streaming detector
  commits it **3.12 s before the run ends**.
- **The save/edit lifecycle [DECIDED 2026-08-13, Sara, session 20;
  docs/05 §8.5]** — five questions asked while those surfaces were being
  written, about the whole life of one chart rather than about the
  window. The picture is saved automatically when the batch decode
  completes, before any editing is possible. An edited re-render
  **overwrites the same file** — one transmission, one file — and there
  is **no Save button**: Apply re-renders and writes, so the file always
  matches the screen. An **edit in progress** is dirty controls, not a
  mode: it begins at the first PHASE/SYNC change or the first click on
  the image and ends at Apply, Auto, or switching to the live view, which
  is the boundary §8.2's pane-hold was missing. The file is named
  `20260813T220417Z.png`, or `20260813T220417Z-JMH.png` with an operator
  label — UTC to seconds, timestamp first, label sanitized and capped at
  32 characters, blank label giving the timestamp alone. **Nova never
  renames a saved file**: a label changed after the save reaches the PNG
  metadata, and renaming is a file operation like deleting, because the
  image list is a view of a folder.
  - **And one reversal**: the **per-station PHASE/SYNC memory is
    removed**. The operator label is unstable free text, the receive
    chain changes the number while the key does not notice, `clock_ppm`
    belongs to the (transmitter, receive chain) pair that no label can
    name, and a stale PHASE actively misleads because it seeds the anchor
    search. The corpus does persist these per station [docs/04 Finding 1]
    — on machines with a preset key and a fixed receive chain, which is
    §8.1's insight for the second time. The preference file keeps the
    image folder alone.
- **Dependency qualifier.** "Nova has no external dependencies" stays
  true of the core, the CLIs and the test suite after M4, and becomes
  false of the GUI binary alone, which links FLTK and RtAudio behind
  `option(NOVA_BUILD_GUI)`. Tests and CLIs must build with it OFF.
  M5's tier-1/tier-2 matrix is only affected for that one target.
- **The live session state machine [built session 22]** —
  `live/session.{hpp,cpp}`: the eight states of docs/05 §4 as code, on
  top of the three streaming stages. Owns the retained store and its
  freeze (§3), the phasing watcher that re-runs `detect_phasing` once a
  second at all three nominal rates and hands the preview its anchor and
  rate seed on a CLOSED run (the §6-item-1 handoff; `PhasingResult`
  gained the measured `period` for it), the forced start, and the
  operator Stop that is the stop-tone path minus the tone. Two bugs the
  screamer caught: the preview was fed to the stream end, so the 2 s of
  stop tone before detection were drawn into the picture by a
  block-size-dependent amount — fixed by the tone detector's new
  `safe_horizon_samples()`, an absolute position the feed never crosses
  into an undetected tone; and a re-entrant `batch_done` from an inline
  decode callback inverted DECODING/SAVED in the event history. Measured:
  drawing starts within 0.12 s of the batch segmentation's picture start
  on all three tone-driven fixtures, the rate seed is −14 ppm from the
  batch fit on VMW, preview dead sector +6/+1/+0 px against the saved
  image. `live_session` runs it all unguarded; seven mutations killed,
  one having first survived a version of the test that never exercised
  the SAVED → START TONE edge. Registered, not fixed: two openings before
  one picture are drawn from the FIRST one live (the batch "last opening"
  rule needs the stop tone, which has not happened yet). Suite count
  30 (+2 GUI).
- **The PNG writer and `png_roundtrip` [built session 22]** —
  `live/png.{hpp,cpp}`: 8-bit greyscale, stored deflate blocks, tEXt
  chunks, no dependency, as decided session 16. The screamer verifies
  against an INDEPENDENT decoder (python3 stdlib zlib/struct/binascii —
  no shared code), container and pixels, three sizes, skipping (77) when
  python3 is absent; three writer mutations all rejected. `sips` reads
  the files too.
- **`nova-preview` [built session 22]** — the CLI that drives the real
  session machine over a recording and writes the PREVIEW, so the
  provisional picture can be looked at by eye (`--force IOC LPM` for
  recordings with no opening, `--phase/--sync` for the operator
  overrides). Checked by eye on VMW (phased from its phasing interval,
  live) and on the unanchored GYA cut with one `--phase` value.
- m4a input via runtime ffmpeg; WAV native.

## M4.5 — tuning aids  [pending; created session 17]
- Spectrum / waterfall display [DECIDED 2026-08-13, Sara, session 17:
  cut from M4, ships here]. It is the one part of the M4 window that
  serves tuning rather than decoding, and nothing in the M4 design —
  no thread, seam, screamer or core field — depends on it.
- Note that **no receiver in the 16-manual corpus has a waterfall**
  [docs/04]. It is an SDR-era affordance with no precedent in the survey,
  which is part of why it defers cleanly.
- What stays in M4 instead: a slim input level meter. Not a partial
  reversal of the cut — without a level readout, a muted, clipping or
  wrong-device input has no diagnosis and every failure looks like "no
  signal". It also has the precedent the waterfall lacks: the FAX-30
  shows signal strength and S/N while receiving [docs/04 Finding 4].

## M5 — packaging & release  [pending]
- CI: tier 1 release-tested (Win64, macOS universal, Linux x86_64);
  tier 2 CI-built (32-bit Win/Linux, ARM, FreeBSD).
- Zero compiler warnings on all runners (grep the log, not the tick).
- Compliance matrix fully populated with test links [docs/02].
- Pre-release: provenance/NOTICE audit, artifact download-and-inspect,
  similarity spot-check recorded (SOP L10 adapted: attribution check
  is the deliverable, since reuse is declared).

## Registered gaps
- Short windows of a deeply faded signal. On the full recording GYA 2300Z
  measures −116.8 ppm and draws straight, but 120 s windows of its faded
  stretch give anything from −1223 to +320 ppm: with few blocks there is
  no baseline to be accurate over. Live decoding (M4) will hit this, since
  it cannot wait for the whole transmission. No screamer — the fixture
  deliberately uses the stable recording (GYA 2324Z) instead.
- Picture content that mimics the optional sync pulse. A dark run at a
  fixed position on every line, followed by white, is by construction
  indistinguishable from the pulse of WMO §5.1.3.3 — the synthetic test
  pattern's own black bar and gradient strip produced 629 "locks" on a
  signal generated with no pulse at all. No library recording does this
  (session 4: white-only stations score 0.14–0.34), so it is registered,
  not fixed.
- Multiple transmissions in one recording (session 7). Segmentation takes
  the first — opening sequence to the first stop tone that follows it —
  and drops the rest; `jmh sample` loses the 143 s of the next chart it
  happens to catch. One recording, one image is the current model.
  Splitting a recording into several images is unbuilt, and not a
  milestone: no other library recording needs it.
- The phasing anchor is measured ONCE, at the middle of the interval, and
  propagated on the fitted clock for the rest of the recording (session 7).
  That is a fixed reference, not a tracked one: on a white-only station a
  mid-stream time-skip would shift the picture with nothing to re-acquire.
  No library recording exercises it — the one time-skip case, himawari, is
  a pulse station that re-acquires — but M4 live decode will.
  Session 8: reachable, not hypothetical. JSC2's timebase steps put its
  propagated phase 160 samples out over the ~90 lines between its two
  anchors. It is a pulse station, so tracking absorbs it; a white-only
  station with that timebase would be drawn wrong and nothing would notice.
  Session 9 narrows it: something would now notice — that combination is
  built synthetically in `roundtrip [10]` and the phasing statistic
  convicts it with no locks anywhere in the recording. The picture is still
  drawn wrong; the difference is that the decoder now says the timebase is
  not linear instead of reporting a confident clock figure.
- ~~Timebase steps are neither detected nor reported~~ — closed session 9.
  `DecodeResult::timebase` reports kLinear / kSteps / kUnknown from two
  independent statistics, and the count is six recordings, not two: every
  JSC file in the library steps, including the three 60 lpm ones. Three
  limits remain, each registered rather than hidden:
  - The reported step RATE is a floor, not a count. The ±8-line median that
    makes a step visible is wider than the gap between dense steps, so they
    merge: a synthetic inserting 90.9 per 1000 lines reports 36.9. Good for
    convicting a recording, useless for measuring an insertion rate.
  - Two recordings can be measured by neither statistic and report
    kUnknown: GYA 2300Z and VMW 2215Z are white-only (no tracked residual)
    with no phasing interval found (no edge to fit). A stepping timebase on
    such a recording would still be invisible — which is precisely the
    dangerous combination, since nothing tracks there either.
    **Session 10 explains both and closes neither.** VMW 2215Z genuinely
    contains no phasing interval — the recording starts mid-transmission,
    and the whole file holds three isolated single lines at positions 2657,
    1638 and 0. That is a measured negative, not a gap to close. GYA 2300Z
    DOES contain one, 40 lines at 4.5–24.5 s, and it is now found and used
    for the anchor; but its edge is too noisy (~15 samples line to line,
    against a 10-sample threshold) to test for steps, so the timebase
    verdict stays kUnknown with the reason named. The blind spot is
    therefore smaller in the way that matters — GYA's picture is now phased
    correctly — and unchanged in verdict count.
    A faded interval that IS stepping remains undetectable by either
    statistic. No library recording is both, so this is unexercised.
    A short cut of
    a perfectly good pulse station reports kUnknown too: the rate needs 128
    drawn lines, and a 60 s fixture at 120 lpm has 120. That is a floor on
    what a rate can honestly be measured over, not a bug, but it means M4's
    live decode has no verdict for the first minute of a transmission.
  - ~~Nothing repairs a stepping timebase~~ — closed session 11, and it
    became a milestone the moment Sara looked at the pictures (M2b). Where
    per-line sync exists the steps are now corrected as well as counted:
    ground-truth synthetic 2.19 px of bar scatter -> 0.00, JSC2 fixture
    2.89 px of line-start error -> 0.61. Where it does NOT exist — a
    white-only station — nothing is repaired and the paper still moves;
    that half is M2b's open item, with VMW 2215Z's staircase as its
    fixture-in-waiting.
- Segmentation costs a full `detect_tones` pass over the recording
  (session 7): ~9 s on the 61-minute JSC4 against a 37 s decode. Fine
  offline, unbudgeted for M4, where the scan wants to be incremental.
- ~~`phasing_anchor_delta` asserted nowhere~~ — closed session 8:
  `fixture_anchor_delta_jmh` and `fixture_anchor_delta_xsg`. The JSC2/−234
  vs JSC3/−55 disagreement that was filed here as unexplained is explained:
  those two recordings' timebases step, and their true porch is normal.
- 240 lpm: deliberately out of scope (not required by ISO 9876 §4.2.4).
- 90 lpm real fixture: none in the library (session 3 batch survey).
- IOC 288 real fixture: none found (no 675 Hz start tone anywhere).
- ±150 Hz LF mode: synthetic testing only, permanently. Sara (session 9)
  knows of no operating station still carrying it, so this is not a gap to
  close by finding a fixture — it is the honest end state.
