cmake_minimum_required(VERSION 3.16)
# 0.4.5 is milestone-based, in the project's own M0-M4 language: M4.5,
# the tuning aids, is the milestone that is complete (M4, the live
# FLTK/RtAudio shell, was 0.4.0 in session 35). It is the ONLY place the
# version is written [E-GAP-001]; core/version.hpp.in is expanded from it
# and `version_flag` fails if any binary disagrees.
project(nova VERSION 0.4.5 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# -Wall -Wextra are GCC/Clang spellings; MSVC reads "-Wall" as /Wall, which
# is orders of magnitude noisier and warns on its own standard library.
# /W3 is MSVC's default; written out so the two branches read as one rule.
if(MSVC)
  add_compile_options(/W3)
else()
  add_compile_options(-Wall -Wextra)
endif()

# M4 is the first milestone with threads [docs/05 §2]: the RtAudio
# callback, the live decode, the batch decode and FLTK's main loop. Only
# the targets that actually start a thread link this.
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

# The version string reaches the code through exactly one generated header,
# so `--version` cannot drift from what this file declares [E-GAP-001].
configure_file(${CMAKE_SOURCE_DIR}/core/version.hpp.in
               ${CMAKE_BINARY_DIR}/generated/nova/version.hpp @ONLY)

add_library(nova-core STATIC
  core/wav.cpp
  core/resample.cpp
  core/demod.cpp
  core/image.cpp
  core/hooks.cpp
  core/fax.cpp
  core/gen.cpp
  core/tones.cpp
  core/phasing.cpp
)
target_include_directories(nova-core PUBLIC core ${CMAKE_BINARY_DIR}/generated)

# nova-live: everything M4 adds that can be wrong about a signal, with no
# FLTK, no RtAudio and no real clock [docs/05 §1], so a test can drive it
# with a fixture instead of a sound card. Dependency-free and built
# unconditionally, like nova-core. The ruler's column mapping
# (live/ruler.cpp), which the GUI consumes but does not own, and the
# streaming front end (live/stream.cpp), which wraps core's batch
# resample/demod in block-with-overlap so the live path and the saved
# image cannot disagree [docs/05 §2.2] — hence the link to nova-core.
# live/tone_stream.cpp is the same idea one stage later: the control-tone
# verdicts the live state machine needs as they arrive [docs/05 §5].
# live/preview.cpp is the same idea one stage later again: the picture
# itself, drawn forward-only as the rows arrive [docs/05 §6], against the
# same sync template core/fax.cpp locks onto (promoted to core/fax.hpp in
# session 21 so the two paths cannot disagree about which feature is the
# line start).
# live/session.cpp is the piece that turns those stages into a session:
# the eight states of docs/05 §4, the retained store and its freeze [§3],
# and the phasing watcher that hands the preview its anchor [§6 item 1].
# live/png.cpp is the save format: hand-rolled greyscale PNG, stored
# deflate blocks, no dependency [ROADMAP M4, decided session 16].
# live/spectrum.cpp is M4.5's tuning strip [ROADMAP M4.5]: a rolling
# spectrum over the RAW capture audio, before the resampler, because the
# operator is tuning a receiver and the strip has to show what the receiver
# is actually delivering rather than the video Nova derives from it. It is
# here rather than in the GUI for the reason everything else in this library
# is: a column that names the wrong frequency is a thing that can be wrong
# about a signal, and it is testable with a generated tone instead of a
# sound card.
add_library(nova-live STATIC
  live/ruler.cpp
  live/spectrum.cpp
  live/stream.cpp
  live/tone_stream.cpp
  live/preview.cpp
  live/session.cpp
  live/png.cpp
  live/engine.cpp
)
target_include_directories(nova-live PUBLIC live)
# live/engine.cpp starts threads 2 and 3 [docs/05 §2]; nova-live is
# otherwise still free of every dependency, FLTK and RtAudio included.
target_link_libraries(nova-live PUBLIC nova-core Threads::Threads)

add_executable(nova-decode cli/nova-decode.cpp)
target_link_libraries(nova-decode PRIVATE nova-core)

add_executable(nova-gen cli/nova-gen.cpp)
target_link_libraries(nova-gen PRIVATE nova-core)

add_executable(nova-tones cli/nova-tones.cpp)
target_link_libraries(nova-tones PRIVATE nova-core)

# The live preview, drivable by hand: the one way to LOOK at the
# provisional renderer's picture rather than read the screamer's account
# of it. Runs the real session machine, so a recording with no opening
# needs --force, exactly as the operator will.
add_executable(nova-preview cli/nova-preview.cpp)
target_link_libraries(nova-preview PRIVATE nova-live)

enable_testing()

# The recordings are not in this repository and never will be: they are
# off-air captures of broadcasts whose copyright varies by issuing service,
# and in one case (JSC, Kyodo News) is a commercial newspaper's. What ships
# instead is fixtures/MANIFEST.md — every fixture's identity, provenance and
# SHA-256 — plus the bounds, which are in this file beside each test.
#
# 30 of the 40 suites need one of those recordings. From a checkout without
# them they are still REGISTERED and reported Skipped, with the reason,
# rather than quietly not existing — a suite that shrinks from 40 to 10
# while
# reporting a green run tells you nothing is wrong when two thirds of the
# evidence is missing.
#
# Known limit of the mechanism, stated because the alternative is believing
# it does more than it does: ctest counts a skipped test as passed in its
# headline, so `ctest` still ends with "100% tests passed out of 40". What
# carries the truth is the "The following tests did not run:" block printed
# after it — 30 lines, each naming a suite — and the configure-time message
# below. Making the skips FAIL instead would be louder and wrong: a checkout
# without recordings is a supported state, not a broken one.
#
# Session 35: this is also why CI does not read ctest's exit code alone.
# `tools/check-suite-inventory.sh` asserts the registered and skipped
# counts, so a suite that quietly stops being registered fails the gate
# instead of passing as a smaller green run [E-GAP-002].
#
# The other 10 — the synthetic matrices, the tone detector, the ruler, the
# PNG round-trip, the audio ring, the hooks, the tuning spectrum, the
# version flag, the untrusted-input guards and the GUI layout — generate
# what they need and run anywhere.
set(NOVA_FIXTURE_SENTINEL
    ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-kiwisdr-image-60s.wav)
if(EXISTS ${NOVA_FIXTURE_SENTINEL})
  set(NOVA_HAVE_FIXTURES ON)
else()
  set(NOVA_HAVE_FIXTURES OFF)
  message(STATUS
    "fixtures: ABSENT - 30 suites needing an off-air recording will be "
    "SKIPPED (see fixtures/MANIFEST.md); the fixture-independent suites "
    "still run")
endif()

# Registers a suite that needs a recording. Present: a normal test. Absent:
# a stub whose output ctest reads as a skip, so `ctest` reports 30 Skipped
# and names the reason instead of reporting a smaller green run.
function(nova_fixture_test)
  cmake_parse_arguments(NFT "" "NAME" "COMMAND" ${ARGN})
  if(NOVA_HAVE_FIXTURES)
    add_test(NAME ${NFT_NAME} COMMAND ${NFT_COMMAND})
  else()
    add_test(NAME ${NFT_NAME} COMMAND ${CMAKE_COMMAND} -E echo
      "SKIPPED: needs an off-air recording, which this repository does not \
carry - see fixtures/MANIFEST.md for its identity and SHA-256")
    set_tests_properties(${NFT_NAME} PROPERTIES
                         SKIP_REGULAR_EXPRESSION "SKIPPED:")
  endif()
endfunction()

add_executable(nova-test tests/test_roundtrip.cpp)
target_link_libraries(nova-test PRIVATE nova-core)
add_test(NAME roundtrip COMMAND nova-test)

add_executable(nova-test-fixture tests/test_fixture.cpp)
target_link_libraries(nova-test-fixture PRIVATE nova-core)
# Fixture suite — bounds measured 2026-08-12, re-bound session 4 after the
# consistency anchor + re-acquisition landed (see test_fixture.cpp header
# for each fixture's unique coverage). Lock bounds sit below the measured
# value with room for noise, but above what the previous code achieved, so
# a regression to the old anchor fails the suite.
# Session 11: --expect-straight-strip is the PICTURE-domain check the suite
# had no form of. It measures the dead-sector edge in the finished pixels —
# 90th percentile of its row-to-row move — and shares no code with the
# decoder; the same flag also holds the decoder's own account of the error
# (place_rms_px) to the same bound, so a disagreement between the two is a
# failure rather than a matter of taste. Bounds sit between the measured new
# value and the measured pre-session-11 one on the same fixture.
#
# The clean reference: a picture with nothing wrong with it must not acquire
# jitter from a corrector meant for the ones that do (1.0 px measured, and
# 0.84 px of place error, unchanged by session 11).
nova_fixture_test(NAME fixture COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-kiwisdr-image-60s.wav 120 115 125 -130 -40 0.95
  --expect-straight-strip 1.5 --expect-rigid-rows 3.0)
# Session 7: line counts below are POST-SEGMENTATION — the start tone and
# phasing interval are no longer drawn [WMO §5.2.3], so a fixture that
# opens inside one decodes fewer lines than it holds. Both of these do:
# 45 phasing lines in the first (its parent's phasing runs 72.5-102.5 s and
# the cut is 80-140 s), 69 lines of start tone + phasing in the LPE one.
nova_fixture_test(NAME fixture_phasing_boundary COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-kiwisdr-60s.wav 120 70 80 -130 -40 0.85)
nova_fixture_test(NAME fixture_lpe COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-60s.wav 120 46 56 -130 -40 0.75)
# The warp fixture is where session 11 shows most: this recording loses
# ~1270 samples in one go, and until session 11 the assembly threw that
# residual away as out of range and drew the whole first half of the picture
# 574 px across (edge move p90 31.2 px). It now follows the skip in a single
# line — one seam, both halves in place, p90 1.0. Its first nine lines do
# not lock either, which is the other half of the same fix: coasting used to
# count as a correction level, so the clamp then walked the first REAL
# measurement in at 0.03 lines a time and the first two drawn lines came out
# 80.7 and 26.3 px out (place rms 5.72 -> 0.64, clamped 10 -> 0).
nova_fixture_test(NAME fixture_warp COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/himawari-jmh-warp-120s.wav 120 230 248 -130 -40 0.85
  --expect-straight-strip 2.0)
# 60 lpm AND stepping: JSC1 inserts samples like the rest of its family, so
# this fixture pins that the corrector works at the other line rate too
# (place rms 3.81 -> 1.38 px, edge p90 4.0 -> 3.0).
nova_fixture_test(NAME fixture_60lpm COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/kyodo-news-jsc1-60lpm-120s.wav 60 95 125 150 450 0.9
  --expect-straight-strip 3.5)
# XSG FYCI 120-240 s: a phasing interval (12-42 s into the cut) followed by
# the FIRST lines of the picture, which is where the assembly's smoothing
# window can still see the control signal. Cut session 11 because no fixture
# covered that: the effect is invisible to every number the decoder
# produces — the first drawn lines do not lock, so the place error reads
# 0.13 px either way — and shows only in the picture, as eight lines sitting
# 88.6 px from the rest of the chart.
nova_fixture_test(NAME fixture_picture_head COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/xsg-fyci-phasing-head-120s.wav 120 100 200 -130 -60 0.7
  --expect-straight-strip 1.5)
# JMH KiwiSDR Himawari 340-460 s: the DROPOUT case, cut session 11b. The
# recording's phase moves ~1270 samples in the middle of it, and the eight
# lines that straddle the move carry no sync lock — so the ±8-line window
# places them where the lines BEFORE the move are, ~75 px from the rest of
# the chart. Sara found that band still there after session 11, and again
# after session 11b: its picture placement searches ±120 px and the true
# move is 574, so the band stayed torn. Session 12 re-locks the rows on the
# SIGNAL — the sync pulse is in the audio at the far level, scores 0.94-0.96
# there against <= 0.22 at the near one — and splits the one row the drop
# landed inside. Nothing the decoder reports about itself sees any of this
# (place rms 0.60 px either way) and neither does the dead-sector edge,
# because those rows have no clean edge to measure: only the picture does,
# by asking what each row matches. 2 rows out of place before 11b, 1 after
# and still 1 after session 12 — the one that remains is the row the drop
# destroyed, whose content is genuinely not in the recording. The check
# lives ONLY here: it asks what each row matches, and a resolution grating
# matches its neighbour at several shifts by construction (the JMH test
# chart reads 23 on a picture with nothing wrong with it).
nova_fixture_test(NAME fixture_dropout COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/himawari-kiwisdr-dropout-120s.wav 120 230 245 -130 -40 0.9
  --expect-rows-in-place 1)
# The false-lock fixture, cut session 26 from HLL 2147Z (Korea Met, 500 hPa
# analysis) as received through the KiwiSDR browser hop and Sara's
# QuickTime/BlackHole parallel capture. On a faded signal whose picture
# content crowds the white gap, the pulse template's white window is
# polluted and a position ~60 samples late out-scores the true one
# (0.72 vs 0.44, measured) — the tracker hops and hops back, and before
# session 26
# the change-point detector blessed each hop as a seam: 8 seams in this 40
# s cut, and the porch edge jogged 22 px for two rows. Now a level move
# that returns within the vouching distance is cancelled instead of
# followed (2 seams, both under 2 px) and the porch never moves more than
# 5 px; the bound sits mid-gap between that and the 22 it was. The check
# is the picture's RIGHT edge, not the dead-sector strip: here the strip
# is unmeasurable by construction, because the same coastline that crowds
# the gap also breaks the 12-light-pixel run the strip statistic looks
# for.
nova_fixture_test(NAME fixture_false_locks COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/hll-2147z-false-locks-40s.wav 120 75 82 -100 -40 0.7
  --expect-straight-porch 10.0)
nova_fixture_test(NAME fixture_white_sector COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-white-sector-120s.wav 120 230 248 -90 0 0.0
  --expect-white-only)
# Weak + white-only: the clock is the ONLY thing holding the picture
# straight, so the clock bound IS the picture-quality screamer here. The
# window is chosen so the old coarse-only estimate fails it: session 5
# measured -51.6 ppm coarse against -118.4 from the fold, and the fold
# reads -117 +/- 1.5 ppm on every window of this recording.
nova_fixture_test(NAME fixture_weak_white COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/gya-weak-white-120s.wav 120 200 220 -135 -95 0.0
  --expect-white-only)
nova_fixture_test(NAME fixture_fill_reject COMMAND nova-test-fixture --expect-reject
  ${CMAKE_SOURCE_DIR}/fixtures/stall-fill-15s.wav)
# The phasing line-start anchor [WMO §5.2.3.4], asserted against the
# decoded picture rather than against the anchor number — session 5's rule.
# VMW is white-only: this phase does not exist anywhere else in the signal,
# and with the image-derived anchor the chart came out rotated by 520 px.
nova_fixture_test(NAME fixture_phasing_anchor COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-phasing-image-160s.wav 120 240 250 -90 -50 0.0
  --expect-phasing-anchor)
# Session 8: the two independent anchors must agree, on a PULSE station,
# where the phasing anchor is measured but never used — so nothing else in
# the suite corroborates either of them there. Bands measured across the
# library (see the note in test_fixture.cpp); one covers each of the two
# phasing waveforms WMO §5.2.3.1-.2 permits, because the edge convention is
# what is being pinned and the two waveforms reach it by different code.
# Both fixtures have an EVEN number of phasing lines, deliberately: reverting
# session 7's integer-line anchor fix moves them to +1928.7 and +1892.6, half
# a line out, and both tests fail. The obvious third candidate,
# test-chart-jmh-kiwisdr-60s.wav, is NOT used here — its run is 45 lines,
# and an odd run is the one case that bug cannot reach.
nova_fixture_test(NAME fixture_anchor_delta_jmh COMMAND nova-test-fixture   # 5/95
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-60s.wav 120 46 56 -130 -40 0.75
  --expect-anchor-delta -125 -25)
# Session 10: the drawn-line band moved 138-148 -> 128-140 because the
# phasing interval this fixture opens with is now measured as 31.5 s rather
# than 27 s, so nine more lines of it are cropped off the head instead of
# being drawn as picture. 31.5 s is the answer that matches WMO's ~30 s;
# the run was previously cut short at the first faded line. The anchor
# delta, which is what this test is actually about, did not move (-107.2).
# Session 11 also uses this fixture for the OTHER thing a phasing->image
# boundary can break: the assembly's smoothing window must not reach back
# into the phasing region, where the template anchors half a dead sector
# away [WMO §5.2.3.1]. It did, so the first drawn lines were placed at the
# control signal's phase and then jumped 88.6 px into the picture.
nova_fixture_test(NAME fixture_anchor_delta_xsg COMMAND nova-test-fixture   # 50/50
  ${CMAKE_SOURCE_DIR}/fixtures/xsg-phasing-image-100s.wav 120 128 140 -130 -60 0.85
  --expect-anchor-delta -160 -55 --expect-straight-strip 3.5)
# Session 9: is the recording's timebase the straight line everything
# upstream assumes? Two fixtures, chosen so that each isolates one half of
# the test and neither can pass by accident.
#
#   JSC2 200-320 s is pure image, so the phasing statistic does not exist
#   on it and only the tracked sync residual can convict: it reads 88.2
#   steps per 1000 lines against 0.0-7.0 on every clean recording in the
#   library. Its +323 ppm clock is out of the -130..0 family precisely
#   because a stepping recording's clock_ppm is clock PLUS insertion rate,
#   so the clock band here is wide and deliberately NOT a linearity claim.
#
#   himawari-jmh-warp-120s is the adversarial LINEAR case: it carries a
#   real KiwiSDR stream time-skip of ~595 px, the largest single phase jump
#   in the fixture set. One skip is not a stepping timebase, and a detector
#   that convicts on max_step or on any single event calls this file
#   non-linear and fails here. (Its rate is 1.6/1000.)
# Session 11b adds the third: the rows are RIGID. Samples inserted in the
# middle of a line move everything after the insertion point and nothing
# before it, which no per-line offset can undo — the two ends of a JSC2 row
# disagree by 10.0 px of 1810 without the intra-line pass and 1.0 px with
# it. The clean fixture above carries the same check at the same bound, so
# a picture with nothing wrong with it cannot be split into pieces either.
#
# Session 11 adds the second claim this fixture can carry: the timebase
# steps AND the picture is drawn straight anyway. Its 239 lines are 100%
# locked, so nothing here is excused by a weak signal — the pre-session-11
# assembly still left 2.89 px of place error on it, which is the flat
# +/-8-line median lagging every step. 1.42 px now.
nova_fixture_test(NAME fixture_timebase_steps COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/kyodo-news-jsc2-steps-120s.wav 120 230 245 150 450 0.95
  --expect-timebase steps --expect-straight-strip 1.5 --expect-rigid-rows 3.0)
nova_fixture_test(NAME fixture_timebase_linear COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/himawari-jmh-warp-120s.wav 120 230 248 -130 -40 0.85
  --expect-timebase linear)
# Session 10: GYA 2300Z's first 120 s, which carry a REAL 40-line phasing
# interval at 4.5-24.5 s that the detector reported as "no phasing" for
# nine sessions. It is deeply faded — only 23 of its 40 lines clear the
# per-line score, and the longest consecutive run of those is 3 — so a
# detector that grows runs from consecutive strong lines finds ten
# fragments and nothing that reaches min_lines. This is the ONLY station in
# the library whose line phase exists nowhere but here, so the fragments
# cost it its anchor entirely.
#
# It pins the other half too, and the two halves pull opposite ways: having
# recovered the interval, the timebase test must NOT then convict it. Its
# per-line edge moves ~15 samples line to line, which is above the 10-sample
# resolution the test claims, so the honest verdict is unmeasurable-because-
# noisy. Read as a raw spread it scores 46.2 — worse than JSC2's 25.5 on a
# recording that has no steps in it at all.
# Session 10: WHICH phasing interval, on a recording that carries two.
# FAXSignal's first 70 s run start tone 0-7 s, phasing 7-22 s, a SECOND
# 300 Hz burst 22-30.5 s, then a second phasing 32-64.5 s. This cut holds
# no stop tone, so no transmission window is known and the rule is "the
# first" — the branch this fixture pins, against "the longest", which takes
# the second interval (64 lines against 30). The windowed branch, where the
# LAST opening before the picture is the one wanted and the full FAXSignal
# recording therefore keeps its 32-64.5 s interval, is pinned with ground
# truth in `tones [13]`.
# 7.00-22.00 s is corroborated by the tone detector, which shares no code
# with this: the start tone ends at 7.00 s.
nova_fixture_test(NAME fixture_phasing_two_openings COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/faxsignal-two-openings-70s.wav
  120 70 85 -40 40 0.85 --expect-phasing-window 7.0 22.0)
# Session 10, the real one-skip case: JMH KiwiSDR Himawari's first 120 s.
# Its phasing interval straddles a single ~95-sample jump, with textbook
# linear edge either side, and reads 96.1 samples off straight — worse than
# any of the six genuinely stepping JSC recordings. The 1922 tracked lines
# of the full recording call it linear, and a phasing witness that convicts
# on the SIZE of the residual rather than on counted persistent moves
# over-rules them. Cut at 120 s so the image witness clears its 128-line
# floor and can be the one that answers.
nova_fixture_test(NAME fixture_phasing_one_skip COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/himawari-kiwisdr-phasing-jump-120s.wav
  120 155 175 -130 -40 0.85 --expect-timebase linear)
nova_fixture_test(NAME fixture_faded_phasing COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/gya-faded-phasing-120s.wav 120 185 200 -135 -95 0.0
  --expect-white-only --expect-phasing-lines 30 --expect-timebase noisy)

# M3 control-signal suite: start/stop tone and phasing detection.
# [4], [5] and [9] are the false-start screamers — picture content with no
# control signals present must produce no events, and the purity margin
# that makes that work is asserted rather than assumed.
add_executable(nova-test-tones tests/test_tones.cpp)
target_link_libraries(nova-test-tones PRIVATE nova-core)
add_test(NAME tones COMMAND nova-test-tones)

# Untrusted-input screamers, session 32, after audit Pass D measured a
# 144-byte file reaching ~12.9 GB of footprint and a WELL-FORMED file with a
# 1 Hz rate hanging in the resampler. Reads no fixture and generates its own
# inputs, deliberately: it is the one suite that must run from any checkout,
# including one with no recordings in it.
add_executable(nova-test-malformed tests/test_malformed.cpp)
target_link_libraries(nova-test-malformed PRIVATE nova-core)
add_test(NAME malformed COMMAND nova-test-malformed)

# M4 seam screamers: structured decode errors, log/progress callbacks,
# cooperative cancellation (docs/03 "Small core changes").
add_executable(nova-test-hooks tests/test_hooks.cpp)
target_link_libraries(nova-test-hooks PRIVATE nova-core)
add_test(NAME hooks COMMAND nova-test-hooks)

# M3 fixture screamers on real off-air signals. Bounds measured 2026-08-12
# (session 6). The start/phasing case is VMW, a WHITE-ONLY station: it has
# no per-line sync in its dead sector at all, so this fixture is the
# evidence that its line-start phase is recoverable from the phasing stage.
# The --expect-none case is the false-start trap on real newspaper text.
add_executable(nova-test-tones-fixture tests/test_tones_fixture.cpp)
target_link_libraries(nova-test-tones-fixture PRIVATE nova-core)
nova_fixture_test(NAME tones_fixture_vmw COMMAND nova-test-tones-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-start-phasing-100s.wav
  --expect-start 56.88 1.0 --expect-phasing 120 50 40)
nova_fixture_test(NAME tones_fixture_no_false_start COMMAND nova-test-tones-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/kyodo-news-jsc1-60lpm-120s.wav --expect-none)
# The library's only real STOP tone, cut session 21 from NMC 2204Z: the
# signal that ENDS a transmission, which every fixture before this one
# lacked, and which fades to nothing for 0.88 s in its middle — the
# gap-bridging rule meeting a real fade instead of a generated one.
nova_fixture_test(NAME tones_fixture_nmc_stop COMMAND nova-test-tones-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/nmc-image-stop-tone-120s.wav
  --expect-stop 111.38 1.0)

# §9 screamer 8 [docs/05]: the ruler's column mapping — the column under a
# screen x is the column the ruler names there, at every zoom, scroll
# offset and both IOC widths. Unguarded: it tests a pure nova-live
# function, so it needs no window and no audio device and runs in every
# build, including NOVA_BUILD_GUI=OFF. It is one of the 38 (+2 with the
# GUI). (This line read "24, +1 with the GUI" until session 29 — two
# counts out of date, and missed by session 28's sweep of exactly this
# class of error, which corrected three others.)
add_executable(nova-test-ruler tests/test_ruler_mapping.cpp)
target_link_libraries(nova-test-ruler PRIVATE nova-live)
add_test(NAME ruler_mapping COMMAND nova-test-ruler)

# §9 screamer 1 [docs/05]: the streaming front end equals the whole-file
# path. Block-with-overlap over core's batch resample/demod, at every
# block size a capture can produce, on a real recording and on generated
# signals at 44100 and 48000 — the rates a sound card actually offers,
# which the 8 kHz fixtures cannot exercise. Also measures the demod
# overlap rather than assuming it. Unguarded: no window, no audio device.
add_executable(nova-test-live-equiv tests/test_live_equiv.cpp)
target_link_libraries(nova-test-live-equiv PRIVATE nova-live)
nova_fixture_test(NAME live_demod_equiv COMMAND nova-test-live-equiv
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-kiwisdr-image-60s.wav)

# §9 screamer 2 [docs/05]: the streaming tone detector reaches the same
# verdicts as detect_tones, as they arrive. Every fixture in the library
# is passed, not only the six that carry a control tone: the eleven that
# carry none are the false-start trap in a second implementation, and a
# streaming detector that invents a start tone would begin a decode out
# of noise with nothing in the saved image to show it. The generated
# cases inside the test cover the stop tone and the IOC 288 opening,
# which no library recording has [AGENTS.md registered gaps].
# Unguarded: no window, no audio device.
# §9 screamer 3 [docs/05]: the provisional renderer draws the picture
# forward-only, and the picture does not depend on how the audio callback
# chunked the stream. Every fixture that holds a picture is rendered and
# compared with the batch image's dead-sector column — the two paths share
# the sync template and nothing else, so agreeing about which column it is
# in is two independent answers. Also pins the live half of the PHASE/SYNC
# asymmetry [docs/05 §7.1], which nothing else covered, and generates an
# IOC 288 signal because no library recording carries one.
# Unguarded: no window, no audio device.
add_executable(nova-test-live-preview tests/test_live_preview.cpp)
target_link_libraries(nova-test-live-preview PRIVATE nova-live)
nova_fixture_test(NAME live_preview COMMAND nova-test-live-preview
  ${CMAKE_SOURCE_DIR}/fixtures/faxsignal-two-openings-70s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/nmc-image-stop-tone-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/gya-faded-phasing-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/gya-weak-white-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/himawari-jmh-warp-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/himawari-kiwisdr-dropout-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/himawari-kiwisdr-phasing-jump-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/kyodo-news-jsc1-60lpm-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/kyodo-news-jsc2-steps-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/stall-fill-15s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-60s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-kiwisdr-60s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-kiwisdr-image-60s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-phasing-image-160s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-start-phasing-100s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-white-sector-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/xsg-fyci-phasing-head-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/xsg-phasing-image-100s.wav)

add_executable(nova-test-live-tones tests/test_live_tones.cpp)
target_link_libraries(nova-test-live-tones PRIVATE nova-live)
nova_fixture_test(NAME live_tones COMMAND nova-test-live-tones
  ${CMAKE_SOURCE_DIR}/fixtures/faxsignal-two-openings-70s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/nmc-image-stop-tone-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/gya-faded-phasing-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/gya-weak-white-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/himawari-jmh-warp-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/himawari-kiwisdr-dropout-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/himawari-kiwisdr-phasing-jump-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/kyodo-news-jsc1-60lpm-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/kyodo-news-jsc2-steps-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/stall-fill-15s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-60s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-kiwisdr-60s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-kiwisdr-image-60s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-phasing-image-160s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-start-phasing-100s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-white-sector-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/xsg-fyci-phasing-head-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/xsg-phasing-image-100s.wav)

add_executable(nova-test-live-session tests/test_live_session.cpp)
target_link_libraries(nova-test-live-session PRIVATE nova-live)
# §9 screamer 10 [docs/05]: the live session state machine. The fixture
# arguments are chosen for coverage, in order: a tone-driven WHITE-ONLY
# station with phasing (the anchor handoff), two tone-driven pulse
# stations on the two phasing waveforms WMO §5.2.3.1-.2 permits, the
# two-openings case, and the library's only real stop tone, which carries
# the forced-start, operator-stop, no-opening and page-cap cases too.
# Unguarded: no window, no audio device.
nova_fixture_test(NAME live_session COMMAND nova-test-live-session
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-phasing-image-160s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-60s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/xsg-fyci-phasing-head-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/faxsignal-two-openings-70s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/nmc-image-stop-tone-120s.wav)

# §9 screamer 4 [docs/05]: the hand-rolled PNG writer decodes back to the
# source pixels under an INDEPENDENT decoder — python3's stdlib zlib,
# struct and binascii, which share no code with live/png.cpp. Needs
# python3, nothing else: absent, the test reports SKIP (77) rather than
# FAIL, one dependency lighter than the GUI tests' FLTK.
add_executable(nova-test-png tests/test_png.cpp)
target_link_libraries(nova-test-png PRIVATE nova-live)
add_test(NAME png_roundtrip COMMAND nova-test-png)
set_tests_properties(png_roundtrip PROPERTIES SKIP_RETURN_CODE 77)

# §9 screamer 11 [docs/05]: the audio ring of §2.1 — the project's first
# lock-free structure and the only one a REALTIME thread writes. Runs a
# real producer thread against a real consumer thread over four million
# samples, and counts the producer's allocations with a global operator
# new, because "no allocation in the callback" is a rule that fails as an
# audio glitch nobody traces back to this file. Header-only, so nothing
# but the test links it; unguarded, no window and no audio device.
# §9 screamer 12 [docs/05]: the wiring of §2 — ring, front end, session
# and batch decode with the threads actually running. The claim is that
# threading changes NOTHING: same states, same rows in the same places,
# same saved pixels as the single-threaded path, at five audio block
# sizes. Fixture 1 is tone-driven (the whole walk with no operator
# input); the rest are forced-started. Unguarded: no window, no audio
# device, and the only file it writes is under the build tree.
add_executable(nova-test-live-engine tests/test_live_engine.cpp)
target_link_libraries(nova-test-live-engine PRIVATE nova-live)
nova_fixture_test(NAME live_engine COMMAND nova-test-live-engine
  ${CMAKE_CURRENT_BINARY_DIR}
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-phasing-image-160s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-60s.wav)

add_executable(nova-test-ring tests/test_ring.cpp)
target_link_libraries(nova-test-ring PRIVATE nova-live Threads::Threads)
add_test(NAME live_ring COMMAND nova-test-ring)

# §9 screamers 5 and 6 [docs/05 §7.1]: the operator's two corrections reach
# the batch decode, and they behave ASYMMETRICALLY when they get there —
# PHASE seeds the anchor search and is refined off, SYNC is used only where
# the period fit had no baseline. Two ctest targets over one binary, for the
# same reason `gui_layout` and `gui_shell` are two: they are two decisions,
# and a run that says "overrides FAILED" would not say which one broke.
# Unguarded: no window, no audio device; the decoy fixture is generated.
add_executable(nova-test-overrides tests/test_overrides.cpp)
target_link_libraries(nova-test-overrides PRIVATE nova-core)
nova_fixture_test(NAME override_phase_seed COMMAND nova-test-overrides phase
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-kiwisdr-image-60s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-white-sector-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-phasing-image-160s.wav)
nova_fixture_test(NAME override_sync_fallback COMMAND nova-test-overrides sync
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-kiwisdr-image-60s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-white-sector-120s.wav
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-phasing-image-160s.wav)

# M4.5's tuning strip [ROADMAP M4.5]. Unguarded and fixture-free: it
# generates its own tones, so it runs from a bare clone like the other
# synthetic matrices. The claims it defends are in Hz — a waterfall is the
# most convincing wrong answer in the project, because colourful noise in
# roughly the right shape reads as a working instrument, and an operator
# nulling a mistune against a marker line that names the wrong frequency has
# been actively misled rather than merely unhelped.
add_executable(nova-test-spectrum tests/test_spectrum.cpp)
target_link_libraries(nova-test-spectrum PRIVATE nova-live)
add_test(NAME tuning_spectrum COMMAND nova-test-spectrum)

# ---------------------------------------------------------------------------
# M4 — the FLTK/RtAudio shell. This is the ONLY target in the project with an
# external dependency [docs/05 §11]. The rule that shapes this block: a
# missing toolkit skips the target and never fails the build, because the
# core, the CLIs and the 38 GUI-independent tests must keep building on a
# machine with neither library installed.
#
# Found by their own tools rather than by find_package: neither FLTK 1.4.5
# nor RtAudio 6.0.1 installs a CMake config package under Homebrew, but both
# ship the interface they document — fltk-config and a .pc file.
option(NOVA_BUILD_GUI "Build the FLTK/RtAudio shell (nova-gui)" ON)

if(NOVA_BUILD_GUI)
  find_program(NOVA_FLTK_CONFIG fltk-config)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(NOVA_RTAUDIO QUIET rtaudio)
  endif()
  # Config-package fallback [M5, the Windows CI row]: a stock Windows
  # machine has neither fltk-config nor pkg-config, but FLTK 1.4 and
  # RtAudio 6 both install CMake config packages (targets fltk::fltk and
  # RtAudio::rtaudio) when built from source. The fltk-config/pkg-config
  # path stays first and is what the macOS and Linux rows still use; the
  # fallback only engages where that path found nothing.
  if(NOT NOVA_FLTK_CONFIG)
    find_package(FLTK CONFIG QUIET)
  endif()
  if(NOT NOVA_RTAUDIO_FOUND)
    find_package(RtAudio CONFIG QUIET)
  endif()

  if(NOVA_FLTK_CONFIG OR (FLTK_FOUND AND TARGET fltk::fltk))
    set(NOVA_HAVE_FLTK ON)
  else()
    set(NOVA_HAVE_FLTK OFF)
  endif()
  if(NOVA_RTAUDIO_FOUND OR TARGET RtAudio::rtaudio)
    set(NOVA_HAVE_RTAUDIO ON)
  else()
    set(NOVA_HAVE_RTAUDIO OFF)
  endif()

  if(NOT NOVA_HAVE_FLTK)
    message(STATUS "nova-gui: SKIPPED - FLTK not found "
      "(no fltk-config, no CMake config package)")
  elseif(NOT NOVA_HAVE_RTAUDIO)
    message(STATUS "nova-gui: SKIPPED - rtaudio not found "
      "(not in pkg-config, no CMake config package)")
  else()
    add_executable(nova-gui gui/nova-gui.cpp)
    # docs/05 §1 is core -> live -> gui, and the link line now says so
    # literally: the GUI links nova-live, which carries nova-core
    # transitively (naming both made the linker warn about a duplicate
    # library). The ruler's column mapping is called by the ruler's draw
    # code and by the zoom control, so no column arithmetic lives in a
    # widget; the streaming front end is there for the capture thread
    # that has not been written yet.
    if(NOVA_FLTK_CONFIG)
      execute_process(COMMAND ${NOVA_FLTK_CONFIG} --cxxflags
        OUTPUT_VARIABLE NOVA_FLTK_CXXFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
      execute_process(COMMAND ${NOVA_FLTK_CONFIG} --ldflags
        OUTPUT_VARIABLE NOVA_FLTK_LDFLAGS OUTPUT_STRIP_TRAILING_WHITESPACE)
      separate_arguments(NOVA_FLTK_CXXFLAGS NATIVE_COMMAND
        "${NOVA_FLTK_CXXFLAGS}")

      target_link_libraries(nova-gui PRIVATE nova-live ${NOVA_RTAUDIO_LIBRARIES})
      target_include_directories(nova-gui PRIVATE ${NOVA_RTAUDIO_INCLUDE_DIRS})
      target_link_directories(nova-gui PRIVATE ${NOVA_RTAUDIO_LIBRARY_DIRS})
      target_compile_options(nova-gui PRIVATE
        ${NOVA_FLTK_CXXFLAGS} ${NOVA_RTAUDIO_CFLAGS_OTHER})
      # LINK_FLAGS, not target_link_options, and the difference is a real bug
      # this skeleton hit: FLTK's ldflags end in two `-weak_framework NAME`
      # pairs, target_link_options de-duplicates the repeated `-weak_framework`
      # token, and the linker is then handed a bare `ScreenCaptureKit` it reads
      # as a filename. LINK_FLAGS is a string and goes through verbatim.
      # But LINK_FLAGS goes BEFORE the object files on the link line, and GNU
      # ld resolves left to right — an archive named before the objects that
      # need it is discarded, and every Fl_* symbol comes back undefined
      # (first Linux CI run, session 39). macOS's ld is order-insensitive,
      # which is why the bug was invisible for five weeks. So: Apple keeps
      # the verbatim string; everywhere else the ldflags go through
      # target_link_libraries, after the objects, in fltk-config's own order.
      if(APPLE)
        set_property(TARGET nova-gui APPEND_STRING PROPERTY
          LINK_FLAGS " ${NOVA_FLTK_LDFLAGS}")
      else()
        separate_arguments(NOVA_FLTK_LDFLAGS_LIST NATIVE_COMMAND
          "${NOVA_FLTK_LDFLAGS}")
        target_link_libraries(nova-gui PRIVATE ${NOVA_FLTK_LDFLAGS_LIST})
      endif()
      message(STATUS "nova-gui: enabled (${NOVA_FLTK_CONFIG}, rtaudio "
                     "${NOVA_RTAUDIO_VERSION})")
    else()
      # The config-package path: include dirs, system libraries and (for
      # static RtAudio on Windows) the audio-API import libs all ride on
      # the imported targets.
      target_link_libraries(nova-gui PRIVATE nova-live
        fltk::fltk RtAudio::rtaudio)
      message(STATUS "nova-gui: enabled (FLTK ${FLTK_VERSION} and RtAudio "
                     "via CMake config packages)")
    endif()
    # §9 screamer 7 [docs/05]: ruler/pane interior alignment at several
    # window sizes, and a window built at a size producing --metrics
    # output identical to one dragged to it. The first test in the
    # project to cover FLTK at all, and one of the two in "38 (+2 with the
    # GUI)".
    # It drives the built binary through a CMake script so it stays
    # portable; it exists only when nova-gui does.
    add_test(NAME gui_layout COMMAND ${CMAKE_COMMAND}
      -DNOVA_GUI=$<TARGET_FILE:nova-gui>
      -P ${CMAKE_SOURCE_DIR}/tests/gui_layout.cmake)
    # §9 screamer 9 [docs/05]: what the shell DOES rather than where its
    # regions are — the transport relabelling and sensitivity, Force
    # Start's gating on explicit IOC + rate, the ruler staying blank
    # until the image width is known, the preference file beside the
    # program, and (sessions 27-28) the whole correction surface: the
    # sixteen-row Apply/Auto rule, the SYNC steppers, click-to-set-PHASE
    # and the two-click slant. Driven through --state / --metrics /
    # --click / --nudge / --then-state, so it needs no window and no
    # audio device; guarded with nova-gui, the second of the two
    # in "38 (+2 with the GUI)".
    # NOVA_FIXTURE and NOVA_TMP arrived with session 31's offline capture:
    # §8.2's receiving indicator needs a transmission arriving behind an
    # edit, so this suite now feeds a real recording through the real
    # engine. It still opens no audio device and no window, and the only
    # thing it writes is under the build tree — `--feed` REFUSES without an
    # explicit folder, which is itself one of the checks.
    nova_fixture_test(NAME gui_shell COMMAND ${CMAKE_COMMAND}
      -DNOVA_GUI=$<TARGET_FILE:nova-gui>
      -DNOVA_FIXTURE=${CMAKE_SOURCE_DIR}/fixtures/vmw-phasing-image-160s.wav
      -DNOVA_TMP=${CMAKE_CURRENT_BINARY_DIR}/gui-shell-capture
      -P ${CMAKE_SOURCE_DIR}/tests/gui_shell.cmake)
  endif()
else()
  # The OFF path said nothing at all until session 34 (C-GAP-003): the
  # SKIPPED lines above only fire when the deps are missing, so a build
  # deliberately configured without the GUI was silent about it.
  message(STATUS "nova-gui: SKIPPED - NOVA_BUILD_GUI=OFF")
endif()

# --version, and the rule that all of them agree [E-GAP-001, Pass E].
# Registered last because the list of binaries depends on whether the GUI
# was built; nova-gui joins the same suite rather than getting a second
# one, because the claim is about the set, not about each tool.
# Fixture-independent: it runs from any checkout, so CI reaches it.
set(NOVA_VERSION_TOOLS
  "nova-decode=$<TARGET_FILE:nova-decode>"
  "nova-gen=$<TARGET_FILE:nova-gen>"
  "nova-tones=$<TARGET_FILE:nova-tones>"
  "nova-preview=$<TARGET_FILE:nova-preview>")
if(TARGET nova-gui)
  list(APPEND NOVA_VERSION_TOOLS "nova-gui=$<TARGET_FILE:nova-gui>")
endif()
add_test(NAME version_flag COMMAND ${CMAKE_COMMAND}
  -DNOVA_VERSION=${PROJECT_VERSION}
  "-DNOVA_TOOLS=$<JOIN:${NOVA_VERSION_TOOLS},;>"
  -P ${CMAKE_SOURCE_DIR}/tests/version_flag.cmake)
