# gui_layout.cmake — §9 screamer 7 [docs/05], run as
#   cmake -DNOVA_GUI=<path to nova-gui> -P tests/gui_layout.cmake
# Added as a ctest target only when the GUI target was built
# (NOVA_BUILD_GUI), so it is one of the two in "35 (+2 with the GUI)".
# (This header read "the +1 in 24 (+1 with the GUI)" until session 28,
# which was two counts and one GUI test out of date.)
#
# Pins docs/05 §8.0 corrections 2 and 4 against `nova-gui --metrics`:
#   2. the ruler is aligned to the image pane's INTERIOR — ruler.x ==
#      pane.x + 2 and ruler.w == pane.w - 4 (the FL_DOWN_BOX bevel) less
#      the vertical scrollbar when one is showing — at every window size,
#      because this bug has been wrong twice already, once as written and
#      once under resize;
#   4. a window BUILT at a size and a window DRAGGED to it produce
#      byte-identical --metrics output, because the shell computes its
#      layout from the window size instead of letting FLTK scale the
#      children.

if(NOT DEFINED NOVA_GUI)
  message(FATAL_ERROR "gui_layout: -DNOVA_GUI=<path to nova-gui> required")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/gui_metrics.cmake)

# The smallest window is the shell's own minimum, which the Zoom control
# raised from 740 to 880 [docs/05 §8.3]: nova-gui rejects a smaller
# --size, so this list moves whenever kMinW does.
set(sizes "880x420" "980x700" "1200x800" "1400x900" "1920x1080")

# Correction 2, at every size. The ruler names image columns, so a tick
# that does not sit over its column is the one failure it cannot have.
foreach(size IN LISTS sizes)
  run_metrics(out --size ${size})
  region(ruler "${out}" rx ry rw rh)
  region(image_pane "${out}" px py pw ph)
  shell_value(vscroll_px "${out}" vscroll)
  math(EXPR want_x "${px} + 2")
  math(EXPR want_w "${pw} - 4 - ${vscroll}")
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
foreach(size "880x420" "1400x900")
  run_metrics(built --size ${size})
  run_metrics(dragged --size 980x700 --resize ${size})
  if(NOT built STREQUAL dragged)
    message(FATAL_ERROR
      "gui_layout FAIL: built at ${size} differs from dragged to ${size}\n"
      "--- built ---\n${built}\n--- dragged ---\n${dragged}")
  endif()
  message(STATUS "gui_layout PASS: built at ${size} == dragged to ${size}")
endforeach()

# ---------------------------------------------------------------------------
# M4.5's tuning strip [ROADMAP M4.5].
#
# Three claims, none of which can be seen in a screenshot and all of which
# an operator would feel:
#   a. the strip EXTENDS the meter strip — full width, directly above the
#      meter, with the meter and the status line not moved by its presence
#      [docs/05 §8.3 item 5: not the sidebar, which is the wrong shape];
#   b. turning it off gives every one of its pixels back TO THE PICTURE, and
#      moves nothing else. A toggle that shrinks the picture permanently, or
#      that shifts the status line, is a toggle nobody will use twice;
#   c. the marker lines are where the band arithmetic puts the two WMO tones
#      [WMO §5.2.1]. 800-3000 Hz over 256 columns is 8.59375 Hz per column,
#      so 1500 Hz is column floor(700/8.59375) = 81 and 2300 Hz is
#      floor(1500/8.59375) = 174. These are hard-coded on purpose: they are
#      Sara's band decision of session 36 expressed as integers, and anyone
#      changing the band should have to come here and re-derive them.
foreach(size IN LISTS sizes)
  run_metrics(on --size ${size} --strip on)
  run_metrics(off --size ${size} --strip off)

  region(tuning_strip "${on}" sx sy sw sh)
  region(level_meter "${on}" mx my mw mh)
  region(status_line "${on}" tx ty tw th)
  region(window "${on}" wx wy ww wh)
  region(image_pane "${on}" px py pw ph)

  # (a) full width, and contiguous with the meter below it.
  #
  # Deliberately THREE checks with three messages rather than one condition
  # with three clauses. A bundled check is killed by whichever clause trips
  # first, which leaves the other two as unproven as if nothing had been
  # tested — session 31 recorded exactly that, and the first version of this
  # block reproduced it: a mutation aimed at the meter's position died on
  # the width clause.
  math(EXPR strip_bottom "${sy} + ${sh}")
  if(NOT sx EQUAL 0)
    message(FATAL_ERROR
      "gui_layout FAIL at ${size}: strip starts at x=${sx}, want 0 — it is a "
      "full-width strip, not a column")
  endif()
  if(NOT sw EQUAL ww)
    message(FATAL_ERROR
      "gui_layout FAIL at ${size}: strip is ${sw} wide in a ${ww} window; a "
      "frequency axis narrower than the window would mis-scale the markers")
  endif()
  if(NOT strip_bottom EQUAL my)
    message(FATAL_ERROR
      "gui_layout FAIL at ${size}: the strip ends at y=${strip_bottom} but "
      "the meter starts at y=${my} — the strip must sit directly on the "
      "meter, extending it rather than floating above a gap")
  endif()
  if(NOT sh EQUAL 72)
    message(FATAL_ERROR
      "gui_layout FAIL at ${size}: strip height is ${sh}, want 72 "
      "(20 trace + 40 waterfall + 12 axis)")
  endif()

  # (b) off: zero height, the picture gains exactly that, nothing else moves.
  region(tuning_strip "${off}" ox oy ow oh)
  region(level_meter "${off}" omx omy omw omh)
  region(status_line "${off}" otx oty otw oth)
  region(image_pane "${off}" opx opy opw oph)
  if(NOT oh EQUAL 0)
    message(FATAL_ERROR
      "gui_layout FAIL at ${size}: strip off still has height ${oh}")
  endif()
  math(EXPR gained "${oph} - ${ph}")
  if(NOT gained EQUAL 72)
    message(FATAL_ERROR
      "gui_layout FAIL at ${size}: hiding the strip gave the picture "
      "${gained} px, want 72 (pane ${ph} -> ${oph})")
  endif()
  if(NOT omy EQUAL my OR NOT oty EQUAL ty)
    message(FATAL_ERROR
      "gui_layout FAIL at ${size}: hiding the strip moved the meter "
      "(${my} -> ${omy}) or the status line (${ty} -> ${oty})")
  endif()

  # (c) the markers, and the state flag that says which layout this is.
  shell_value(strip_visible "${on}" vis_on)
  shell_value(strip_visible "${off}" vis_off)
  shell_value(strip_black_col "${on}" black)
  shell_value(strip_white_col "${on}" white)
  if(NOT vis_on EQUAL 1 OR NOT vis_off EQUAL 0)
    message(FATAL_ERROR
      "gui_layout FAIL at ${size}: strip_visible is ${vis_on} with --strip "
      "on and ${vis_off} with --strip off")
  endif()
  if(NOT black EQUAL 81 OR NOT white EQUAL 174)
    message(FATAL_ERROR
      "gui_layout FAIL at ${size}: the WMO tones are marked at columns "
      "${black} (1500 Hz) and ${white} (2300 Hz); the 800-3000 Hz band over "
      "256 columns puts them at 81 and 174")
  endif()
  message(STATUS "gui_layout PASS ${size}: tuning strip ${sw}x${sh} above "
    "the meter, markers at ${black}/${white}, off returns ${gained} px")
endforeach()

# Correction 4 again, with the strip hidden: the built-at == dragged-to
# invariant is about the layout FUNCTION, so it has to hold in both of the
# layouts that function can now produce.
foreach(size "880x420" "1400x900")
  run_metrics(built --size ${size} --strip off)
  run_metrics(dragged --size 980x700 --resize ${size} --strip off)
  if(NOT built STREQUAL dragged)
    message(FATAL_ERROR
      "gui_layout FAIL (strip off): built at ${size} differs from dragged "
      "to ${size}\n--- built ---\n${built}\n--- dragged ---\n${dragged}")
  endif()
  message(STATUS "gui_layout PASS: strip off, built at ${size} == dragged")
endforeach()

message(STATUS "gui_layout: all checks passed")
