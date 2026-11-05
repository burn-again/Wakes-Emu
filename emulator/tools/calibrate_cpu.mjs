// calibrate_cpu.mjs -- fit the CPU meter to Wakes' hardware measurements.
// Run it through tools/calibrate.sh, which builds what each step needs:
//
//   --profile WASM   a profiling build (EMU_CPU_PROFILE=1): measures each engine's work per
//                    kind (float, memory, integer, control flow, maths library), fits a
//                    weight per kind, writes the model to emulator/cpu.json
//   --check WASM     a build counting with that model: reads Wakes' own AUD line per
//                    engine, as the hardware table did, and records how far the meter is
//                    from each hardware figure in cpu.json (what the page shows)
//
// The ground truth is docs/PLAITS-ENGINES.md's "Re-measured on hardware with the M4a-M4d
// build, Marbles running" table: each engine's mean cost of an audio block, from Wakes'
// AUD line on an SP-1. Both steps play the same engines with Marbles' clock running.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { WakesDevice, BTN, RESETREAS } from '../web/emu-core.js';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');
const cpuJson = path.join(root, 'emulator', 'cpu.json');
const arg = (name) => { const i = process.argv.indexOf(name); return i > 0 ? process.argv[i + 1] : null; };
const profileWasm = arg('--profile');
const checkWasm = arg('--check');
const config = arg('--config') || 'the default build';
if (!profileWasm && !checkWasm) { throw new Error('give --profile WASM or --check WASM (see tools/calibrate.sh)'); }

// Engines whose hardware figure measured a different workload from this firmware's. They
// are shown, never fitted.
const EXCLUDE = {
	additive: 'changed after the hardware session (M4e: kRampStride 4 to 2, more work per sample)',
	chiptune: 'renders one voice here (TRIG is always patched in v0.9.0); 73.7 % fits its five-voice chord mode',
};

// ---- the hardware table, from the docs ----
const doc = fs.readFileSync(path.join(root, 'docs', 'PLAITS-ENGINES.md'), 'utf8');
const at = doc.indexOf('Re-measured on hardware');
if (at < 0) { throw new Error('PLAITS-ENGINES.md: the hardware re-measurement table is not there'); }
const hw = [];
for (const line of doc.slice(at).split('\n').slice(0, 60)) {
	const m = /^\|\s*(\d+)\s*\|\s*[^|]*\|\s*([a-z0-9-]+)\s*\|\s*\**([\d.]+)\**\s*\|/i.exec(line);
	if (m) { hw.push({ plaits: Number(m[1]), name: m[2], hardware: Number(m[3]) }); }
	if (hw.length && line.trim() === '') { break; }
}
if (hw.length < 5) { throw new Error(`PLAITS-ENGINES.md: found only ${hw.length} hardware rows`); }

// ---- one session: every engine in turn, Marbles' clock running ----
async function session(wasmPath, measure) {
	const module = new WebAssembly.Module(fs.readFileSync(wasmPath));
	let log = '';
	let frames = 0;
	const dev = new WakesDevice(module, { onConsole: (s) => { log += s; } });
	dev.autoReboot = false;
	dev.writeFile('/WAKES/CNFG/engines.cnfg', '[engines]\n' + hw.map((e, i) => `${i + 1} = ${e.name}`).join('\n') + '\n');
	const run = (ms) => {
		const end = dev.nowMs() + ms;
		while (dev.state === 'on' && dev.nowMs() < end) {
			dev.runUntil(Math.min(end, dev.nowMs() + 20));
			frames += dev.readAudio().left.length;
		}
	};
	const tap = (bit) => { dev.setButton(bit, true); run(110); dev.setButton(bit, false); run(60); };
	dev.setFnc(true);
	await dev.boot(RESETREAS.OFF);
	run(2000);
	dev.setFnc(false);
	run(1500);
	if (!/engine stack: \d+ engines/.test(log)) { throw new Error('Wakes did not load the engine stack'); }
	tap(BTN.PLAY);                       // PLAITS: PLAY runs Marbles' clock
	run(500);
	const out = [];
	for (let k = 0; k < hw.length; k++) {
		if (k > 0) { tap(BTN.T3); }      // ENG+
		out.push(measure({ dev, run, logOf: () => log, frames: () => frames, engine: hw[k] }));
	}
	return out;
}

const AUD = /AUD run=\d+ blk=\d+ .*?cyc avg=([\d.]+)% max=([\d.]+)%.*? eng=(\d+) mrb=(\d+)/g;

// ---- --profile: work per kind, per block, as % of the block ----
function measureProfile({ dev, run, logOf, frames }) {
	run(1500);                            // settle on the new engine
	const f0 = frames();
	dev.ex.emu_cpu_window_reset();
	run(4000);
	const blocks = dev.ex.emu_cpu(1);
	const perBlockFrames = (frames() - f0) / blocks;
	const budget = perBlockFrames * 64e6 / 48000;
	const kinds = [0, 1, 2, 3, 4].map((k) => 100 * dev.ex.emu_cpu(10 + k) / 8 / blocks / budget);
	const eng = [...logOf().matchAll(/eng=(\d+) mrb=(\d+)/g)].at(-1);
	return { kinds, budget, marbles: eng ? eng[2] === '1' : null };
}

// Weights per kind (fp, mem, int, ctl; the maths library keeps 1), pulled toward one common
// factor with strength lam. Solves (XtX + lam W) m = Xt h + lam W s0.
function fitKinds(rows, lam) {
	const X = rows.map((r) => r.kinds.slice(0, 4));
	const h = rows.map((r) => r.hardware - r.kinds[4]);
	const sum = X.map((x) => x.reduce((a, b) => a + b, 0));
	const s0 = sum.reduce((a, x, i) => a + x * h[i], 0) / sum.reduce((a, x) => a + x * x, 0);
	if (!isFinite(lam)) { return [s0, s0, s0, s0]; }
	const n = 4;
	const mean = [0, 1, 2, 3].map((c) => X.reduce((a, x) => a + x[c], 0) / X.length);
	const A = [...Array(n)].map((_, i) => [...Array(n)].map((_, j) =>
		X.reduce((a, x) => a + x[i] * x[j], 0) + (i === j ? lam * mean[i] * mean[i] : 0)));
	const b = [...Array(n)].map((_, i) => X.reduce((a, x, r) => a + x[i] * h[r], 0) + lam * mean[i] * mean[i] * s0);
	// Gaussian elimination
	for (let c = 0; c < n; c++) {
		let p = c;
		for (let r = c + 1; r < n; r++) { if (Math.abs(A[r][c]) > Math.abs(A[p][c])) { p = r; } }
		[A[c], A[p]] = [A[p], A[c]]; [b[c], b[p]] = [b[p], b[c]];
		for (let r = c + 1; r < n; r++) {
			const f = A[r][c] / A[c][c];
			for (let k = c; k < n; k++) { A[r][k] -= f * A[c][k]; }
			b[r] -= f * b[c];
		}
	}
	const m = Array(n).fill(0);
	for (let r = n - 1; r >= 0; r--) {
		m[r] = (b[r] - A[r].slice(r + 1).reduce((a, v, k) => a + v * m[r + 1 + k], 0)) / A[r][r];
	}
	return m;
}

const predict = (r, m) => r.kinds.slice(0, 4).reduce((a, x, c) => a + x * m[c], 0) + r.kinds[4];

function loo(rows, lam) {
	let se = 0;
	for (let i = 0; i < rows.length; i++) {
		const m = fitKinds(rows.filter((_, k) => k !== i), lam);
		se += (predict(rows[i], m) - rows[i].hardware) ** 2;
	}
	return Math.sqrt(se / rows.length);
}

if (profileWasm) {
	const meas = await session(profileWasm, measureProfile);
	const all = hw.map((e, k) => ({ ...e, ...meas[k] }));
	const fitRows = all.filter((r) => !EXCLUDE[r.name]);
	const lams = [0.03, 0.1, 0.3, 1, 3, 10, 30, Infinity];
	const scored = lams.map((lam) => {
		const m = fitKinds(fitRows, lam);
		const s0 = fitKinds(fitRows, Infinity)[0];
		const sane = m.every((v) => v > 0.25 * s0 && v < 4 * s0);
		return { lam, m, sane, loo: loo(fitRows, lam) };
	});
	const best = scored.filter((s) => s.sane).sort((a, b) => a.loo - b.loo)[0];
	const single = scored.find((s) => !isFinite(s.lam));
	const m = best.m;
	console.log('lambda   leave-one-out rms   weights fp mem int ctl');
	for (const s of scored) {
		console.log(`${String(s.lam).padEnd(8)} ${s.loo.toFixed(1).padStart(8)} points     ${s.m.map((v) => v.toFixed(2)).join(' ')}${s.sane ? '' : '  (rejected: a weight out of range)'}${s === best ? '   <- chosen' : ''}`);
	}
	console.log('\nengine             hardware  model   kinds (fp mem int ctl lib, % of block, unweighted)');
	for (const r of all) {
		const p = predict(r, m);
		console.log(`${r.name.padEnd(18)} ${r.hardware.toFixed(1).padStart(6)} % ${p.toFixed(1).padStart(6)} %   ${r.kinds.map((v) => v.toFixed(1)).join(' ')}${EXCLUDE[r.name] ? '   (not fitted)' : ''}`);
	}
	const out = {
		scale: 1,
		classes: { fp: +m[0].toFixed(4), mem: +m[1].toFixed(4), int: +m[2].toFixed(4), ctl: +m[3].toFixed(4), lib: 1 },
		fit: {
			engines: fitRows.length,
			loo_rms_points: +best.loo.toFixed(1),
			single_factor_loo_rms_points: +single.loo.toFixed(1),
			ridge: best.lam,
		},
		excluded: EXCLUDE,
		source: 'docs/PLAITS-ENGINES.md, "Re-measured on hardware with the M4a-M4d build, Marbles running"',
		measured_with: config,
	};
	fs.writeFileSync(cpuJson, JSON.stringify(out, null, 1) + '\n');
	console.log(`\nleave-one-out: ${best.loo.toFixed(1)} points (one factor for everything: ${single.loo.toFixed(1)}). Wrote ${path.relative(root, cpuJson)}.`);
}

// ---- --check: Wakes' own AUD line on the calibrated build ----
if (checkWasm) {
	const rows = await session(checkWasm, ({ run, logOf, engine }) => {
		const lines = () => [...logOf().matchAll(AUD)];
		const n0 = lines().length;
		while (lines().length < n0 + 2) { run(100); }   // skip the window that straddles the switch
		const m = lines().at(-1);
		if (Number(m[3]) !== engine.plaits) {
			throw new Error(`Wakes is on engine ${m[3]}, expected ${engine.plaits} (${engine.name})`);
		}
		return { emulator: Number(m[1]), peak: Number(m[2]), marbles: m[4] === '1' };
	});
	const model = JSON.parse(fs.readFileSync(cpuJson, 'utf8'));
	const all = hw.map((e, k) => ({ ...e, ...rows[k], error: +(rows[k].emulator - e.hardware).toFixed(1) }));
	const fitted = all.filter((r) => !EXCLUDE[r.name]);
	const rms = Math.sqrt(fitted.reduce((a, r) => a + r.error * r.error, 0) / fitted.length);
	const worst = fitted.reduce((a, r) => (Math.abs(r.error) > Math.abs(a.error) ? r : a));
	console.log(`engine             hardware   meter   error   (Wakes' AUD line; Marbles running: ${all.every((r) => r.marbles) ? 'yes' : 'NO'})`);
	for (const r of all) {
		console.log(`${r.name.padEnd(18)} ${r.hardware.toFixed(1).padStart(6)} % ${r.emulator.toFixed(1).padStart(6)} % ${(r.error >= 0 ? '+' : '') + r.error.toFixed(1)}${EXCLUDE[r.name] ? '   (not fitted: ' + EXCLUDE[r.name] + ')' : ''}`);
	}
	console.log(`\nfitted engines: rms ${rms.toFixed(1)} points, worst ${worst.name} ${worst.error > 0 ? '+' : ''}${worst.error}`);
	model.check = {
		rms_points: +rms.toFixed(1),
		worst: { name: worst.name, error: worst.error },
		engines: all.map(({ name, plaits, hardware, emulator, error }) => ({ name, plaits, hardware, emulator, error, fitted: !EXCLUDE[name] })),
		measured_with: config,
	};
	fs.writeFileSync(cpuJson, JSON.stringify(model, null, 1) + '\n');
}
