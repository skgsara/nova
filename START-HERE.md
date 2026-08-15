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
Pick IOC 576 and the ruler lights up in image columns; change Zoom and the
column at the left edge stays put. Help → About.
It also answers without opening anything — and opens no sound card when
it does: `./build/nova-gui --devices` lists the input devices, `--metrics`
prints the real geometry of every region and the shell's state, and
`--state decoding` shows the transport rules (the button greys, still
reading "Start").
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
