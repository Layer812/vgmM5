# vgmM5 Tab5 R61 FIX1 — Cardputer R5 feedback candidate

This independent branch is **NOT** a new physical release.
Baseline: main commit 742d3143bfe845d99f8531f68eacf1e25f1dab1f. Original R61 production audio/BIN and
all entries in `tab5/PUBLIC_SOURCE_SHA256.json` remain pinned and unchanged.

## Already in R61

- R33/ R55: YM2151 operator modulation is not multiplied by another 2,
  the key perceived Cardputer R5 improvement.
- R38 KF uses bits 7..2.
- R36 DT2 is note-relative.
- R59 high phase-step conversion is defined modulo 2^32.
- R41/R47 native 62.5kHz + FIR25; UI, MDX, SD, queues, Speaker and
  16MiB/HumanXDF protection must remain unchanged.

## Candidate

Only `tab5/firmware/src/fm_engine.c` changes.
Under `VGMM5_TAB5_YM2151_R5_ALGO0_PREVIEW=1` the YM2151
algorithm-0 path is 0 -> 2 -> 1 -> 3 in physical slot numbering,
matching the standalone Cardputer R5 graph.
`-DVGMM5_TAB5_YM2151_R5_ALGO0_PREVIEW=0` restores the exact R61
code path for compilation and comparison. Other YM2151 algorithms
and all OPN/OPNA/OPL2/OPLL, PCM and ADPCM branches are unchanged.

`R5_CANDIDATE_PATCH_LOCK.json` separately pins the one edited source blob.
The original `PUBLIC_SOURCE_SHA256.json` is not changed.
`build_tab5_r5_candidate.py` checks all original SHA-256 pins
except that one independently pinned file. This build runner
creates a BIN only; it does not enumerate ports, erase, write or flash.

Status: source candidate; build and hardware audio/performance tests
pending. The R61 intermittent clicking issue remains independent
and unresolved. Never describe this candidate as a click-noise fix.
No media or game music added.
