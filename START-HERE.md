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
(26 test suites (+2 with the GUI): three synthetic matrices + 20
real-fixture screamers + the ruler mapping + the live/batch equivalence
of the front end and of the tone detector; `gui_layout` and `gui_shell`
join when FLTK and RtAudio are installed.)
See the M4 shell, if FLTK and RtAudio are installed — the full window
with no decode behind it yet: `./build/nova-gui`
Pick IOC 576 and the ruler lights up in image columns; change Zoom and the
column at the left edge stays put. Help → About, Settings → Image folder.
It also answers without opening anything: `./build/nova-gui --devices` lists
the input devices, `--metrics` prints the real geometry of every region and
the shell's state, and `--state decoding` shows the transport rules
(the button greys, still reading "Start").
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

**Where things live:**
- `docs/00` — prior-art survey and reuse ledger
- `docs/01` — the signal spec (authoritative)
- `docs/02` — ISO 9876 §4.2 compliance matrix (acceptance checklist)
- `docs/03` — pre-M4 standards/provenance/GUI-readiness audit
- `docs/04` — commercial receiver UI survey; the M4 design decisions
- `docs/05` — the M4 shell design (all questions decided, session 17)
- `ROADMAP.md` — milestones, done vs pending
- `SESSION-LOG.md` — newest first, append-only, tracked in git
