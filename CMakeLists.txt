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
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-kiwisdr-60s.wav 120 115 125 -130 -40 0.85)
add_test(NAME fixture_lpe COMMAND nova-test-fixture
  ${CMAKE_SOURCE_DIR}/fixtures/test-chart-jmh-60s.wav 120 115 125 -130 -40 0.75)
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
