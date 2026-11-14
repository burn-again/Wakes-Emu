# The drum firmware — every control (CONFIG_SP1_DRUMS)

The drum firmware replaces the Plaits voice with a four-voice drum kit, a step sequencer, a
bass voice, four bus effects and the rocker's rhythm tricks. Marbles stays the rhythm brain:
its clock drives the sequencer, and its t and X outputs can be cabled to the drums, the kit,
the FX and the bass. Kits save in your PRST slots, and `WAKES/CNFG/drums.cnfg` holds the
settings that have no fader.

Everything Wakes does outside the voice is unchanged: power on and off, the `••` holds, the
battery display, VOL, USB audio and MIDI, PRST slots (`••` + PLAY), the rip (`••` + PLAY held
3 s), and Marbles itself. `docs/UI-PAGES.md` still describes those. One difference: the drive
(`••` + VOL) — see *The output stage* below.

---

## Pages

The row of four buttons is the map. **T*n* alone** opens one of four pages; **`••` + T*n***
opens drum *n*. The device starts on **SOUND**, with the acid line on pattern 1 over the
kick and the hat (drums 2 and 4 muted).

| page | how you get there | its own button again |
|---|---|---|
| **SOUND** | **T1** (and at power-on) | T1: the acid line **off / on** |
| **PATTERN** | **T2** | — |
| **LANES** | **T3** | — |
| **KIT** | **T4** | — |
| **DRUM 1–4** | **`••` + T1–T4** | `••` + T*n*: **mute / unmute** drum *n* |
| **SETTINGS** | `••` double tap | one `••` tap → the page you came from |
| **MARBLES** | `••` triple tap | `••` triple tap → SOUND |

- A **tap** is a `••` press under 300 ms with nothing else touched; the next tap must start
  within 400 ms. The second tap opens SETTINGS at once and a third goes to MARBLES, so a
  single tap on SETTINGS only leaves once those 400 ms have passed.
- **Holding `••`** shows the page's SHIFT layer.
- Entering a page flashes its button's LED (a drum's page: its LED over a half-lit row);
  SETTINGS breathes. The row then shows the page's four values, as in Wakes.
- Pickup is Wakes': a fader that does not match the value it is showing catches up rather
  than jumping, on every page.

## Faders

| page | F1 | F2 | F3 | F4 |
|---|---|---|---|---|
| **SOUND** (T1) | **cutoff** | **resonance** | **decay** | **drive** |
| **SOUND + SHIFT** | envelope depth | accent | slide time | gate length |
| **PATTERN** (T2) | **pattern** (28 zones) | **transpose**, ±12 semitones | **octave**, −2 … +2 | **sparse ↔ dense** |
| **PATTERN + SHIFT** | saw ↔ pulse | sub | filter FM | **level** |
| **LANES** (T3) | gate density | accent density | slide density | octave density |
| **LANES + SHIFT** | gate length | accent length | slide length | octave length |
| **KIT** (T4) | kit pitch, ±12 semitones | kit tone | kit decay | **fill** |
| **KIT + SHIFT** | DJ filter (centre off, down low-pass, up high-pass) | wavefolder | Chebyshev | through-zero FM |
| **DRUM *n*** (`••` + T*n*) | pitch | tone | decay | mod |
| **DRUM *n* + SHIFT** | **density** | level | **length** | excite |
| **SETTINGS** | **swing** | **morph** | FM carrier pitch (±3 octaves around G3) | bus drive (×1 … ×4 into the soft clip) |
| **SETTINGS + SHIFT** | **bass octave** (5 steps, −2 … +2) | **bass shape** (saw → square → triangle) | **bass cutoff** | **bass decay** |

- **SOUND**'s four are the ones whose moves are recorded (keep / undo / snap back, below).
- **PATTERN F1** chooses the pattern: 28 zones along the fader, with hysteresis. The row
  shows which quarter of the list you are in and the log names it (`PATTERN 9: jungle
  (101)`). The new pattern is **taken up at the next bar line**, so sweeping the fader does
  not chop the groove; it brings its own sound to SOUND and its lanes to LANES. Choosing a
  pattern turns the line on.
- **Transpose** has a centre detent (none). **Octave** is five zones. **Sparse ↔ dense** has
  a centre detent too: it moves all four lanes' densities together, down to nothing, up to
  every weighted mark.
- **Level**: 0.7 on the fader (where it starts) is −6 dB in the mix, the top 0 dB.
- KIT's four are **offsets for every drum** with a 10 % centre detent: centred is the kit as
  stored. **Fill** adds to every drum's density, up to ±0.5: up brings in the ghost notes, down
  thins the groove to its strongest hits.
- A drum's **pitch** fader is its note over the whole range (0 … 120); KIT pitch adds to it.
- **level**: centred is the kit's own balance; up is up to twice as loud.
- What tone, mod and excite do depends on the voice: tone is its main colour, mod its second
  axis, excite its third (the strike, the pitch drop, the noise).

## The sequencer

Every drum plays a **weight map**: one digit per 16th, 0 (never) to 9 (always), two maps per
drum (A and B). Each kit carries its own four pairs. A step plays when its weight is over the
drum's threshold, so one map holds the groove and all its ghost notes at once:

- **density** (DRUM *n* + SHIFT F1) sets the threshold: at 0.5 the weights 5 and up play, at 1
  everything with a weight, at 0 nothing. KIT **fill** moves every drum's density together.
- The weight also sets the **velocity**: 9 is a full hit, 7–8 and 5–6 are a little softer, and
  the low weights are quiet, darker and shorter ghost notes.
- **length** (DRUM *n* + SHIFT F3) is the loop, in five zones: **8, 12, 16, 24, 32 steps**. A
  map shorter than the loop repeats; a 16-step loop over a 32-step map plays its first half.
  Drums with different lengths drift against each other (polymeter).
- **morph** (SETTINGS F2) blends every drum from map A to map B, weight by weight.
- **swing** (SETTINGS F1) delays the odd 16ths, up to a third of a step.
- Marbles' **JITTER** (t page F3) wobbles each step's threshold and each hit's tone and decay;
  its **DEJA VU** (F4) decides how much of that randomness repeats: below the middle, part of it
  is drawn fresh each time the loop comes round; at or above the middle, the loop repeats.
- The sequencer follows Marbles' clock: RATE, the ×¼ / ×1 / ×4 range, MIDI clock, tape slow.

**SETTINGS `••` + T1–T4** swaps drum *n*'s maps for the **next kit's** maps for that drum (the
row flashes, the console names it: `PATTERN drum 1: 8-bit breaks's (16 steps)`). The voice stays
the same, so you can play one kit's sounds with another kit's grooves. The length fader resets
to the new map's length.

## The bass

A mono bass (saw / square / triangle with a sub-octave, a resonant low-pass with its own
envelope, glide) that plays **notes from Marbles' scale**, chosen by Marbles' **Y** output.

**SETTINGS `••` + FFWD / RWD** steps the mode:

| mode | the bass plays |
|---|---|
| **off** | nothing (the default) |
| **on the Y clock** | a note on each tick of Marbles' Y clock, the note from Y |
| **only in the rests** | 8th notes, but only on steps where every drum is rested — rest the kit with the rocker and the bass fills the hole |

- **Y's divider** is Marbles' SETTINGS F4 (Y RATE): one bass note per 64 … 1 beats; the
  default is one every 8 beats.
- The **scale** is Marbles' selected one (`••` + FFWD / RWD on Marbles' X page). Y spans two
  octaves of it from the root (`drums.cnfg` sets the root and how many octaves).
- SETTINGS + SHIFT shapes it: octave, shape, cutoff, decay. Resonance, envelope amount, glide,
  sub level and the bass level in the mix are in `drums.cnfg`.
- A t output cabled to **bass** strikes it too, in any mode except off.

## The acid line

A 303 / 101 voice (`third_party/tinykit/tinyacid`) playing a 16-step pattern **in the bass's
place**. It is on at power-on and has three pages: **T1 SOUND, T2 PATTERN, T3 LANES**. Choose
a pattern with **PATTERN F1**, or step to the next / previous with **`••` + FFWD / RWD** on
any of the three pages; **T1 on SOUND** switches the line off and on. There are twenty-eight:

| pattern | | |
|---|---|---|
| 1 **acid-home** | 303 | C minor, accents on the ones, two slides up |
| 2 **campfire** | 101 | slow D mixolydian, long ties, a soft pulse with a little noise |
| 3 **sixth-form** | 101 | A dorian, rests that let the release ring, one octave lift |
| 4 **octave-hopper** | 303 square | one note, every other step an octave up |
| 5 **rubber** | 303 | nearly all slides and ties, a long glide |
| 6 **crawl** | 303 | sparse; sub, filter FM and drive |
| 7 **fold-garden** | 101 | twelve steps, so it turns against the kit every three bars |
| 8 **three-accents** | 303 | accents in a row (they stack the cutoff), marks with weights |
| 9 **jungle** | 101 | a sub that holds, drops and slides; almost no filter |
| 10 **raga** | 101 | Bhairav's notes, every move a slide |
| 11 **reggaeton** | 303 square | the bass on the dembow's 3 + 3 + 2 |
| 12 **cumbia** | 101 | root on the one, fifth and root before the next; the second bar a fourth up |
| 13 **thrash** | 303 | sixteenths on the low E, short and driven, accents as the riff |
| 14 **ambient** | 101 | 32 steps and one note-on: a single tone that drifts |
| 15 **markov** | 303 | one walk of a Markov chain over A minor pentatonic; gate, accent and octave lanes loop every 12 steps under 16 pitches, so it comes round every three bars |
| 16 **electro-house** | 101 | off the beat, so the kick's duck pumps it |
| 17 **french-house** | 101 | a disco octave line in F# minor; the filter is yours to sweep |
| 18 **psytrance** | 303 | nothing on the kick, three sixteenths after it |
| 19 **dub-techno** | 101 | a stab off the beat with a long tail, for the filter and the FM |
| 20 **uk-garage** | 101 | a skipping sub; play it swung |
| 21 **trap** | 101 | long sub notes that bend into each other, driven |
| 22 **synthwave** | 101 pulse | an eighth-note bass with the octave on the off-beats |
| 23 **afrobeat** | 101 | a D-minor line that answers itself across the bar |
| 24 **ebm** | 303 square | sixteenths, root and octave, accents on the ones |
| 25 **footwork** | 101 | a sub in threes against a kit in fours |
| 26 **italo** | 101 pulse | octaves and fifths in sixteenths |
| 27 **gqom** | 303 | low, sparse and broken, with filter FM |
| 28 **user** | 303 | `line =` in drums.cnfg's `[acid]` |

- A pattern brings its sound: SOUND, SOUND + SHIFT and PATTERN + SHIFT's wave, sub and FM are
  set to the pattern's own values when it is taken up (the faders catch up from there). The
  pattern fader, transpose, octave, sparse ↔ dense and level stay where they are.
- **SETTINGS `••` + FFWD / RWD** steps what plays the bass part: the acid line, nothing, the
  old bass on the Y clock, the old bass in the rests (`LINE off`, `LINE acid 3: sixth-form
  (101)`). SETTINGS + SHIFT is always the old bass's four.
- It runs on the sequencer's 16ths, with the swing, and through the bus FX with the kit.
- **The kick ducks its cutoff**: each hit on slot 1 dips it (`duck` in drums.cnfg, half an
  octave) and it comes back over about a tenth of a second.
- The pattern is kept in the PRST slot by name (`line = campfire`), with every page's faders.
  A slot saved before the acid line was the default says `bass = off`: that is not honoured,
  so the line plays; `line = off` is.
- Not yet: rests from the rocker.

### LANES: each lane's density and loop length

**T3** opens LANES (`PAGE lanes`).

| | F1 | F2 | F3 | F4 |
|---|---|---|---|---|
| **LANES** | **gate** density | **accent** density | **slide** density | **octave** density |
| **LANES + SHIFT** | gate length | accent length | slide length | octave length |

- **Density** has a centre detent: centred is the pattern as written (weights 5 and up). Up
  brings in the marks written with a weight (`C2!4`); down thins the lane, to nothing at the
  bottom: gate down mutes notes, slide down turns bends into plucked notes, accent down
  flattens the line.
- **Length** is eight zones, with hysteresis (a fader resting on a boundary does not flip
  between two lengths; the drum pages' loop length and the bass octave have it too): **2, 3, 4, 5, 6, 8, 12** steps, and at the top **the pattern's
  own**. A shorter lane loops its first steps under all the notes, so it drifts against them:
  an accent lane 3 long over 16 notes moves the accents every beat. The log says what a lane
  became (`LANE accent: 3 steps`).
- A pattern sets the page when you step to it: densities centred, lengths its own (markov
  starts with three lanes at 12).
- The rocker is the acid pages': keep / undo / add / snap back. **Snap back and add cover
  this page too**: home is SOUND's four faders, the eight lane faders and the takes.
- The page's faders are kept in the PRST slot (`lanes_faders`, `lanes_shift`).

### Fader moves: keep, undo, add, snap back

The kit always remembers **the last bar** of the four sound faders (cutoff, resonance, decay,
drive; 64 points a bar), so there is nothing to arm: play a move, and if it was good, keep it.
On **SOUND, PATTERN and LANES, the rocker without `••`** is:

| | |
|---|---|
| **FFWD tap** | **keep**: the last bar of fader moves becomes a *take* and loops from now on. Up to four takes stack. The row shows how many |
| **RWD tap** | **undo**: the newest take goes |
| **FFWD held 0.6 s** | **add**: what plays now (the faders and the takes) is *home* |
| **RWD held 0.6 s** | **snap back**: at the next bar line the four faders return to home and the takes made since go. If nothing has changed since home was set, it goes to the home before it (four deep) |

- A take is stored **relative to where the faders stood when you kept it**, so it rides under
  the hand: keep a sweep, then move the fader and the whole sweep moves with it.
- Home starts as the pattern's own sound (or the preset's, after a load). Stepping to another
  pattern drops the takes and starts a new home.
- After a snap back the faders catch up from home, as after any page change.
- Takes are not saved in the PRST slot, and nothing is kept while the clock is stopped.
- On KIT and the drum pages the rocker is the rhythm tricks below, and on SETTINGS the rest
  style, as before.

What it is for (`drumtest` renders each as `jam*.wav`):

1. **Sweep and keep** — play a cutoff sweep over one bar, keep it, take your hand away: it
   loops. Flick the resonance on the off-beats for a bar, keep that too; undo it; snap back.
2. **Stabs under the hand** — keep a bar of filter stabs with the fader left low, then raise
   the fader slowly: the loop plays the rhythm, the hand plays the level.
3. **Breathing** — one slow swell of cutoff and decay over a bar of a held line, kept, and the
   line breathes the same way every bar while you work on the drums.
4. **Build and drop** — keep a closing-filter bar, push drive and resonance up by hand for
   four bars, then hold RWD: on the bar line everything is home again at once.

And one per genre pattern (`genre_*.wav`, ten bars each over a kit that suits it): two bars
as written, a fader move played in bar 3 and kept, three bars of it looping, one lane changed
in bars 7-8, and a snap back at bar 9. The last ten also move one bus FX by hand over bars
4-8. `drumtest` prints what each one does.

**A pattern as text** — one token per 16th, up to 32:

| | |
|---|---|
| `C2` `D#2` `Bb1` | a note (C2 = MIDI 36) |
| `.` | a rest |
| `_` | a tie: the note before holds through this step |
| `!` | after a note: accent |
| `~` | after a note: slide **into** it (no retrigger, the pitch glides) |
| `^` `v` | after a note: an octave up / down |
| `/5` | after a note: the note's own weight |

A mark may carry a weight 1-9, as the drums' maps do: `C2!4` is an accent that only sounds
with the accent lane's density up. Gate, accent, slide and octave are **four lanes** over
the same steps, each with a density (0.5 = as written with weights 5 and up) and a loop
length (0 = the pattern's; shorter and the lane drifts against the notes). Each step is
decided once, a step ahead, so a slide always has a held note behind it.

## Cables (MARBLES)

On MARBLES, **`••` + T1–T4** (on release) patches one of Marbles' outputs. Each press steps that
output to the next destination; the row flashes it and the console names it
(`CABLE t1 -> slot 1`, `CABLE X2 -> its slot's tone`). **`••` + T*n* held 2 s** is Wakes' Unpatch:
that output goes back to none.

| page | T1 | T2 | T3 | T4 |
|---|---|---|---|---|
| **t page** | t1 | t2 | t3 | the **Y clock** |
| **X page** (T3 from the t page) | X1 | X2 | X3 | **Y** |

**t outputs and the Y clock** (gates) → none · **slot 1–4** · **bass** · **accent**

- A slot with a t output cabled to it plays **that output's gates instead of its map**: Marbles
  takes over that drum. t2's hits take their velocity from X2, so they come with ghost notes.
- **accent**: whatever plays on that tick plays louder.

**X1–X3** (voltages) → none · its slot's **pitch** · **tone** · **decay** · **velocity** · KIT
**pitch** · KIT **tone** · KIT **decay** · **fill** · FX **filter** · **fold** · **Chebyshev** · **FM**

**Y** → none · **bass note** · KIT tone · KIT decay · fill · filter · fold · Chebyshev · FM

- "Its slot" is the slot the output's t partner is cabled to (X1 with t1 …), or else the slot it
  nudges by default: X1 slot 1, X3 slot 2, X2 slot 3, Y slot 4.
- **Depth** per output: **X SHIFT F1–F3** for X1–X3, centred = none, up = more, down = inverted;
  **t SHIFT F1** (GTLT) for Y. On **bass note**, Y's depth sets how far the notes reach.
- Marbles' **range** (`[J]`) sets each output's span; **INTELLIGENT** picks one per cable: two
  octaves for pitch and bass note, ±5 V for the centred destinations (tone, decay, KIT, filter),
  0 … 5 V for the pushes (velocity, fill, fold, Chebyshev, FM).
- Several cables on one destination add up, clamped to what one could do alone.
- Without any cable, each drum's hits still take a little tone from its X output (a random
  parameter lock), and a melodic voice (marimba, vibes, bell, kalimba …) takes its pitch from it.
- Cables act only while the clock runs.

## Buttons

| control | SOUND · PATTERN · LANES | KIT | DRUM *n* | SETTINGS |
|---|---|---|---|---|
| **T1–T4** | open SOUND · PATTERN · LANES · KIT (T1 on SOUND: the line off / on) | the same | the same | **mute / unmute** drum *n* |
| **`••` + T1–T4** | open DRUM *n* | open DRUM *n* | open DRUM *n*; its own: **mute / unmute** it | drum *n*'s **pattern** → the next kit's maps |
| **`••` + FFWD / RWD** | **next / previous acid pattern** (at the bar) | **next / previous kit** | **next / previous model** | **the line**: acid / off / bass on Y / bass in the rests |
| **FFWD / RWD** | **keep / undo** (tap), **add / snap back** (held) | the rhythm tricks, below | the same, for that drum | rests: ring out / choke |
| **PLAY** | Marbles' clock run / stop | the same | the same | the same |

- The four bus FX are always in the path: at zero they do nothing (KIT + SHIFT). The
  `••` + T on / off switches of the first drum firmware are gone; `••` + T is the drum pages.
- A muted drum fades out over 12 ms if it is ringing, and stays silent until unmuted. Auditioning
  it from its own page (below) still plays it.
- The eight kits: lich techno, 8-bit breaks, modal gamelan, idm rungler, dub techno, iron vibes,
  glass rungler, gong garden (or **classic**, the LXR-derived voices, in a `CONFIG_SP1_CLASSIC`
  build). The row shows the kit: one LED for kits 1–4, the same LED with the others at half for
  5–8. Changing kit reloads all four voices, their maps and their drum pages.
- The model list (47 models, 53 with classic) is the spec's default list; the row shows where you
  are on it as a bar. A classic voice can be on one drum at a time.

## The rocker (FFWD up, RWD down)

**Playing** means Marbles' clock runs.

| page | playing: RWD | playing: FFWD | stopped: RWD | stopped: FFWD |
|---|---|---|---|---|
| **KIT** | held: **tape slow** | tap: **rotate the kit** | press: **step one 16th** | held: **burst** |
| **DRUM *n*** | held: **rest drum *n*** | held: **release** drum *n*'s rests; tap: **clear** them at the next bar | press: **audition** drum *n* | held: **burst drum *n*** |
| **SETTINGS** | press: rests **choke** | press: rests **ring out** | the same | the same |
| **MARBLES** | held: **rest every drum** | held: release every drum; tap: clear all at the next bar | press: step one 16th | held: burst |

On MARBLES the clock range (`[B]`, ×¼ / ×1 / ×4) moves to **`••` + FFWD / RWD on the t page**,
because the plain rocker is the rests'. `••` + FFWD / RWD on the X page is still the scale.

### Rests

- Each slot has a **one-bar mask of sixteen 16ths**. A bar is four of Marbles' t2 ticks, so a rest
  follows the clock, RATE and the ×¼ / ×4 range included.
- RWD held writes rests under the playhead, FFWD held erases them, and the bar repeats: what you
  rested keeps resting every bar while the sequencer and Marbles go on around it.
- The rocker acts on a step if it is down at the step's start **or within 30 ms after**, so a
  press a hair early lands on the step it was aimed at.
- An FFWD **tap** (under 300 ms) clears the page's rests **at the next bar line** (it also gives
  back the step under the playhead straight away, like any FFWD press).
- **Choke** (the default): a rested step fades whatever that slot is ringing over 12 ms.
  **Ring out**: a rest only blocks new hits, so tails sound through.
- With the bass **only in the rests**, a step every drum rests plays a bass note instead.

### Tape slow (KIT, RWD held, playing)

The whole machine — Marbles' clock, the sequencer, the voices, the bass and the FX — slows like a
finger on a cassette: it sinks towards ×0.33 (time constant 0.7 s) while held and speeds back up
over 1.2 s when let go. Pitch, tempo and decays bend together. The three numbers are in
`drums.cnfg`.

### Kit rotation (KIT, FFWD tap, playing)

Each tap moves every voice one slot along **on the next beat** (`drums.cnfg` can make it the next
step or bar): slot 1 plays drum 4, slot 2 drum 1, and so on. Four taps bring the kit home. The
slots keep their maps, cables and rests; DRUM *n*'s page keeps controlling the sound you call
drum *n*, wherever it plays.

### Step-through (KIT or MARBLES, RWD, stopped)

Each press plays the **next 16th** of the sequencer, and every fourth press also sends Marbles
one tick, so its sequence walks on with the steps. With DEJA VU locked the presses walk the
loop. (Not while MIDI is clocking Marbles.)

### Audition and burst (stopped)

On a drum page RWD plays that drum and FFWD held repeats it at the burst division (`••` + FFWD /
RWD sets that division on stock Wakes; here those keys pick models, so the division stays where
it was, 1/32 by default). On KIT and MARBLES the burst repeats whatever played on the last step.

---

## Saving: PRST slots

A slot's `.prst` gets a **`[drums]` section** after Wakes' own: the kit, the four voices, their
gains and which are melodic, every page's faders (KIT, DRUM 1–4, SETTINGS, each with SHIFT), the
FX on / off, the mutes, choke or ring out, the bass mode, each drum's maps and the eight cables.
It is saved on power-off and loaded at power-on and with `••` + PLAY, like the rest of the slot.

```ini
[drums]
kit = 1
voices = 7.0 4.0 7.3 8.1
gains = 0.8000 0.4500 0.3000 0.3500
melodic = 0
kit_faders = 0.5000 0.5000 0.5000 0.5000
kit_shift = 0.5000 0.0000 0.0000 0.0000
drum1_faders = 0.3000 0.2000 0.4500 0.3000
drum1_shift = 0.5000 0.5000 0.5000 0.6000
...
settings_faders = 0.0000 0.0000 0.5000 0.0000
settings_shift = 0.5000 0.0000 0.4000 0.4000
fx = 15
mute = 0
rests = choke
bass = y
patterns = 2 1 1 1
t_cables = 1 0 0 0
x_cables = 0 2 0 13
```

- **Stock Wakes ignores the section**, so a slot saved here still loads there, and a slot from
  stock Wakes loads here with the drums as they were.
- Every field is checked on its own: a bad or missing one keeps its value, and the console says
  how many were bad (`STORE PRST 1: [drums] kit 1, 0 bad fields`). After loading:
  `DRUMS kit 1: lich techno, bass y, 3 cables, rests choke`.
- `voices` is engine.model per drum; `patterns` is the kit (1–8) each drum's maps come from;
  `t_cables` are t1, t2, t3 and the Y clock, `x_cables` X1, X2, X3 and Y, as numbers in the
  order of the lists above (0 = none).
- The rip (`••` + PLAY held 3 s) puts the current kit back as stored: its own maps, FX at zero
  (on), SETTINGS and the bass faders at their defaults, nothing muted, every rest and cable
  cleared, and the rest style and bass mode as `drums.cnfg` gives them.

## drums.cnfg

`WAKES/CNFG/drums.cnfg` is read at every power-on; if it is missing, the built-in copy is written
to the drive. Like Wakes' other `.cnfg` files it is read line by line: a bad line keeps its
default and is named on the console (`CNFG drums.cnfg line 17: tape_release -- not a number`).

| key | default | |
|---|---|---|
| **[drums]** | | |
| `kit` | 1 | the kit at power-on, 1–8 (a PRST slot's `[drums]` overrides it) |
| `rest_style` | choke | `choke` or `ring` |
| `rotate_quantize` | beat | when a rotation lands: `step`, `beat` or `bar` |
| `tape_floor` | 0.33 | tape slow's slowest speed, 0.1 … 1 |
| `tape_fall` | 0.7 | seconds (a time constant) to sink towards it |
| `tape_release` | 1.2 | seconds to speed back up; 0 = at once |
| **[bass]** | | |
| `mode` | off | `off`, `y` or `rests` at power-on |
| `root` | 36 | MIDI note of the scale's lowest degree |
| `octaves` | 2 | how far Y reaches, 1 … 4 |
| `resonance` | 0.4 | 0 … 1 |
| `envelope` | 0.5 | the filter envelope, 0 … 1 |
| `glide` | 0.15 | 0 … 1 |
| `sub` | 0.4 | the square an octave down, 0 … 1 |
| `level` | −15 | dB in the mix, −40 … 0 |

## The output stage

Four voices, the bass and the bus FX meet in one place before they leave the drums:

1. **A leveller** holds a stacked kit near one level: a peak follower that compresses 4:1 above
   a threshold a single hit stays under, and lets go over 150 ms. A plain groove passes
   untouched; everything playing at once comes down about 3 dB instead of piling into the clip.
2. **SETTINGS F4 (bus drive)** and **`••` + VOL (the drive)** multiply into
3. **one soft clip**, whose top is just under 16-bit full scale, so nothing after it ever clips
   hard.

The drive's steps in the drum firmware are **off, +3, +6, +10, +15 dB** (stock Wakes goes to +24
dB, which turns a whole kit into a square wave). It is applied inside the drums' soft clip in
place of Wakes' own drive stage, so it costs no extra CPU and the signal is shaped once, not
twice.

## The log

Every action names itself on the console: `PAGE drum 2`, `KIT 3: modal gamelan`,
`MODEL drum 1: 12.5% pulse (31 of 47)`, `FX fold off`, `MUTE drum 2 muted`, `RESTS choke`,
`PATTERN drum 1: …`, `BASS on the Y clock (Y picks the note)`, `CABLE t1 -> slot 1`,
`ROTATE …` / `ROTATED: slot 1 plays drum 4`, `TAPE slow …`, `STEP one 16th`,
`AUDITION drum 2`. The kit reports back what it did: `RESTS per bar: slot 1 0, slot 2 0,
slot 3 10, slot 4 0 (of 16)` whenever the rests change, and `HIT drums 0x2 (1)` for hits while
stopped.

## Building it

The drum firmware is the Plaits image with one more option; everything else is `docs/BUILD.md`:

```sh
west build -p always -b stem_player wakes-sp1/firmware -d build-drums \
    -- -DBOARD_ROOT="$PWD/wakes-sp1" -DCONFIG_SP1_PLAITS=y -DCONFIG_SP1_DRUMS=y
python wakes-sp1/tools/ci/check_image.py build-drums
```

Built that way with the pinned Zephyr v4.3.1 and SDK 0.17.4 (2026-11-13, this branch): no
warnings, every `check_image.py` check passes, and the image is **608,568 B** (66.6 % of the
slot) with **167,496 B** of RAM (63.9 %). The acid line is 19.6 KB of that flash and 12.4 KB
of that RAM (the drum firmware before it: 588,944 B and 155,080 B). With `CONFIG_SP1_DRUMS`
off, the image is the base's byte for byte apart from the build time (compared at the same
source and build paths; the build directory's name moves 8 bytes of padding otherwise).
CI does not build this configuration, and it has not run on an SP-1.

## CPU

On the M4 harness, compiled with the firmware's own flags (instructions per sample, averaged
over 3 s; Plaits' engines are 417–532 there):

| kits | groove | worst | worst + all FX | tape slow |
|---|---|---|---|---|
| all eight | 125–163 | 267–335 | 389–454 | 165–183 |

*Groove* is the sequencer on the kit's maps; *worst* adds every decay up, density and fill at
full, t1–t3 cabled to slots and gated every 16th, X1–X3 and Y cabled, and the bass on every beat.
A strike costs at most ~2,600 instructions (modal voices; most voices under 1,200).

Where it goes, per sample: a voice 9–65 (fresh modal voices and the FM drums are the
heaviest), the four bus FX together 121, the bass ~100 while it sounds. The voices and the bass
add straight into the mix at their level; the FX work their amounts out once per block while a
fader is still; and the drums' audio code is built with loop-invariant motion and loop
unswitching (CMakeLists.txt), which Plaits' flags otherwise turn off.

In the emulator's calibrated model (good to about ±7 points; Wakes' Plaits engines average
56–82 % of a block on hardware), with the sequencer, the bass on the Y clock and two FX:

| tempo | average | heaviest block |
|---|---|---|
| normal | 32–39 % | 58–86 % |
| Marbles' RATE high, or ×4 | 62–71 % | 77–89 % |

**The acid line**, in the same model at the default tempo with kit 1 (`node
tools/host-tests/drums-emu/acid-cpu.mjs`; the whole audio thread, 4 s each). The drums and
tinyacid are not among the engines the model was fitted to, so read these as ±7 points at best:

| playing | average | heaviest block |
|---|---|---|
| four drums, bass off | 29.6 % | 51.3 % |
| kick + hat | 27.7 % | 42.8 % |
| kick + hat + the old bass on the Y clock | 28.5 % | 52.4 % |
| kick + hat + acid-home (303) | 44.0 % | 61.5 % |
| kick + hat + campfire (101: pulse, sub, noise) | 54.5 % | 70.0 % |
| kick + hat + crawl (303: sub, filter FM, drive) | 41.5 % | 67.3 % |
| four drums + crawl | 43.3 % | 76.4 % |
| four drums + crawl + all four bus FX | 61.1 % | 93.9 % |

So the line costs about 16 points over a kick and a hat as a 303 and about 27 as the 101 with
its long gates (a point of that is the fader recording, which runs whether or not a take is
kept), and the heaviest block of everything at once stays under the budget. That block was
100.5 % until three things were taken out of it: an accent no longer recomputes the filter's
decay (both are kept ready), a note that falls on a block of drum strikes waits one block
(0.5 ms), and the fader record is written once per point, not once per block. tinyacid
alone is 218-274 instructions a sample under Callgrind on x86-64 (`acidbench.c`; tinybass is
124). Built with `-DTA_OS=1` (the ladder once a sample instead of twice) the same eight rows
were 3-6 points lower when measured before the fader recording went in (the last row then:
57.8 % / 93.7 % against 60.0 % / 99.2 %). Not measured on an SP-1.

The modal kits (modal gamelan, iron vibes) peak highest, when four voices are struck on the
same 16th. Wakes queues one block, so a single block over budget is absorbed without a
dropout. As a safety net, a **CPU guard** fades the quietest ringing tail (below −20 dB) when
a block's drum render goes over 80 % of the budget, one voice per block, and logs
`CPU guard: n tails faded`. Watch the `AUD` line on hardware (`fail=` counts dropouts).
