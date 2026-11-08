# The drum firmware — every control (CONFIG_SP1_DRUMS)

The drum firmware replaces the Plaits voice with a four-voice drum kit, four bus effects and
the rocker's rhythm tricks. Marbles stays the rhythm brain. This page is the control map for
**steps 1 and 2** (pages and the rocker). The sequencer, Marbles' cables to the kit, the bass
voice and saving kits in presets come in later steps, so a few faders are already in place for
them but do nothing yet (marked *later*).

Everything Wakes does outside the voice is unchanged: power on and off, the `••` holds, the
battery display, VOL, the drive (`••` + VOL), USB audio and MIDI, PRST slots (`••` + PLAY), the
rip (`••` + PLAY held 3 s), and Marbles itself. `docs/UI-PAGES.md` still describes those.

---

## Pages

| page | how you get there | how you leave |
|---|---|---|
| **KIT** | power on; or T*n* again from DRUM *n* | — |
| **DRUM 1–4** | T1–T4 from KIT or another drum page | the same T again → KIT |
| **SETTINGS** | `••` double tap | one `••` tap → the page you came from |
| **MARBLES** | `••` triple tap (or T4 from MARBLES' t / X page to come back) | `••` triple tap → KIT |

- A **tap** is a `••` press under 300 ms with nothing else touched; the next tap must start
  within 400 ms. The second tap opens SETTINGS at once and a third goes to MARBLES, so a
  single tap on SETTINGS only leaves once those 400 ms have passed.
- **Holding `••`** shows the page's SHIFT layer. SETTINGS has none of its own and shows KIT's
  (the FX).
- Entering a drum page flashes its LED (T1 for drum 1 …), KIT flashes all four at half,
  SETTINGS breathes. The row then shows the page's four values, as in Wakes.
- Pickup is Wakes': a fader that does not match the value it is showing catches up rather
  than jumping, on every page.

## Faders

| page | F1 | F2 | F3 | F4 |
|---|---|---|---|---|
| **KIT** | kit pitch, ±12 semitones | kit tone | kit decay | kit mod |
| **KIT + SHIFT** | DJ filter (centre off, down low-pass, up high-pass) | wavefolder | Chebyshev | through-zero FM |
| **DRUM *n*** | pitch | tone | decay | mod |
| **DRUM *n* + SHIFT** | density *(later)* | level | length *(later)* | excite |
| **SETTINGS** | swing *(later)* | morph *(later)* | FM carrier pitch (±3 octaves around G3) | bus drive (×1 … ×4 into the soft clip) |

- KIT's four are **offsets for every drum** with a 10 % centre detent: centred is the kit as
  stored. (F4 becomes FILL when the sequencer arrives.)
- A drum's **pitch** fader is its note over the whole range (0 … 120); KIT pitch adds to it.
- **level**: centred is the kit's own balance; up is up to twice as loud.
- What tone, mod and excite do depends on the voice: tone is its main colour, mod its second
  axis, excite its third (the strike, the pitch drop, the noise).

## Buttons

| control | KIT / DRUM *n* | SETTINGS |
|---|---|---|
| **T1–T4** | open DRUM *n*; the same T again → KIT | **mute / unmute** drum *n* (the row shows who plays) |
| **`••` + T1–T4** | **FX 1–4 on / off**: filter, fold, Chebyshev, FM (the row shows which are on) | — |
| **`••` + FFWD / RWD** | KIT: **next / previous built-in kit**. DRUM *n*: **next / previous model** for that drum | — |
| **PLAY** | Marbles' clock run / stop | the same |

- A muted drum fades out over 12 ms if it is ringing, and stays silent until unmuted. Auditioning
  it from its own page (below) still plays it.
- FX start **on, at zero**: push a KIT + SHIFT fader and you hear it. Switching one off keeps its
  amount for when it comes back.
- The eight kits: lich techno, 8-bit breaks, modal gamelan, idm rungler, dub techno, iron vibes,
  glass rungler, gong garden (or **classic**, the LXR-derived voices, in a `CONFIG_SP1_CLASSIC`
  build). The row shows the kit: one LED for kits 1–4, the same LED with the others at half for
  5–8. Changing kit reloads all four voices and their drum pages.
- The model list (47 models, 53 with classic) is the spec's default list; the row shows where you
  are on it as a bar. A classic voice can be on one drum at a time.

## The rocker (FFWD up, RWD down)

**Playing** means Marbles' clock runs.

| page | playing: RWD | playing: FFWD | stopped: RWD | stopped: FFWD |
|---|---|---|---|---|
| **KIT** | held: **tape slow** | tap: **rotate the kit** | press: **step Marbles one tick** | held: **burst** |
| **DRUM *n*** | held: **rest drum *n*** | held: **release** drum *n*'s rests; tap: **clear** them at the next bar | press: **audition** drum *n* | held: **burst drum *n*** |
| **SETTINGS** | press: rests **choke** | press: rests **ring out** | the same | the same |
| **MARBLES** | held: **rest every drum** | held: release every drum; tap: clear all at the next bar | press: step Marbles one tick | held: burst |

On MARBLES the clock range (`[B]`, ×¼ / ×1 / ×4) moves to **`••` + FFWD / RWD on the t page**,
because the plain rocker is the rests'. `••` + FFWD / RWD on the X page is still the scale.

### Rests

- Each slot has a **one-bar mask of sixteen 16ths**. A bar is four of Marbles' t2 ticks, so a rest
  follows the clock, RATE and the ×¼ / ×4 range included.
- RWD held writes rests under the playhead, FFWD held erases them, and the bar repeats: what you
  rested keeps resting every bar while Marbles goes on generating around it.
- The rocker acts on a step if it is down at the step's start **or within 30 ms after**, so a
  press a hair early lands on the step it was aimed at.
- An FFWD **tap** (under 300 ms) clears the page's rests **at the next bar line** (it also gives
  back the step under the playhead straight away, like any FFWD press).
- **Choke** (the default): a rested step fades whatever that slot is ringing over 12 ms.
  **Ring out**: a rest only blocks new hits, so tails sound through.

### Tape slow (KIT, RWD held, playing)

The whole machine — Marbles' clock, the voices and the FX — slows like a finger on a cassette:
it sinks towards ×0.33 (time constant 0.7 s) while held and speeds back up over 1.2 s when let
go. Pitch, tempo and decays bend together. (These three numbers go into `drums.cnfg` later.)

### Kit rotation (KIT, FFWD tap, playing)

Each tap moves every voice one slot along **on the next beat**: slot 1 plays drum 4, slot 2 drum 1,
and so on. Four taps bring the kit home. The slots keep their triggers and rests; DRUM *n*'s page
keeps controlling the sound you call drum *n*, wherever it plays.

### Step-through (KIT or MARBLES, RWD, stopped)

Each press sends Marbles exactly one tick, and whatever it decides on that tick plays. Its
sequence walks on rather than restarting, so with DEJA VU locked the presses walk the loop.
(Not while MIDI is clocking Marbles.)

### Audition and burst (stopped)

On a drum page RWD plays that drum and FFWD held repeats it at the burst division (`••` + FFWD /
RWD sets that division on stock Wakes; here those keys pick models, so the division stays where
it was, 1/32 by default). On KIT and MARBLES the burst repeats whatever played on the last step.

---

## Where the hits come from (until the sequencer)

| slot | plays on | its Marbles voltage |
|---|---|---|
| 1 | t1 | X1 |
| 2 | t3 | X3 |
| 3 | t2, every tick (velocity from X2: ghost notes) | X2 |
| 4 | every 4th t2 tick | Y |

The voltage nudges each hit's tone a little, like a random parameter lock; a melodic voice
(marimba, vibes, bell, kalimba …) takes its **pitch** from it instead, on a major pentatonic over
two octaves.

## The log

Every action names itself on the console: `PAGE drum 2`, `KIT 3: modal gamelan`,
`MODEL drum 1: 12.5% pulse (31 of 47)`, `FX fold off`, `MUTE drum 2 muted`, `RESTS choke`,
`ROTATE …` / `ROTATED: slot 1 plays drum 4`, `TAPE slow …`, `STEP Marbles one tick`,
`AUDITION drum 2`. The kit reports back what it did: `RESTS per bar: slot 1 0, slot 2 0,
slot 3 10, slot 4 0 (of 16)` whenever the rests change, and `HIT drums 0x2 (1)` for hits while
stopped.

## CPU

On the M4 harness (instructions per sample; Plaits' engines are 417–532 there): a normal groove
100–205, every voice ringing 241–330, the same with all four FX 423–512, tape slow held 149–182.
In the emulator's calibrated model the drum firmware averages about 22 % playing, against
56–82 % for Wakes' Plaits engines on hardware; single blocks with several hits reach about 60 %,
and about 92 % with the heaviest kit, all four FX up and every decay pushed up — inside the one
queued block of margin Wakes has (`sp1_audio.c`). Watch the `AUD` line on hardware.
