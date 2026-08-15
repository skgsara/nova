# gui_shell.cmake — §9 screamer 9 [docs/05], run as
#   cmake -DNOVA_GUI=<path to nova-gui> -P tests/gui_shell.cmake
# Added as a ctest target only when the GUI target was built
# (NOVA_BUILD_GUI), so it is one of the two in "34 (+2 with the GUI)".
#
# gui_layout pins where the shell's regions ARE; this pins what the shell
# DOES — the §8.3 and §8.4 behaviour rules, which are the ones a widget
# edit can quietly break without moving a single pixel:
#
#   - the transport: one button relabelled by state, "Stop" from READY
#     through STOP TONE and "Start" everywhere else, insensitive during
#     DECODING and active again at SAVED, never reading a state name
#     [§8.3 item 4, §8.4 item 4];
#   - Force Start insensitive unless IOC and rate are BOTH explicit, and
#     insensitive while receiving [§8.4 item 3];
#   - the ruler blank and disabled while the image width is unknown, lit
#     with the right width and tick step when it is known [§8.3 item 1,
#     §8.4 item 5];
#   - the transport inert on an INSPECTION run: --metrics brings up no
#     capture, so a window must not claim to do what it cannot [§3].
#     Session 23 changed the reason (the program can capture now) but not
#     the check — it is also what keeps this suite runnable with no audio
#     device, and what stops inspecting Nova from opening a microphone;
#   - the preference file next to the executable is read at startup, and
#     merely inspecting the shell leaves no file behind [§8.4 item 1].
#
# --state drives the shell into a live state exactly as nova-live will,
# which is what makes all of this testable with no window and no audio
# device.

if(NOT DEFINED NOVA_GUI)
  message(FATAL_ERROR "gui_shell: -DNOVA_GUI=<path to nova-gui> required")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/gui_metrics.cmake)

function(expect LABEL TEXT NAME WANT)
  shell_value(${NAME} "${TEXT}" got)
  if(NOT got STREQUAL WANT)
    message(FATAL_ERROR
      "gui_shell FAIL ${LABEL}: ${NAME} is \"${got}\", want \"${WANT}\"")
  endif()
endfunction()

# --- the transport, state by state [§8.3 item 4, §8.4 item 4] ---------------
# Both dropdowns are explicit here so that Force Start's gating is
# satisfied and what is left under test is the STATE's effect on it.
# The device column is §8.3 item 9: the Device menu reopens the stream,
# so it is insensitive from READY through DECODING — the two states with
# nothing to lose are IDLE and SAVED.
# Fields: state | button label | start active | force active | progress active
#         | device menu active
set(rows
  "idle|Start|1|1|0|1"
  "ready|Stop|1|0|0|0"
  "start-tone|Stop|1|0|0|0"
  "phasing|Stop|1|0|0|0"
  "drawing|Stop|1|0|0|0"
  "stop-tone|Stop|1|0|0|0"
  "decoding|Start|0|0|1|0"
  "saved|Start|1|1|0|1")

foreach(row IN LISTS rows)
  string(REPLACE "|" ";" f "${row}")
  list(GET f 0 st)
  list(GET f 1 want_label)
  list(GET f 2 want_start)
  list(GET f 3 want_force)
  list(GET f 4 want_progress)
  list(GET f 5 want_device)

  run_metrics(out --state ${st} --ioc 576 --rate 120)
  expect("state ${st}" "${out}" start_label "${want_label}")
  expect("state ${st}" "${out}" start_active "${want_start}")
  expect("state ${st}" "${out}" force_active "${want_force}")
  # The progress bar is populated ONLY during DECODING, from the nine
  # decode stages — it is not a general busy indicator [§4, §8].
  expect("state ${st}" "${out}" progress_active "${want_progress}")
  expect("state ${st}" "${out}" device_active "${want_device}")

  # The button never reads a state name: states live in the status line
  # [docs/04 Finding 3], and a button that narrates is a fake control.
  shell_value(start_label "${out}" label)
  if(NOT label MATCHES "^(Start|Stop)$")
    message(FATAL_ERROR
      "gui_shell FAIL state ${st}: button reads \"${label}\", which is not "
      "Start or Stop")
  endif()
  # ... and the status line does carry one.
  shell_value(state "${out}" token)
  string(TOUPPER "${st}" want_token)
  if(NOT token STREQUAL want_token)
    message(FATAL_ERROR
      "gui_shell FAIL state ${st}: status line reads \"${token}\"")
  endif()
  message(STATUS "gui_shell PASS ${st}: [${label}] start=${want_start} "
    "force=${want_force} progress=${want_progress}")
endforeach()

# --- Force Start needs BOTH dropdowns explicit [§8.4 item 3] ---------------
# Deactivate, never prompt: with either on Auto the button cannot start,
# and it says so by being insensitive rather than by opening a dialog.
set(gates
  "auto|auto|0"
  "576|auto|0"
  "auto|120|0"
  "576|120|1"
  "288|60|1")
foreach(row IN LISTS gates)
  string(REPLACE "|" ";" f "${row}")
  list(GET f 0 g_ioc)
  list(GET f 1 g_rate)
  list(GET f 2 want_force)
  run_metrics(out --state idle --ioc ${g_ioc} --rate ${g_rate})
  expect("force gate ioc=${g_ioc} rate=${g_rate}" "${out}" force_active
    "${want_force}")
  message(STATUS
    "gui_shell PASS force gate: ioc=${g_ioc} rate=${g_rate} -> ${want_force}")
endforeach()

# --- the ruler is blank until the width is known [§8.3 item 1] -------------
# Width is round(IOC * pi), measured from the real decoder: 1810 columns
# at IOC 576, 905 at IOC 288. In AUTO, before a start tone, Nova does not
# know which, and a ruler drawn on a guess would be a lie in the one
# place a lie is most expensive.
set(widths "auto|0|0" "576|1|1810" "288|1|905")
foreach(row IN LISTS widths)
  string(REPLACE "|" ";" f "${row}")
  list(GET f 0 w_ioc)
  list(GET f 1 want_active)
  list(GET f 2 want_cols)
  run_metrics(out --state ready --ioc ${w_ioc} --rate 120)
  expect("ruler ioc=${w_ioc}" "${out}" ruler_active "${want_active}")
  expect("ruler ioc=${w_ioc}" "${out}" image_cols "${want_cols}")
  message(STATUS
    "gui_shell PASS ruler: ioc=${w_ioc} -> active=${want_active} "
    "cols=${want_cols}")
endforeach()

# --- tick step follows the displayed scale, not the image [§8.3 item 1] ----
# The same three numbers ruler_mapping pins as pure arithmetic, checked
# here through the real window geometry so that the pane interior the
# shell computes is the one the mapping was given.
set(ticks
  "980x700|576|fit|100"   # the default window
  "880x420|576|fit|200"   # the minimum window the Zoom control forces
  "980x700|576|200|20"    # the doc's unconditional example
  "980x700|288|fit|50")
foreach(row IN LISTS ticks)
  string(REPLACE "|" ";" f "${row}")
  list(GET f 0 t_size)
  list(GET f 1 t_ioc)
  list(GET f 2 t_zoom)
  list(GET f 3 want_step)
  run_metrics(out --size ${t_size} --state ready --ioc ${t_ioc}
    --rate 120 --zoom ${t_zoom})
  expect("tick ${t_size} ioc=${t_ioc} zoom=${t_zoom}" "${out}" tick_step
    "${want_step}")
  message(STATUS "gui_shell PASS tick step: ${t_size} ioc=${t_ioc} "
    "zoom=${t_zoom} -> ${want_step} columns")
endforeach()

# --- scrollbars only when the image exceeds the pane [§8.3 item 3] ---------
# In a 980 px window the pane interior is 768 px, and an IOC 576 chart is
# 1810 columns: 452 px at 25% (fits), 905 px at 50% (does not). Fit never
# scrolls, because its scale is defined as pane / cols. Vertically
# nothing scrolls yet — there are no rows to draw — and the day that
# changes, this line is the one that says so.
set(bars
  "auto|fit|0"
  "auto|200|0"
  "576|fit|0"
  "576|25|0"
  "576|50|16"
  "576|200|16"
  "288|50|0"
  "288|100|16")
foreach(row IN LISTS bars)
  string(REPLACE "|" ";" f "${row}")
  list(GET f 0 b_ioc)
  list(GET f 1 b_zoom)
  list(GET f 2 want_h)
  run_metrics(out --state ready --ioc ${b_ioc} --rate 120 --zoom ${b_zoom})
  expect("scrollbar ioc=${b_ioc} zoom=${b_zoom}" "${out}" hscroll_px
    "${want_h}")
  expect("scrollbar ioc=${b_ioc} zoom=${b_zoom}" "${out}" vscroll_px "0")
  message(STATUS
    "gui_shell PASS scrollbar: ioc=${b_ioc} zoom=${b_zoom} -> h=${want_h}")
endforeach()

# --- the pane follows the newest row, and the picture REALLY moves ---------
# [§8.3 item 3, Sara session 26; the bounce fixed session 27.]
#
# The rule: while rows are arriving the bottom of the pane is the newest
# line. The trap: Fl_Scroll scrolls by MOVING its child, so yposition() is
# a cached copy of the child's position that a child resize invalidates
# without telling anyone — and layout_view() resizes the child on every
# batch. Session 26's follow set that cached number correctly and the
# picture bounced anyway, so this checks where the picture ACTUALLY sits.
# Asking Fl_Scroll where it thinks it is would pass on the broken code:
# measured before the fix, yposition read 632 while the chart sat at 150.
#
# At zoom 100 one image row is one screen pixel, so the numbers are the
# chart's own: rows_drawn - pane_interior_h, once there are enough rows to
# exceed the pane at all.
foreach(row_batch "8x150" "12x97" "5x400")
  execute_process(
    COMMAND ${NOVA_GUI} --state drawing --ioc 576 --rate 120 --zoom 100
            --follow ${row_batch}
    RESULT_VARIABLE rv OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rv EQUAL 0)
    message(FATAL_ERROR
      "nova-gui --follow ${row_batch} exited ${rv}\n${out}\n${err}")
  endif()
  string(REPLACE "\n" ";" lines "${out}")
  set(seen 0)
  set(prev_actual -1)
  foreach(line IN LISTS lines)
    if(NOT line MATCHES "^ +([0-9]+) +([0-9]+) +([0-9]+) +([0-9]+) +([0-9]+)$")
      continue()
    endif()
    set(b ${CMAKE_MATCH_1})
    set(rows ${CMAKE_MATCH_2})
    set(max_y ${CMAKE_MATCH_3})
    set(actual ${CMAKE_MATCH_5})
    math(EXPR seen "${seen} + 1")
    # The newest row is at the bottom of the pane: the offset the child
    # really sits at is the largest the content allows.
    if(NOT actual EQUAL max_y)
      message(FATAL_ERROR
        "gui_shell FAIL follow ${row_batch}: batch ${b} (${rows} rows) sits "
        "at ${actual}, but the newest row is at ${max_y} — the pane is not "
        "following it")
    endif()
    # And it gets there by going DOWN. This is the half that convicts the
    # bounce: an offset that retreats is the chart jumping back up.
    if(actual LESS prev_actual)
      message(FATAL_ERROR
        "gui_shell FAIL follow ${row_batch}: batch ${b} scrolled BACK from "
        "${prev_actual} to ${actual} — the picture bounced")
    endif()
    set(prev_actual ${actual})
  endforeach()
  if(seen EQUAL 0)
    message(FATAL_ERROR
      "gui_shell FAIL follow ${row_batch}: no batch lines parsed from:\n${out}")
  endif()
  message(STATUS
    "gui_shell PASS follow: ${row_batch} batches, ${seen} checked, "
    "newest row at the pane bottom throughout")
endforeach()

# The follow is for rows ARRIVING. In every other state the scroll is the
# operator's, and nothing moves it [§8.3 item 3] — after SAVED the operator
# is reading the chart, and a pane that kept yanking to the bottom would
# fight them.
foreach(st "ready" "phasing" "decoding" "saved")
  execute_process(
    COMMAND ${NOVA_GUI} --state ${st} --ioc 576 --rate 120 --zoom 100
            --follow 6x300
    RESULT_VARIABLE rv OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rv EQUAL 0)
    message(FATAL_ERROR "nova-gui --follow in ${st} exited ${rv}\n${err}")
  endif()
  string(REPLACE "\n" ";" lines "${out}")
  foreach(line IN LISTS lines)
    if(NOT line MATCHES "^ +([0-9]+) +([0-9]+) +([0-9]+) +([0-9]+) +([0-9]+)$")
      continue()
    endif()
    if(NOT CMAKE_MATCH_5 EQUAL 0)
      message(FATAL_ERROR
        "gui_shell FAIL follow in ${st}: the pane scrolled itself to "
        "${CMAKE_MATCH_5}; outside DRAWING the scroll belongs to the operator")
    endif()
  endforeach()
  message(STATUS "gui_shell PASS follow: nothing scrolls itself in ${st}")
endforeach()

# --- the correction controls are grey, and they SAY WHY [§3] ---------------
# §3: "The PHASE/SYNC controls must then be visibly disabled with the
# reason shown — not silently inert. Manual adjustment is not offered and
# then found not to work."
#
# An INSPECTION run has no engine behind it, so nothing has been decoded
# and no correction is possible in any state — which is the point here:
# whatever the state says, the controls are grey and they SAY WHY. The
# rule that decides them when there IS something behind them is the
# truth table below, and the lifecycle they drive is `live_engine`'s
# `test_rerender`.
foreach(st "idle" "ready" "drawing" "decoding" "saved")
  run_metrics(out --state ${st} --ioc 576 --rate 120)
  # Auto needs a decoded picture to restore, and an inspection run has
  # none in any state.
  expect("post-decode ${st}" "${out}" auto_active "0")
  # ...and the reason is never blank. An inspection run has no engine
  # behind it, so it says the honest thing about itself.
  shell_value(correct_reason "${out}" why)
  if(why STREQUAL "")
    message(FATAL_ERROR
      "gui_shell FAIL correction ${st}: the controls are grey and say "
      "nothing; §3 requires the reason to be shown")
  endif()
  message(STATUS "gui_shell PASS correction ${st}: grey, reason \"${why}\"")
endforeach()

# --- what the correction surface offers [§7, §8.5 item 4] ------------------
# `--correction` prints the whole truth table of `correction_for`, four
# booleans wide. Checked as RULES rather than transcribed row for row: a
# copy of the table would restate the implementation and agree with it
# whatever it said.
execute_process(COMMAND ${NOVA_GUI} --correction
  RESULT_VARIABLE rv OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rv EQUAL 0)
  message(FATAL_ERROR "nova-gui --correction exited ${rv}\n${out}\n${err}")
endif()
string(REPLACE "\n" ";" lines "${out}")
set(rows 0)
foreach(line IN LISTS lines)
  if(NOT line MATCHES "^ +([01]) +([01]) +([01]) +([01]) \\| +([01]) +([01]) +([01])$")
    continue()
  endif()
  set(live ${CMAKE_MATCH_1})
  set(can ${CMAKE_MATCH_2})
  set(dirty ${CMAKE_MATCH_3})
  set(applied ${CMAKE_MATCH_4})
  set(inputs ${CMAKE_MATCH_5})
  set(ap ${CMAKE_MATCH_6})
  set(au ${CMAKE_MATCH_7})
  math(EXPR rows "${rows} + 1")
  set(where "live=${live} can=${can} dirty=${dirty} applied=${applied}")

  # 1. No button is ever active with nothing behind it. This is the rule
  #    session 26 found broken on the air (finding 2: an active Start that
  #    swallowed clicks), stated for this surface.
  if(ap EQUAL 1 AND live EQUAL 0 AND can EQUAL 0)
    message(FATAL_ERROR
      "gui_shell FAIL correction ${where}: Apply is active with no surface "
      "behind it")
  endif()
  if(au EQUAL 1 AND can EQUAL 0)
    message(FATAL_ERROR
      "gui_shell FAIL correction ${where}: Auto is active with no picture "
      "to re-render")
  endif()
  if(inputs EQUAL 1 AND live EQUAL 0 AND can EQUAL 0)
    message(FATAL_ERROR
      "gui_shell FAIL correction ${where}: the boxes are editable with "
      "nothing to correct")
  endif()

  # 2. Post-decode, nothing typed and nothing applied: both buttons grey.
  #    The picture already IS the measured render, so Apply would rewrite
  #    the same file and Auto would undo nothing [§8.5 items 2, 4].
  if(live EQUAL 0 AND can EQUAL 1 AND dirty EQUAL 0 AND applied EQUAL 0)
    if(NOT ap EQUAL 0 OR NOT au EQUAL 0)
      message(FATAL_ERROR
        "gui_shell FAIL correction ${where}: a button is active with "
        "nothing to do (apply=${ap} auto=${au})")
    endif()
  endif()
  # 3. An edit in progress makes both live: something to apply, something
  #    to discard [§8.5 item 4, "an edit begins at the first dirty control"].
  if(live EQUAL 0 AND can EQUAL 1 AND dirty EQUAL 1)
    if(NOT ap EQUAL 1 OR NOT au EQUAL 1)
      message(FATAL_ERROR
        "gui_shell FAIL correction ${where}: an edit is in progress and "
        "cannot be applied or abandoned (apply=${ap} auto=${au})")
    endif()
  endif()
  # 4. A correction already applied, nothing newly typed: Auto can undo it,
  #    Apply has nothing new to send.
  if(live EQUAL 0 AND can EQUAL 1 AND dirty EQUAL 0 AND applied EQUAL 1)
    if(NOT au EQUAL 1 OR NOT ap EQUAL 0)
      message(FATAL_ERROR
        "gui_shell FAIL correction ${where}: an applied correction must be "
        "undoable and must not re-apply itself (apply=${ap} auto=${au})")
    endif()
  endif()
  # 5. The live surface [§7]: Apply always available — the correction goes
  #    forward from the next row whatever the boxes hold — and Auto never,
  #    because there is no re-render to restore, only rows not yet drawn.
  if(live EQUAL 1)
    if(NOT ap EQUAL 1)
      message(FATAL_ERROR
        "gui_shell FAIL correction ${where}: the live surface must always "
        "accept Apply")
    endif()
    if(can EQUAL 0 AND NOT au EQUAL 0)
      message(FATAL_ERROR
        "gui_shell FAIL correction ${where}: Auto has no meaning on the "
        "live surface")
    endif()
  endif()
endforeach()
if(NOT rows EQUAL 16)
  message(FATAL_ERROR
    "gui_shell FAIL correction: expected all 16 combinations, parsed "
    "${rows}:\n${out}")
endif()
message(STATUS
  "gui_shell PASS correction surface: all 16 combinations obey the rules")

# --- the SYNC steppers follow the BOX [session 28] -------------------------
# They are the SYNC input in another shape, so "active" must mean the same
# thing for both. A stepper live over a dead box is session 26's finding 2
# again — a button that does nothing — and a stepper grey over a live box
# is the control silently missing. Either way the count is what tells:
# `sync_steps_active` is 0 or 4 and agrees with `sync_active`.
foreach(st "idle" "ready" "drawing" "decoding" "saved")
  run_metrics(out --state ${st} --ioc 576 --rate 120)
  shell_value(sync_active "${out}" sa)
  shell_value(sync_steps_active "${out}" ss)
  if(sa EQUAL 1)
    set(want 4)
  else()
    set(want 0)
  endif()
  if(NOT ss EQUAL want)
    message(FATAL_ERROR
      "gui_shell FAIL steppers ${st}: sync_active=${sa} so ${want} steppers "
      "should be active, ${ss} are")
  endif()
  message(STATUS
    "gui_shell PASS steppers ${st}: ${ss} active, box ${sa}")
endforeach()

# ...and the steppers sit under the SYNC box they step, inside the panel.
run_metrics(out --state saved --ioc 576 --rate 120)
region(sync_input "${out}" six siy siw sih)
region(status_panel "${out}" spx spy spw sph)
math(EXPR panel_r "${spx} + ${spw}")
set(prev_r 0)
foreach(tag sync_step_m10 sync_step_m1 sync_step_p1 sync_step_p10)
  region(${tag} "${out}" bx by bw bh)
  math(EXPR sync_bottom "${siy} + ${sih}")
  if(by LESS sync_bottom)
    message(FATAL_ERROR
      "gui_shell FAIL steppers: ${tag} at y=${by} is not below the SYNC box "
      "(bottom ${sync_bottom})")
  endif()
  math(EXPR br "${bx} + ${bw}")
  if(bx LESS spx OR br GREATER panel_r)
    message(FATAL_ERROR
      "gui_shell FAIL steppers: ${tag} (${bx}..${br}) leaves the panel "
      "(${spx}..${panel_r})")
  endif()
  # Left to right in the order they are labelled, so the row reads as a
  # scale rather than as four buttons in an arbitrary order.
  if(NOT bx GREATER prev_r AND NOT prev_r EQUAL 0)
    message(FATAL_ERROR
      "gui_shell FAIL steppers: ${tag} at x=${bx} does not follow the "
      "previous stepper (ends ${prev_r})")
  endif()
  set(prev_r ${br})
endforeach()
message(STATUS "gui_shell PASS steppers: under the box, in order, in panel")

# --- where a SYNC nudge STARTS [sync_step] ---------------------------------
# The trap this checks: a blank box means "as measured", and the measured
# clock is not 0 ppm — the white-only fixtures read -70 to -118. A nudge
# that started at zero would make the operator's FIRST click a jump of the
# whole clock error, away from correct, on exactly the stations the control
# exists for. Checked as rules, like the table above.
execute_process(COMMAND ${NOVA_GUI} --sync-step
  RESULT_VARIABLE rv OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rv EQUAL 0)
  message(FATAL_ERROR "nova-gui --sync-step exited ${rv}\n${out}\n${err}")
endif()
string(REPLACE "\n" ";" lines "${out}")
set(rows 0)
set(saw_blank_from_shown 0)
foreach(line IN LISTS lines)
  if(NOT line MATCHES
     "^ +([^ ]+) +(-?[0-9.]+) +([01]) +(-?[0-9.]+) \\| +(-?[0-9.]+) +([01])$")
    continue()
  endif()
  set(typed ${CMAKE_MATCH_1})
  set(shown ${CMAKE_MATCH_2})
  set(valid ${CMAKE_MATCH_3})
  set(delta ${CMAKE_MATCH_4})
  set(value ${CMAKE_MATCH_5})
  set(from_shown ${CMAKE_MATCH_6})
  math(EXPR rows "${rows} + 1")
  set(where "typed=${typed} shown=${shown} valid=${valid} delta=${delta}")

  # 1. A nudge from a blank box starts at the shown clock whenever there
  #    IS one. This is the whole point of the field.
  if(typed STREQUAL "\"\"" AND valid EQUAL 1)
    if(NOT from_shown EQUAL 1)
      message(FATAL_ERROR
        "gui_shell FAIL sync-step ${where}: a blank box with a decoded "
        "clock behind it must start from that clock, not from zero")
    endif()
    set(saw_blank_from_shown 1)
    # ...and it lands exactly one step from it.
    math(EXPR want "0")  # cmake math is integer-only; compare in tenths
    string(REPLACE "." "" s10 "${shown}")
    string(REPLACE "." "" d10 "${delta}")
    string(REPLACE "." "" v10 "${value}")
    # the printer drops a trailing ".0", so a whole number needs scaling
    if(NOT shown MATCHES "\\.")
      math(EXPR s10 "${shown} * 10")
    endif()
    if(NOT delta MATCHES "\\.")
      math(EXPR d10 "${delta} * 10")
    endif()
    if(NOT value MATCHES "\\.")
      math(EXPR v10 "${value} * 10")
    endif()
    math(EXPR want "${s10} + ${d10}")
    if(NOT v10 EQUAL want)
      message(FATAL_ERROR
        "gui_shell FAIL sync-step ${where}: expected shown+delta "
        "(${want} tenths), got ${v10}")
    endif()
  endif()

  # 2. A TYPED value always wins over the shown clock — including a typed
  #    "0", which is the operator saying zero ppm and is not the same
  #    thing as blank. That distinction is the same one core/ makes with
  #    NaN rather than 0 for `clock_ppm_fallback`.
  if(NOT typed STREQUAL "\"\"" AND NOT typed STREQUAL "-")
    if(NOT from_shown EQUAL 0)
      message(FATAL_ERROR
        "gui_shell FAIL sync-step ${where}: a typed value must outrank the "
        "shown clock")
    endif()
  endif()

  # 3. With nothing decoded there is no clock to start from, so zero is
  #    all there is — and it must not claim otherwise.
  if(valid EQUAL 0 AND NOT from_shown EQUAL 0)
    message(FATAL_ERROR
      "gui_shell FAIL sync-step ${where}: nothing has been decoded, so "
      "there is no shown clock to start from")
  endif()
endforeach()
if(rows LESS 10)
  message(FATAL_ERROR
    "gui_shell FAIL sync-step: parsed only ${rows} cases:\n${out}")
endif()
if(NOT saw_blank_from_shown EQUAL 1)
  message(FATAL_ERROR
    "gui_shell FAIL sync-step: no blank-box case with a decoded clock — "
    "the rule that matters was never exercised")
endif()
message(STATUS
  "gui_shell PASS sync-step: ${rows} cases, a blank box starts at the "
  "shown clock")

# --- a nudge IS an edit [§8.5 item 4] --------------------------------------
# FLTK does not fire an input's callback for a programmatic `value()`, so
# the stepper has to declare the edit itself. Forget that line and the box
# moves while the shell still believes nothing changed — the operator's
# correction sitting in a control that reports itself clean, and Apply grey
# over it. Pressing the button N times must move the value N times AND
# leave the edit dirty; pressing it zero times must leave both alone.
run_metrics(out --state drawing --ioc 576 --rate 120 --nudge 0)
shell_value(edit_dirty "${out}" d0)
shell_value(sync_value "${out}" v0)
if(NOT d0 EQUAL 0 OR NOT v0 STREQUAL "")
  message(FATAL_ERROR
    "gui_shell FAIL nudge: with no nudge the box is \"${v0}\" and dirty="
    "${d0}; an untouched shell is clean")
endif()
foreach(n 1 3)
  run_metrics(out --state drawing --ioc 576 --rate 120 --nudge ${n})
  shell_value(edit_dirty "${out}" d)
  shell_value(sync_value "${out}" v)
  if(NOT d EQUAL 1)
    message(FATAL_ERROR
      "gui_shell FAIL nudge ${n}: the box reads \"${v}\" but the edit is not "
      "dirty — Apply would stay grey over the operator's own change")
  endif()
  # Nothing is decoded on an inspection run, so there is no shown clock and
  # the nudges accumulate from zero: N presses of +1 read N.
  if(NOT v STREQUAL "${n}")
    message(FATAL_ERROR
      "gui_shell FAIL nudge ${n}: expected the box to read \"${n}\", got "
      "\"${v}\"")
  endif()
  message(STATUS "gui_shell PASS nudge ${n}: box \"${v}\", edit dirty")
endforeach()

# --- click-to-set-PHASE [§8.3 item 1, ROADMAP M4 item 5] -------------------
# The click must name the column the RULER names at that x — ruler.hpp's
# whole correctness claim — so the exact numbers are checked at the FIXED
# zooms, where the scale is an exact ratio and the expected column is an
# integer this file can derive independently of the implementation:
#   100%  1 px per column  -> column x
#   200%  2 px per column  -> column x/2
#    25%  1 px per 4 cols  -> column 4x
# Fit is deliberately not checked numerically here: its scale is
# pane/cols, so any expectation would have to recompute the pane interior
# and would end up restating the code. `ruler_mapping` pins Fit.
foreach(case "100;300;300" "100;0;0" "200;300;150" "200;0;0" "25;300;1200")
  list(GET case 0 z)
  list(GET case 1 cx)
  list(GET case 2 want)
  run_metrics(out --state drawing --ioc 576 --rate 120 --zoom ${z}
              --click-rows 200 --click ${cx})
  shell_value(phase_value "${out}" got)
  if(NOT got STREQUAL "${want}")
    message(FATAL_ERROR
      "gui_shell FAIL click: at ${z}% a click ${cx} px into the pane should "
      "name column ${want}, named \"${got}\"")
  endif()
  # ...and a click is an edit, the same way a nudge is [§8.5 item 4].
  shell_value(edit_dirty "${out}" d)
  if(NOT d EQUAL 1)
    message(FATAL_ERROR
      "gui_shell FAIL click: at ${z}% a click set PHASE to \"${got}\" but "
      "left the edit clean — Apply would stay grey over it")
  endif()
  message(STATUS "gui_shell PASS click: ${z}% x=${cx} -> column ${got}")
endforeach()

# A click past the image's right edge NAMES NOTHING [ruler.hpp,
# `column_at`]: there is no picture there, so there is no dead sector
# there, and inventing a column the operator did not point at is worse
# than doing nothing. At 25% the 1810-column image is 452 px wide, so
# 600 px in is off the picture while still inside the pane.
run_metrics(out --state drawing --ioc 576 --rate 120 --zoom 25
            --click-rows 200 --click 600)
shell_value(phase_value "${out}" got)
shell_value(edit_dirty "${out}" d)
if(NOT got STREQUAL "" OR NOT d EQUAL 0)
  message(FATAL_ERROR
    "gui_shell FAIL click: a click beyond the image's right edge named "
    "\"${got}\" (dirty=${d}); it names nothing")
endif()
message(STATUS "gui_shell PASS click: past the right edge names nothing")

# The image can act exactly where PHASE can be typed — one rule, three
# surfaces (box, steppers, image). A pane that accepted clicks with the
# boxes grey would be a control that is offered and then found not to
# work [§3], the failure this shell must not have.
foreach(st "idle" "ready" "drawing" "decoding" "saved")
  run_metrics(out --state ${st} --ioc 576 --rate 120 --click-rows 200)
  shell_value(phase_active "${out}" pa)
  shell_value(image_click_enabled "${out}" ce)
  if(NOT ce EQUAL pa)
    message(FATAL_ERROR
      "gui_shell FAIL click ${st}: PHASE is typeable=${pa} but the image "
      "accepts clicks=${ce}; they are the same rule")
  endif()
  # ...and where it cannot act, a click really does not act. This asks the
  # HANDLER what it did (`click_named`, -1 for "did not act") rather than
  # only reading the box afterwards: `apply_state`'s edit-end rule [§8.5
  # item 4] clears the boxes whenever a correction is impossible, so a
  # click that wrongly acted would be wiped a moment later and the shell
  # would look right afterwards. Verified: removing the handler's guard
  # leaves every box-and-dirty check passing and only this one fails.
  run_metrics(out --state ${st} --ioc 576 --rate 120 --zoom 100
              --click-rows 200 --click 300)
  shell_value(click_named "${out}" named)
  shell_value(phase_value "${out}" got)
  shell_value(edit_dirty "${out}" d)
  if(pa EQUAL 0)
    if(NOT named EQUAL -1)
      message(FATAL_ERROR
        "gui_shell FAIL click ${st}: the image cannot act here, but the "
        "handler named column ${named}")
    endif()
    if(NOT got STREQUAL "" OR NOT d EQUAL 0)
      message(FATAL_ERROR
        "gui_shell FAIL click ${st}: the image cannot act here, but a click "
        "set PHASE to \"${got}\" (dirty=${d})")
    endif()
  else()
    if(named EQUAL -1)
      message(FATAL_ERROR
        "gui_shell FAIL click ${st}: PHASE is typeable here, but a click "
        "300 px into the pane did not act")
    endif()
  endif()
  message(STATUS "gui_shell PASS click ${st}: image=${ce}, PHASE=${pa}")
endforeach()

# Fl_Scroll's cached xposition against the child's own left edge. Session
# 27 found the vertical twin of this lying, with the pane visibly wrong
# and the number the code checked reading perfectly; the ruler and the
# click both read the cached copy, so if it ever lies they lie together
# and only this comparison would show it.
foreach(z "fit" "100" "200")
  run_metrics(out --state drawing --ioc 576 --rate 120 --zoom ${z}
              --click-rows 200)
  shell_value(scroll_x_actual "${out}" sx)
  if(NOT sx EQUAL 0)
    message(FATAL_ERROR
      "gui_shell FAIL click: at zoom ${z} the child sits ${sx} px from the "
      "pane's interior edge with no horizontal scroll applied")
  endif()
endforeach()
message(STATUS "gui_shell PASS click: the child's edge agrees with xposition")

# The reason has somewhere to be READ, not just somewhere to be stored: it
# is a region of the panel, inside the sidebar, under the two buttons it
# explains — not in the sidebar's empty lower area, which §8 already spoke
# for (§8.2's receiving indicator).
run_metrics(out --state saved --ioc 576 --rate 120)
region(correct_why "${out}" wx wy ww wh)
region(auto_button "${out}" ax ay aw ah)
region(status_panel "${out}" sx sy sw sh)
if(NOT wy GREATER ay)
  message(FATAL_ERROR
    "gui_shell FAIL: the reason (y=${wy}) is not below the Auto button "
    "(y=${ay}) it explains")
endif()
math(EXPR panel_right "${sx} + ${sw}")
math(EXPR why_right "${wx} + ${ww}")
if(wx LESS sx OR why_right GREATER panel_right OR ww LESS 100)
  message(FATAL_ERROR
    "gui_shell FAIL: the reason is ${wx}..${why_right}, outside the panel "
    "${sx}..${panel_right} or too narrow to read (${ww} px)")
endif()
message(STATUS
  "gui_shell PASS: the reason has ${ww}x${wh} px under the buttons")

# --- an inspection run brings up no capture, and the window says so --------
# Until session 23 this passed because nothing in the program COULD
# capture. It now can — nova-gui opens a real input stream and runs the
# live decode — and the claim has changed rather than disappeared: the
# live half comes up only after the window is shown, so --metrics and
# --devices never open a sound card. That is what keeps this suite
# runnable on a machine with no audio device, and it is also the reason
# inspecting Nova cannot switch on somebody's microphone.
#
# So both buttons are still insensitive here even with the dropdowns
# explicit. A Start that greys itself is honest; a Start that does
# nothing when pressed is not [§3, file header].
run_metrics(out --ioc 576 --rate 120)
expect("no capture" "${out}" start_active "0")
expect("no capture" "${out}" force_active "0")
message(STATUS "gui_shell PASS: transport inert with no capture behind it")

# --- the preference file [§8.4 item 1] -------------------------------------
# Plain text, next to the executable, read at startup. Any existing file
# is put back afterwards: this test must not eat a real setting.
get_filename_component(gui_dir "${NOVA_GUI}" DIRECTORY)
set(conf "${gui_dir}/nova.conf")
set(saved "${gui_dir}/nova.conf.gui_shell_backup")
if(EXISTS "${conf}")
  file(RENAME "${conf}" "${saved}")
  set(had_conf TRUE)
else()
  set(had_conf FALSE)
endif()

# Inspecting the shell with no preference file must not create one — a
# read is not a write.
run_metrics(out)
expect("no prefs file" "${out}" image_folder "(unset)")
if(EXISTS "${conf}")
  message(FATAL_ERROR
    "gui_shell FAIL: running --metrics created ${conf} where there was none")
endif()

file(WRITE "${conf}" "# written by gui_shell\nimage_folder=/tmp/nova-gui-shell\n")
run_metrics(out)
expect("prefs read" "${out}" image_folder "/tmp/nova-gui-shell")
expect("prefs read" "${out}" prefs_writable "1")
file(REMOVE "${conf}")
if(had_conf)
  file(RENAME "${saved}" "${conf}")
endif()
message(STATUS "gui_shell PASS: the preference file beside the program is read")

message(STATUS "gui_shell: all checks passed")
