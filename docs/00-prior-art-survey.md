# 00 — Prior-art survey and reuse ledger (P0.5 record)

Date: 2026-08-12. Surveyor: Kimi Code CLI + Sara.

## Conclusion

The WEFAX/F3C signal is **fully specified in public standards**
(WMO-No. 386 Vol. I Part III §5; ISO 9876:2015 for receiver behaviour).
No reverse engineering is required anywhere in this project. All
surviving open-source decoders belong to one GPL lineage, so algorithm
reuse is lawful under Nova's GPLv3+ (GPLv2+ code incorporated under
its "or any later version" clause).

## The lineage (verified)

```
ACFax 0.981011  (A. Czechanowski DL4SDC, 1995–98, GPLv2+)
  └─ HamFax     (C. Schmitt DH1CS, 2001–11, GPLv2+)
       └─ yahfax (sourceforge)
            └─ weatherfax_pi (S. D'Epagnier, OpenCPN plugin, GPLv3)
                 └─ KiwiSDR extensions/FAX (jks-prv; file-level GPLv3)
JWX 3.0   (P. Lutus, 2011–13, GPLv2+) — independent lineage, studied only
fldigi    (W1HKJ et al., GPLv3+) — studied; same hamfax-era heritage
Isobar    (S. Sakuragawa, 2026, GPLv3+) — author's own prior work
```

Evidence: KiwiSDR's `FaxDecoder.cpp` header names weatherfax_pi and
"adapted from yahfax... an improved adaptation of hamfax"; its FIR
coefficient tables are ACFax's `firwide/firmiddle/firnarrow` integers
verbatim (checked against `mod_demod.c`).

## Per-tool notes

| Tool | Take | Leave |
|---|---|---|
| ACFax | fs/4 quadrature downconvert; 9-tap FIR tables (3 widths); amplitude-normalized delay discriminator; arcsin linearization idea; **retain raw demod stream → non-destructive post-adjustment** | 8-bit/8 kHz fixed point; `SHORT int` type punning; OSS audio |
| HamFax | feature shape: IOC scaling, TX exists here only | Qt4, autotools, 8 kHz AU |
| weatherfax_pi / KiwiSDR | fractional sample-rate tracking + fractional accumulator resampling; phasing wedge-fit over ~7% of line, median over ~40 lines, 10–90% spread rejection; false-start filtering | KiwiSDR runtime plumbing |
| JWX | Goertzel start/stop detection (250 ms window) as reference; failure-mode catalogue: one-shot sync average, no per-line resync, hardcoded IOC 576/120, AFC abandoned | Java Sound workarounds |
| Isobar | per-line sync lock approach; session/fixture doctrine | KG-FAX interop (out of scope) |
| fldigi | mature C++ modem idioms | suite architecture |

## Reuse ledger

Running record — one row per reused artifact, added the day it enters
the tree. Empty at scaffold time.

| Artifact | From | Licence | Where in Nova | Date |
|---|---|---|---|---|
| _(none yet)_ | | | | |

## Access notes

- hamfax.sourceforge.net and sourceforge.net/projects/hamfax return
  HTTP 403 to this host (fetcher and curl); HamFax was surveyed via
  its GitHub mirror (sergioisidoro/ham-fax) README and the FreeBSD
  ports tree metadata.
- ACFax source: ftp.funet.fi/pub/ham/unix/Linux/misc/acfax-981011.tar.gz
  (still the FreeBSD MASTER_SITE).
- KiwiSDR repo has no root LICENSE; FaxDecoder.cpp carries a per-file
  GPLv3 header (D'Epagnier). Reuse per-file-licensed accordingly.
