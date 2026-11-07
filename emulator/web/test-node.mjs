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

// MIDI files (CONFIG_SP1_MIDIFILE, docs/MIDIFILE.md): rocker down + T1-T4 picks
// WAKES/MIDI/1 .. 4. A file made here (format 1: a tempo track, then a C major chord and
// three notes over it), a non-MIDI file in slot 2, then PLAY.
dev.snapshotDrive();
if (dev.drive.has('/WAKES/MIDI/1')) {
	const vlq = (n) => { const b = [n & 0x7f]; while ((n >>>= 7)) { b.unshift((n & 0x7f) | 0x80); } return b; };
	const be = (n, k) => Array.from({ length: k }, (_, i) => (n >>> (8 * (k - 1 - i))) & 255);
	const track = (evs) => {
		let at = 0;
		const d = evs.flatMap(([t, ...m]) => { const r = [...vlq(t - at), ...m]; at = t; return r; });
		return [0x4d, 0x54, 0x72, 0x6b, ...be(d.length, 4), ...d];
	};
	const q = 96;
	const notes = [[0, 60, 4], [0, 64, 4], [0, 67, 4], [2, 72, 1], [3, 74, 1], [4, 76, 2]];
	const evs = notes.flatMap(([b, n, len]) => [[b * q, 0x90, n, 100], [(b + len) * q, 0x80, n, 0]])
		.sort((a, b) => a[0] - b[0] || a[1] - b[1]);
	const smf = new Uint8Array([0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 1, 0, 2, ...be(q, 2),
		...track([[0, 0xff, 0x51, 3, 0x07, 0xa1, 0x20], [0, 0xff, 0x2f, 0]]),
		...track([...evs, [6 * q, 0xff, 0x2f, 0]])]);
	dev.writeFile('/WAKES/MIDI/1/test.mid', smf);
	dev.writeFile('/WAKES/MIDI/2/notes.mid', new TextEncoder().encode('not a MIDI file\n'));
	const rocker = (b, t) => {
		step(t); dev.setButton(BTN.RWD, true); tap(b, t + 100); step(t + 300); dev.setButton(BTN.RWD, false);
	};
	dev.setFnc(true);
	await dev.service();
	step(2000);
	dev.setFnc(false);
	step(2500);
	rocker(BTN.T2, 2600);
	rocker(BTN.T1, 3200);
	step(3800);
	const third = log.slice(log.lastIndexOf('=== Wakes v0.9.0 ==='));
	check(/MIDIFILE 2: notes\.mid -- not a MIDI file/.test(third), 'MIDI files: a file that is not MIDI is refused, by name');
	check(/MIDIFILE 1: test\.mid \(\d+ B\) -- format 1, 2 tracks, 6 notes 60\.\.76, up to 4 at once, 6\.00 beats/.test(third),
		'MIDI files: rocker down + T1 loads WAKES/MIDI/1 and reads it right');
	peak = 0;
	tap(BTN.PLAY, 4000);
	step(8000);
	const played = log.slice(log.lastIndexOf('=== Wakes v0.9.0 ==='));
	check(/MIDI active/.test(played) && peak > 0.05, 'MIDI files: PLAY plays it through the MIDI note path');
	rocker(BTN.T1, 8200);
	step(8800);
	check(/MIDIFILE off/.test(log.slice(log.lastIndexOf('=== Wakes v0.9.0 ==='))), 'MIDI files: the same gesture again turns it off');
} else {
	console.log('skip  MIDI files: this build has no CONFIG_SP1_MIDIFILE');
}

// ---- the experiments in docs/HEADROOM.md: a fresh ON for each ----
const freshBoot = async () => {
	if (dev.state === 'on') {
		step(dev.nowMs() + 600);           // a button just before "••" cancels the power-off
		dev.setFnc(true);
		step(dev.nowMs() + 5500);          // the 3 s gesture and the save
		dev.setFnc(false);                 // SYSTEM_OFF waits for the release
		step(dev.nowMs() + 10000);         // returns once it is off
	}
	dev.setFnc(true);
	await dev.service();
	step(2000);
	dev.setFnc(false);
	step(2500);
};
const fncTap = (t) => { step(t); dev.setFnc(true); step(t + 80); dev.setFnc(false); };
const settings = (t) => { fncTap(t); fncTap(t + 200); };          // "••" double-tap
const rms = (from, to) => {                                       // the output's level, dBFS
	step(from);
	let e = 0, n = 0;
	while (dev.state === 'on' && dev.nowMs() < to) {
		dev.runUntil(Math.min(to, dev.nowMs() + 10));
		for (const x of dev.readAudio().left) { e += x * x; n++; }
	}
	return 10 * Math.log10(e / Math.max(n, 1) + 1e-12);
};
const since = () => log.slice(log.lastIndexOf('=== Wakes v0.9.0 ==='));

// Reverb (CONFIG_SP1_REVERB): PLAITS SETTINGS T1. A strike's tail, dry and then with step 3;
// then 2-op FM with the clock running, which leaves too little CPU: the guard pauses it.
await freshBoot();
settings(2600); tap(BTN.T1, 3100); tap(BTN.T1, 3300); fncTap(3600);
if (/REVERB 2 of 4/.test(since())) {
	settings(4000); tap(BTN.T1, 4400); tap(BTN.T1, 4600); tap(BTN.T1, 4800); fncTap(5100);   // 3, 4, off
	tap(BTN.RWD, 5600);
	const dry = rms(6600, 7400);
	settings(9000); tap(BTN.T1, 9400); tap(BTN.T1, 9600); tap(BTN.T1, 9800); fncTap(10100);   // 3
	tap(BTN.RWD, 10600);
	const wet = rms(11600, 12400);
	check(wet > dry + 6, `reverb: step 3 lifts the tail 1-1.8 s after a strike (${dry.toFixed(1)} -> ${wet.toFixed(1)} dBFS)`);
	tap(BTN.T3, 12600); tap(BTN.T3, 12800);                                                     // 2-op FM
	tap(BTN.PLAY, 13200);
	step(16000);
	check(/ENGINE [^\n]*2-op-FM/.test(since()) && /REVERB paused/.test(since()),
		'reverb: the CPU guard pauses it on 2-op FM with the clock running');
	tap(BTN.PLAY, 16200);
} else {
	console.log('skip  reverb: this build has no CONFIG_SP1_REVERB');
}

// Arpeggiator (CONFIG_SP1_ARP): MARBLES SETTINGS T1, a chord held over USB-MIDI, the clock
// stopped (free at 120 BPM, 1/32): a stream of strikes, one per step.
await freshBoot();
dev.setUsb(USB.COMPUTER);
step(3000);
tap(BTN.T4, 3200); settings(3500); tap(BTN.T1, 4000); fncTap(4300);
if (/ARP up/.test(since())) {
	step(4800);
	dev.sendMidi([0x90, 60, 100, 0x90, 64, 100, 0x90, 67, 100]);
	step(10000);
	const trig = [...since().matchAll(/ trig=(\d+)/g)].map((m) => Number(m[1]));
	check(trig.some((n) => n >= 25), `arp: a held chord becomes a stream of notes (${trig.at(-1)} strikes in the last AUD window)`);
	dev.sendMidi([0x80, 60, 0, 0x80, 64, 0, 0x80, 67, 0]);
} else {
	console.log('skip  arp: this build has no CONFIG_SP1_ARP');
}
dev.setUsb(USB.UNPLUGGED);

const secs = (performance.now() - t0) / 1000;
console.log(`\n${frames} frames of audio (${(frames / 48000).toFixed(1)} s of device time) ` +
	`in ${secs.toFixed(2)} s wall (${(frames / 48000 / secs).toFixed(1)}x real time)`);
process.exit(ok ? 0 : 1);
