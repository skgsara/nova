PASS:          D
AUDITOR:       Claude Sonnet 5, 2026-08-16, fresh subagent, no authoring context
INPUTS:        Source tree at /Users/sakuragawasara/Documents/2026/2026 Amateur Radio/isobar-nova/nova.
               Read: CMakeLists.txt (full), core/wav.cpp, core/wav.hpp, core/image.cpp,
               core/image.hpp, core/resample.cpp, core/fax.cpp (targeted sections:
               buffer sizing, `long` usage, out_lines/width computation), core/phasing.cpp
               (targeted), core/demod.cpp (targeted), live/ring.hpp (full), live/session.cpp
               (full), live/session.hpp (targeted), live/tone_stream.cpp/.hpp (targeted),
               live/png.cpp (targeted), live/engine.hpp (grepped for constants),
               cli/nova-decode.cpp (full).
               Pass C's report: /tmp/nova-audit-c/PASS-C-REPORT.md (received in full, read
               in full before starting this pass).
               Gate 0 values as given in the task (not re-derived, not read from
               docs/06-audit-gate0.md, which was NOT opened).
               Not read: SESSION-LOG.md, ROADMAP.md, AGENTS.md, START-HERE.md,
               docs/06-audit-gate0.md, docs/audit/PASS-B-REPORT.md, any git history.
               Confirmed: no forbidden file was opened at any point in this pass.
               Tooling: existing build at build/ was used for all dynamic testing
               (NOT rebuilt, per instructions). /usr/bin/time -l for memory measurement.
               `perl -e 'alarm N; exec @ARGV'` for timeout enforcement (no `timeout`
               binary on this machine, confirmed absent before starting). `xcrun otool -tV`
               for disassembly (the PATH-default `otool`, from an Anaconda install, silently
               produced empty output — this was caught and worked around by using
               `xcrun otool` explicitly; noted here in case it recurs in a future pass).
               `clang -target x86_64-apple-macosx11.0 -c` used to cross-compile individual
               .cpp files with the project's actual CMake-derived flags, to inspect x86-64
               codegen on this arm64-only machine, since no x86-64 hardware or emulator was
               available and BENCH_METHOD is not required this round (this is static codegen
               inspection, not a benchmark, and no performance figures are claimed from it).
               Malformed WAV fixtures were synthesized in /tmp/nova-audit-d/malformed/ via a
               small Python script (not part of the repo).

================================================================================
1. CRITICAL — UNBOUNDED ALLOCATION / UNBOUNDED WORK ON UNTRUSTED INPUT
================================================================================

ID:            D-PERF-001
PASS:          D
SEVERITY:      critical
LOAD-BEARING:  yes
SOURCE:        core/wav.cpp:57-70
IMPLEMENTED:   core/wav.cpp:67-70
VERDICT:       DEVIATES
EVIDENCE:      The "data" chunk's declared size field (a raw `uint32_t` read directly
               from the file at core/wav.cpp:57, `uint32_t size = rd_u32(f);`) is passed
               straight into `data.resize(size)` at core/wav.cpp:68 with no check against
               the number of bytes actually remaining in the file, and no independent
               sanity cap.

               Tested by crafting a 144-byte WAV file whose header declares a "data"
               chunk of 0xFFFFFFF0 (~4.29 GB) but whose file contains only 100 bytes
               after the header (/tmp/nova-audit-d/malformed/huge_data_chunk_truncated.wav,
               built by hand with Python, not from the repo). Ran:
                 /usr/bin/time -l perl -e 'alarm 60; exec @ARGV' \
                   ./build/nova-decode huge_data_chunk_truncated.wav /tmp/out.pgm
               Result: exit code 142 (SIGALRM — the process did NOT return within 60 s),
               "maximum resident set size" = 5,498,355,712 bytes (~5.5 GB), "peak memory
               footprint" = 12,892,398,928 bytes (~12.9 GB), 930,908,270,963 instructions
               retired. A second, independent test file declaring size=0xFFFFFFFF
               (/tmp/nova-audit-d/malformed/max_u32_data_chunk.wav) reproduced the same
               result: exit 142, ~5.5 GB RSS, ~12.9 GB peak footprint, within a 45 s
               alarm. A 144-byte file drives an almost-13 GB, 60-second-plus hang.

               The 12.9 GB figure is larger than the ~4.29 GB the single `resize()` call
               requests because the pipeline re-expands the same nominal sample count at
               several later stages (mono float samples in `Wav::samples`, then the
               resampled buffer, then the demodulated video buffer) before any stage
               would have had a chance to reject it — none of those stages caps its size
               either; see NOTES.
NOTES:         What would change the verdict to CONFORMS: a check, immediately after
               reading `size` at core/wav.cpp:57, that `size` does not exceed the number
               of bytes actually remaining in the open ifstream (computable via
               `tellg`/file size, or simply reading into a growable buffer with an
               explicit maximum and treating a short actual read as truncation), and/or
               an independent absolute cap (e.g. based on a maximum plausible recording
               duration at any supported sample rate) applied before `resize()`. Note
               that other malformed-file classes tested in this pass (truncated headers,
               zero/garbage channel counts, unsupported bit depths, a junk chunk with an
               oversized declared size that is a NON-data/fmt chunk and is only
               `seekg`'d past) were all handled correctly — this is not a general parser
               fragility, it is specifically the one `resize()` call that trusts an
               attacker-controlled field with no bound.

ID:            D-PERF-002
PASS:          D
SEVERITY:      critical
LOAD-BEARING:  yes
SOURCE:        core/wav.cpp:80, core/resample.cpp:21-28
IMPLEMENTED:   core/resample.cpp:27-28
VERDICT:       DEVIATES
EVIDENCE:      Independent of D-PERF-001 and, notably, requiring NO lie about file size
               at all: `w.sample_rate` (core/wav.cpp:80, `static_cast<int>(rate)` from
               the header's raw declared rate field) is later divided into the fixed
               internal rate to form a resample ratio (cli/nova-decode.cpp:94,
               `nova::resample(w.samples, w.sample_rate, kInternalRate)` with
               kInternalRate=8000). `resample_ratio()` (core/resample.cpp:21) only
               rejects `ratio <= 0.0` (line 23) — there is no upper bound on the ratio,
               and therefore none on `out_n = in.size() * ratio` (core/resample.cpp:27)
               or the `std::vector<float> out(out_n)` allocation that follows on line 28.

               Tested with a WAV file that is entirely well-formed and internally
               consistent — the declared "data" chunk size exactly matches the actual
               bytes present (200,000 bytes of real, if silent, 16-bit stereo audio),
               only the declared sample rate is absurd: 1 Hz
               (/tmp/nova-audit-d/malformed/tiny_rate_real_data.wav, 200,044 bytes on
               disk). Ran:
                 /usr/bin/time -l perl -e 'alarm 45; exec @ARGV' \
                   ./build/nova-decode tiny_rate_real_data.wav /tmp/out2.pgm
               Result: exit code 142 (hang past 45 s), "maximum resident set size" =
               1,602,174,976 bytes (~1.6 GB and still climbing when killed), "peak
               memory footprint" = 1,602,487,760 bytes, 741,911,252,650 instructions
               retired. The 1.6 GB figure matches the arithmetic exactly: mono-ized
               input is 50,000 samples; ratio = 8000/1 = 8000; out_n = 50,000 * 8000 =
               400,000,000 floats = 1.6 GB, before the O(zero_crossings) per-output-
               sample convolution loop (core/resample.cpp:38-57) even finishes running
               over that many output samples, which is what produces the hang on top of
               the allocation.
NOTES:         This is arguably the more serious of the two resample-adjacent findings
               because it needs no truncation, no lying chunk-size field, and no chunk
               parsing edge case — a completely well-formed, correctly-sized WAV file
               with one implausible header field (a 1 Hz sample rate; nothing in
               core/wav.cpp or the resample call site rejects any positive rate,
               however implausible for audio) is sufficient. What would change the
               verdict: a plausibility check on `w.sample_rate` after reading the WAV
               header (e.g. reject rates outside something like 1 kHz-500 kHz, which
               covers every real sound card and every WEFAX-relevant rate with room to
               spare), and/or a cap in `resample()`/`resample_ratio()` on the ratio or on
               `out_n` relative to `in.size()`, independent of where the implausible
               rate came from.

ID:            D-PERF-003
PASS:          D
SEVERITY:      critical
LOAD-BEARING:  yes
SOURCE:        live/session.cpp:530, live/session.cpp:439-448, live/tone_stream.cpp:88-106
IMPLEMENTED:   live/session.cpp:136-226 (push), live/session.cpp:529-537 (trim_preroll)
VERDICT:       DEVIATES
EVIDENCE:      Traced statically, NOT dynamically executed — see the caveat in NOTES and
               the corresponding gap (D-GAP-004). This is the one finding in this pass
               built from source reading rather than a reproduced crash/hang, so it is
               reported with lower confidence than D-PERF-001/002 and its severity
               should be read with that in mind.

               `LiveSession::push()` (live/session.cpp:136) unconditionally appends every
               incoming video sample to `retained_` (line 146,
               `retained_.insert(retained_.end(), video, video + n);`) and calls
               `trim_preroll()` at the end of every call (line 223). `trim_preroll()`
               (live/session.cpp:529) opens with `if (in_transmission_) return;` at line
               530 — i.e. it performs NO trimming at all while a transmission is
               considered in progress. `in_transmission_` is set true in
               `begin_opening()` (live/session.cpp:379, called when a start tone is
               detected) and is only cleared by `end_transmission()` or an operator
               `stop_capture()` in the READY/START-TONE/PHASING states
               (live/session.cpp:85, 260, 314) — none of which happen automatically
               while the state machine is waiting on a start-tone run to close.

               The only automatic bound on how long `retained_` can grow while
               `in_transmission_` is true and no picture has started drawing yet
               (`kDrawingPreview`, which does have a bound — the "page cap" at
               `cap_sample_`, live/session.cpp:217-218) is the give-up path in
               `watch_step()` (live/session.cpp:439-448): if `tone_end_known_` is true
               AND enough wait time (`opt_.phasing_wait_sec`, default 70 s per
               live/session.hpp:124) has elapsed since the tone ended, drawing begins
               from the tone's end regardless of whether phasing was found. But
               `tone_end_known_` (live/session.cpp:191-198) is only set once the start
               tone's run CLOSES — i.e. once `StreamToneDetector::run_open()`
               (live/tone_stream.cpp:143) returns false. A run only closes on a gap of
               more than `max_gap_` consecutive cold (non-hot) frames
               (live/tone_stream.cpp:106, `if (++c.cold > max_gap_) close(c);`) — there
               is no absolute maximum run duration anywhere in tone_stream.cpp/.hpp
               (grepped for `kMax`, `max_duration`, `timeout`, `watchdog`: no matches),
               and none in live/engine.hpp/.cpp either (grepped for the same terms and
               for any `constexpr`/`k`-prefixed constant in engine.hpp: none found).

               The consequence: a live input that sustains a continuous single-frequency
               tone at the start-tone frequency (purity above `opt.tones.purity`) for an
               extended period — whether a genuine stuck transmitter, an interfering
               carrier, or a deliberately hostile signal — never lets the run close,
               `tone_end_known_` never becomes true, the give-up path never fires, and
               `retained_` (a plain `std::vector<float>`, live/session.hpp:249) grows for
               as long as the tone is sustained, entirely unbounded by anything in this
               pass's reading of the code.
NOTES:         This was NOT reproduced by running the live engine with a synthesized
               sustained tone and measuring RSS over time — doing so credibly would need
               a purpose-built harness (feed StreamToneDetector/LiveSession a synthetic
               steady tone for, say, 10-30 minutes of simulated audio time and watch
               `retained_.size()` or process RSS climb without bound), which this pass's
               time budget did not include. The static trace is thorough (every
               constant/bound-related identifier in the relevant files was grepped, and
               every early-return/guard on the path from `push()` to unbounded growth was
               read directly), but a dynamic reproduction would be materially stronger
               evidence and could also reveal a bound this pass missed (e.g. an upstream
               cap in engine.cpp on how much audio a stalled `LiveSession` is fed before
               the operator or an external supervisor is expected to intervene — this
               pass read live/engine.hpp's constants but did not read the whole of
               live/engine.cpp's runtime loop end-to-end). What would change the
               verdict: either a demonstrated dynamic bound (closing this as
               NOT-APPLICABLE) or a reproduced unbounded-growth run (confirming it and
               raising confidence to the level of D-PERF-001/002). See D-GAP-004.

================================================================================
2. MINOR — PARSER ROBUSTNESS AND PORTABILITY-ADJACENT FINDINGS
================================================================================

ID:            D-PERF-004
PASS:          D
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        core/wav.cpp:128-138
IMPLEMENTED:   core/wav.cpp:128-138
VERDICT:       DEVIATES
EVIDENCE:      Every other sample-format branch in `read_wav()` (16/8/24-bit PCM,
               core/wav.cpp:86-114) manually assembles each sample from individual
               bytes with explicit shifts (e.g. line 91, `int16_t v =
               static_cast<int16_t>(p[0] | (p[1] << 8));`), which is correct on any host
               byte order because it never depends on the host's native multi-byte
               layout. The `fmt==3, bits==32` (IEEE float) branch is the one exception:
               core/wav.cpp:133-134 does `float v; std::memcpy(&v, p, 4);` — a raw
               byte-for-byte copy of the file's little-endian-encoded float bytes into a
               native `float`, which is only correct if the host's native `float`
               representation is little-endian IEEE-754, matching the file's encoding.
               On a big-endian host this would silently produce wrong sample values
               (not a parse failure, not a thrown exception — corrupted audio that the
               rest of the pipeline would decode as if it were valid).
NOTES:         Gate 0 places this floor at 64-bit little-endian only (x86-64, AArch64),
               both of which are little-endian in every realistic configuration, so this
               does NOT cause any actual corruption on the declared floor and is not a
               floor violation. It is flagged because Gate 0's instructions specifically
               ask for exactly this pattern — silent corruption rather than a loud
               failure on non-floor hardware — and because it is a genuine inconsistency
               against the rest of the same function, which was written byte-safe
               throughout. What would change the verdict to CONFORMS: either an explicit
               comment/static_assert recording the little-endian-host assumption as
               deliberate (matching the floor), or the same manual byte-assembly pattern
               used by the other four branches (e.g. reading the 4 bytes into a
               `uint32_t` via shifts, then `memcpy`-ing that into the float, which is
               endian-correct on any host since the reassembly targets host order
               explicitly).

ID:            D-PERF-005
PASS:          D
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        core/wav.cpp:58-66
IMPLEMENTED:   core/wav.cpp:58-66
VERDICT:       DEVIATES
EVIDENCE:      The "fmt " chunk handler always reads exactly 16 bytes' worth of fields
               (fmt, channels, rate, byte_rate, block_align, bits — two
               `rd_u16`/`rd_u32` calls each) regardless of the chunk's own declared
               `size`. Line 65, `if (size > 16) f.seekg(size - 16, ...)`, only handles
               the case where the declared size is LARGER than 16; there is no handling
               for a declared size SMALLER than 16 (legal-looking but atypical for a
               base PCM fmt chunk, and not rejected before the fixed 16-byte read
               proceeds anyway).

               Tested with a fmt chunk that declares size=8 but only 12 bytes are
               present in that position before the following "data" tag
               (/tmp/nova-audit-d/malformed/short_fmt_chunk.wav). Result in this specific
               case: the parser reads 16 bytes regardless, consuming into what was meant
               to be the following chunk's tag/size bytes, and the stream position ends
               up desynchronized enough that the subsequent tag read does not match
               "data", so the file is correctly rejected as "malformed WAV (missing
               fmt/data)" — no crash, no hang, clean error, verified via
               `/usr/bin/time -l ./build/nova-decode short_fmt_chunk.wav /tmp/out.pgm`.
NOTES:         Not a security or availability finding on the evidence gathered (this
               pass could not construct an input where the desync leads anywhere worse
               than a clean rejected-file error), but it is a genuine parser-correctness
               gap: the code does not validate the fmt chunk's declared size against
               what it is about to read, so its behavior on a short fmt chunk is
               "whatever bytes happen to follow get silently reinterpreted as fmt
               fields," which happened to fail safely in the one construction tried
               here. What would change the verdict: an explicit `if (size < 16) throw`
               before the fixed-width read.

ID:            D-PERF-006
PASS:          D
SEVERITY:      minor
LOAD-BEARING:  no
SOURCE:        core/fax.cpp:143-145, core/fax.cpp:961, core/fax.cpp:1821-1891,
               core/phasing.cpp:61-162, core/resample.cpp:36-39, core/gen.cpp:57-60
IMPLEMENTED:   (same locations)
VERDICT:       DEVIATES
EVIDENCE:      `long` (not `long long`, not a fixed-width or `ptrdiff_t`/`std::size_t`
               type) is used as the type for sample-domain loop counters and indices in
               several places, e.g. core/resample.cpp:36 `const long center =
               static_cast<long>(t);` and the surrounding loop over `k`
               (core/resample.cpp:38-39, bounded by `static_cast<long>(in.size())`), and
               core/phasing.cpp:61-162's circular-buffer index arithmetic. On the
               declared floor (x86-64/AArch64, presumably under the LP64 data model used
               by macOS and Linux) `long` is 64-bit and none of this is a problem in
               practice. Gate 0 explicitly asks this be flagged regardless: on Windows
               (LLP64), `long` is 32-bit, and a sample-domain index held in a 32-bit
               `long` silently truncates/wraps for inputs beyond `2^31` samples (about
               74.5 hours of audio at 8 kHz, less at higher processing rates) rather
               than failing loudly.
NOTES:         README.md does not claim Windows support (not verified against the
               forbidden-file set in this pass, taken as given), so this is not a floor
               violation and is recorded as informational-adjacent-to-minor per Gate 0's
               explicit instruction to flag it anyway. What would change the verdict:
               replacing `long` with `std::ptrdiff_t` or `int64_t` in these sample-index
               contexts, which is unambiguous and portable across data models, or an
               explicit statement that Windows is out of scope so the pattern is
               accepted as-is.

================================================================================
3. CONFORMS — INSTRUCTION-SET EXTENSIONS AND FLOATING-POINT STRATEGY
================================================================================

ID:            D-PERF-007
PASS:          D
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        CMakeLists.txt (whole file), build/CMakeFiles/nova-core.dir/flags.make
IMPLEMENTED:   n/a (build configuration, not a source finding)
VERDICT:       CONFORMS
EVIDENCE:      `grep -n -E "march|mtune|ffast-math|Ofast|-O3|-O2|SIMD|avx|sse|neon|-flto|native"
               CMakeLists.txt` found no matches beyond the file's own
               `add_compile_options(-Wall -Wextra)` — no `-march`/`-mtune`/`-mcpu` flag
               of any kind is set anywhere in the build. The actually-applied flags for
               the existing build/ tree were read directly from
               build/CMakeFiles/nova-core.dir/flags.make: `-O3 -DNDEBUG -std=c++17 -arch
               arm64 -Wall -Wextra` — no fast-math, no explicit CPU target beyond the
               `-arch arm64` Apple clang default.

               Inspected actual generated code, not just source: `xcrun otool -tV
               build/nova-decode` on the existing arm64 build shows `fm_demod`
               (core/demod.cpp) using only baseline NEON vector instructions
               (`movi.2d`, `mov.16b`) alongside scalar FP — NEON is a mandatory part of
               the AArch64 baseline, so nothing here is "above the floor" on arm64
               by definition.

               For the x86-64 side of the declared floor, which cannot be built or run
               on this arm64-only machine, this pass cross-compiled (compile-only,
               `.o` output, not linked or executed) core/demod.cpp, core/resample.cpp,
               core/fax.cpp, core/phasing.cpp and core/tones.cpp with `clang -target
               x86_64-apple-macosx11.0 -c -O3 -std=c++17 -Wall -Wextra` — i.e. the
               project's actual CMake-derived flags, minus only the arm64-specific
               `-arch` value, with no `-march` added (matching the project's own build,
               which adds none). Disassembling each resulting object with `xcrun otool
               -tV` and grepping for AVX-family register names:
                 demod.cpp:    0 ymm/zmm matches, 242 xmm matches
                 resample.cpp: 0 ymm/zmm matches, 106 xmm matches
                 fax.cpp:      0 ymm/zmm matches, 3143 xmm matches
                 phasing.cpp:  0 ymm/zmm matches, 576 xmm matches
                 tones.cpp:    0 ymm/zmm matches, 1659 xmm matches
               Zero AVX/AVX2/AVX-512 instructions (ymm/zmm registers) were emitted by
               the compiler for any of the five DSP-heavy files under the project's
               actual flags; only xmm (SSE2, part of the x86-64 baseline — SSE2 is
               architecturally guaranteed on every x86-64 CPU) register usage was found.
               This is consistent with Clang's documented behavior of targeting the
               generic x86-64 baseline when no `-march`/`-mcpu` is given.
NOTES:         This is codegen inspection, not a benchmark, and no performance claim is
               made from it (per Gate 0, BENCH_METHOD is not required and unmeasured
               performance claims would be a gap, not a finding — none is made here).
               The object files were compiled but never linked or executed; genuine
               runtime behavior on x86-64 hardware remains unverified (see D-GAP-003).
               If the project's actual x86-64 build were ever produced with a different
               toolchain, a CI override, or a developer's local `-march=native`
               (nothing in the tracked CMakeLists.txt does this, but an out-of-tree
               invocation could), this evidence would not cover that path.

ID:            D-PERF-008
PASS:          D
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        core/demod.cpp:83, core/fax.cpp:162, core/resample.cpp:58,
               core/gen.cpp:137
IMPLEMENTED:   (same locations)
VERDICT:       CONFORMS
EVIDENCE:      Under a 64-bit-only floor the classic x87-vs-SSE float-divergence
               question does not apply (no x87 target exists on either x86-64 or
               AArch64) — stated rather than padded, per Gate 0's instruction.
               Checked instead for the two things that DO still matter: fast-math flags
               (none found anywhere in CMakeLists.txt or the applied build flags — see
               D-PERF-007) and fragile exact-equality comparisons between independently
               computed floating-point values. `grep`-ing core/*.cpp for float/double
               `==`/`!=` against zero found four sites (core/demod.cpp:83, `re != 0.0
               || im != 0.0`; core/fax.cpp:162, `denom != 0.0`; core/resample.cpp:58,
               `norm != 0.0`; core/gen.cpp:137, `opt.ppm != 0.0`), all direct
               reads confirmed by inspection: every one is a division-by-zero guard on
               a value being tested against a literal, or a check of a user-supplied
               parameter against its default, not a comparison of two independently
               computed floating-point results for equality (which is the fragile
               pattern that drifts under `-ffast-math` or a different FMA contraction
               setting). No such comparison was found in the files sampled.
NOTES:         This was a targeted grep across core/*.cpp, not an exhaustive line-by-
               line read of every arithmetic expression in the codebase (core/fax.cpp
               alone is 1965 lines); a comparison pattern outside the four sites found
               could exist unread. Given the absence of fast-math flags, though, the
               practical risk this item is checking for (float results drifting between
               builds/optimization levels and breaking an exact-equality check) is
               already substantially mitigated at the build-flag level regardless of
               what a fuller comment-by-comment sweep might additionally find.

================================================================================
4. PEAK MEMORY MEASUREMENTS
================================================================================

ID:            D-PERF-009
PASS:          D
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        core/fax.cpp:1531-1536 (image buffer sizing), core/wav.cpp,
               core/resample.cpp, core/demod.cpp (upstream buffers)
IMPLEMENTED:   n/a — measurement, not a source-line finding
VERDICT:       NOT-APPLICABLE (no bound exists to conform to or deviate from —
               TARGET_FLOOR_RAM is not set per Gate 0; this entry reports the measured
               figure as instructed and registers the missing bound as D-GAP-001)
EVIDENCE:      Two measurements, both on the EXISTING build/ tree (not rebuilt), using
               `/usr/bin/time -l`:

               1. Largest fixture on disk (fixtures/vmw-phasing-image-160s.wav,
                  2,560,044 bytes, IOC 576, 120 lpm, 247 lines decoded, 1810 px wide
                  final image):
                    /usr/bin/time -l ./build/nova-decode \
                      fixtures/vmw-phasing-image-160s.wav /tmp/vmw.pgm
                  Result: 1.69 s real, "maximum resident set size" = 28,475,392 bytes
                  (~28.5 MB), "peak memory footprint" = 27,984,232 bytes.

               2. A synthetic, substantially larger chart generated with the project's
                  own `nova-gen` tool (not a crafted/malformed file — a genuine, well-
                  formed signal at the project's own IOC-576/120-lpm parameters, 3000
                  image lines, ~25.6 minutes of audio, 24,560,044-byte WAV file):
                    ./build/nova-gen /tmp/long_ioc576.wav --lpm 120 --ioc 576 \
                      --lines 3000 --dev 400
                    /usr/bin/time -l ./build/nova-decode /tmp/long_ioc576.wav /tmp/long.pgm
                  Decoded successfully: lines=2999, locked=2999, place error 0.25 px
                  rms. Result: 16.88 s real, "maximum resident set size" = 242,073,600
                  bytes (~242 MB), "peak memory footprint" = 241,697,296 bytes.

               Memory scales roughly with input size across these two points (2.56 MB
               input -> 28.5 MB RSS, ~11x; 24.6 MB input -> 242 MB RSS, ~10x) — consistent,
               no super-linear blowup observed on well-formed input in this range. This
               is the expected/legitimate-input memory profile, in contrast to
               D-PERF-001/002 where a tiny or moderately-sized but MALFORMED/implausible
               input produces multi-gigabyte allocations disproportionate to the bytes
               actually supplied.
NOTES:         "Largest supported IOC and line count" has no documented upper bound in
               the code that this pass found (out_lines is driven directly by audio
               duration; width is one of exactly two fixed values, 905 or 1810, per
               core/fax.cpp:1531) — a legitimately much longer real-world recording (a
               multi-hour continuous capture, for instance) would scale further, bounded
               ultimately by how much actual audio the operator or file supplies, which
               is a materially different risk profile than D-PERF-001/002's "tiny file,
               huge allocation" pattern. No REALTIME_BUDGET exists to check the 16.88 s
               / 3000-line figure against (D-GAP-002). No TARGET_FLOOR_RAM exists to
               check either figure against (D-GAP-001).

================================================================================
5. POSITIVE FINDING — ENDIANNESS AND RING-BUFFER DESIGN
================================================================================

ID:            D-PERF-010
PASS:          D
SEVERITY:      informational
LOAD-BEARING:  no
SOURCE:        core/wav.cpp:11-33, live/png.cpp:40-47, live/ring.hpp (whole file)
IMPLEMENTED:   (same locations)
VERDICT:       CONFORMS
EVIDENCE:      Two positive findings recorded for contrast against D-PERF-003/004:

               1. Both the WAV reader's integer paths (core/wav.cpp:11-33, the
                  `rd_u32`/`rd_u16`/`wr_u32`/`wr_u16` helpers, and their use throughout
                  `read_wav`/`write_wav`) and the PNG writer (live/png.cpp:40-47,
                  `put_be32`) do manual byte-by-byte assembly/disassembly with explicit
                  shifts, never a `memcpy`/`reinterpret_cast` of a multi-byte integer
                  onto host memory. This is correct on any host byte order, little- or
                  big-endian, exceeding what the little-endian-only floor requires. The
                  one exception (the float-sample WAV branch) is D-PERF-004, immediately
                  above.

               2. `AudioRing` (live/ring.hpp, read in full) is a fixed-capacity,
                  lock-free single-producer/single-consumer ring with allocation only at
                  construction (`explicit AudioRing(std::size_t capacity)`,
                  live/ring.hpp:36-37) — the producer side (live/ring.hpp:50-69) never
                  allocates, never blocks, and explicitly drops and COUNTS overrun
                  samples rather than growing (live/ring.hpp:63-65), which the header's
                  own comment identifies as a deliberate choice to make loss visible
                  rather than hidden or unbounded. This is the correct design for the
                  "live audio ring" item this pass was asked to check, and it does not
                  share the unbounded-growth problem found in D-PERF-003 (a different
                  structure, `LiveSession::retained_`, a `std::vector<float>` with no
                  capacity limit).
NOTES:         None; recorded per the protocol's expectation that navigability/design
               contrasts be given a positive counterpart where one exists, mirroring
               Pass C's own C-MAINT-019 practice.

================================================================================
GAP REGISTER
================================================================================

ID:            D-GAP-001
PASS:          D
QUESTION:      Whether the measured peak-memory figures in D-PERF-009 (28.5 MB / 242 MB
               on legitimate input) and D-PERF-001/002 (5.5 GB / 1.6+ GB and climbing on
               malformed/implausible input) are acceptable.
BLOCKED BY:    TARGET_FLOOR_RAM is explicitly not set per Gate 0.
RESOLVES IF:   A future Gate 0 sets a RAM bound, against which these measured figures
               can be checked directly.

ID:            D-GAP-002
PASS:          D
QUESTION:      Whether the 16.88 s decode time for a 3000-line/~25.6-minute recording
               (D-PERF-009) is acceptable, and whether any DSP stage's algorithmic
               complexity (this pass did not do complexity analysis on any stage) would
               become a practical problem at inputs larger than what was tested.
BLOCKED BY:    REALTIME_BUDGET is explicitly not set per Gate 0, and BENCH_METHOD is
               explicitly not required this round — no formal benchmark was run, and
               per Gate 0's instruction an unmeasured performance claim would be a gap,
               not a finding, so none is made beyond the wall-clock figures reported
               with their exact test conditions above.
RESOLVES IF:   A future Gate 0 sets a REALTIME_BUDGET and designates a floor CPU, and a
               benchmark pass measures actual DSP-stage timing against it.

ID:            D-GAP-003
PASS:          D
QUESTION:      Whether the x86-64 side of the declared floor is genuinely free of
               above-baseline instruction-set reliance at RUNTIME, and whether the
               project actually builds and runs correctly on x86-64 at all.
BLOCKED BY:    This is an arm64-only Mac; no x86-64 hardware, VM, or emulator was used.
               D-PERF-007's cross-compiled `.o` files were disassembled but never
               linked or executed — codegen inspection only.
RESOLVES IF:   A pass run on (or with access to) x86-64 hardware or an emulator that
               builds the project's actual CMake configuration for x86-64 and both
               inspects the resulting binaries and runs the test suite against them.

ID:            D-GAP-004
PASS:          D
QUESTION:      Whether D-PERF-003 (unbounded growth of LiveSession::retained_ while a
               start-tone run never closes) is real in practice, or whether some bound
               this pass's static trace missed (e.g. in the untraced remainder of
               live/engine.cpp, or in the GUI layer, gui/nova-gui.cpp, which this pass
               did not read at all) prevents it.
BLOCKED BY:    No dynamic reproduction was attempted. Constructing one needs a harness
               that feeds LiveSession/StreamToneDetector a synthesized, sustained
               single-frequency tone at a qualifying purity for an extended simulated
               duration (tens of minutes of audio-time, which need not take tens of
               minutes of wall-clock time if fed in large blocks) while monitoring
               `retained_.size()` or process RSS.
RESOLVES IF:   Either such a harness demonstrates unbounded growth (confirming
               D-PERF-003 at the same confidence level as D-PERF-001/002), or it
               demonstrates a ceiling this pass did not find (revising the verdict to
               NOT-APPLICABLE or CONFORMS).

ID:            D-GAP-005
PASS:          D
QUESTION:      Cross-reference against Pass A's item A.3 (corrupted-input testing),
               which this pass's malformed-WAV fuzzing (section 1, above) directly
               overlaps with.
BLOCKED BY:    Pass A has not run. There is no A.3 result to compare against, confirm,
               or contradict. This pass is the first to touch corrupted-input testing
               on this codebase, under this audit protocol.
RESOLVES IF:   Pass A runs and either reproduces D-PERF-001/002/003 independently
               (strengthening confidence) or reports different results on the same
               attack surface (worth reconciling).

ID:            D-GAP-006
PASS:          D
QUESTION:      Whether D-PERF-004 (the WAV float-sample branch's host-endianness
               assumption) actually produces corrupted samples on a real big-endian
               host, as opposed to being correct by some compiler- or platform-specific
               accident this pass did not consider.
BLOCKED BY:    No big-endian hardware or emulator was available. The finding rests on
               reading core/wav.cpp:128-138 and reasoning about what `memcpy`-ing
               little-endian file bytes into a native `float` does on a hypothetical
               big-endian host, not on an observed failure.
RESOLVES IF:   A big-endian build/run (e.g. under QEMU) either reproduces corrupted
               32-bit-float WAV decoding or shows the assumption is not exercised in
               practice for some reason this pass missed.

================================================================================
PASS SUMMARY
================================================================================

Findings by severity:
  critical:        3   (D-PERF-001, D-PERF-002, D-PERF-003)
  major:            0
  minor:            3   (D-PERF-004, D-PERF-005, D-PERF-006)
  informational:    4   (D-PERF-007, D-PERF-008, D-PERF-009, D-PERF-010)

  Total findings:  10

Load-bearing findings: 3 (D-PERF-001, D-PERF-002, D-PERF-003). All three are
availability/resource-exhaustion issues reachable from untrusted input (a malformed or
merely implausible WAV file for the first two, a sustained hostile or stuck RF tone for
the third) with no benchmark needed to establish them — they are crash/hang/unbounded-
growth findings by direct reproduction (D-PERF-001, D-PERF-002) or thorough static trace
(D-PERF-003), which is exactly the category Gate 0 designates critical regardless of the
missing RAM/realtime bounds.

Gaps: 6 (D-GAP-001 through D-GAP-006).

Pass C findings considered for override: none. Every Pass C finding (C-MAINT-001 through
C-MAINT-019, C-BUILD-001) is a maintainability observation — function length, nesting
depth, duplicated literals/formulas, comment style, navigability, or a build-simplicity
confirmation — none of them claims or implies any performance characteristic, and this
pass found no case where acting on a Pass C finding would conflict with a measured
performance result. The protocol's override clause (D may override C only with a
benchmark on TARGET_FLOOR_CPU demonstrating a REALTIME_BUDGET miss) therefore never
came into play in either direction: there was nothing in Pass C to override, and Gate 0
also confirms no such benchmark exists this round regardless.

What this pass did NOT cover:
  - Dynamic reproduction of D-PERF-003 (the live-session unbounded-growth finding) —
    static trace only; see D-GAP-004.
  - Any runtime testing on actual x86-64 hardware — only static/cross-compiled codegen
    inspection on this arm64 machine; see D-GAP-003.
  - Big-endian host behavior — no such hardware available; D-PERF-004 and the positive
    finding D-PERF-010 are both source-inspection only; see D-GAP-006.
  - Any formal benchmark against a CPU/realtime floor — none exists (TARGET_FLOOR_CPU
    and REALTIME_BUDGET are both unset per Gate 0), and per the task's instructions no
    performance number is estimated or implied beyond the specific, reproducible wall-
    clock and memory figures reported with their exact test conditions (D-PERF-009,
    D-GAP-002).
  - gui/nova-gui.cpp was not read at all in this pass (Pass C already flagged it as
    only partially read, C-GAP-004; this pass adds no further coverage of it, including
    for the live-session question in D-GAP-004, where the GUI layer is a candidate
    location for a bound this pass did not find).
  - tests/*.cpp were not read.
  - Algorithmic complexity analysis of any DSP stage (FFT/convolution/search loop
    complexity classes) — only end-to-end wall-clock and memory were measured, not
    per-stage profiling.
  - Exhaustive fuzzing — the malformed-WAV corpus built for this pass (15 files, listed
    in /tmp/nova-audit-d/malformed/) targets the specific fields Gate 0 named (chunk
    sizes, sample rate, channel count, bit depth, truncation at three different points)
    plus the two blow-up cases this pass derived from reading the code
    (D-PERF-001/002); it is a targeted corpus, not a coverage-guided fuzzer run, and a
    real fuzzing campaign could surface issues this pass's 15 hand-built files did not.
  - No file in the forbidden list (SESSION-LOG.md, ROADMAP.md, AGENTS.md, START-HERE.md,
    docs/06-audit-gate0.md, docs/audit/PASS-B-REPORT.md, git history) was read at any
    point in this pass — isolation was maintained throughout.
