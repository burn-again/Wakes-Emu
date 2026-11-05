# MIDI files — experimental (`CONFIG_SP1_MIDIFILE`)

Wakes can play a **Standard MIDI File from its drive** into the Plaits voice. The notes go through
the same path as USB-MIDI, so note priority, legato, glide and velocity follow `midi.cnfg` as they
would for a keyboard. **Marbles' clock is the file's position**: PLAY starts it from the top, RATE
and the t range set its tempo, and it loops.

⚠️ **Experimental, and off in device builds.** `CONFIG_SP1_MIDIFILE` defaults to `n`. The browser
emulator builds it on (`EMU_KCONFIG= emulator/build.sh` builds without it). Nothing here has run
on hardware yet, including the rocker + T chord (see *Hardware* below).

---

## Using it

1. **Put a file on the drive** in `WAKES/MIDI/1`, `2`, `3` or `4`. Any name ending `.mid` or
   `.midi` works; if a folder holds several, the first in alphabetical order plays, as with
   PRST's folders. Wakes creates the four folders at ON.
   - On the device: plug it into a computer while it is off (drive mode).
   - In the emulator: switch on once, click **`WAKES/MIDI/1/`** in the drive panel, then
     **Add files to WAKES/MIDI/1/…**.
2. **Hold the rocker down and press T1–T4** to pick that slot. The slot's LED lights and the
   console names the file:
   `MIDIFILE 1: dowland.mid (5454 B) -- format 1, 3 tracks, 586 notes 40..73, up to 5 at once,
   195.00 beats; plays with the clock`.
   The same gesture on the slot that is playing turns it off (`MIDIFILE off`). A file that cannot
   be played says why (`not a MIDI file`, `more than the 16 KB this build plays`, …).
3. **PLAY** starts the clock and the file from its first note, on t2's first tick. PLAY again
   stops it and its notes. A file picked while the clock runs starts on the next beat.

The file is forgotten at power-off: every ON starts with none.

### The gesture, and what it changes

- **On PLAITS**, the rocker's TRIG still fires on the press: it is played, and moving it would make
  it late. Picking a file therefore costs one strike.
- **On MARBLES**, rocker down steps the t range down. In this build that step moves to the
  **release**, and is skipped if a T was pressed during the hold. This is the same rule `••` + T
  already uses for buttons with two meanings. FFWD is unchanged.
- The T button's own action (engine step, page, module) does not happen when it is part of the
  gesture.

---

## How it plays

- **Tempo is Marbles'**: one quarter note per beat of the master clock, at RATE (120 BPM at the
  centre), times the t range, or following MIDI clock when that drives Marbles. The file's own
  tempo is not used. The Dowland test file, written at 96 BPM, plays at 120 until RATE comes down.
- **What is played**: note-ons and note-offs from every track and every channel, merged in time
  order and retagged to `midi.cnfg`'s channel. CCs, program changes, pitch bend and SysEx are
  skipped.
- **It loops** at the end of the longest track, with every note released first.
- **Marbles keeps playing too.** The default patch's t2 → TRIG strikes every tick, and X1 or
  X2 → V/Oct adds random pitch on top of the file. That's a fine way to play, but for the file
  alone, unroute both: MARBLES, hold `••`, tap T2 round the t ring to `none`; then the X page
  (T3), hold `••`, tap that X output to `none`.

### Polyphony: one voice

Plaits is one voice, so a chord sounds as one note. `midi.cnfg`'s `note_priority` decides which.
Measured on a solo-guitar Dowland piece (586 notes, mostly 2–3 sounding at once, up to 5), with
Marbles unrouted. The pitch heard in the rendered audio was compared with each rule's prediction
from the file:

| `note_priority` | what you hear | note-ons that become a new pitch | heard vs predicted |
|---|---|---|---|
| `last` (default) | the newest note: a chord's last note, then whatever is still held | 488 of 586 (83 %) | 99 % |
| `high` | the top line, which in most music is the melody | 454 of 586 (77 %) | 98 % |
| `low` | the bass line | 125 of 586 (21 %) | 100 % |

**Strummed chords glide.** A strum arrives a few milliseconds apart per string, and with the
default `legato = auto` each later note is "played over a held one", so it glides up the chord in
a quick flick rather than striking. `legato = off` strikes every note instead, and a lower
`portamento` shortens the glide.

**For the melody, set `note_priority = high`** in `WAKES/CNFG/midi.cnfg`. The page's drive panel
can edit it; switch off and on to apply. The **chords** engine (slot 7 in the default
`engines.cnfg`) is another way to hear harmony from one line: it builds a chord on each note.

---

## Limits

| | |
|---|---|
| Formats | 0 and 1. Format 2 and SMPTE time are refused with a reason. |
| Size | 16 KB per file (`CONFIG_SP1_MIDIFILE_MAX_KB`, 4–64), held in RAM while selected. A melody is usually a few KB; a whole orchestral file is not. |
| Tracks | 16 |
| Position | restarts at every PLAY; no "continue", no seeking |
| Persistence | none yet: the slot is not saved in PRST |

## Cost

The player itself is one comparison per Plaits block. Notes cost what MIDI notes cost: the note
logic, the glide, and one more strike path. Measured on the emulator's CPU meter with VA, the
clock at 120 BPM and the Dowland file, against the same patch with no file:

| | avg | dearest block |
|---|---|---|
| no file | 64.6 % | 67.3 % |
| file playing | 66.5 % | 74.4 % |

Loading runs on the storage thread, below audio, and takes milliseconds.

## Safety

The audio thread never parses an unchecked byte. The storage thread reads the file and
`sp1_mf_check()` walks every event of every track before the file is handed over: lengths,
running status, data bytes. A file that fails is refused and named in the log. The check and the
player were fuzzed with 40,000 corrupted and truncated copies of two files under AddressSanitizer
and UBSan: 4,907 still parsed as valid and played to the end, the rest were refused, and there
were no faults. Ownership of the buffer passes between main, storage and audio through one state
word, so a file is never replaced while it plays. The audio thread lets go of it, with its notes
off, first.

## Hardware

Rocker + T is a chord across the two button ladders (`sp1_controls.c`). The ladders are decoded
separately, so both presses register, but they share one supply rail and a press on one shifts
the other's reading slightly. The estimate is 10–20 counts against the decoder's ±40 window,
which should be enough, but the firmware's own rule is that thresholds are measured on the
device, not computed. **Check rocker down + T1–T4 on hardware with the guided capture before
relying on it.** The emulator decodes the two ladders independently, so it cannot show this.

## Ideas

- Channel 10 to the drum hits (`docs/DRUMS.md`): General MIDI 35/36 kick, 38/40 snare, 42/44/46
  hat.
- A `midifile.cnfg`: which track or channel, the file's own tempo instead of Marbles', and
  start on the next bar.
- The selected slot saved in PRST.
