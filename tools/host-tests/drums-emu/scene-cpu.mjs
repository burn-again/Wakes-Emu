// scene-cpu.mjs -- every HOME scene on the emulator's CPU meter (the wasm build; emulator/README.md
// says how far to trust it). One boot, the clock running; each scene is chosen on HOME F3, given
// a bar to be taken up and settle, then measured for 4 s: mean and worst block, as a share of
// the block's cycle budget.
//   EMU_KCONFIG="SP1_DRUMS=y" sh emulator/build.sh web && node scene-cpu.mjs emulator/web
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const web = path.resolve(process.argv[2] || 'emulator/web');
const { WakesDevice, BTN, RESETREAS } = await import(pathToFileURL(path.join(web, 'emu-core.js')).href);
let log = '';
const dev = new WakesDevice(new WebAssembly.Module(fs.readFileSync(path.join(web, 'wakes.wasm'))),
	{ onConsole: (s) => { log += s; } });
const step = (until) => { while (dev.state === 'on' && dev.nowMs() < until) { dev.runUntil(Math.min(until, dev.nowMs() + 10)); dev.readAudio(); } };
let t = 0;
const wait = (ms) => { t += ms; step(t); };
const budget = () => dev.ex.emu_audio_block_frames() * 64e6 / 48000;
for (let k = 0; k < 4; k++) dev.setFader(k, 0.5);
dev.setFnc(true); await dev.boot(RESETREAS.OFF); wait(1700); dev.setFnc(false); wait(300);
dev.setButton(BTN.PLAY, true); wait(90); dev.setButton(BTN.PLAY, false); wait(300);
dev.setFader(2, 0.0); wait(300);                       // catch HOME F3
const N = 27, rows = [];
for (let i = 0; i < N; i++) {
	dev.setFader(2, 0.9 * (i + 0.5) / N);
	const from = log.length;
	wait(4500);                                        // up to a bar at 60 BPM, then settle
	const name = (log.slice(from).match(/SCENE \d+: ([^,]+), ([^,]+), (\d+) BPM/) || []).slice(1);
	dev.ex.emu_cpu_window_reset();
	const g0 = log.length;
	wait(4000);
	const blocks = dev.ex.emu_cpu(1);
	const avg = 100 * dev.ex.emu_cpu(0) / blocks / budget(), max = 100 * dev.ex.emu_cpu(2) / budget();
	const shed = (log.slice(g0).match(/CPU guard/g) || []).length;
	rows.push({ i: i + 1, name: name[0] || '?', kit: name[1] || '?', bpm: name[2] || '?', avg, max, shed });
	console.log(`${String(i + 1).padStart(2)} ${(name[0] || '?').padEnd(14)} ${(name[1] || '?').padEnd(14)} ${String(name[2] || '?').padStart(3)} BPM  avg ${avg.toFixed(1).padStart(5)} %  worst ${max.toFixed(1).padStart(6)} %${shed ? '  guard ' + shed : ''}`);
}
