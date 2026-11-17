# SP-1 acid + drums — the manual

A 303 / 101 acid line over a four-voice drum kit, on the SP-1. This page is how to play it.
Every control in detail, with the reasons, is `docs/DRUM-PAGES.md`.

## The controls, once

| | |
|---|---|
| **F1–F4** | four faders, bottom to top |
| **T1–T4** | the four buttons with lights |
| **PLAY** | start / stop |
| **Rocker** | FFWD (up) and RWD (down) |
| **VOL+ / VOL−** | volume |
| **`••`** | the function button. Held, it shows the page's second layer ("SHIFT") |

**Faders catch up.** When a page opens, a fader that is not where its value is does nothing
until you move it through that value. Then it takes over.

**Changes land on the beat.** A new scene, kit, pattern, randomise or undo waits for the
next bar line, so the groove never breaks.

## The nine pages

```
                          HOME   (power-on, and "••" double tap from anywhere)
                            |
   T1 SOUND    T2 PATTERN    T3 LANES    T4 KIT        <- the four buttons
   "••"+T1     "••"+T2       "••"+T3     "••"+T4
   DRUM 1      DRUM 2        DRUM 3      DRUM 4        <- "••" held + a button
```

Each page has its four faders, and four more while `••` is held. The lights flash the page
when you arrive: HOME all four, T1–T4 their own light, a drum page its light over a dim row.

---

### HOME — shuffle the presets

The page the SP-1 starts on, at 120 BPM, with the acid line on over the kick and the hat.

| | F1 | F2 | F3 | F4 |
|---|---|---|---|---|
| **HOME** | **drums ↔ line** | **kit** (8) | **scene** (27) | **DJ filter** |
| **+ `••`** | swing | morph (map A → B) | FM pitch | bus drive |

- **F1 drums ↔ line**: middle (it clicks in) both full; down fades the acid line out; up
  fades the drums out.
- **F2 kit**: eight drum kits along the fader.
- **F3 scene**: a whole preset. Its pattern and sound, the kit it suits, which drums play,
  its swing, tempo and effects. Your F1 balance and F4 filter stay where you left them.
- **F4 DJ filter**: middle off, down low-pass, up high-pass, over everything.

| Rocker on HOME | |
|---|---|
| **FFWD / RWD tap** | tempo +1 / −1 BPM |
| **FFWD / RWD held** | tempo glides up / down, 20 BPM a second (20–140) |
| **`••` + FFWD** | **randomise**: what plays now goes to *temp*, then a random scene with every performance page moved a little from it |
| **`••` + RWD tap** | **undo**: back to *temp*, the state before the last randomise |
| **`••` + RWD held 1 s** | **write** what plays now to the drive (the current preset slot), while playing |

The randomise keeps things musical: sound and lanes stay near the scene's own, the
transpose lands in key half the time, and now and then a lane gets its own length, a touch
of filter FM or some swing. Undo goes back one randomise, not all the way.

---

### T1 SOUND — the acid voice

| | F1 | F2 | F3 | F4 |
|---|---|---|---|---|
| **SOUND** | cutoff | resonance | decay | drive |
| **+ `••`** | envelope depth | accent | slide time | gate length |

**T1 again** switches the acid line off and on.

---

### T2 PATTERN — which line, and where

| | F1 | F2 | F3 | F4 |
|---|---|---|---|---|
| **PATTERN** | pattern (28) | transpose ±12 (clicks at 0) | octave −2 … +2 | sparse ↔ dense (clicks in the middle) |
| **+ `••`** | saw ↔ pulse | sub | filter FM | level |

**Sparse ↔ dense** moves all four lanes at once: down thins the line to nothing, up brings
in every optional note, accent, slide and octave the pattern has.

---

### T3 LANES — the pattern's four layers

A pattern is four lanes over the same steps: notes (gate), accents, slides, octave jumps.

| | F1 | F2 | F3 | F4 |
|---|---|---|---|---|
| **LANES** | gate density | accent density | slide density | octave density |
| **+ `••`** | gate length | accent length | slide length | octave length |

- **Density**: middle (clicks in) is the pattern as written; up adds its optional marks,
  down thins it. Slide down turns bends into plucked notes; accent down flattens the line.
- **Length**: 2, 3, 4, 5, 6, 8 or 12 steps, or (at the top) the pattern's own. A short lane
  loops under the notes and drifts against them: accents 3 long over 16 notes move every beat.

---

### SOUND, PATTERN and LANES share the rocker

| Rocker | |
|---|---|
| **FFWD tap** | **keep**: the last bar of SOUND's fader moves loops from now on (up to 4 takes) |
| **RWD tap** | **undo** the newest take |
| **FFWD held** | **add**: what plays now becomes *home* |
| **RWD held** | **snap back**: at the bar line, back to *home* (SOUND, LANES and the takes) |
| **`••` + FFWD / RWD** | next / previous pattern |

The SP-1 always remembers the last bar of the four SOUND faders, so there is nothing to
arm: play a move, and if it was good, keep it. A take rides under your hand. Keep a sweep,
then move the fader, and the whole sweep moves with it.

---

### T4 KIT — the drums together

| | F1 | F2 | F3 | F4 |
|---|---|---|---|---|
| **KIT** | kit pitch ±12 | kit tone | kit decay | fill (ghost notes in / out) |
| **+ `••`** | DJ filter | wavefolder | Chebyshev | ring FM |

| Rocker on KIT | playing | stopped |
|---|---|---|
| **RWD** | held: **tape slow** | press: step one 16th |
| **FFWD** | tap: **rotate the kit** (the drums swap lanes, on the beat) | held: burst |
| **`••` + FFWD / RWD** | next / previous kit | the same |

---

### `••` + T1–T4: DRUM 1–4 — one drum

| | F1 | F2 | F3 | F4 |
|---|---|---|---|---|
| **DRUM n** | pitch | tone | decay | mod |
| **+ `••`** | density | level | loop length (8–32) | excite |

**`••` + its button again** mutes / unmutes that drum. At power-on drums 2 and 4 are muted.

| Rocker on DRUM n | playing | stopped |
|---|---|---|
| **RWD** | held: write **rests** for this drum | press: audition it |
| **FFWD** | held: erase them; tap: clear them at the next bar | held: burst it |
| **`••` + FFWD / RWD** | next / previous model (the drum's sound) | the same |

---

## Everywhere

| | |
|---|---|
| **PLAY** | start / stop |
| **`••` double tap** | HOME |
| **`••` triple tap** | MARBLES (Wakes' random sequencer: its clock shape, jitter, déjà vu, scales and cables); triple tap again for HOME |
| **`••` tap** | battery level |
| **`••` + PLAY** | next preset slot (1–4) |
| **`••` + PLAY held 3 s** | the rip: every page back to its defaults |
| **`••` + VOL+ / VOL−** | output drive |
| **`••` held 3 s** (nothing else) | power off. The current slot is saved |

## The 27 scenes (HOME F3)

The lights show which quarter of the list F3 is in; the console names the scene.

| | | | |
|---|---|---|---|
| 1 acid-home | 8 three-accents | 15 markov | 22 synthwave |
| 2 campfire | 9 jungle | 16 electro-house | 23 afrobeat |
| 3 sixth-form | 10 raga | 17 french-house | 24 ebm |
| 4 octave-hopper | 11 reggaeton | 18 psytrance | 25 footwork |
| 5 rubber | 12 cumbia | 19 dub-techno | 26 italo |
| 6 crawl | 13 thrash | 20 uk-garage | 27 gqom |
| 7 fold-garden | 14 ambient | 21 trap | |

Jungle, thrash and footwork were written faster than HOME's top tempo and play at 140.

## Saving

- **Power-off** saves everything to the current preset slot: every page's faders, the
  scene, the kit, the mutes and the tempo. Recorded takes are not saved.
- **HOME `••` + RWD held** writes it now, without powering off.
- **`••` + PLAY** steps between four slots; each remembers its own state.
- After flashing a new firmware, do the rip once so the newest pages start at their
  defaults.

## What is missing

These were asked for, or are gaps, and are **not** built yet:

1. **Your own presets on HOME F3.** "Write" saves to the current preset slot (one of four),
   not into the scene list. Adding written presets to F3, after the 27, needs a file of its
   own on the drive.
2. **A scale fader.** Transpose and octave exist; mapping a pattern's notes into a chosen
   scale does not.
3. **Takes are not saved** with the preset.
4. **No button any more** for the rest style (choke / ring out), the old Y / rests bass, or
   swapping one drum's pattern for another kit's. `drums.cnfg` sets the first two.
5. **Genre drum patterns.** The kits play their own grooves; there is no dembow, breakbeat
   or cumbia drum pattern to match those scenes.
6. **A USB-MIDI keyboard** cannot play or transpose the acid line.
7. **The tempo stops at 140**, so three scenes play slower than written.
8. **Screenless readout.** Scene and pattern show only as a quarter of the list on the lights;
   the name is on the console.
9. **CPU on the device.** In the emulator every scene fits (dub-techno, the heaviest, peaks at
   85 %), but its meter reads the modal kits (modal gamelan, iron vibes, gong garden) about
   15 points light. Under load the acid filter drops to one pass for a moment, and the
   drums fade a tail as a last resort. The console says when either happens.
10. **CI** does not build this firmware; it is built by hand.
