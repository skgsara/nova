# gui_metrics.cmake — the shared reader for `nova-gui --metrics` output,
# included by gui_layout.cmake and gui_shell.cmake so the two screamers
# cannot disagree about how to parse the shell they both drive.
#
# --metrics prints two blocks: a region table of `name x y w h` integers,
# and a "# shell state" block of `name "value"` pairs. region() reads the
# first, shell_value() the second, and no line of one can be mistaken for
# a line of the other — a state line has no four integers, and a region
# line has no quotes.
#
# Requires NOVA_GUI to be set by the including script.

function(run_metrics OUT)
  execute_process(
    COMMAND ${NOVA_GUI} --metrics ${ARGN}
    RESULT_VARIABLE rv
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
  if(NOT rv EQUAL 0)
    message(FATAL_ERROR
      "nova-gui --metrics ${ARGN} exited ${rv}\nstdout:\n${out}\nstderr:\n${err}")
  endif()
  set(${OUT} "${out}" PARENT_SCOPE)
endfunction()

# Extracts the x/y/w/h of a named region from --metrics output.
function(region NAME TEXT OUT_X OUT_Y OUT_W OUT_H)
  string(REGEX MATCH "(^|\n)  ${NAME} +(-?[0-9]+) +(-?[0-9]+) +(-?[0-9]+) +(-?[0-9]+)"
    m "${TEXT}")
  if(NOT m)
    message(FATAL_ERROR
      "region '${NAME}' not found in --metrics output:\n${TEXT}")
  endif()
  set(${OUT_X} "${CMAKE_MATCH_2}" PARENT_SCOPE)
  set(${OUT_Y} "${CMAKE_MATCH_3}" PARENT_SCOPE)
  set(${OUT_W} "${CMAKE_MATCH_4}" PARENT_SCOPE)
  set(${OUT_H} "${CMAKE_MATCH_5}" PARENT_SCOPE)
endfunction()

# --- the offline capture's trace [session 31] ------------------------------
# `nova-gui --mark NAME` prints one line per mark, WHERE IT IS ASKED FOR in
# the action sequence:
#
#   mark buffered  recv_active=1 recv_rows=84 ... pane_rows=246 saves=1 ...
#
# One process, several marks, because §8.2's rules are transitions — a
# buffer that survives an Apply, a pane that changes hands at a click — and
# a check spanning two processes cannot observe a transition [session 28].
#
# Unlike run_metrics this does NOT pass --metrics: the marks are the output.
function(run_actions OUT)
  execute_process(
    COMMAND ${NOVA_GUI} ${ARGN}
    RESULT_VARIABLE rv
    OUTPUT_VARIABLE out
    ERROR_VARIABLE err)
  if(NOT rv EQUAL 0)
    message(FATAL_ERROR
      "nova-gui ${ARGN} exited ${rv}\nstdout:\n${out}\nstderr:\n${err}")
  endif()
  set(${OUT} "${out}" PARENT_SCOPE)
endfunction()

# One mark's whole line, so a field can never be read out of the wrong mark.
function(mark_line NAME TEXT OUT)
  string(REGEX MATCH "(^|\n)mark ${NAME} +([^\n]*)" m "${TEXT}")
  if(NOT m)
    message(FATAL_ERROR "mark '${NAME}' not found in output:\n${TEXT}")
  endif()
  set(${OUT} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()

# One field of one mark, as a value — for carrying a number from one moment
# to another, which is how "the pane now holds the picture that WAS buffered"
# gets asked about two different numbers instead of about itself.
function(mark_get NAME FIELD TEXT OUT)
  mark_line("${NAME}" "${TEXT}" line)
  string(REGEX MATCH "(^| )${FIELD}=([^ ]+)" m "${line}")
  if(NOT m)
    message(FATAL_ERROR "field '${FIELD}' not in mark '${NAME}': ${line}")
  endif()
  set(${OUT} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()

function(mark_expect LABEL TEXT NAME FIELD WANT)
  mark_get("${NAME}" "${FIELD}" "${TEXT}" got)
  if(NOT got STREQUAL WANT)
    mark_line("${NAME}" "${TEXT}" line)
    message(FATAL_ERROR
      "gui_shell FAIL ${LABEL}: at mark '${NAME}', ${FIELD} is \"${got}\", "
      "want \"${WANT}\"\n  ${line}")
  endif()
endfunction()

# One field of one `panel NAME field="value"` line [session 37]. Separate
# from mark_get because these values CONTAIN SPACES — they are the strings
# the operator reads — so they are quoted and cannot be parsed by the
# unquoted mark reader.
function(panel_get NAME FIELD TEXT OUT)
  string(REGEX MATCH "(^|\n)panel ${NAME} +([^\n]*)" m "${TEXT}")
  if(NOT m)
    message(FATAL_ERROR "panel line '${NAME}' not found in output:\n${TEXT}")
  endif()
  set(line "${CMAKE_MATCH_2}")
  string(REGEX MATCH "(^| )${FIELD}=\"([^\"]*)\"" f "${line}")
  if(NOT f)
    message(FATAL_ERROR "field '${FIELD}' not in panel '${NAME}': ${line}")
  endif()
  set(${OUT} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()

function(panel_expect LABEL TEXT NAME FIELD WANT)
  panel_get("${NAME}" "${FIELD}" "${TEXT}" got)
  if(NOT got STREQUAL WANT)
    message(FATAL_ERROR
      "gui_shell FAIL ${LABEL}: at mark '${NAME}', the panel's ${FIELD} "
      "reads \"${got}\", want \"${WANT}\"")
  endif()
endfunction()

# Reads one `fit_NAME box needs "widest"` line from the text-fit block
# [session 37]. Three integers and a quoted string, so it can be mistaken
# for neither a region line (four integers, no quotes) nor a state line
# (a name and a quoted value, no integers between them).
function(fit_line NAME TEXT OUT_BOX OUT_NEEDS OUT_WIDEST)
  string(REGEX MATCH "(^|\n)  fit_${NAME} +(-?[0-9]+) +(-?[0-9]+) +\"([^\"]*)\""
    m "${TEXT}")
  if(NOT m)
    message(FATAL_ERROR
      "fit line 'fit_${NAME}' not found in --metrics output:\n${TEXT}")
  endif()
  set(${OUT_BOX} "${CMAKE_MATCH_2}" PARENT_SCOPE)
  set(${OUT_NEEDS} "${CMAKE_MATCH_3}" PARENT_SCOPE)
  set(${OUT_WIDEST} "${CMAKE_MATCH_4}" PARENT_SCOPE)
endfunction()

# Reads a quoted value from the "# shell state" block.
function(shell_value NAME TEXT OUT)
  string(REGEX MATCH "(^|\n)  ${NAME} +\"([^\"]*)\"" m "${TEXT}")
  if(NOT m)
    message(FATAL_ERROR
      "shell value '${NAME}' not found in --metrics output:\n${TEXT}")
  endif()
  set(${OUT} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()
