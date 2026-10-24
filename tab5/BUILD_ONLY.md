# R61 FIX1 Build-only contract

`RUN_THIS_TAB5_BUILD_ONLY.cmd` runs `build_tab5.py`.

- Verifies source SHA-256 before fetching dependencies, after the build, and verifies the native BIN header.
- Uses pioarduino `54.03.21`, M5Unified Git SHA `e125e73f28b6b83e621e48df432af426bb2ff935`, and the pinned M5GFX dependency plus the exact-guarded two-revision overlay.
- Runs `pio pkg install`, vendor overlay patch, and `pio run` with environment `tab5-r61-speaker-revert`.
- Writes `.pio` build files *only* under this project; does not enumerate serial ports, connect to a device, flash bootloader, flash partition table, or run `pio upload`.
- M5GFX version other than 0.2.33/0.2.34 or source SHA mismatch is an intentional fail-closed stop.

This release is not an automated end-user flasher. Use the author-provided M5Burner share code for the distributed image, noting the unverified image SHA caveat.
