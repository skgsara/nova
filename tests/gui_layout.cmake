# gui_layout.cmake — §9 screamer 7 [docs/05], run as
#   cmake -DNOVA_GUI=<path to nova-gui> -P tests/gui_layout.cmake
# Added as a ctest target only when the GUI target was built
# (NOVA_BUILD_GUI), so it is the +1 in "24 (+1 with the GUI)".
#
# Pins docs/05 §8.0 corrections 2 and 4 against `nova-gui --metrics`:
#   2. the ruler is aligned to the image pane's INTERIOR — ruler.x ==
#      pane.x + 2 and ruler.w == pane.w - 4 (the FL_DOWN_BOX bevel) — at
#      every window size, because this bug has been wrong twice already,
#      once as written and once under resize;
#   4. a window BUILT at a size and a window DRAGGED to it produce
#      byte-identical --metrics output, because the shell computes its
#      layout from the window size instead of letting FLTK scale the
#      children.

if(NOT DEFINED NOVA_GUI)
  message(FATAL_ERROR "gui_layout: -DNOVA_GUI=<path to nova-gui> required")
endif()

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

set(sizes "740x420" "980x700" "1200x800" "1400x900" "1920x1080")

# Correction 2, at every size. The ruler names image columns, so a tick
# that does not sit over its column is the one failure it cannot have.
foreach(size IN LISTS sizes)
  run_metrics(out --size ${size})
  region(ruler "${out}" rx ry rw rh)
  region(image_pane "${out}" px py pw ph)
  math(EXPR want_x "${px} + 2")
  math(EXPR want_w "${pw} - 4")
  if(NOT rx EQUAL want_x OR NOT rw EQUAL want_w)
    message(FATAL_ERROR
      "gui_layout FAIL at ${size}: ruler is ${rx},${ry} ${rw}x${rh}; "
      "pane interior is ${want_x},- ${want_w}x- "
      "(pane ${px},${py} ${pw}x${ph})")
  endif()
  message(STATUS "gui_layout PASS ${size}: ruler == pane interior "
    "(${rx},${ry} ${rw}x${rh})")
endforeach()

# Correction 4: built-at == dragged-to, byte for byte, at the two ends of
# the range.
foreach(size "740x420" "1400x900")
  run_metrics(built --size ${size})
  run_metrics(dragged --size 980x700 --resize ${size})
  if(NOT built STREQUAL dragged)
    message(FATAL_ERROR
      "gui_layout FAIL: built at ${size} differs from dragged to ${size}\n"
      "--- built ---\n${built}\n--- dragged ---\n${dragged}")
  endif()
  message(STATUS "gui_layout PASS: built at ${size} == dragged to ${size}")
endforeach()

message(STATUS "gui_layout: all checks passed")
