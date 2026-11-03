// test-node.mjs -- the wasm build, headless, in Node: the same lifecycle the page runs.
//
//   node emulator/web/test-node.mjs
//
// Wakes is woken by "••", held 2 s (power-on), TRIG is struck, the clock started, MIDI
// played over USB, then "••" held 5 s (power-off). Checks: it boots, the drive gets its
// CNFG files, audio is not silent, power-off reaches SYSTEM_OFF, "••" wakes it again
// and the drive survives.
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

dev.setUsb(USB.COMPUTER);
step(4500);
peak = 0;
dev.sendMidi([0x90, 60, 100]); step(4900); dev.sendMidi([0x80, 60, 0]);
step(5200);
check(/MIDI port enabled/.test(log), 'USB host: MIDI port enabled');
check(peak > 0.05, 'audio from a MIDI note');
dev.setUsb(USB.UNPLUGGED);

dev.setFnc(true);
step(10500);
dev.setFnc(false);
step(12000);
check(dev.state === 'off' && STOP_NAME[dev.lastStop] === 'SYSTEM_OFF', `power-off: ${STOP_NAME[dev.lastStop]}`);

dev.setFnc(true);
await dev.service();
check(dev.state === 'on' && dev.boots === 2, 'wakes again on "••"');
step(2000);
dev.setFnc(false);
step(2600);
const second = log.slice(log.lastIndexOf('=== Wakes v0.9.0 ==='));
check(/engines\.cnfg: \d+ B read, 0 problems/.test(second), 'second boot reads the CNFG files it wrote');
check(/PRST 1: 1\.prst/.test(second), 'second boot loads the preset saved at power-off');

const secs = (performance.now() - t0) / 1000;
console.log(`\n${frames} frames of audio (${(frames / 48000).toFixed(1)} s), ` +
	`device time ~${(12 + 2.6).toFixed(1)} s in ${secs.toFixed(2)} s wall ` +
	`(${((12 + 2.6) / secs).toFixed(1)}x real time)`);
process.exit(ok ? 0 : 1);
