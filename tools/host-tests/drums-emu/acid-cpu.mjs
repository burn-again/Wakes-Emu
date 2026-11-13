// acid-cpu.mjs -- what the acid line costs, on the emulator's CPU meter (the wasm build,
// which counts estimated Cortex-M4F cycles; emulator/README.md says how far to trust it:
// +-7 points on Plaits' engines, and the drums and tinyacid are not in its calibration).
//
//   EMU_KCONFIG="SP1_DRUMS=y" sh emulator/build.sh web
//   node tools/host-tests/drums-emu/acid-cpu.mjs emulator/web
//
// One boot, the clock running throughout; each line is 4 s of audio blocks: the mean and
// the worst block, as a share of the block's cycle budget (the whole audio thread: Marbles,
// the drums, the bus FX, the output stage).
import fs from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';

const web = path.resolve(process.argv[2] || 'emulator/web');
const { WakesDevice, BTN, RESETREAS } = await import(pathToFileURL(path.join(web, 'emu-core.js')).href);
let log = '';
const dev = new WakesDevice(new WebAssembly.Module(fs.readFileSync(path.join(web, 'wakes.wasm'))),
	{ onConsole: (s) => { log += s; if (process.argv.includes('-v')) process.stdout.write(s); } });
const step = (until) => { while (dev.state === 'on' && dev.nowMs() < until) { dev.runUntil(Math.min(until, dev.nowMs() + 10)); dev.readAudio(); } };
let t = 0;
const wait = (ms) => { t += ms; step(t); };
const tap = (b) => { dev.setButton(b, true); wait(90); dev.setButton(b, false); wait(210); };
const fn = (b) => { dev.setFnc(true); wait(100); dev.setButton(b, true); wait(90); dev.setButton(b, false); wait(110); dev.setFnc(false); wait(200); };
const shiftFader = (k, v) => { dev.setFnc(true); wait(100); dev.setFader(k, v > 0.5 ? 0.0 : 1.0); wait(60); dev.setFader(k, v); wait(60); dev.setFnc(false); wait(120); };
const budget = () => dev.ex.emu_audio_block_frames() * 64e6 / 48000;
const measure = (what) => {
	wait(1500);                                   // settle
	dev.ex.emu_cpu_window_reset();
	const from = log.length;
	wait(4000);
	const blocks = dev.ex.emu_cpu(1);
	const avg = 100 * dev.ex.emu_cpu(0) / blocks / budget();
	const max = 100 * dev.ex.emu_cpu(2) / budget();
	const shed = (log.slice(from).match(/CPU guard: (\d+) tail/g) || []).length;
	console.log(`${what.padEnd(58)} avg ${avg.toFixed(1).padStart(5)} %   worst block ${max.toFixed(1).padStart(5)} %${shed ? `   CPU guard fired ${shed}x` : ''}`);
};

for (let k = 0; k < 4; k++) dev.setFader(k, 0.5);
dev.setFnc(true); await dev.boot(RESETREAS.OFF); wait(1700); dev.setFnc(false); wait(300);
tap(BTN.PLAY);
measure('kit 1 (four drums), bass off');
dev.setFnc(true); wait(90); dev.setFnc(false); wait(110); dev.setFnc(true); wait(90); dev.setFnc(false); wait(500);   // SETTINGS
tap(BTN.T2); tap(BTN.T4);
measure('kick + hat (drums 2 and 4 muted), bass off');
fn(BTN.FFWD);
measure('kick + hat + the old bass on the Y clock');
fn(BTN.FFWD); fn(BTN.FFWD);
if (!/BASS acid 1: acid-home/.test(log)) { console.log('the acid line did not start'); process.exit(1); }
measure('kick + hat + acid-home (303)');
fn(BTN.FFWD);
measure('kick + hat + campfire (101: pulse, sub, noise)');
fn(BTN.FFWD); fn(BTN.FFWD); fn(BTN.FFWD); fn(BTN.FFWD);
if (!/BASS acid 6: crawl/.test(log)) { console.log('crawl was not reached'); process.exit(1); }
measure('kick + hat + crawl (303: sub, filter FM, drive)');
tap(BTN.T2); tap(BTN.T4);
measure('four drums + crawl');
dev.setFnc(true); wait(90); dev.setFnc(false); wait(700);                                    // one tap: back to KIT
for (let k = 0; k < 4; k++) shiftFader(k, k === 0 ? 0.8 : 0.6);                              // KIT + SHIFT: all four bus FX up
measure('four drums + crawl + all four bus FX');
