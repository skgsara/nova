# version_flag.cmake — every binary reports the SAME version, and reports
# the version CMake was told [E-GAP-001, Pass E].
#
# Run as:
#   cmake -DNOVA_VERSION=<x.y.z> -DNOVA_TOOLS=<name=path;name=path;...>
#         -P tests/version_flag.cmake
#
# What this can actually catch, stated plainly, because a check whose two
# sides are equal by construction catches nothing [session 30's lesson]:
# both sides here are NOT the same object. One side is `project(nova
# VERSION ...)` in CMakeLists.txt; the other is the bytes a built binary
# writes to stdout. They agree only as long as every tool goes through the
# generated core/version.hpp. The failure this exists for is the ordinary
# one — somebody writes "0.4.0" as a literal in one tool, or adds a fifth
# tool and forgets the flag entirely — and both of those make a real
# difference appear between the two sides.
#
# It also pins that each tool names ITSELF: `nova-tones --version` saying
# "nova-decode" is a copy-paste that a version-only comparison would pass.

if(NOT DEFINED NOVA_VERSION)
  message(FATAL_ERROR "version_flag: -DNOVA_VERSION=<x.y.z> required")
endif()
if(NOT DEFINED NOVA_TOOLS)
  message(FATAL_ERROR "version_flag: -DNOVA_TOOLS=<name=path;...> required")
endif()

# The placeholder is its own failure: a project that reaches a release
# decision still carrying CMake's default has not chosen a version, which
# is the whole substance of E-GAP-001.
if(NOVA_VERSION STREQUAL "0.0.0")
  message(FATAL_ERROR
    "version_flag: version is still CMake's 0.0.0 placeholder [E-GAP-001]")
endif()

set(_checked 0)
foreach(_entry IN LISTS NOVA_TOOLS)
  if(NOT _entry MATCHES "^([^=]+)=(.+)$")
    message(FATAL_ERROR "version_flag: malformed NOVA_TOOLS entry '${_entry}'")
  endif()
  set(_name "${CMAKE_MATCH_1}")
  set(_path "${CMAKE_MATCH_2}")

  if(NOT EXISTS "${_path}")
    message(FATAL_ERROR "version_flag: ${_name} not built at ${_path}")
  endif()

  # --version must answer with no other arguments at all. Every tool here
  # refuses a short command line, so this also pins that the flag is read
  # AHEAD of the argument-count check rather than after it.
  execute_process(COMMAND "${_path}" --version
                  RESULT_VARIABLE _rc
                  OUTPUT_VARIABLE _out
                  ERROR_VARIABLE _err
                  OUTPUT_STRIP_TRAILING_WHITESPACE)

  if(NOT _rc EQUAL 0)
    message(FATAL_ERROR
      "version_flag: ${_name} --version exited ${_rc}\n"
      "  stdout: ${_out}\n  stderr: ${_err}")
  endif()

  # On stdout, exactly "<tool> <version>", and nothing else: a packager
  # pipes this. Anything on stderr means the usage text leaked through.
  if(NOT _out STREQUAL "${_name} ${NOVA_VERSION}")
    message(FATAL_ERROR
      "version_flag: ${_name} --version printed '${_out}', "
      "expected '${_name} ${NOVA_VERSION}'")
  endif()
  if(NOT _err STREQUAL "")
    message(FATAL_ERROR
      "version_flag: ${_name} --version wrote to stderr: ${_err}")
  endif()

  message(STATUS "version_flag: ${_name} ${NOVA_VERSION} OK")
  math(EXPR _checked "${_checked} + 1")
endforeach()

# A loop over an empty list passes silently, which would make this suite
# green while checking nothing at all — the failure mode this project has
# found five times [see verify-the-instrument-first].
if(_checked LESS 4)
  message(FATAL_ERROR
    "version_flag: only ${_checked} tools checked; the four CLI tools are "
    "the minimum (nova-gui joins when FLTK and RtAudio are present)")
endif()

message(STATUS "version_flag: ${_checked} binaries agree on ${NOVA_VERSION}")
