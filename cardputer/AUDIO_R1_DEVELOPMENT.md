# Cardputer AUDIO_R1 — experimental development branch

**Baseline**: main commit `2e6f0f9f7ad7a3f5e5544cceb231e60f88cfff64` (Cardputer v0.98 native ESP32-S3 build PASS, GitHub Actions run 40148553881).

Only two original Cardputer engine files are modified:

- `src/fm_engine.c`: use the proven modulo-2^32 helper when YM2151 pitch increments exceed representable `uint32_t`. Keep original Cardputer DT1/DT2, multiplier and sample rate.
- `src/pcm_engine.c`: replace 4 C140/C352 signed `int8_t << 8` expressions and 4 C140 negative dynamic left shifts by equivalent multiplication. Maintain existing audio amplitude and mixer semantics.

43 other source/header files and the original partition table are **byte-identical to v0.98**. Original root `src/`, main-branch `cardputer/`, and `tab5/` are not changed.

**Host Clang UBSan**: 212,370 in-range phase values, 3,479 modulo cases and 2,048 signed PCM cases; zero mismatches. Full-song PCM bit identity, Cardputer native-build success and physical audio quality remain to be established for this experimental branch. No flashing is performed.

**Do not merge into main until native compilation and actual listening verification are complete.**
