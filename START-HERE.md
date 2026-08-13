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
(21 test suites: two synthetic matrices + 19 real-fixture screamers.)
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
- `ROADMAP.md` — milestones, done vs pending
- `SESSION-LOG.md` — newest first, append-only, tracked in git
