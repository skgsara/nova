# START-HERE — Nova

**What this is, in one breath:** Nova decodes shortwave weather-fax
broadcasts into images, built purely from the public WMO/ISO standards,
GPLv3+, C++17 — the standards-first successor to Isobar.

**Rough day? Three steps to resume:**
1. Read the top entry of `SESSION-LOG.md`.
2. Say "let's continue" — the agent reads the log, states the next step
   back to you, and you confirm or redirect.
3. That's it. Nothing else needs to be remembered; the logs remember.

**One command that proves the project is alive:**
```
cmake -B build -S . && cmake --build build && ctest --test-dir build
```
(35 test suites (+2 with the GUI): three synthetic matrices + 22
real-fixture screamers + the ruler mapping + the live/batch equivalence
of the front end, of the tone detector, of the picture itself, of the
whole session state machine and of the WIRING with the threads actually
running + the audio ring + the PNG round-trip under an independent
decoder + the two operator corrections behaving asymmetrically;
`gui_layout` and `gui_shell` join when FLTK and RtAudio are installed.)
See the M4 shell, if FLTK and RtAudio are installed — the full window,
and since session 23 with the live decode behind it: `./build/nova-gui`
It opens the remembered input device (the default on first run); the
Device menu takes effect the moment it changes and persists, so feed it a
receiver (or a virtual cable) and press Start; Force Start needs IOC and
Rate both explicit and is how you begin mid-transmission. Rows appear in the pane as they are
drawn, the level meter moves, and a completed transmission is decoded and
saved to the folder in Settings → Image folder, named by UTC timestamp.
PHASE and SYNC go live while the preview is drawing — and since session 27
they work on the SAVED chart too: type a column or a ppm, press Apply, and
the picture is re-rendered from the raw stream kept behind it and written
back over the same file (one transmission, one file — there is no Save
button). Auto puts the automatic decode back, byte for byte. When a
correction is not possible the two buttons say why instead of just being
grey.
PHASE and SYNC are two matching rows — caption, box, arming button — and
SYNC has four steppers on the row beneath it, −10 −1 +1 +10, so a slant
can be nulled by eye instead of by typing a number. From a blank box they
start at the clock the picture was drawn on, not at zero: on a white-only
station that is −70 to −118 ppm, and starting at zero would make the first
click a jump of the whole error. PHASE has no steppers on purpose — it is
a seed refined within ±54 columns, so a smaller nudge would move nothing.
Its instrument is the click.
Since session 29 both picture gestures are ARMED, which is hamfax's shape:
press the small button beside PHASE, then click the dead sector, and PHASE
takes that column (same arithmetic as the ruler, so tick and click agree).
Press the one beside SYNC, then click one feature twice, and Nova measures
the slant between the clicks. A click with nothing armed does nothing at
all — that is the point, so a stray click cannot move your numbers. Armed,
the pointer is a crosshair and the reason line says what to click; the
gesture disarms itself when it is done, so there is no mode to leave.
A short baseline still measures and tells you what it is worth: ten rows
apart is ±55 ppm, which the reason line says, because that is your call to
make and not the program's. Two clicks on the SAME row measure nothing —
there is no baseline at all — and the gesture stays armed so you can just
click again.
Since session 30 a transmission arriving while you are correcting a chart
does NOT take the screen from you: it draws into a background buffer
behind a compact RECEIVING panel in the sidebar's lower area — state, line
count, thumbnail — and the picture comes forward only when you click it.
Nothing promotes on its own, so pressing Apply cannot replace your own
correction with the incoming page. It is still saved to its own file the
moment it finishes; the hold is on the pane, never on the disk. Since
session 31 Apply also does the right thing while that is going on — it
re-renders the chart you are looking at, not the one arriving behind the
indicator — and clicking the indicator actually hands the pane over,
which until then it could not. (This one needs two transmissions to see,
so it has still never been looked at by a person; what defends it is
`live_engine`'s `test_background_buffer` and `gui_shell`'s §8.2 block,
which drives the whole sequence through an offline capture.)
Pick IOC 576 and the ruler lights up in image columns; change Zoom and the
column at the left edge stays put. Help → About.
It also answers without opening anything — and opens no sound card when
it does: `./build/nova-gui --devices` lists the input devices, `--metrics`
prints the real geometry of every region and the shell's state, and
`--state decoding` shows the transport rules (the button greys, still
reading "Start").
Since session 31 it can also run a whole capture from a FILE — no sound
card, no window, the real engine and the real handlers — which is the only
way the two-transmission case above gets checked at all. It writes images,
so it refuses to run until told where to put them:
```
./build/nova-gui --image-folder /tmp/nova-out \
  --feed fixtures/vmw-phasing-image-160s.wav,100 --stop-capture --mark saved \
  --type phase 900 --feed fixtures/vmw-phasing-image-160s.wav,50 --mark buffered \
  --apply --mark after-apply --recv-click --mark promoted
```
Each `--mark` prints a line: whether anything is buffered, how many rows it
has, how many the PANE has, and whether the edit is still open.
No FLTK or RtAudio? The build says `nova-gui: SKIPPED` and everything else
builds and passes as before.
Try the decoder: `./build/nova-decode fixtures/test-chart-jmh-kiwisdr-image-60s.wav out.pgm`
See the control signals: `./build/nova-tones fixtures/vmw-start-phasing-100s.wav`
See a recording that loses 1270 samples mid-picture drawn in one piece —
the half-page offset that was there until session 11:
`./build/nova-decode fixtures/himawari-jmh-warp-120s.wav warp.pgm`
See a white-only station phased from its phasing interval — the picture
that was drawn rotated by 520 px until session 7:
`./build/nova-decode fixtures/vmw-phasing-image-160s.wav vmw.pgm`
See the LIVE picture — the forward-only preview, drawn row by row the way
the pane will draw it, driven by the real session state machine (start
tone, phasing watcher, anchor handoff and all):
`./build/nova-preview fixtures/vmw-phasing-image-160s.wav vmw-prev.pgm`
(Recordings that open mid-transmission have no opening for the machine to
hear; give it the operator's answer: `--force 576 120`, and `--phase` /
`--sync` exercise the overrides. The screamer form:
`ctest --test-dir build -R live_preview -V` — and `live_session` for the
machine itself.)
See the two operator corrections behave DIFFERENTLY, which is the whole
point of them — the same recording, both ways. On a white-only station
(no per-line sync, so nothing measures the clock) the typed ppm is used;
on a pulse station the fit outranks it and the page does not move:
`./build/nova-decode fixtures/vmw-white-sector-120s.wav w.pgm --sync 2000`
`./build/nova-decode fixtures/test-chart-jmh-kiwisdr-image-60s.wav p.pgm --sync 2000`
(`--phase FRAC` is the other one, and is a seed: point it near the dead
sector and the search refines back onto it; point it at a different
feature and the picture moves there.)
Hear the library's only real stop tone — 450 Hz, fading 0.88 s mid-tone,
cut session 21 so the signal that ENDS a transmission is in the library:
`./build/nova-tones fixtures/nmc-image-stop-tone-120s.wav`

**Where things live:**
- `docs/00` — prior-art survey and reuse ledger
- `docs/01` — the signal spec (authoritative)
- `docs/02` — ISO 9876 §4.2 compliance matrix (acceptance checklist)
- `docs/03` — pre-M4 standards/provenance/GUI-readiness audit
- `docs/04` — commercial receiver UI survey; the M4 design decisions
- `docs/05` — the M4 shell design (all questions decided, session 17)
- `ROADMAP.md` — milestones, done vs pending
- `SESSION-LOG.md` — newest first, append-only, tracked in git
