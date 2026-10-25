# Cardputer AUDIO_R1 — experimental development branch

**Baseline**: main commit `2e6f0f9f7ad7a3f5e5544cceb231e60f88cfff64` (Cardputer v0.98 native ESP32-S3 build PASS, GitHub Actions run 40148553881).

Only two original Cardputer engine files are modified:

- `src/fm_engine.c`: use the proven modulo-2^32 helper when YM2151 pitch increments exceed representable `uint32_t`. Keep original Cardputer DT1/DT2, multiplier and sample rate.
- `src/pcm_engine.c`: replace 4 C140/C352 signed `int8_t << 8` expressions and 4 C140 negative dynamic left shifts by equivalent multiplication. Maintain existing audio amplitude and mixer semantics.

43 other source/header files and the original partition table are **byte-identical to v0.98**. Original root `src/`, main-branch `cardputer/`, and `tab5/` are not changed.

**Host Clang UBSan**: 212,370 in-range phase values, 3,479 modulo cases and 2,048 signed PCM cases; zero mismatches. ESP32-S3 native-build **PASS** (GitHub Actions [run 40148923112](https://github.com/Layer812/vgmM5/actions/runs/40148923112)). Full-song PCM bit identity and physical audio quality remain unverified for this development branch. No flashing is performed.

**Built binary (not flashed):** 1,241,360 bytes, SHA-256 `fcc4a0d6d7e37a3ac040018e8c4f49aa5296b46d137eb69e67ae15cbb86e9ca5`. CI artifact: [BIN, ELF, full log](https://github.com/Layer812/vgmM5/actions/runs/40148923112/artifacts/12582351435).

**Do not merge into main until physical listening verification is complete.**
