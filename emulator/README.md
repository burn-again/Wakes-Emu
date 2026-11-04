# Wakes-Emu: Wakes on a virtual SP-1

Run Wakes in a browser, or headless from a script, and try a change in seconds
instead of flashing it. The emulator compiles **`firmware/src` unchanged** against a
virtual SP-1: a stand-in for the slice of Zephyr Wakes uses, plus models of the chips
around the nRF52840. The same `main.c`, the same control decode, the same Plaits and
Marbles, the same storage code. What it cannot tell you is listed below; read that
before you trust a result.

## Use it

**In the browser.** Open `emulator/web/index.html` from a web server (WebAssembly does
not load from `file://`):

```sh
sh emulator/build.sh web
python3 -m http.server 8000 -d emulator/web     # then open http://localhost:8000
```

The page boots the device cold, as right after a flash: on battery with nothing held,
Wakes prints `powering off, press "••" to start` and goes to SYSTEM_OFF. Hold `••`
(or **F**) for 1.5 s to switch it on. From there it behaves as Wakes does: `••` held
is SHIFT, held 3 s switches off, a double tap opens SETTINGS, and so on
(`docs/UI-PAGES.md`).

The device is drawn as the manual's diagram has it: VOL− and VOL+ on the top edge, the
rocker on the left edge (up is FFWD, BRST on the Plaits page; down is RWD, a single
TRIG), PLAY and `••` on the right edge, beside the ▲ and the two dots.

| Key | SP-1 control |
|---|---|
| **F** | `••` |
| **1**–**4** | T1–T4 |
| **Space** | PLAY |
| **↑** / **↓** | the rocker: up (FFWD) / down (RWD) |
| **←** / **→** | VOL− / VOL+ |
| Alt + click | latch a key down, for combinations such as `••` + T2 |

Faders take a drag, the scroll wheel, or the arrow keys once focused. The bench beside
the device sets the USB-C cable (unplugged, a charger, or a computer: USB-MIDI and the
CDC console), the battery voltage, and MIDI notes (an on-screen octave, or a MIDI
keyboard where the browser allows Web MIDI). The serial console shows everything Wakes
prints.

The **Manual** panel holds the nine pages of `docs/Wakes - Manual.pdf`, and with *Follow
Wakes* on it turns to the page for what Wakes is showing: Plaits, Marbles' t or X side,
SETTINGS (it reads the `MODULE`, `MARBLES` and `PAGE` lines Wakes prints). The
**Reference** panel renders the docs beside it: the control map (`docs/UI-PAGES.md`), the
Plaits engines, the Marbles settings, MIDI, the defaults, and the safety rules. Both are
copied next to the page by `build.sh web` (the page images need poppler's `pdftoppm`). The WAKES drive panel is the eMMC volume: edit a `.cnfg`, save it, switch Wakes
off and on, and it reads the new file. The drive is kept in your browser between visits.

**Headless.** For scripted checks and CI:

```sh
sh emulator/build.sh native
build/emu/native/wakes_headless --ms 8000 --wav out.wav --drive out-drive \
    0:fnc=1 2000:fnc=0 2500:rwd=1 2600:rwd=0 3000:f1=0.8 4000:play=1 4100:play=0
```

Events are `T:control=value` at T ms of device time: `fnc`, `play`, `t1`–`t4`, `volup`,
`voldown`, `ffwd`, `rwd` (1/0), `f1`–`f4` (0–1), `usb` (0 unplugged, 1 charger,
2 computer), `batt` (mV), `midi=90,3c,64` (hex bytes; needs `usb=2`). `--load DIR`
puts files on the drive before boot. The run prints the console, writes what the I2S
stream played to the WAV, and exits 3 if the device hit its watchdog, faulted, or
`main()` returned.

`node emulator/web/test-node.mjs` runs the wasm build through a full lifecycle (boot,
drive, TRIG, USB-MIDI, power off, wake, drive read back) and exits non-zero on failure.

## Test your own change

1. Edit `firmware/src` (or `config/`, or the Plaits overrides) as you would for the device.
2. `sh emulator/build.sh native` and run `wakes_headless` with the gestures you care
   about, or `sh emulator/build.sh web` and reload the page.
3. When it does what you want, build the real image and check the things the emulator
   cannot (below) on the device, with `docs/SAFETY.md`'s checklist.

Pushing to `main` runs `.github/workflows/emulator.yml`: it builds both, runs both tests,
and publishes the page to GitHub Pages (Settings → Pages → Source: GitHub Actions, once).

## What is real and what is modelled

| | In the emulator |
|---|---|
| Wakes' code | **Real**: every file in `firmware/src` but two, compiled unchanged; the generated headers come from the same `tools/gen_*.py`, the Plaits overrides from `tools/host-tests/mkovr.py`, the `CONFIG_` values from `firmware/Kconfig` + `prj.conf` |
| Threads | Zephyr's rule (highest priority ready runs; a wake that outranks preempts), as coroutines on **virtual time**: audio, main and the storage thread interleave as on the device |
| Faders, buttons | The ladder voltages Wakes' tables were measured from, including the rail sag a pressed ladder puts on the faders; Wakes' own decode, debounce and sag rejection run on them |
| `••`, charger, USB VBUS | GPIO and POWER registers; `••` and VBUS wake the device from SYSTEM_OFF |
| LEDs | The PWM duty Wakes sets on each channel |
| Audio | The I2S stream, clocked at exactly 48 kHz while the audio oscillator pin is high; what the speaker and headphones get |
| Codecs | CS42L42 and TAS2505 as I2C register files that answer Wakes' ID and clock checks; no analog model |
| Watchdog | 4 s, fed through the WDT reload registers; a starved watchdog resets the device |
| Power | SYSTEM_OFF, soft reset and faults end the instance; the next boot is fresh RAM, as on the nRF |
| eMMC + FAT | Replaced: files live in an in-memory volume behind Zephyr's `fs_*` calls, still gated by Wakes' own disk callbacks (card lock, owner) |
| USB | Wakes' `sp1_usbd.c` runs on a fake device stack; a "computer" enumerates it, and MIDI bytes go into Wakes' own receive path. USB audio out and the mass-storage class are not emulated |

Replaced modules: `sp1_emmc.c` (the bit-banged MMC driver) by `src/emu_emmc.c`,
`sp1_uac.c` (USB Audio Class out) by `src/emu_uac.c`, and feldd's USB-MIDI class.

## What it cannot tell you

- **CPU.** Running code takes no virtual time, and your computer is far faster than a
  64 MHz Cortex-M4. A change that plays cleanly here can overrun the 2 ms audio block on
  the device. Wakes' own CPU lines read 0 % here. Use `tools/plaits-bench` and the
  device's `CPU` / `AUD` log lines.
- **RAM and stacks.** The device has 256 KB and 3–4 KB thread stacks; the emulator's
  are far larger. Check the real build's size report.
- **Analog behaviour.** Codec gain staging, the speaker, the headphone jack's detect,
  ADC noise, the battery curve.
- **Timing between threads.** Interleaving follows Zephyr's rule at blocking points, but
  the device can also preempt inside a computation. A race that needs that will not show.
- **The bootloader.** T1 + T4 recovery, flashing, and anything below `0x20000`.
- **USB itself.** Enumeration, USB audio out, drive mode as a computer sees it.

So: use it for behaviour (gestures, pages, MIDI, config files, presets, logs, the
sound of a DSP change) and keep the device for performance, power and safety. Never
skip the hardware checklist because the emulator was happy.

## How it is built

```
emulator/
  build.sh               generates headers, compiles native (gcc) and web (clang wasm32-wasi + Asyncify)
  include/               emu_zephyr.h: the Zephyr/nRF/CMSIS slice Wakes uses; one-line stand-ins for each header
  src/emu_kernel.c       threads, sleep, semaphores, mutexes, slabs, on virtual time
  src/emu_board.c        registers, ADC, PWM, I2C, I2S, watchdog, console, USB, MIDI
  src/emu_fs.c           Zephyr fs_* over an in-memory volume
  src/emu_emmc.c         sp1_emmc.h stand-in;  src/emu_uac.c: sp1_uac.h stand-in
  src/emu_arch_*.c       thread switching: ucontext (native), Asyncify (wasm)
  native/wakes_headless.c  scripted runs, WAV and drive out
  web/                   index.html, app.js (the bench page), emu-core.js (device runtime), worklet.js,
                         md.js (renders the reference docs); manual/ is generated
  tools/gen_config.py    emu_autoconf.h + app version from firmware/Kconfig, prj.conf, VERSION
  tools/gen_manual.py    manual pages (PNG), the PDF and the reference docs, into web/manual/
```

The web build needs clang/lld 18+, a wasm32-wasi sysroot with libc++, and Binaryen's
`wasm-opt`. On Ubuntu 24.04:

```sh
sudo apt-get install clang lld wasi-libc libclang-rt-18-dev-wasm32 \
    libc++-18-dev-wasm32 libc++abi-18-dev-wasm32 binaryen poppler-utils
```

On macOS, Homebrew's `llvm`, `lld`, `wasi-libc`, `wasi-runtimes` and `binaryen` should
provide the same pieces (not tried); the GitHub Actions build always works.

Not affiliated with Teenage Engineering or Mutable Instruments. Same licence as Wakes
(MIT; see `LICENSE` and `NOTICE`).
