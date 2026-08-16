# gui_shell.cmake — §9 screamer 9 [docs/05], run as
#   cmake -DNOVA_GUI=<path to nova-gui> -P tests/gui_shell.cmake
# Added as a ctest target only when the GUI target was built
# (NOVA_BUILD_GUI), so it is one of the two in "35 (+2 with the GUI)".
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
#     merely inspecting the shell leaves no file behind [§8.4 item 1];
#   - the CORRECTION surface [§7, §7.1, §8.5, added sessions 27-29]: all
#     sixteen combinations of `correction_for` checked against the rules
#     rather than against a copy of the table; the SYNC steppers active
#     exactly where the box is, FLANKING the box they step, and starting a
#     nudge at the clock the picture was DRAWN on rather than at zero;
#     the click naming the column the ruler names at that x and naming
#     nothing past the image's right edge;
#   - the two ARMED GESTURES [session 29]: that an unarmed click does
#     nothing at all, that each gesture disarms itself when it completes,
#     that the SYNC measurement leaves PHASE alone, that a short baseline
#     still measures and is labelled with what it is worth, that two
#     clicks on one row measure nothing and leave the gesture live, that
#     re-arming discards a half-made measurement, and that neither the
#     anchor nor the arming outlives the picture it was made against.
#
# Those last two groups are interaction surface, which is where this
# project's defects have actually lived (sessions 25, 26 and 27 each found
# one on the air with a green suite). So they are driven through the
# shell's REAL handlers via --arm / --click / --nudge / --then-state
# rather than by restating the arithmetic here, and every check in this
# file has been verified by deliberately reintroducing the defect it
# exists to catch.
#
# Two cautions this file was taught the hard way, both worth reading
# before adding a check to it. **A seam makes a rule reachable; it does
# not make a check correct** — session 28's first attempts at two of these
# passed against builds with the rule deleted, once because a second rule
# was cleaning up after the first and once because the check spanned two
# processes and so could not see a transition. And **a check that only
# runs at a parameter's identity value is not checking that parameter** —
# session 29 found two rules that survived mutation because every
# measurement check ran at 100% zoom, where the scale is 1.0.
#
# --state drives the shell into a live state exactly as nova-live will,
# and --then-state drives a SECOND state so that rules firing on a
# transition are reachable; together that is what makes all of this
# testable with no window and no audio device.

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

# ...and the steppers FLANK the SYNC box they step [session 29, Sara].
#
# This is a layout check that earns its place, which most do not. The old
# layout put the four steppers on a full-width row beneath BOTH boxes, and
# the first person to look at the built window read them as PHASE's — an
# understandable misreading that costs the deliberate asymmetry between the
# two controls (PHASE has no steppers ON PURPOSE, because a nudge smaller
# than the ±54-column search window it is refined within moves nothing).
# Two buttons on either side of the box cannot be misread that way, and
# "cannot be misread" is the whole claim, so it is worth pinning.
run_metrics(out --state saved --ioc 576 --rate 120)
region(sync_input "${out}" six siy siw sih)
region(phase_input "${out}" pix piy piw pih)
region(status_panel "${out}" spx spy spw sph)
math(EXPR panel_r "${spx} + ${spw}")
math(EXPR sync_r "${six} + ${siw}")
set(prev_r 0)
set(left 0)
set(right 0)
foreach(tag sync_step_m10 sync_step_m1 sync_step_p1 sync_step_p10)
  region(${tag} "${out}" bx by bw bh)
  math(EXPR br "${bx} + ${bw}")
  # On the SAME ROW as the box, which is what "flanking" means and what a
  # row below could never claim.
  if(NOT by EQUAL siy OR NOT bh EQUAL sih)
    message(FATAL_ERROR
      "gui_shell FAIL steppers: ${tag} is ${bw}x${bh} at y=${by}, not on the "
      "SYNC box's row (y=${siy} h=${sih})")
  endif()
  if(br LESS_EQUAL six)
    math(EXPR left "${left} + 1")
  elseif(bx GREATER_EQUAL sync_r)
    math(EXPR right "${right} + 1")
  else()
    message(FATAL_ERROR
      "gui_shell FAIL steppers: ${tag} (${bx}..${br}) overlaps the SYNC box "
      "(${six}..${sync_r})")
  endif()
  if(bx LESS spx OR br GREATER panel_r)
    message(FATAL_ERROR
      "gui_shell FAIL steppers: ${tag} (${bx}..${br}) leaves the panel "
      "(${spx}..${panel_r})")
  endif()
  # Left to right in the order they are labelled, so the row still reads as
  # a scale: -10 -1 [box] +1 +10.
  if(NOT bx GREATER prev_r AND NOT prev_r EQUAL 0)
    message(FATAL_ERROR
      "gui_shell FAIL steppers: ${tag} at x=${bx} does not follow the "
      "previous stepper (ends ${prev_r})")
  endif()
  set(prev_r ${br})
endforeach()
if(NOT left EQUAL 2 OR NOT right EQUAL 2)
  message(FATAL_ERROR
    "gui_shell FAIL steppers: ${left} left of the SYNC box and ${right} "
    "right of it; flanking is two and two")
endif()
# The steppers must not reach the PHASE box's row either — the misreading
# this layout exists to prevent is precisely "these belong to PHASE".
foreach(tag sync_step_m10 sync_step_p10)
  region(${tag} "${out}" bx by bw bh)
  math(EXPR phase_bottom "${piy} + ${pih}")
  if(by LESS phase_bottom)
    message(FATAL_ERROR
      "gui_shell FAIL steppers: ${tag} at y=${by} reaches the PHASE box's "
      "row (bottom ${phase_bottom}); PHASE has no steppers on purpose")
  endif()
endforeach()
message(STATUS "gui_shell PASS steppers: 2 + [box] + 2, on one row, in panel")

# Each arming button sits on the row of the box its gesture fills in, and
# inside the panel. A button beside the wrong box would name the wrong
# value — the same misreading as the steppers, on a control where it would
# move the operator's data rather than merely confuse them.
foreach(pair "phase_arm;phase_input" "sync_arm;cap_sync")
  list(GET pair 0 btn)
  list(GET pair 1 anchor)
  region(${btn} "${out}" bx by bw bh)
  region(${anchor} "${out}" ax ay aw ah)
  # Rows OVERLAP rather than share a y: an Fl_Input is inset a pixel from
  # the caption box beside it, and pinning the inset would be pinning a
  # decoration. What has to be true is that they are the same ROW.
  math(EXPR b_bot "${by} + ${bh}")
  math(EXPR a_bot "${ay} + ${ah}")
  if(by GREATER_EQUAL a_bot OR ay GREATER_EQUAL b_bot)
    message(FATAL_ERROR
      "gui_shell FAIL arm: ${btn} spans ${by}..${b_bot} and ${anchor} "
      "${ay}..${a_bot}; an arming button belongs on its own gesture's row")
  endif()
  math(EXPR br "${bx} + ${bw}")
  if(bx LESS spx OR br GREATER panel_r OR bw LESS 24)
    message(FATAL_ERROR
      "gui_shell FAIL arm: ${btn} is ${bx}..${br} (${bw} px wide), outside "
      "the panel ${spx}..${panel_r} or too small to hit")
  endif()
endforeach()
message(STATUS "gui_shell PASS arm: each button on its own gesture's row")

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

# --- an UNARMED click does nothing [session 29, Sara] ----------------------
# The rule the whole arming design exists for, checked before anything that
# depends on it: with no gesture declared, a click on the picture must not
# reach the operator's data at all. This is Sara's accidental-click
# objection, and it is answerable only here — everything else about arming
# is convenience, this is the part that protects a value.
#
# Checked in the states where a correction IS possible, because "nothing
# happens" is trivially true where nothing could have happened anyway. The
# handler's own report (`click_action`) is read rather than only the boxes:
# `apply_state`'s edit-end rule clears the boxes whenever a correction is
# impossible, so a click that wrongly acted could be wiped a moment later
# and leave the shell looking correct [session 28's lesson].
foreach(st "drawing" "saved")
  run_metrics(out --state ${st} --ioc 576 --rate 120 --zoom 100
              --click-rows 1200 --click 300,50)
  shell_value(click_action "${out}" act)
  shell_value(phase_value "${out}" ph)
  shell_value(sync_value "${out}" sv)
  shell_value(pending_row "${out}" pend)
  shell_value(image_click_enabled "${out}" ce)
  if(NOT act STREQUAL "none" OR NOT ph STREQUAL "" OR NOT sv STREQUAL ""
     OR NOT pend EQUAL -1)
    message(FATAL_ERROR
      "gui_shell FAIL unarmed ${st}: a click with no gesture armed did "
      "\"${act}\" (PHASE=\"${ph}\" SYNC=\"${sv}\" pending=${pend}); an "
      "unarmed picture is not listening")
  endif()
  # ...and it does not offer to, either: the crosshair is the affordance
  # and it must agree with the rule, or the cursor invites a click the
  # handler will refuse [§3, one rule and three witnesses].
  if(NOT ce EQUAL 0)
    message(FATAL_ERROR
      "gui_shell FAIL unarmed ${st}: the picture shows a crosshair with no "
      "gesture armed")
  endif()
  message(STATUS "gui_shell PASS unarmed ${st}: the picture is not listening")
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
              --click-rows 200 --arm phase --click ${cx})
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

# The PHASE gesture is ONE click and then it is over [see Arm; hamfax's
# lifecycle]. This is what makes arming not-a-mode: the operator cannot be
# left in a state where the next stray click moves something. A gesture
# that stayed armed would be exactly the trap the button was added to
# remove.
run_metrics(out --state drawing --ioc 576 --rate 120 --zoom 100
            --click-rows 1200 --arm phase --click 300,50)
shell_value(arm "${out}" a)
shell_value(image_click_enabled "${out}" ce)
shell_value(phase_arm_pushed "${out}" pushed)
if(NOT a STREQUAL "none" OR NOT ce EQUAL 0 OR NOT pushed EQUAL 0)
  message(FATAL_ERROR
    "gui_shell FAIL arm: the PHASE gesture is complete but the picture is "
    "still armed (arm=${a}, crosshair=${ce}, button=${pushed})")
endif()
message(STATUS "gui_shell PASS arm: the PHASE gesture disarms itself")

# The three witnesses of the armed state cannot disagree, because a
# disagreement is how the operator ends up clicking a picture that is not
# listening (or not clicking one that is): the button's pushed look, the
# crosshair, and the rule the handler actually obeys.
foreach(want "phase" "sync")
  run_metrics(out --state drawing --ioc 576 --rate 120 --zoom 100
              --click-rows 1200 --arm ${want})
  shell_value(arm "${out}" a)
  shell_value(image_click_enabled "${out}" ce)
  shell_value(phase_arm_pushed "${out}" pp)
  shell_value(sync_arm_pushed "${out}" sp)
  if(NOT a STREQUAL "${want}" OR NOT ce EQUAL 1)
    message(FATAL_ERROR
      "gui_shell FAIL arm ${want}: armed as \"${a}\" with crosshair=${ce}")
  endif()
  if(want STREQUAL "phase" AND (NOT pp EQUAL 1 OR NOT sp EQUAL 0))
    message(FATAL_ERROR
      "gui_shell FAIL arm phase: buttons read PHASE=${pp} SYNC=${sp}")
  endif()
  if(want STREQUAL "sync" AND (NOT sp EQUAL 1 OR NOT pp EQUAL 0))
    message(FATAL_ERROR
      "gui_shell FAIL arm sync: buttons read PHASE=${pp} SYNC=${sp}")
  endif()
  # Pressing the armed button again disarms — the way out, and the reason
  # this is a declaration rather than a mode.
  run_metrics(out --state drawing --ioc 576 --rate 120 --zoom 100
              --click-rows 1200 --arm ${want} --arm ${want})
  shell_value(arm "${out}" a2)
  shell_value(image_click_enabled "${out}" ce2)
  if(NOT a2 STREQUAL "none" OR NOT ce2 EQUAL 0)
    message(FATAL_ERROR
      "gui_shell FAIL arm ${want}: pressing it twice left arm=\"${a2}\" "
      "(crosshair=${ce2}); there would be no way out of the gesture")
  endif()
  message(STATUS "gui_shell PASS arm ${want}: button, crosshair and rule agree")
endforeach()

# The arming buttons follow the BOXES, for the reason the steppers do: an
# arm over a dead box offers a gesture with nowhere to put its answer [§3].
foreach(st "idle" "ready" "drawing" "decoding" "saved")
  run_metrics(out --state ${st} --ioc 576 --rate 120 --click-rows 200)
  shell_value(phase_active "${out}" pa)
  shell_value(phase_arm_active "${out}" paa)
  shell_value(sync_arm_active "${out}" saa)
  if(NOT paa EQUAL pa OR NOT saa EQUAL pa)
    message(FATAL_ERROR
      "gui_shell FAIL arm ${st}: PHASE is typeable=${pa} but the arming "
      "buttons are ${paa}/${saa}; they are the same rule")
  endif()
endforeach()
message(STATUS "gui_shell PASS arm: the buttons follow the boxes")

# A click past the image's right edge NAMES NOTHING [ruler.hpp,
# `column_at`]: there is no picture there, so there is no dead sector
# there, and inventing a column the operator did not point at is worse
# than doing nothing. At 25% the 1810-column image is 452 px wide, so
# 600 px in is off the picture while still inside the pane.
run_metrics(out --state drawing --ioc 576 --rate 120 --zoom 25
            --click-rows 200 --arm phase --click 600)
shell_value(phase_value "${out}" got)
shell_value(edit_dirty "${out}" d)
if(NOT got STREQUAL "" OR NOT d EQUAL 0)
  message(FATAL_ERROR
    "gui_shell FAIL click: a click beyond the image's right edge named "
    "\"${got}\" (dirty=${d}); it names nothing")
endif()
message(STATUS "gui_shell PASS click: past the right edge names nothing")

# An ARMED picture can act exactly where PHASE can be typed — one rule,
# four surfaces now (box, steppers, arm buttons, image). A pane that
# accepted clicks with the boxes grey would be a control that is offered
# and then found not to work [§3], the failure this shell must not have.
#
# Session 29: the arming is attempted in every state, and where the surface
# is dead it must not take — `set_arm` runs, and `apply_state` puts it
# straight back down, so an operator cannot arm a picture that has nowhere
# to put the answer.
foreach(st "idle" "ready" "drawing" "decoding" "saved")
  run_metrics(out --state ${st} --ioc 576 --rate 120 --click-rows 200
              --arm phase)
  shell_value(phase_active "${out}" pa)
  shell_value(image_click_enabled "${out}" ce)
  if(NOT ce EQUAL pa)
    message(FATAL_ERROR
      "gui_shell FAIL click ${st}: PHASE is typeable=${pa} but an armed "
      "image accepts clicks=${ce}; they are the same rule")
  endif()
  # ...and where it cannot act, a click really does not act. This asks the
  # HANDLER what it did (`click_named`, -1 for "did not act") rather than
  # only reading the box afterwards: `apply_state`'s edit-end rule [§8.5
  # item 4] clears the boxes whenever a correction is impossible, so a
  # click that wrongly acted would be wiped a moment later and the shell
  # would look right afterwards. Verified: removing the handler's guard
  # leaves every box-and-dirty check passing and only this one fails.
  run_metrics(out --state ${st} --ioc 576 --rate 120 --zoom 100
              --click-rows 200 --arm phase --click 300)
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

# --- two-click SYNC [session 28] -------------------------------------------
# A slant is the same feature at two ROWS, so the measurement is
# (dcol/drow)/width * 1e6. Checked at 100% zoom, where a screen pixel is a
# column and a row, so the expected ppm is arithmetic this file does
# independently: 40 columns over 400 rows at width 1810 is
# (0.1/1810)*1e6 = 55.2 ppm.
set(CLICK ${NOVA_GUI})
macro(click_run OUT)
  run_metrics(${OUT} --state drawing --ioc 576 --rate 120 --zoom 100
              --click-rows 1200 --arm sync ${ARGN})
endmacro()

click_run(out --click 300,50 --click 340,450)
shell_value(click_action "${out}" act)
shell_value(sync_value "${out}" sv)
if(NOT act STREQUAL "sync" OR NOT sv STREQUAL "55.2")
  message(FATAL_ERROR
    "gui_shell FAIL two-click: 40 columns over 400 rows at width 1810 is "
    "55.2 ppm; got action=${act} SYNC=\"${sv}\"")
endif()
message(STATUS "gui_shell PASS two-click: 40/400 -> ${sv} ppm")

# The measurement is signed the same both ways round: clicking bottom then
# top flips dcol and drow together, so it is the SAME slant and the
# operator does not have to know which order the gesture wanted.
click_run(down --click 300,50 --click 340,450)
click_run(up --click 340,450 --click 300,50)
shell_value(sync_value "${down}" sv_down)
shell_value(sync_value "${up}" sv_up)
if(NOT sv_down STREQUAL "${sv_up}")
  message(FATAL_ERROR
    "gui_shell FAIL two-click: top-then-bottom reads ${sv_down} ppm but "
    "bottom-then-top reads ${sv_up}; it is one measurement")
endif()
message(STATUS "gui_shell PASS two-click: both orders -> ${sv_up} ppm")

# **The SYNC gesture does not touch PHASE** [session 29]. This is what
# declaring the gesture bought: session 28's fused gesture had to set PHASE
# from the first click, which then needed the "PHASE takes the upper click"
# rule to stop a slanted chart's lower column poisoning the anchor. A
# declared gesture does one thing, and a SYNC measurement that quietly
# moved PHASE would apply an anchor the operator never asked for — silently,
# because they are watching the SYNC box.
foreach(order "300,50;340,450" "340,450;300,50")
  list(GET order 0 c1)
  list(GET order 1 c2)
  click_run(out --click ${c1} --click ${c2})
  shell_value(phase_value "${out}" ph)
  shell_value(click_action "${out}" act)
  if(NOT act STREQUAL "sync" OR NOT ph STREQUAL "")
    message(FATAL_ERROR
      "gui_shell FAIL two-click: measuring a slant (${c1} then ${c2}) set "
      "PHASE to \"${ph}\"; the SYNC gesture measures SYNC and nothing else")
  endif()
endforeach()
message(STATUS "gui_shell PASS two-click: the slant leaves PHASE alone")

# The SYNC gesture is TWO clicks and then it is over, and its first click
# is deliberately NOT an edit: it changes no value the operator can see, so
# marking the edit dirty there would arm Apply with nothing to apply —
# session 26's "active button that does nothing" [see click_image].
click_run(one --click 300,50)
shell_value(arm "${one}" a1)
shell_value(click_action "${one}" act1)
shell_value(edit_dirty "${one}" d1)
shell_value(pending_row "${one}" pend1)
if(NOT a1 STREQUAL "sync" OR NOT act1 STREQUAL "anchor" OR NOT pend1 EQUAL 50)
  message(FATAL_ERROR
    "gui_shell FAIL two-click: after one click arm=\"${a1}\" action=${act1} "
    "pending=${pend1}; the gesture is half done and must still be armed")
endif()
if(NOT d1 EQUAL 0)
  message(FATAL_ERROR
    "gui_shell FAIL two-click: the anchor alone dirtied the edit, so Apply "
    "is live with nothing to apply")
endif()
click_run(two --click 300,50 --click 340,450)
shell_value(arm "${two}" a2)
shell_value(edit_dirty "${two}" d2)
shell_value(image_click_enabled "${two}" ce2)
if(NOT a2 STREQUAL "none" OR NOT ce2 EQUAL 0)
  message(FATAL_ERROR
    "gui_shell FAIL two-click: the measurement is complete but arm=\"${a2}\" "
    "(crosshair=${ce2})")
endif()
if(NOT d2 EQUAL 1)
  message(FATAL_ERROR
    "gui_shell FAIL two-click: a measured SYNC left the edit clean — Apply "
    "would stay grey over the operator's own number")
endif()
message(STATUS "gui_shell PASS two-click: anchor, then measure, then done")

# **A short baseline MEASURES and says what it is worth** [session 29,
# Sara]. Until session 29 `min_baseline_rows` was a GATE and a second click
# inside it was re-read as re-picking PHASE; arming answers that question
# outright, so the number went back to being only a precision statement.
# The operator's judgement is theirs — what is owed them is the error bar,
# not a refusal.
#
# The two cases are chosen to have the SAME answer and different worth:
# 40 columns over 400 rows and 1 column over 10 rows are both 55.2 ppm, and
# only the annotation tells them apart.
click_run(out --click 300,50)
shell_value(min_baseline_rows "${out}" need)
if(need LESS 2)
  message(FATAL_ERROR "gui_shell FAIL two-click: baseline is ${need} rows")
endif()
math(EXPR too_close "50 + ${need} - 1")
click_run(out --click 300,50 --click 340,${too_close})
shell_value(click_action "${out}" act)
shell_value(sync_value "${out}" sv)
shell_value(slant_note "${out}" note)
if(NOT act STREQUAL "sync" OR sv STREQUAL "")
  message(FATAL_ERROR
    "gui_shell FAIL short baseline: a second click ${need} rows short of "
    "the reference must still MEASURE (action=${act}, SYNC=\"${sv}\") — the "
    "gate was removed in session 29")
endif()
if(NOT note MATCHES "short baseline")
  message(FATAL_ERROR
    "gui_shell FAIL short baseline: measured over ${too_close} rows and the "
    "note does not say it is short: \"${note}\"")
endif()
# A 10-row baseline at 100% on a 1810-column image is 1e6/(10*1810) = 55
# ppm of the operator's own aim, which is the same size as the errors this
# control exists to remove. The number itself is the point: an annotation
# that did not scale with the baseline would be decoration.
click_run(out --click 300,50 --click 301,60)
shell_value(sync_value "${out}" sv_short)
shell_value(slant_note "${out}" note_short)
if(NOT sv_short STREQUAL "55.2")
  message(FATAL_ERROR
    "gui_shell FAIL short baseline: 1 column over 10 rows is 55.2 ppm, got "
    "\"${sv_short}\"")
endif()
if(NOT note_short MATCHES "55 ppm" OR NOT note_short MATCHES "10 rows")
  message(FATAL_ERROR
    "gui_shell FAIL short baseline: 1 px of aim over 10 rows is +/-55 ppm; "
    "the note says \"${note_short}\"")
endif()
# ...and a LONG baseline is not annotated as short, or the label means
# nothing. Same measurement, forty times the baseline.
click_run(out --click 300,50 --click 340,450)
shell_value(slant_note "${out}" note_long)
if(note_long MATCHES "short baseline")
  message(FATAL_ERROR
    "gui_shell FAIL short baseline: a 400-row baseline is not short, but "
    "the note says \"${note_long}\"")
endif()
if(NOT note_long MATCHES "1 ppm")
  message(FATAL_ERROR
    "gui_shell FAIL short baseline: 1 px of aim over 400 rows is +/-1 ppm; "
    "the note says \"${note_long}\"")
endif()
message(STATUS
  "gui_shell PASS short baseline: same 55.2 ppm, \"${note_short}\" vs "
  "\"${note_long}\"")

# **The uncertainty scales with ZOOM**, and this check exists because a
# mutation that dropped the zoom term from `slant_error_ppm` SURVIVED
# everything above [session 29]. Every measurement check ran at 100%, where
# the scale is 1.0 and the term is invisible — the classic shape of a
# survivor that is neither an equivalent mutant nor code to delete, but a
# hole in the test.
#
# What is actually being said: the operator's aim is one SCREEN pixel,
# which is `1/scale` image columns, so the SAME IMAGE-ROW baseline is worth
# less at a coarser zoom. Forty image rows is +/-14 ppm at 100% and +/-55
# at 25%. The clicks differ in pixels precisely so that they do NOT differ
# in image rows: 40 rows at 100% is 40 px, at 25% it is 10 px.
run_metrics(out --state drawing --ioc 576 --rate 120 --zoom 100
            --click-rows 1200 --arm sync --click 300,50 --click 301,90)
shell_value(slant_note "${out}" note_100)
run_metrics(out --state drawing --ioc 576 --rate 120 --zoom 25
            --click-rows 1200 --arm sync --click 300,50 --click 301,60)
shell_value(slant_note "${out}" note_25)
if(NOT note_100 MATCHES "40 rows" OR NOT note_25 MATCHES "40 rows")
  message(FATAL_ERROR
    "gui_shell FAIL zoom precision: the two runs must measure over the SAME "
    "40 image rows or the comparison says nothing — got \"${note_100}\" and "
    "\"${note_25}\"")
endif()
if(NOT note_100 MATCHES "14 ppm")
  message(FATAL_ERROR
    "gui_shell FAIL zoom precision: 1 screen px over 40 rows at 100% is "
    "+/-14 ppm; the note says \"${note_100}\"")
endif()
if(NOT note_25 MATCHES "55 ppm")
  message(FATAL_ERROR
    "gui_shell FAIL zoom precision: at 25% one screen pixel is 4 columns, so "
    "the same 40-row baseline is +/-55 ppm, not what \"${note_25}\" says — "
    "a precision claim that ignores the zoom is worth nothing at Fit")
endif()
message(STATUS
  "gui_shell PASS zoom precision: 40 rows is \"${note_100}\" / \"${note_25}\"")

# Two points on the SAME ROW measure nothing — there is no baseline at all,
# and the slant is 0/0. hamfax, where this gesture comes from, divides by
# zero here. Nova keeps the anchor and stays armed, because the operator's
# gesture is not finished: they aimed badly, and asking again is the fix.
click_run(out --click 300,50 --click 340,50)
shell_value(click_action "${out}" act)
shell_value(sync_value "${out}" sv)
shell_value(arm "${out}" a)
shell_value(pending_row "${out}" pend)
if(NOT act STREQUAL "none" OR NOT sv STREQUAL "")
  message(FATAL_ERROR
    "gui_shell FAIL same row: two clicks on row 50 produced action=${act} "
    "SYNC=\"${sv}\"; there is no baseline to measure over")
endif()
if(NOT a STREQUAL "sync" OR NOT pend EQUAL 50)
  message(FATAL_ERROR
    "gui_shell FAIL same row: the gesture must survive a bad aim "
    "(arm=\"${a}\", pending=${pend}) so the operator can simply click again")
endif()
message(STATUS "gui_shell PASS same row: no baseline, gesture still live")

# **Re-arming discards the half-made measurement**, and this check exists
# because a mutation removing that clearing SURVIVED everything above
# [session 29]. `set_arm`'s comment claimed the rule; nothing tested it.
#
# The operator's action is the ordinary one — "I clicked the wrong place,
# start again" — and the failure it prevents is the plausible-looking kind:
# a fresh first click paired with an anchor from before the restart
# measures a slant between two points the operator never meant to pair,
# and produces a NUMBER rather than an obvious error. Same failure as an
# anchor outliving the edit, one gesture earlier.
click_run(out --click 300,50 --arm sync --arm sync --click 340,450)
shell_value(click_action "${out}" act)
shell_value(sync_value "${out}" sv)
shell_value(pending_row "${out}" pend)
if(NOT act STREQUAL "anchor" OR NOT sv STREQUAL "")
  message(FATAL_ERROR
    "gui_shell FAIL re-arm: after disarming and arming again, the next "
    "click is a FIRST click — it measured instead (action=${act}, "
    "SYNC=\"${sv}\"), pairing it with an anchor from the abandoned gesture")
endif()
if(NOT pend EQUAL 450)
  message(FATAL_ERROR
    "gui_shell FAIL re-arm: the fresh anchor should be row 450, is ${pend}")
endif()
# Switching to the OTHER gesture discards it too, for the same reason.
click_run(out --click 300,50 --arm phase --arm sync --click 340,450)
shell_value(click_action "${out}" act2)
shell_value(sync_value "${out}" sv2)
if(NOT act2 STREQUAL "anchor" OR NOT sv2 STREQUAL "")
  message(FATAL_ERROR
    "gui_shell FAIL re-arm: an anchor survived a switch to the PHASE "
    "gesture and back (action=${act2}, SYNC=\"${sv2}\")")
endif()
message(STATUS "gui_shell PASS re-arm: starting again really starts again")

# The baseline is zoom-dependent, and that is not a detail: click
# precision is one screen pixel, which is `1/scale` columns, so a view
# scaled down needs a longer baseline to say the same thing. Zooming IN
# must never ask for MORE rows.
set(prev_need 0)
foreach(z "25" "50" "100" "200")
  run_metrics(out --state drawing --ioc 576 --rate 120 --zoom ${z}
              --click-rows 1200)
  shell_value(min_baseline_rows "${out}" n)
  if(prev_need GREATER 0 AND n GREATER prev_need)
    message(FATAL_ERROR
      "gui_shell FAIL two-click: zoom ${z} needs ${n} rows, more than the "
      "${prev_need} the smaller zoom needed")
  endif()
  set(prev_need ${n})
  message(STATUS "gui_shell PASS two-click: zoom ${z}% needs ${n} rows")
endforeach()

# The REASON LINE carries the armed state [session 29]. The buttons and the
# crosshair say THAT a gesture is live; only this says what to do with it,
# and for SYNC it also names the baseline the current zoom wants — now as
# advice rather than as a gate, since any second click measures.
click_run(out --click 300,50)
shell_value(correct_reason "${out}" why)
shell_value(min_baseline_rows "${out}" need)
if(NOT why MATCHES "${need}")
  message(FATAL_ERROR
    "gui_shell FAIL two-click: a measurement is pending and the reason "
    "line does not name the ${need}-row baseline: \"${why}\"")
endif()
message(STATUS "gui_shell PASS two-click: pending says \"${why}\"")

# Every armed state says what to click, and no armed state is silent: the
# gesture has no widget of its own to read, so a blank line here is a
# gesture the operator cannot discover [§3's duty, applied to a control
# with nothing to point at].
foreach(case "phase;dead sector" "sync;click one end")
  list(GET case 0 want)
  list(GET case 1 phrase)
  run_metrics(out --state drawing --ioc 576 --rate 120 --zoom 100
              --click-rows 1200 --arm ${want})
  shell_value(correct_reason "${out}" why)
  if(NOT why MATCHES "${phrase}")
    message(FATAL_ERROR
      "gui_shell FAIL arm ${want}: armed, and the reason line says "
      "\"${why}\" instead of naming what to click")
  endif()
endforeach()
message(STATUS "gui_shell PASS arm: every armed state says what to click")

# The measurement's note outlives the click that made it, because the
# operator reads it AFTER the number lands in the box, not during — and it
# does not outlive the edit, because a note describing a replaced number is
# a false label.
click_run(out --click 300,50 --click 301,60)
shell_value(correct_reason "${out}" why)
if(NOT why MATCHES "short baseline")
  message(FATAL_ERROR
    "gui_shell FAIL short baseline: the measurement is over and the reason "
    "line has already forgotten what it was worth: \"${why}\"")
endif()
click_run(out --click 300,50 --click 301,60 --arm phase)
shell_value(slant_note "${out}" note)
if(NOT note STREQUAL "")
  message(FATAL_ERROR
    "gui_shell FAIL short baseline: arming a new gesture left the old "
    "measurement's note standing: \"${note}\"")
endif()
message(STATUS "gui_shell PASS short baseline: the note lives as long as it is true")

# A half-made measurement ends with the edit [§8.5 item 4]. An anchor
# remembered from a chart no longer on the pane would complete a slant
# measured across two DIFFERENT pictures — the one failure this gesture
# can have that produces a plausible number rather than an obvious one.
#
# This has to happen inside ONE process: the rule fires on a TRANSITION,
# and two `run_metrics` calls are two programs, so the second one would
# start with nothing pending and pass no matter what the code did. That
# is what --then-state exists for, and the first version of this check
# got it wrong — it passed against a build with the clearing deleted.
#
# Session 29 found this one BY HAND on the built code, and it is the
# session's sharpest lesson. The clearing used to sit inside the edit-end
# block, guarded by `edit_dirty` — correct then, because the first click of
# a slant also set PHASE, so an anchor could not exist without a dirty
# edit. Declaring the gestures broke that coupling silently: the SYNC
# gesture's first click deliberately changes nothing the operator can see,
# so it does not dirty the edit, and the anchor began surviving into states
# with no picture behind it. It was still unreachable — arming clears it
# and arming is the only route to a second click — which is exactly what
# makes it worth a screamer: net-correct for an incidental reason was one
# refactor away from not being correct at all.
foreach(st "idle" "ready" "decoding" "saved")
  click_run(out --click 300,50 --then-state ${st})
  shell_value(pending_row "${out}" pend)
  shell_value(arm "${out}" a)
  if(NOT pend EQUAL -1)
    message(FATAL_ERROR
      "gui_shell FAIL two-click ${st}: an anchor survived into a surface "
      "with no correction to make (pending=${pend}); the next click there "
      "would measure a slant across two different pictures")
  endif()
  # ...and neither does the arming, for the same reason one step earlier:
  # a gesture armed against a picture that has left the pane would fire on
  # whatever arrives next [see Arm].
  if(NOT a STREQUAL "none")
    message(FATAL_ERROR
      "gui_shell FAIL two-click ${st}: the picture is gone and the gesture "
      "is still armed (arm=\"${a}\")")
  endif()
  # The values themselves go with it, checked through the PHASE gesture
  # because that is the one that writes a box.
  run_metrics(out --state drawing --ioc 576 --rate 120 --zoom 100
              --click-rows 1200 --arm phase --click 300,50
              --then-state ${st})
  shell_value(phase_value "${out}" ph)
  if(NOT ph STREQUAL "")
    message(FATAL_ERROR
      "gui_shell FAIL two-click ${st}: PHASE \"${ph}\" survived the edit")
  endif()
endforeach()
message(STATUS "gui_shell PASS two-click: no anchor survives the edit's end")

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
