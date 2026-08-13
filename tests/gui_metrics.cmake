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

# Reads a quoted value from the "# shell state" block.
function(shell_value NAME TEXT OUT)
  string(REGEX MATCH "(^|\n)  ${NAME} +\"([^\"]*)\"" m "${TEXT}")
  if(NOT m)
    message(FATAL_ERROR
      "shell value '${NAME}' not found in --metrics output:\n${TEXT}")
  endif()
  set(${OUT} "${CMAKE_MATCH_2}" PARENT_SCOPE)
endfunction()
