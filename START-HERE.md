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
(Until code exists: the project being "alive" = docs/01 + docs/02
present and the compliance matrix readable.)

**Where things live:**
- `docs/00` — prior-art survey and reuse ledger
- `docs/01` — the signal spec (authoritative)
- `docs/02` — ISO 9876 §4.2 compliance matrix (acceptance checklist)
- `ROADMAP.md` — milestones, done vs pending
- `SESSION-LOG.md` — newest first, append-only, private (gitignored)
