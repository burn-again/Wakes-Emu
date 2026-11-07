# Headroom: what else from Mutable Instruments fits on the SP-1

A survey of the Mutable Instruments line against the SP-1's budget: a 64 MHz Cortex-M4F, 256 KB
of RAM and one mono voice at 48 kHz. Two of the ideas are built as experiments:

- **Clouds' reverb** (`CONFIG_SP1_REVERB`)
- **Yarns' arpeggiator** (`CONFIG_SP1_ARP`)

Both are off in device builds and on in the emulator. Nothing here has run on hardware.

---

## The budget

### CPU: it depends on the engine

The emulator's CPU meter, Wakes v0.9.0, with Marbles running at 120 BPM and the default patch.
Figures are % of the 2 ms audio block. The meter is calibrated against the hardware table in
`docs/PLAITS-ENGINES.md`, and its rms error per engine is about 6 points.

| engine | avg | dearest block | | engine | avg | dearest block |
|---|---|---|---|---|---|---|
| speech | 30.7 | 34.4 | | swarm | 64.9 | 67.9 |
| modal | 48.6 | 51.4 | | string | 67.9 | 71.5 |
| waveshaping | 56.4 | 58.7 | | hi-hat | 69.8 | 72.6 |
| filtered-noise | 62.1 | 67.4 | | bass-drum | 69.9 | 70.3 |
| chords | 64.4 | 68.1 | | wavetable | 70.2 | 73.0 |
| virtual-analog | 64.6 | 67.4 | | particle | 75.4 | 79.7 |
| | | | | snare-drum | 76.2 | 79.3 |
| | | | | additive | 77.6 | 80.1 |
| | | | | grain-formant | 83.4 | 86.5 |
| | | | | 2-op-FM | 90.0 | 92.7 |

**Anything always-on is paid on top of every engine.** The dearest engine (2-op FM) leaves
about 8 points, the cheapest about 65. So an effect either costs less than ~8 %, or it needs a
guard that steps aside on the heavy engines. The reverb below does the latter.

⚠️ On hardware an audio thread over budget is worse than a glitch. It starves the main thread,
which feeds the watchdog, and after 4 s the device resets. The emulator cannot show this,
because its code runs in zero virtual time.

### RAM: roughly half is used

Static RAM in Wakes' own code plus Plaits and Marbles, summed from the build's objects (`.data` +
`.bss`):

| build | static RAM |
|---|---|
| stock (`prj.conf`) | **96 KB** |
| with all four experiments (drums, MIDI files, reverb, arp) | **146 KB** (+32 reverb, +17 MIDI file, the rest small) |

Zephyr's kernel, the USB classes, FatFs and the thread stacks come on top. A typical build of
this kind uses 25–40 KB for those, which leaves roughly **120 KB free stock, ~75 KB with
everything**. Those numbers are estimates. The real figure is one CI run away:
`tools/ci/size_report.py` reads the ELF that `.github/workflows/build.yml` builds.

### Licences: STM32 yes, AVR no

Wakes is MIT. The eurorack repository's licence reads *"Code (AVR projects): GPL3.0. Code
(STM32F projects): MIT license."*

- **Usable (MIT):** Clouds, Rings, Elements, Warps, Tides, Stages, Peaks, Yarns, Braids, Frames,
  Beads, Plaits, Marbles.
- **Not usable as code:** Grids, Branches, Edges, Kinks and the other AVR modules. Their code
  and data are GPL-3.0, and copying them would make the firmware GPL. That includes Grids' drum
  maps.

---

## The candidates

The prototyped rows are measured. The other rows are estimates, from each effect's delay-memory
size in the source and an operation count; check them the way the reverb was checked (below).

| idea (module) | what it would do on the SP-1 | CPU | RAM | fit |
|---|---|---|---|---|
| **Reverb** (Clouds; Rings and Elements have the same one) | a tail after the voice | **14.6 %** at half rate (measured: ARM count and meter) | **32 KB** | ✅ **prototyped**. Runs on 13 of 16 engines; a guard pauses it on 2-op FM, grain-formant and additive |
| **Arpeggiator** (Yarns) | held MIDI chords → patterns on Marbles' clock | **0.2 %** (measured) | < 0.1 KB | ✅ **prototyped** |
| Diffuser (Clouds) | smear / pseudo-reverb, much lighter | ~3–4 % at 24 kHz | 4 KB (16-bit) | likely fits on every engine but FM; mono |
| Chorus / ensemble (Rings) | stereo width for the mono voice | ~3–5 % / ~6–9 % | 4 / 8 KB | fits, but needs a **stereo audio path**: today the synth renders mono and `sp1_audio.c` copies it to both sides, and USB audio would follow |
| Pitch shifter (Clouds) | octave or fifth up / down on the voice | ~5–6 % | 8 KB | fits on most engines |
| Frequency shifter / ring mod (Warps) | the voice against itself (OUT × AUX), beyond Wakes' `outxaux` | ~5–8 % | < 1 KB | fits on most engines |
| Drums (Peaks) | 808-style kick / snare, integer DSP made for a 72 MHz M3 | ~3–6 % | < 1 KB | a far cheaper alternative to the Plaits drum engines the drum hits use (`docs/DRUMS.md`) |
| Slope / segment generators (Tides 2, Stages) | LFOs and envelopes as another modulation source, routed like Marbles' X | < 2 % | < 1 KB | fits; the cost is UI, not CPU |
| Sequencer / step recorder (Yarns) | record a line from MIDI, play it on the clock | ~0 | ~1 KB | fits; UI work |
| Keyframer (Frames) | morph between stored fader snapshots | < 1 % | ~1 KB | fits; UI work |
| Braids models Plaits lacks (bowed, blown, fluted waveguides, VOSIM, toy) | more engines | ~10–40 % | flash: tens of KB of tables | possible, engine by engine, on the same CPU terms as Plaits' engines |
| Resonator (Rings) as an insert | Plaits exciting a modal resonator | ~40–60 % at 48 kHz | 8–16 KB | ✗ only on the cheapest engines; Plaits' own modal and string engines already cover much of it |
| Granular (Clouds, Beads) | live sampling and grains | moderate | Clouds' buffer alone is 118 KB | ✗ RAM |
| Elements | a whole modal voice | most of the CPU | large | ✗ |
| A topographic drum map, Grids-like | patterns for the drum hits | ~0 | ~3 KB | idea only: would have to be written from scratch, with its own patterns (Grids is GPL) |

**Two things carry over to every row above.** Running an effect at half rate halves its cost,
which suits anything whose output is mostly below 12 kHz. A guard that measures the engine's
actual load lets a fixed-cost effect exist at all on a device whose dearest engine has 8 points
spare.

---

## Prototype 1: Clouds' reverb (`CONFIG_SP1_REVERB`)

**Use it:** PLAITS, double-tap `••` for SETTINGS, then **T1** cycles **off → 1 → 2 → 3 → 4 →
off**. The model row shows a bar; tap `••` once to leave. This replaces T1's "show the scale" on
that page; T2 and T3 still show the scale as they step it.

| step | RT60 at 1 kHz | wet |
|---|---|---|
| 1 | ~1.8 s | 26 % |
| 2 | ~2.3 s | 30 % |
| 3 | ~3.8 s | 38 % |
| 4 | ~6 s | 45 % |

Low notes ring a little longer than the table and highs shorter, because of the loop's low-pass.

### How it is built

- **Clouds' code, unmodified.** `clouds/dsp/fx/{fx_engine,reverb}.h` and `clouds/dsp/frame.h`
  are vendored as copies (`third_party/eurorack/README.md`). It is a Griesinger topology (four
  input allpasses, then a loop of two allpass pairs with delays), with 12-bit delay memory of
  16 384 words = 32 KB. Rings' version of the same reverb needs 64 KB at 48 kHz, too much here.
- **Mono in, mono out**, after the voice and before the drive and the limiter, so the tail is
  limited with the rest.
- **Half rate.** The reverb runs at 24 kHz (pairs averaged in, linear interpolation out); the
  dry / wet mix is at 48 kHz. Clouds tuned its delays for 32 kHz, so the room is a third larger
  and darker, and RT60 cannot go much below ~1.6 s. Only 0.01–0.15 % of the tail's energy is
  above 8 kHz.
- **The guard.** The audio thread measures the engine's load without the reverb, plus the
  reverb's own cost, every block. If the two together pass 90 % of the block, the reverb pauses
  (`REVERB paused: …` in the log). It stays paused until the engine or the reverb step changes,
  then measures for ~130 ms before judging again. A first version let it back in on a quiet
  moment and flapped: tail cut, memory cleared, over and over.
- **Off costs nothing:** `Process` is not called. Turning it on clears the memory over 8 blocks,
  never all 32 KB in one.

### What it costs, and how that was measured

| engine | without | with reverb | |
|---|---|---|---|
| virtual-analog | 64.6 / 67.4 | 79.2 / 82.1 | |
| waveshaping | 56.4 / 58.7 | 71.0 / 73.7 | |
| 2-op-FM | 90.0 / 92.7 | 90.1 / 92.6 | guard: paused |
| grain-formant | 83.4 / 86.5 | 83.5 / 86.5 | guard: paused |
| additive | 77.6 / 80.1 | 77.7 / 80.5 | guard: paused |
| wavetable | 70.2 / 73.0 | 84.8 / 87.2 | |
| chords | 64.4 / 68.1 | 78.8 / 84.4 | |
| speech | 30.7 / 34.4 | 45.6 / 49.3 | |
| swarm | 64.9 / 67.9 | 79.5 / 82.2 | |
| filtered-noise | 62.1 / 67.4 | 77.1 / 81.6 | |
| particle | 75.4 / 79.7 | 89.8 / 92.7 | |
| string | 67.9 / 71.5 | 82.5 / 85.8 | |
| modal | 48.6 / 51.4 | 63.2 / 66.0 | |
| bass-drum | 69.9 / 70.3 | 84.5 / 84.9 | |
| snare-drum | 76.2 / 79.3 | 90.8 / 93.5 | |
| hi-hat | 69.8 / 72.6 | 84.4 / 87.0 | |

(avg / dearest block, % of the audio block, clock running.)

The meter's model has no hardware measurement for this code, so the reverb was counted on ARM
directly. clang compiled the same function for the Cortex-M4F (`--target=thumbv7em-none-eabihf
-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -O2`), and the instructions in each loop were counted at 1–4
cycles by kind:

- Clouds' loop is **264 instructions per sample**, about **290–390 cycles**.
- At half rate, with the resampling, that is **12–16.5 %** of the CPU.

The meter first said 28.9 %. Clouds' code is 16-bit loads, saturations and int ↔ float
conversions, one instruction each on ARM and several in wasm. So `emulator/tools/wasm_cycles.py`
now scales those two functions by the ARM count (`FUNC_SCALE`, factor 0.50). That gives 14.6 %,
the ARM midpoint.

---

## Prototype 2: Yarns' arpeggiator (`CONFIG_SP1_ARP`)

**Use it:** MARBLES, double-tap `••` for SETTINGS, then **T1** cycles **off → up → up-down →
random → as played → off**. One LED marks the mode. Then hold a chord: on a MIDI keyboard,
or from a MIDI file (`docs/MIDIFILE.md`), whose chords it plays in turn.

⚠️ MARBLES SETTINGS T1 is unbound in Wakes, but reserved: the code says *do not put anything on
it without asking*. It is used here because this is an experiment behind a Kconfig option;
upstream would need Adara's agreement.

- **The step is the burst division** (`••` + rocker on PLAITS: 1/1 … 1/128, default 1/32). At
  1/8 and 120 BPM that is one note every 250 ms. With the clock running it is phase-locked to
  Marbles' master ramp and starts on beat 1, like a MIDI file. With the clock stopped it runs
  free at Marbles' tempo, and the first note sounds as soon as a key goes down.
- **Each note is half a step long**, played through the MIDI path's own note-on / note-off. So
  `midi.cnfg`'s legato, glide and velocity apply, and the LPG plucks each note.
- **The stepping is Yarns'** (`yarns/part.cc`, `Part::ClockArpeggiator`, MIT; ported, as
  Wakes' MIDI code already is). That covers the note and octave walk, the up-down turnaround,
  random, and played vs sorted order. Not ported: Yarns' rhythmic patterns, euclidean mode,
  chord direction, and the octave range (one octave here).

Measured: C–E–G held, up gives 60 64 67 60 64 67 … at exactly one note per step; up-down gives
60 64 67 64 60 64 67; as played gives the keys in the order they went down. A MIDI file's held
chord comes out as an arpeggio on Marbles' clock. Cost: **0.2 %** on average at 1/32 (16 notes a
second at 120 BPM).

## What is next

- The real RAM and flash figures from a device build's size report, then a hardware run of both
  prototypes. The guard's decisions there come from the real cycle counter.
- **For the reverb:** a pre-delay or a damping control; and Clouds' diffuser as a lighter option
  for the engines the guard pauses.
- **For the arp:** octave range, gate length and pattern as `midi.cnfg` keys, and Yarns'
  euclidean patterns.
- A stereo audio path, which opens Rings' chorus and ensemble and a stereo reverb.
