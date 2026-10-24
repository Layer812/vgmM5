# Tab5 R61 FIX1 — release status

Release date: 2026-10-25 (JST)  
Target: M5Stack Tab5 / ESP32-P4

- Reference source: VGMM5_R61_R58_SPEAKER_R60_LCD_FIX1 (229 source-pinned files in preserved package).
- Source archive SHA-256: `904ce8928cfc49964aabefbcdec3e69535a4486454e9f143380b0510294feb82`.
- Native-build/flash report archive SHA-256: `935939abefd083b0246e99eebfe2f6c878f9f2e621b5ec30b43d86c58204bf0a`.
- Built app `firmware.bin`: 868,576 bytes. SHA-256: `099d26838d9c84edc32eb1855489c725e8c5e897e749d29cab509abd140beede`.
- Native compile: PASS. Device identity + app-only flashing: PASS.
- Runtime: VGM/MDX music played; intermittent severe click noise persists; reboot/power-state correlation suggested, not proven.
- This revision also includes the [full Tab5 R61 FIX1 source ZIP](vgmM5_Tab5_R61_FIX1_source.zip), including a safe build-only script. **No verification has linked the M5Burner share-code image to the locally tested BIN.**
- No sample music, logs with device identifiers, user-specific backup images, bootloaders, or partition-table flashing scripts included.
- The archive's source and build-only scripts are also published as browsable files under [`tab5/firmware/src/`](firmware/src) and the `tab5/` directory; the legacy root `src/` is unchanged.
