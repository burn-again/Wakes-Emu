# Drum firmware scenarios for the Wakes emulator

Scripted sessions that drive the whole firmware -- main.c, the power code, the drum pages
and the audio thread -- in the emulator's headless runner, and fail unless every step prints
what it should (`--expect`).

| script | what it plays |
|---|---|
| `run1-pages.sh` | power on (SOUND), T2 PATTERN, T3 LANES, T4 KIT, `••` + T2 DRUM 2 and again to unmute and mute it, `••` + T3, T4, `••` double tap to SETTINGS and its mutes, one tap back, `••` + FFWD (kit 2), `••` + T1 and `••` + FFWD (next model), T1 SOUND and T1 again (the line off, on), triple tap to MARBLES and back |
| `run2-rocker.sh` | PLAY, rests on DRUM 3 and their clear at the bar, kit rotation, tape slow, rests choke / ring out, rests on every drum from MARBLES, `••` + rocker for the t range, then stopped: step-through on MARBLES and KIT, audition and burst on DRUM 2 |
| `run3-sequencer.sh` | the sequencer playing: KIT F4 fill up and back, SETTINGS `••` + T1 (drum 1's pattern from the next kit), `••` + FFWD (bass on the Y clock), three cables from MARBLES (t1 -> slot 1, X2 -> tone, Y -> bass note), then a `••` hold to power off; the drive is dumped to `$OUT` (default `run3-drive`) |
| `run4-reload.sh` | boots from run3's drive (`$IN`) with a bad `tape_release` in drums.cnfg: the line is named, the slot's `[drums]` loads with 0 bad fields, the kit, bass and cables are back, and the next power-off saves the same `[drums]` text |

| `run5-acid.sh` | the acid line from power-on: PLAY, SOUND F1 / F2 as its cutoff and resonance, T2 PATTERN, `••` + FFWD to the next pattern (taken up at the bar), then a `••` hold to power off: the slot keeps `line = campfire` and the mutes. `WAV=file` writes what it played |
| `run6-acid-moves.sh` | a cutoff sweep on SOUND F1, FFWD tap (keep), RWD tap (undo), keep again, FFWD held (add), resonance moved, RWD held (snap back: home, one take), RWD held again (the home before, no takes). `WAV=file` writes what it played |
| `run7-acid-lanes.sh` | PATTERN F1 moved to the markov pattern, T3 LANES: the accent lane's density down and back, SHIFT F3 stepping the slide lane's length through its zones, RWD held (snap back: the lanes home again), T4 KIT, then power off: the slot keeps the line and `lanes_faders` / `lanes_shift` |
| `acid-cpu.mjs` | not a pass / fail run: the **wasm** build's CPU meter over eight workloads, from the kit alone to four drums, the acid line and all four bus FX (`node acid-cpu.mjs emulator/web`) |

The emulator lives on the `claude/wakes-emulator` branch (`emulator/`). Its `build.sh` needs
three changes to build this tree: `sp1_drums_ui.c`, `sp1_drums_patch.c` and `sp1_acid.c` in `WAKES_C` (and no `sp1_midifile.c`),
`sp1_drums.cc` in `WAKES_CXX`, and the tinykit sources (`tinykit`, `tinyfx`, `tinybass`, `tinyacid`) compiled with `-I third_party/tinykit`;
build with `EMU_KCONFIG="SP1_DRUMS=y"`. Then:

    WAKES_HEADLESS=path/to/wakes_headless tools/host-tests/drums-emu/run1-pages.sh
    WAKES_HEADLESS=path/to/wakes_headless tools/host-tests/drums-emu/run2-rocker.sh
    WAKES_HEADLESS=path/to/wakes_headless tools/host-tests/drums-emu/run3-sequencer.sh
    WAKES_HEADLESS=path/to/wakes_headless tools/host-tests/drums-emu/run4-reload.sh
    WAKES_HEADLESS=path/to/wakes_headless tools/host-tests/drums-emu/run5-acid.sh
    WAKES_HEADLESS=path/to/wakes_headless tools/host-tests/drums-emu/run6-acid-moves.sh
    WAKES_HEADLESS=path/to/wakes_headless tools/host-tests/drums-emu/run7-acid-lanes.sh

Each prints `[emu] N of N expectations met` and exits 0, or names what was missing.
