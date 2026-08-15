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
# then found not to work." Session 27 retains the raw stream behind the
# displayed image, so there is now something for a correction to act on;
# the lifecycle that spends it is M4 item 4, so Apply and Auto stay grey
# after SAVED ON PURPOSE — an active button that does nothing is the one
# failure this shell must not have (session 26, finding 2). What is
# checked here is that the greying is explained rather than mute.
foreach(st "idle" "ready" "drawing" "decoding" "saved")
  run_metrics(out --state ${st} --ioc 576 --rate 120)
  # The post-decode pair is grey in every state, because item 4 is not
  # built. When it is, this list is what says so out loud.
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
