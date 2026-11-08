# lxr/ — classic drum voices (GPL-3.0-only)

The drum, snare, hi-hat and cymbal DSP from the Erica Synths LXR firmware
(github.com/erica-synths/lxr-desktop-app), ported to the SP-1 drum firmware as the "classic" engine.

**Licence.** This folder is GPL-3.0-only with the additional terms in `NOTICE-upstream.md`. A firmware
binary built with it (`TK_WITH_LXR`) must be distributed under GPL-3.0 with its source. Wakes' MIT code is
compatible with that. Builds without the flag contain none of this code and stay MIT.

**Names.** The voices are called "classic" on the device and in presets. "Erica Synths", "LXR" and
"LXR-02" are their trademarks and are not used to suggest the firmware comes from them.

**Modified from upstream** (GPL-3.0 section 5: marked as modified):
- every `double` changed to `float` (the SP-1's Cortex-M4F has no double-precision FPU);
- `REAL_FS` set to 48000 (upstream 44140);
- the STM32 TCM placement macros (`INDTCM`, `INDTCMZ`, `IN_ITCM_RAM`) are empty, so that code and data
  go in the normal sections (the nRF52840 has no TCM);
- `calcNoise()` tests the phase wrap explicitly instead of reading the APSR flags;
- STM32 HAL, RNG, ADC and parameter-array dependencies replaced by stubs (`stub/`, `lxr_stubs.c`);
- `lxr_kit.c` (new): the five-control wrapper, rendering in LXR's 64-sample blocks.

**Instances** are fixed by the upstream code: three drum voices (classic kick, tom, perc), one snare,
one hi-hat and one cymbal, so a kit can use each classic model once.

**Cost** (Cortex-M4F harness, float, per sample): drum voice 214, snare 98, hi-hat 125, cymbal 128.

**To remove it:** delete this folder and build without `TK_WITH_LXR`. Kits and presets that name a
classic model fall back to the FM engine.
