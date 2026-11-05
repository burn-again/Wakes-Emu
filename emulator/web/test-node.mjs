// test-node.mjs -- the wasm build, headless, in Node: the same lifecycle the page runs.
//
//   node emulator/web/test-node.mjs
//
// Wakes is woken by "••", held 2 s (power-on), TRIG is struck, the clock started, MIDI
// played over USB, then "••" held 5 s (power-off). Checks: it boots, the drive gets its
// CNFG files, audio is not silent, power-off reaches SYSTEM_OFF, "••" wakes it again
// and the drive survives. Then, in a build with the drum hits, t3 goes to KICK and the
// clock runs: kicks strike, the CPU stays in budget, and the route is saved in PRST.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { WakesDevice, BTN, USB, RESETREAS, STOP_NAME } from './emu-core.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const module = new WebAssembly.Module(fs.readFileSync(path.join(here, 'wakes.wasm')));
const verbose = process.argv.includes('-v');
let log = '';
const dev = new WakesDevice(module, {
	onConsole: (s) => { log += s; if (verbose) { process.stdout.write(s); } },
});

let ok = true;
const check = (cond, what) => {
	console.log(`${cond ? 'PASS' : 'FAIL'}  ${what}`);
	if (!cond) { ok = false; }
};

let peak = 0, frames = 0;
const step = (until) => {
	while (dev.state === 'on' && dev.nowMs() < until) {
		dev.runUntil(Math.min(until, dev.nowMs() + 10));
		const a = dev.readAudio();
		frames += a.left.length;
		for (const x of a.left) { peak = Math.max(peak, Math.abs(x)); }
	}
};

const t0 = performance.now();
dev.setFnc(true);
await dev.boot(RESETREAS.OFF);
step(2000);
dev.setFnc(false);
step(2500);
check(/Wakes v0\.9\.0/.test(log), 'boots: banner names Wakes v0.9.0');
check(dev.audioRunning(), 'I2S is running');
dev.snapshotDrive();
check(dev.drive.has('/WAKES/CNFG/midi.cnfg'), 'drive: WAKES/CNFG/midi.cnfg written');

for (const t of [2600, 3000, 3400]) {
	step(t); dev.setButton(BTN.RWD, true); step(t + 80); dev.setButton(BTN.RWD, false);
}
step(4000);
check(peak > 0.05, `audio after TRIG (peak ${(20 * Math.log10(peak || 1e-9)).toFixed(1)} dBFS)`);

// The CPU meter: the build counts estimated cycles, and Wakes' own AUD line reads them.
dev.ex.emu_cpu_window_reset();
step(4400);
const blocks = dev.ex.emu_cpu(1);
const cpuPct = 100 * dev.ex.emu_cpu(0) / blocks / (dev.ex.emu_audio_block_frames() * 64e6 / 48000);
check(blocks > 100 && cpuPct > 20 && cpuPct < 100, `CPU meter: virtual analog at ${cpuPct.toFixed(1)} % of the audio block`);


dev.setUsb(USB.COMPUTER);
step(4900);
peak = 0;
dev.sendMidi([0x90, 60, 100]); step(5300); dev.sendMidi([0x80, 60, 0]);
step(5600);
check(/MIDI port enabled/.test(log), 'USB host: MIDI port enabled');
check(peak > 0.05, 'audio from a MIDI note');
dev.setUsb(USB.UNPLUGGED);

dev.setFnc(true);
step(10500);
dev.setFnc(false);
step(12000);
check(dev.state === 'off' && STOP_NAME[dev.lastStop] === 'SYSTEM_OFF', `power-off: ${STOP_NAME[dev.lastStop]}`);
const aud = [...log.matchAll(/cyc avg=([\d.]+)%/g)].map((m) => Number(m[1]));
check(aud.some((v) => v > 20), `Wakes' own AUD line reads the counted cycles (${aud.at(-1)} %)`);

dev.setFnc(true);
await dev.service();
check(dev.state === 'on' && dev.boots === 2, 'wakes again on "••"');
step(2000);
dev.setFnc(false);
step(2600);
const second = log.slice(log.lastIndexOf('=== Wakes v0.9.0 ==='));
check(/engines\.cnfg: \d+ B read, 0 problems/.test(second), 'second boot reads the CNFG files it wrote');
check(/PRST 1: 1\.prst/.test(second), 'second boot loads the preset saved at power-off');

// Drum hits (CONFIG_SP1_DRUM, docs/DRUMS.md; the emulator builds them on): MARBLES, t
// SHIFT, T3 seven steps round the t ring to KICK, then the clock. In the coin-toss model
// t2 (TRIG, the default) is every tick and t3 the coin, so the kicks fall among notes.
const tap = (b, t) => { step(t); dev.setButton(b, true); step(t + 80); dev.setButton(b, false); };
if (/ drum=\d+/.test(log)) {
	tap(BTN.T4, 2800);
	step(3200); dev.setFnc(true);
	for (let k = 0; k < 7; k++) { tap(BTN.T3, 3300 + 200 * k); }
	step(4800); dev.setFnc(false);
	tap(BTN.PLAY, 5200);
	step(5400);
	dev.ex.emu_cpu_window_reset();
	peak = 0;
	step(13000);
	const worst = 100 * dev.ex.emu_cpu(2) / (dev.ex.emu_audio_block_frames() * 64e6 / 48000);
	const avg = 100 * dev.ex.emu_cpu(0) / dev.ex.emu_cpu(1) /
		(dev.ex.emu_audio_block_frames() * 64e6 / 48000);
	const drums = log.slice(log.lastIndexOf('=== Wakes v0.9.0 ==='));
	const hits = [...drums.matchAll(/ drum=(\d+)/g)].reduce((s, m) => s + Number(m[1]), 0);
	check(/ROUTE t3 -> KICK/.test(drums), 'drums: t SHIFT + T3 reaches KICK');
	check(hits > 1 && peak > 0.05, `drums: ${hits} kicks struck (peak ${(20 * Math.log10(peak || 1e-9)).toFixed(1)} dBFS)`);
	check(worst < 100, `drums: CPU avg ${avg.toFixed(1)} %, dearest block ${worst.toFixed(1)} %`);
	dev.setFnc(true);
	step(18500);
	dev.setFnc(false);
	step(20000);
	dev.snapshotDrive();
	const prst = dev.drive.get('/WAKES/PRST/1/1.prst');
	check(dev.state === 'off' && prst && /= *kick/i.test(new TextDecoder().decode(prst.data)),
		'drums: power-off saves the KICK route in 1.prst');
} else {
	console.log('skip  drums: this build has no CONFIG_SP1_DRUM');
}

const secs = (performance.now() - t0) / 1000;
console.log(`\n${frames} frames of audio (${(frames / 48000).toFixed(1)} s of device time) ` +
	`in ${secs.toFixed(2)} s wall (${(frames / 48000 / secs).toFixed(1)}x real time)`);
process.exit(ok ? 0 : 1);
