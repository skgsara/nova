cmake_minimum_required(VERSION 3.16)
project(nova VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

add_library(nova-core STATIC
  core/wav.cpp
  core/resample.cpp
  core/demod.cpp
  core/image.cpp
  core/fax.cpp
  core/gen.cpp
  core/tones.cpp
  core/phasing.cpp
)
target_include_directories(nova-core PUBLIC core)

add_executable(nova-decode cli/nova-decode.cpp)
target_link_libraries(nova-decode PRIVATE nova-core)

add_executable(nova-gen cli/nova-gen.cpp)
target_link_libraries(nova-gen PRIVATE nova-core)

add_executable(nova-tones cli/nova-tones.cpp)
target_link_libraries(nova-tones PRIVATE nova-core)

enable_testing()
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
add_test(NAME fixture COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-kiwisdr-image-60s.wav 120 115 125 -130 -40 0.95)
# Session 7: line counts below are POST-SEGMENTATION — the start tone and
# phasing interval are no longer drawn [WMO §5.2.3], so a fixture that
# opens inside one decodes fewer lines than it holds. Both of these do:
# 45 phasing lines in the first (its parent's phasing runs 72.5-102.5 s and
# the cut is 80-140 s), 69 lines of start tone + phasing in the LPE one.
add_test(NAME fixture_phasing_boundary COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-kiwisdr-60s.wav 120 70 80 -130 -40 0.85)
add_test(NAME fixture_lpe COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-60s.wav 120 46 56 -130 -40 0.75)
add_test(NAME fixture_warp COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/himawari-jmh-warp-120s.wav 120 230 248 -130 -40 0.85)
add_test(NAME fixture_60lpm COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/kyodo-news-jsc1-60lpm-120s.wav 60 95 125 150 450 0.9)
add_test(NAME fixture_white_sector COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-white-sector-120s.wav 120 230 248 -90 0 0.0
  --expect-white-only)
# Weak + white-only: the clock is the ONLY thing holding the picture
# straight, so the clock bound IS the picture-quality screamer here. The
# window is chosen so the old coarse-only estimate fails it: session 5
# measured -51.6 ppm coarse against -118.4 from the fold, and the fold
# reads -117 +/- 1.5 ppm on every window of this recording.
add_test(NAME fixture_weak_white COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/gya-weak-white-120s.wav 120 200 220 -135 -95 0.0
  --expect-white-only)
add_test(NAME fixture_fill_reject COMMAND nova-test-fixture --expect-reject
  ${CMAKE_SOURCE_DIR}/fixtures/stall-fill-15s.wav)
# The phasing line-start anchor [WMO §5.2.3.4], asserted against the
# decoded picture rather than against the anchor number — session 5's rule.
# VMW is white-only: this phase does not exist anywhere else in the signal,
# and with the image-derived anchor the chart came out rotated by 520 px.
add_test(NAME fixture_phasing_anchor COMMAND nova-test-fixture
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
add_test(NAME fixture_anchor_delta_jmh COMMAND nova-test-fixture   # 5/95
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-60s.wav 120 46 56 -130 -40 0.75
  --expect-anchor-delta -125 -25)
# Session 10: the drawn-line band moved 138-148 -> 128-140 because the
# phasing interval this fixture opens with is now measured as 31.5 s rather
# than 27 s, so nine more lines of it are cropped off the head instead of
# being drawn as picture. 31.5 s is the answer that matches WMO's ~30 s;
# the run was previously cut short at the first faded line. The anchor
# delta, which is what this test is actually about, did not move (-107.2).
add_test(NAME fixture_anchor_delta_xsg COMMAND nova-test-fixture   # 50/50
  ${CMAKE_SOURCE_DIR}/fixtures/xsg-phasing-image-100s.wav 120 128 140 -130 -60 0.85
  --expect-anchor-delta -160 -55)
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
add_test(NAME fixture_timebase_steps COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/kyodo-news-jsc2-steps-120s.wav 120 230 245 150 450 0.95
  --expect-timebase steps)
add_test(NAME fixture_timebase_linear COMMAND nova-test-fixture
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
add_test(NAME fixture_phasing_two_openings COMMAND nova-test-fixture
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
add_test(NAME fixture_phasing_one_skip COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/himawari-kiwisdr-phasing-jump-120s.wav
  120 155 175 -130 -40 0.85 --expect-timebase linear)
add_test(NAME fixture_faded_phasing COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/gya-faded-phasing-120s.wav 120 185 200 -135 -95 0.0
  --expect-white-only --expect-phasing-lines 30 --expect-timebase noisy)

# M3 control-signal suite: start/stop tone and phasing detection.
# [4], [5] and [9] are the false-start screamers — picture content with no
# control signals present must produce no events, and the purity margin
# that makes that work is asserted rather than assumed.
add_executable(nova-test-tones tests/test_tones.cpp)
target_link_libraries(nova-test-tones PRIVATE nova-core)
add_test(NAME tones COMMAND nova-test-tones)

# M3 fixture screamers on real off-air signals. Bounds measured 2026-08-12
# (session 6). The start/phasing case is VMW, a WHITE-ONLY station: it has
# no per-line sync in its dead sector at all, so this fixture is the
# evidence that its line-start phase is recoverable from the phasing stage.
# The --expect-none case is the false-start trap on real newspaper text.
add_executable(nova-test-tones-fixture tests/test_tones_fixture.cpp)
target_link_libraries(nova-test-tones-fixture PRIVATE nova-core)
add_test(NAME tones_fixture_vmw COMMAND nova-test-tones-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/vmw-start-phasing-100s.wav
  --expect-start 56.88 1.0 --expect-phasing 120 50 40)
add_test(NAME tones_fixture_no_false_start COMMAND nova-test-tones-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/kyodo-news-jsc1-60lpm-120s.wav --expect-none)
