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

| page | how you get there | how you leave |
|---|---|---|
| **KIT** | power on; or T*n* again from DRUM *n* | — |
| **DRUM 1–4** | T1–T4 from KIT or another drum page | the same T again → KIT |
| **SETTINGS** | `••` double tap | one `••` tap → the page you came from |
| **MARBLES** | `••` triple tap | `••` triple tap → KIT |

- A **tap** is a `••` press under 300 ms with nothing else touched; the next tap must start
  within 400 ms. The second tap opens SETTINGS at once and a third goes to MARBLES, so a
  single tap on SETTINGS only leaves once those 400 ms have passed.
- **Holding `••`** shows the page's SHIFT layer.
- Entering a drum page flashes its LED (T1 for drum 1 …), KIT flashes all four at half,
  SETTINGS breathes. The row then shows the page's four values, as in Wakes.
- Pickup is Wakes': a fader that does not match the value it is showing catches up rather
  than jumping, on every page.

## Faders

| page | F1 | F2 | F3 | F4 |
|---|---|---|---|---|
| **KIT** | kit pitch, ±12 semitones | kit tone | kit decay | **fill** |
| **KIT + SHIFT** | DJ filter (centre off, down low-pass, up high-pass) | wavefolder | Chebyshev | through-zero FM |
| **DRUM *n*** | pitch | tone | decay | mod |
| **DRUM *n* + SHIFT** | **density** | level | **length** | excite |
| **SETTINGS** | **swing** | **morph** | FM carrier pitch (±3 octaves around G3) | bus drive (×1 … ×4 into the soft clip) |
| **SETTINGS + SHIFT** | **bass octave** (5 steps, −2 … +2) | **bass shape** (saw → square → triangle) | **bass cutoff** | **bass decay** |

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
place**. It is the bass mode after "only in the rests": **SETTINGS `••` + FFWD / RWD** steps
on through the acid patterns (`BASS acid 1: acid-home (303)`), nine of them, and back round
to off. The row shows the pattern's number in binary (T1 the low bit) over a half-lit row.

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
| 9 **user** | 303 | `line =` in drums.cnfg's `[acid]` |

- A pattern brings its sound (character, waveform, envelope depth, accent, slide time ...).
  **SETTINGS + SHIFT** is then **F1 cutoff · F2 resonance · F3 decay · F4 drive**, set to the
  pattern's own values when you step to it (the faders catch up from there). Stepping back
  to the bass restores the bass's four.
- It runs on the sequencer's 16ths, with the swing, and through the bus FX with the kit.
- **The kick ducks its cutoff**: each hit on slot 1 dips it (`duck` in drums.cnfg, half an
  octave) and it comes back over about a tenth of a second.
- The pattern is kept in the PRST slot by name (`bass = campfire`).
- Not yet: pages for the lanes' densities and lengths (drums.cnfg has them), snap back,
  recorded fader moves, rests from the rocker.

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

| control | KIT / DRUM *n* | SETTINGS |
|---|---|---|
| **T1–T4** | open DRUM *n*; the same T again → KIT | **mute / unmute** drum *n* (the row shows who plays) |
| **`••` + T1–T4** | **FX 1–4 on / off**: filter, fold, Chebyshev, FM (the row shows which are on) | drum *n*'s **pattern** → the next kit's maps |
| **`••` + FFWD / RWD** | KIT: **next / previous built-in kit**. DRUM *n*: **next / previous model** for that drum | **bass mode**: off / on the Y clock / only in the rests |
| **PLAY** | Marbles' clock run / stop | the same |

- A muted drum fades out over 12 ms if it is ringing, and stays silent until unmuted. Auditioning
  it from its own page (below) still plays it.
- FX start **on, at zero**: push a KIT + SHIFT fader and you hear it. Switching one off keeps its
  amount for when it comes back.
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
| kick + hat + acid-home (303) | 42.8 % | 66.5 % |
| kick + hat + campfire (101: pulse, sub, noise) | 53.3 % | 68.8 % |
| kick + hat + crawl (303: sub, filter FM, drive) | 40.3 % | 73.5 % |
| four drums + crawl | 42.2 % | 81.5 % |
| four drums + crawl + all four bus FX | 60.0 % | 99.2 % |

So the line costs about 15 points over a kick and a hat as a 303 and about 25 as the 101 with
its long gates, and the one workload that reaches the budget is everything at once. tinyacid
alone is 218-274 instructions a sample under Callgrind on x86-64 (`acidbench.c`; tinybass is
124). Built with `-DTA_OS=1` (the ladder once a sample instead of twice) the same eight rows
are 3-6 points lower (the last: 57.8 % / 93.7 %). Not measured on an SP-1.

The modal kits (modal gamelan, iron vibes) peak highest, when four voices are struck on the
same 16th. Wakes queues one block, so a single block over budget is absorbed without a
dropout. As a safety net, a **CPU guard** fades the quietest ringing tail (below −20 dB) when
a block's drum render goes over 80 % of the budget, one voice per block, and logs
`CPU guard: n tails faded`. Watch the `AUD` line on hardware (`fail=` counts dropouts).
