# Drum firmware scenarios for the Wakes emulator

Scripted sessions that drive the whole firmware -- main.c, the power code, the drum pages
and the audio thread -- in the emulator's headless runner, and fail unless every step prints
what it should (`--expect`).

| script | what it plays |
|---|---|
| `run1-pages.sh` | power on, KIT -> DRUM 2 -> DRUM 3 -> KIT, `••` double tap to SETTINGS, mutes, one tap back, `••` + T1 (FX off), `••` + FFWD (kit 2), DRUM 1 `••` + FFWD (next model), triple tap to MARBLES and back |
| `run2-rocker.sh` | PLAY, rests on DRUM 3 and their clear at the bar, kit rotation, tape slow, rests choke / ring out, rests on every drum from MARBLES, `••` + rocker for the t range, then stopped: step-through on MARBLES and KIT, audition and burst on DRUM 2 |
| `run3-sequencer.sh` | the sequencer playing: KIT F4 fill up and back, SETTINGS `••` + T1 (drum 1's pattern from the next kit), `••` + FFWD (bass on the Y clock), three cables from MARBLES (t1 -> slot 1, X2 -> tone, Y -> bass note), then a `••` hold to power off; the drive is dumped to `$OUT` (default `run3-drive`) |
| `run4-reload.sh` | boots from run3's drive (`$IN`) with a bad `tape_release` in drums.cnfg: the line is named, the slot's `[drums]` loads with 0 bad fields, the kit, bass and cables are back, and the next power-off saves the same `[drums]` text |

The emulator lives on the `claude/wakes-emulator` branch (`emulator/`). Its `build.sh` needs
three changes to build this tree: `sp1_drums_ui.c` and `sp1_drums_patch.c` in `WAKES_C` (and no `sp1_midifile.c`),
`sp1_drums.cc` in `WAKES_CXX`, and the tinykit sources compiled with `-I third_party/tinykit`;
build with `EMU_KCONFIG="SP1_DRUMS=y"`. Then:

    WAKES_HEADLESS=path/to/wakes_headless tools/host-tests/drums-emu/run1-pages.sh
    WAKES_HEADLESS=path/to/wakes_headless tools/host-tests/drums-emu/run2-rocker.sh
    WAKES_HEADLESS=path/to/wakes_headless tools/host-tests/drums-emu/run3-sequencer.sh
    WAKES_HEADLESS=path/to/wakes_headless tools/host-tests/drums-emu/run4-reload.sh

Each prints `[emu] N of N expectations met` and exits 0, or names what was missing.
