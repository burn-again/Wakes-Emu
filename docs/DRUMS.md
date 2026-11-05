# Drum hits — experimental (`CONFIG_SP1_DRUM`)

Marbles' t outputs can **strike the Plaits voice as a drum**: a t output routed to **KICK**,
**SNARE** or **HAT** plays Plaits' own bass drum, snare drum or hi-hat engine on its gate, and
the next ordinary TRIG gives the voice back to the selected engine. One voice, two
characters: tonal notes and drum hits taking turns on the same Marbles clock.

⚠️ **Experimental, and off in device builds.** `CONFIG_SP1_DRUM` defaults to `n`, so a firmware
built from `prj.conf` is exactly what it was. The browser emulator (`emulator/`) builds it on,
so this is the place to try it; `EMU_KCONFIG= emulator/build.sh` builds the emulator without it.
Nothing here has run on hardware yet.

---

## Using it

1. **T4** → MARBLES, and **T2** for the t page if it is not the one showing.
2. Hold **`••`** (t SHIFT) and tap **T1**, **T2** or **T3** to step that output round its
   destination ring. The drums come after the six existing destinations: none → TRIG →
   LEVEL → FM → TIMBRE → MORPH → HARMONICS → **KICK → SNARE → HAT** → *(wrap)*. From
   `none`, KICK is seven taps.
3. **PLAY** starts the clock.

The console names each step (`ROUTE t3 -> KICK`), the AUD line counts the hits since the last
one (`drum=4`), and a preset saves the routes as `t1 = kick`, `snare` or `hat`.

### Glyphs

Patterns no other t destination uses (`docs/UI-PAGES.md` has the rest):

| destination | on t1–t3 |
|---|---|
| KICK | `●○○●` |
| SNARE | `●○●○` |
| HAT | `○●○●` |

---

## How it plays

- **A drum hit takes the voice** from the block its gate rises in: Plaits switches to that drum
  engine (with the same `Reset()` it does when the engine fader moves) and strikes it through
  the ordinary TRIG path. Plaits' TRIG delay means the engine has changed before the strike
  reaches it.
- **Any other TRIG takes it back** — RWD, the burst, a MIDI note, or a t output routed to TRIG.
  The note cuts whatever tail the drum had left, just as a hit cuts the note before it.
- **Nothing striking?** A drum lets go of the voice **1 s after its gate falls**, by when its
  tail has died away. That matters in a drone (LEVEL connected, SETTINGS F4): the drone comes
  back by itself after the last hit. Choosing another engine gives the voice back at once.
- **Same tick, several things:** a drum beats a note, and **KICK beats SNARE beats HAT**,
  whichever t output each comes from. A hat on t2 (the clock, every tick) therefore fills only
  the ticks the kick and snare leave free.
- **A drum plays at its own settings.** The faders, the attenuverters, Marbles' X outputs and
  MIDI pitch all go on shaping the selected engine; none of them reach a drum, and LEVEL is
  unpatched while a drum plays (accent 0.8). A held note (a scale selected) is re-chosen by
  notes only, not by hits.
- **OUTPUT** works as for any engine: Plaits' drum engines put the **analog** model on OUT and
  the **synthetic** one on AUX, so `aux` is the other drum kit and `out+aux` both at once.

### The settings

Fixed for now, in `kDrums` at the top of `firmware/src/sp1_synth.cc`. MORPH is the decay on
all three.

| drum | engine | note | HARMONICS | TIMBRE | MORPH |
|---|---|---|---|---|---|
| KICK | 21 bass drum | 33 (A1, 55 Hz) | 0.40 attack / drive | 0.45 brightness | 0.50 decay |
| SNARE | 22 snare drum | 50 (D3) | 0.55 tone ↔ noise | 0.50 drum modes | 0.40 decay |
| HAT | 23 hi-hat | 64 | 0.55 metal ↔ noise | 0.65 high-pass | 0.25 decay |

A `WAKES/CNFG/drums.cnfg` to tune these without a rebuild is the obvious next step.

---

## Recipes

| t model (`[E]`, T1 on the t page) | routes | what you get |
|---|---|---|
| **coin toss** | t2 → TRIG (the default), t3 → KICK | every tick is a note or a kick; **t BIAS** sets the odds |
| **drums** | t1 → KICK, t2 → TRIG, t3 → SNARE | Marbles' drum patterns (`{1,0,0,0,2,0,0,0}` …) as kick and snare, notes on the ticks between |
| **drums** | t1 → KICK, t2 → HAT, t3 → SNARE | a mono drum machine: kick, hat, hat, hat, snare, hat, hat, hat; t BIAS roams the pattern bank |
| **independent Bernoulli** | t1 → HAT, t2 → TRIG, t3 → KICK | notes, with hats and kicks dropping in on their own odds |

`DEJA VU` on the t side (SETTINGS, T2) locks whichever of these you like into a loop.

---

## Cost

Measured on the emulator's CPU meter (calibrated against the hardware table in
`docs/PLAITS-ENGINES.md`), virtual analog selected, clock running at 120 BPM:

| t3 → | avg | dearest block |
|---|---|---|
| none | 64.6 % | 67.2 % |
| KICK | 67.0 % | 70.8 % |
| SNARE | 70.0 % | 79.0 % |
| HAT | 66.9 % | 72.0 % |

A drum **replaces** the engine for the blocks it plays rather than adding to it, so the worst
case stays that of the dearest engine you select. Plaits renders both the analog and the
synthetic model of each drum every block; rendering only the one the OUTPUT setting plays would
save the other's cost, if this ever ships.

---

## Presets and other builds

The tokens `kick`, `snare` and `hat` are experimental (`EXPERIMENTAL` in `tools/gen_prst.py`):
they are emitted only into a build with the option, and they are **not** part of the PRST
fingerprint, so the format number does not change. A build with drums reads every existing
preset; a build without them reads `t3 = kick` as a field it does not know — that output goes
to its default and the log says `1 bad, first t3` — and reads the rest of the preset as usual.
Before this ships in a release, the tokens move into `CHOICES` with a new format version.
