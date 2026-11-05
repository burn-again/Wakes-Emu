// app.js -- the bench page: draws the SP-1, wires its controls to the emulated board,
// plays the I2S stream, shows the CDC console and the WAKES drive.
import { WakesDevice, BTN, USB, RESETREAS, STOP, STOP_NAME, driveToJson, driveFromJson }
	from './emu-core.js';
import { renderMarkdown } from './md.js';

const $ = (id) => document.getElementById(id);
const DRIVE_KEY = 'wakes-emu-drive-v1';
const LOOKAHEAD_MS = 60;      // audio rendered ahead of the speaker
const MAX_CATCHUP_MS = 250;   // after a stall (hidden tab), skip rather than race

// ------------------------------------------------------------------ console
const term = $('term');
const MAX_LINES = 4000;
let lines = 0;
let pending = '';
function appendText(text, cls) {
	const span = document.createElement('span');
	if (cls) { span.className = cls; }
	span.textContent = text;
	term.appendChild(span);
	lines += (text.match(/\n/g) || []).length;
	while (lines > MAX_LINES && term.firstChild) {
		const t = term.firstChild.textContent;
		lines -= (t.match(/\n/g) || []).length;
		term.removeChild(term.firstChild);
	}
	if ($('follow').checked) { term.scrollTop = term.scrollHeight; }
}
function consoleOut(s) {
	pending += s;
	let i;
	while ((i = pending.indexOf('\n')) >= 0) {
		const line = pending.slice(0, i + 1);
		pending = pending.slice(i + 1);
		const warn = /^(!!|.*\b(FAULT|WATCHDOG|warn|fail(ed)?|error)\b)/i.test(line);
		appendText(line, warn ? 'w' : (/^\[(ON|STANDBY)\]|^(AUD|CPU|BLK) /.test(line) ? 'd' : ''));
		const m = /=== Wakes v(\S+) ===/.exec(line);
		if (m) { $('fw-version').textContent = `firmware v${m[1]}`; }
		followManual(line);
		recLine(line);
		cpuLine(line);
	}
}
function hostNote(s) { appendText(`── ${s} ──\n`, 'h'); }

// ------------------------------------------------------------------ device
let dev = null;
const state = { sync: null, lastBoots: 0 };

function setChip(kind, text) {
	$('state-chip').dataset.state = kind;
	$('state-text').textContent = text;
}

function onState(s, info) {
	if (s === 'on') {
		const r = info.resetreas;
		const why = r & RESETREAS.DOG ? 'after the watchdog' : r & RESETREAS.SREQ ? 'after a reset'
			: r & RESETREAS.VBUS ? 'woken by USB' : r & RESETREAS.OFF ? 'woken by ••' : 'cold start';
		hostNote(`power: nRF52840 boots (${why})`);
		$('sp1').dataset.off = '0';
		manual.module = 'plaits'; manual.side = 't'; manual.layer = 'base';
		if (rec.armed) { recStart(r); }
	} else {
		const why = info.why;
		$('sp1').dataset.off = '1';
		if (why === STOP.SYSTEM_OFF) {
			hostNote('SYSTEM_OFF: dark until •• or USB wakes it');
		} else {
			hostNote(`${STOP_NAME[why]}: the device restarts`);
		}
		saveDrive();
		renderFiles();
		if (rec.on) { recStop(why === STOP.SYSTEM_OFF ? 'the device switched off' : `the device stopped: ${STOP_NAME[why]}`); }
	}
}

async function loadWasm() {
	const resp = await fetch('wakes.wasm');
	if (!resp.ok) { throw new Error(`wakes.wasm: HTTP ${resp.status}. Build it with emulator/build.sh web.`); }
	const bytes = await resp.arrayBuffer();
	return WebAssembly.compile(bytes);
}

// ------------------------------------------------------------------ audio
const audio = { ctx: null, node: null, gain: null };
// The output stage lives in worklet.js: a FIFO the run loop fills, the audio thread drains.

async function startAudio() {
	if (audio.ctx) {
		await audio.ctx.resume();
		return;
	}
	const ctx = new AudioContext({ sampleRate: 48000, latencyHint: 'interactive' });
	await ctx.audioWorklet.addModule('worklet.js');
	const node = new AudioWorkletNode(ctx, 'wakes-out', { outputChannelCount: [2] });
	const gain = ctx.createGain();
	gain.gain.value = Number($('monitor').value);
	node.connect(gain).connect(ctx.destination);
	Object.assign(audio, { ctx, node, gain });
	state.sync = null;     // re-anchor the device clock to the audio clock
}

function setSound(on) {
	const b = $('sound-btn');
	b.setAttribute('aria-pressed', on ? 'true' : 'false');
	b.textContent = on ? 'Sound on' : 'Turn sound on';
	if (!on && audio.ctx) { audio.ctx.suspend(); }
}

$('sound-btn').addEventListener('click', async () => {
	const on = $('sound-btn').getAttribute('aria-pressed') !== 'true';
	if (on) {
		try { await startAudio(); setSound(true); } catch (e) { hostNote(`audio unavailable: ${e.message}`); }
	} else {
		setSound(false);
	}
});
$('monitor').addEventListener('input', (e) => {
	$('monitor-read').textContent = `${Math.round(e.target.value * 100)}%`;
	if (audio.gain) { audio.gain.gain.value = Number(e.target.value); }
});

// ------------------------------------------------------------------ the run loop
function clockMs() {
	return audio.ctx && audio.ctx.state === 'running' ? audio.ctx.currentTime * 1000 : performance.now();
}

let busy = false;
async function tick() {
	if (!dev || busy) { return; }
	busy = true;
	try {
		await dev.service();
		if (dev.state === 'on') {
			const aud = !!(audio.ctx && audio.ctx.state === 'running');
			if (!state.sync || state.sync.boots !== dev.boots || state.sync.audio !== aud) {
				state.sync = { boots: dev.boots, wall: clockMs(), dev: dev.nowMs(), audio: aud };
				if (audio.node) { audio.node.port.postMessage('flush'); }
			}
			let target = state.sync.dev + (clockMs() - state.sync.wall) + LOOKAHEAD_MS;
			if (target - dev.nowMs() > MAX_CATCHUP_MS) {
				state.sync = { boots: dev.boots, wall: clockMs(), dev: dev.nowMs(), audio: aud };
				target = dev.nowMs() + LOOKAHEAD_MS;
			}
			dev.runUntil(target);
			const a = dev.readAudio();
			if (a.left.length && audio.node && audio.ctx.state === 'running') {
				audio.node.port.postMessage({ l: a.left, r: a.right }, [a.left.buffer, a.right.buffer]);
			}
		}
	} finally {
		busy = false;
	}
}

// LEDs and the status chip, once per frame.
const tled = [0, 1, 2, 3].map((i) => $(`tled${i}`));
const pled = [0, 1, 2, 3].map((i) => $(`pled${i}`));
function frame() {
	if (dev) {
		const l = dev.leds();
		for (let i = 0; i < 4; i++) {
			tled[i].style.setProperty('--b', l[i] > 0 ? (0.12 + 0.88 * Math.pow(l[i], 0.45)).toFixed(3) : '0');
			pled[i].style.setProperty('--b', l[4 + i] > 0 ? (0.12 + 0.88 * Math.pow(l[4 + i], 0.45)).toFixed(3) : '0');
		}
		if (dev.state === 'on') {
			setChip('on', `running · ${(dev.nowMs() / 1000).toFixed(1)} s${cpu.mean !== null ? ` · CPU ${Math.round(cpu.mean)} %` : ''}`);
			$('hint').hidden = dev.nowMs() > 4000;
		} else if (dev.lastStop === STOP.SYSTEM_OFF) {
			setChip('off', 'off (SYSTEM_OFF)');
			$('hint').hidden = false;
		} else {
			setChip('reset', 'resetting');
		}
	}
	requestAnimationFrame(frame);
}

// ------------------------------------------------------------------ controls
const FNC = 'FNC';
const latched = new Set();     // keys held down by the page (right-click, Alt+click, Hold keys)
let holdMode = false;
const REC_NAME = { FNC: 'fnc', PLAY: 'play', T1: 't1', T2: 't2', T3: 't3', T4: 't4',
	VOL_UP: 'volup', VOL_DOWN: 'voldown', FFWD: 'ffwd', RWD: 'rwd' };

// A press shorter than two of Wakes' control scans (~18 ms) never registers, on the device
// as here. A finger cannot press that briefly, but a click or a tap can: hold every
// press for at least MIN_PRESS_MS before letting go.
const MIN_PRESS_MS = 45;
const downAt = {};
const releaseTimer = {};

function press(name, down) {
	if (!dev) { return; }
	clearTimeout(releaseTimer[name]);
	if (down) {
		downAt[name] = performance.now();
	} else {
		const held = performance.now() - (downAt[name] || 0);
		if (held < MIN_PRESS_MS) {
			releaseTimer[name] = setTimeout(() => applyPress(name, false), MIN_PRESS_MS - held);
			return;
		}
	}
	applyPress(name, down);
}

function applyPress(name, down) {
	if (!dev) { return; }
	if (!audio.ctx && down) {
		startAudio().then(() => setSound(true)).catch(() => {});
	}
	const was = name === FNC ? !!dev.inputs.fnc : !!(dev.inputs.buttons & (1 << BTN[name]));
	if (name === FNC) { dev.setFnc(down); } else { dev.setButton(BTN[name], down); }
	document.querySelectorAll(`[data-btn="${name}"]`).forEach((k) => { k.dataset.down = down ? '1' : '0'; });
	if (was !== down) { recEvent(REC_NAME[name], down ? 1 : 0); }
	if (name === FNC) { renderDeviceLabels(); }
	if (down) { tick(); }
}

function setLatched(name, on) {
	if (on) { latched.add(name); } else { latched.delete(name); }
	document.querySelectorAll(`[data-btn="${name}"]`).forEach((k) => { k.dataset.latched = on ? '1' : '0'; });
	$('let-go').disabled = latched.size === 0;
	$('hold-note').textContent = latched.size
		? `Holding ${[...latched].map(keyName).join(' + ')}. Now play the rest.`
		: holdMode ? 'Tap a key to hold it down; tap it again to let go.'
			: 'Right-click a key to hold it down; right-click again to let go.';
}

function toggleLatch(name) {
	if (latched.has(name)) {
		setLatched(name, false);
		press(name, false);
	} else {
		setLatched(name, true);
		press(name, true);
	}
}

function letGoAll() {
	for (const n of [...latched]) { setLatched(n, false); press(n, false); }
}

function keyName(n) {
	return { FNC: '••', VOL_UP: 'VOL+', VOL_DOWN: 'VOL−' }[n] || n;
}

document.querySelectorAll('[data-btn]').forEach((el) => {
	const name = el.dataset.btn;
	el.addEventListener('pointerdown', (e) => {
		e.preventDefault();
		// Right-click, Alt+click, or Hold keys: the key stays down until it is clicked
		// the same way again, so the other hand (the mouse) is free for the rest.
		if (e.button === 2 || e.altKey || holdMode) {
			toggleLatch(name);
			return;
		}
		if (e.button !== 0 || latched.has(name)) { return; }
		el.setPointerCapture(e.pointerId);
		press(name, true);
	});
	const up = () => { if (!latched.has(name) && el.dataset.down === '1') { press(name, false); } };
	el.addEventListener('pointerup', up);
	el.addEventListener('pointercancel', up);
	el.addEventListener('lostpointercapture', up);
	el.addEventListener('keydown', (e) => {
		if ((e.key === 'Enter' || e.key === ' ') && !e.repeat) { e.preventDefault(); e.stopPropagation(); press(name, true); }
	});
	el.addEventListener('keyup', (e) => {
		if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); e.stopPropagation(); if (!latched.has(name)) { press(name, false); } }
	});
	el.addEventListener('contextmenu', (e) => e.preventDefault());
});
document.querySelectorAll('.key.tk').forEach((k) => {
	const l = document.createElement('span');
	l.className = 'klab';
	k.appendChild(l);
});
$('hold-mode').addEventListener('click', () => {
	holdMode = !holdMode;
	$('hold-mode').setAttribute('aria-pressed', holdMode ? 'true' : 'false');
	$('hold-mode').textContent = holdMode ? 'Holding keys: on' : 'Hold keys';
	setLatched('_', false);
});
$('let-go').addEventListener('click', letGoAll);
$('sp1').addEventListener('contextmenu', (e) => e.preventDefault());

// The rocker is vertical on the left edge; VOL- and VOL+ sit side by side on the top edge.
const KEYMAP = { f: FNC, F: FNC, '1': 'T1', '2': 'T2', '3': 'T3', '4': 'T4', ' ': 'PLAY',
	ArrowUp: 'FFWD', ArrowDown: 'RWD', ArrowLeft: 'VOL_DOWN', ArrowRight: 'VOL_UP' };
// Leave a key to the focused control when that control uses it.
function typing(e) {
	const t = e.target;
	if (!t || !t.tagName) { return false; }
	if (t.tagName === 'TEXTAREA' || t.tagName === 'SELECT' || (t.tagName === 'INPUT' && t.type !== 'checkbox')) {
		return true;
	}
	if (t.getAttribute('role') === 'slider' && e.key.startsWith('Arrow')) { return true; }
	if ((t.tagName === 'BUTTON' || t.tagName === 'LABEL') && (e.key === ' ' || e.key === 'Enter')) { return true; }
	return false;
}
window.addEventListener('keydown', (e) => {
	if (e.key === 'Escape' && latched.size) { letGoAll(); return; }
	const name = KEYMAP[e.key];
	if (!name || typing(e) || e.metaKey || e.ctrlKey) { return; }
	e.preventDefault();
	if (!e.repeat && !latched.has(name)) { press(name, true); }
});
window.addEventListener('keyup', (e) => {
	const name = KEYMAP[e.key];
	if (!name || typing(e)) { return; }
	e.preventDefault();
	if (!latched.has(name)) { press(name, false); }
});
window.addEventListener('blur', () => {   // a key held when the window loses focus
	Object.keys(BTN).concat([FNC]).forEach((n) => { if (!latched.has(n)) { press(n, false); } });
});

// faders
const faders = [];
for (let i = 0; i < 4; i++) {
	const f = document.createElement('div');
	f.className = 'fader';
	f.tabIndex = 0;
	f.setAttribute('role', 'slider');
	f.setAttribute('aria-label', `Fader ${i + 1}`);
	f.setAttribute('aria-orientation', 'vertical');
	f.setAttribute('aria-valuemin', '0');
	f.setAttribute('aria-valuemax', '100');
	f.innerHTML = '<span class="flab"></span><span class="slot"></span><span class="knob"></span><span class="val"></span>';
	$('faders').appendChild(f);
	faders.push(f);
	const setFrom = (clientY) => {
		const r = f.querySelector('.slot').getBoundingClientRect();
		setFader(i, 1 - (clientY - r.top) / r.height);
	};
	f.addEventListener('pointerdown', (e) => {
		e.preventDefault(); f.focus(); f.setPointerCapture(e.pointerId); setFrom(e.clientY);
	});
	f.addEventListener('pointermove', (e) => { if (f.hasPointerCapture(e.pointerId)) { setFrom(e.clientY); } });
	f.addEventListener('wheel', (e) => { e.preventDefault(); setFader(i, fv[i] - Math.sign(e.deltaY) * 0.02); }, { passive: false });
	f.addEventListener('keydown', (e) => {
		const step = e.shiftKey ? 0.1 : 0.01;
		if (e.key === 'ArrowUp' || e.key === 'ArrowRight') { e.preventDefault(); setFader(i, fv[i] + step); }
		if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') { e.preventDefault(); setFader(i, fv[i] - step); }
		if (e.key === 'Home') { e.preventDefault(); setFader(i, 0); }
		if (e.key === 'End') { e.preventDefault(); setFader(i, 1); }
	});
}
const fv = [0.5, 0.5, 0.5, 0.5];
function setFader(i, v) {
	v = Math.min(1, Math.max(0, v));
	fv[i] = v;
	const f = faders[i];
	f.querySelector('.knob').style.top = `calc(4px + ${(1 - v).toFixed(4)} * (100% - 8px))`;
	f.querySelector('.val').textContent = Math.round(v * 4095);
	f.setAttribute('aria-valuenow', String(Math.round(v * 100)));
	if (dev) { dev.setFader(i, v); }
	recEvent(`f${i + 1}`, v.toFixed(3));
}

// bench supply
document.querySelectorAll('#usb-seg button').forEach((b) => {
	b.addEventListener('click', () => {
		const mode = Number(b.dataset.usb);
		document.querySelectorAll('#usb-seg button').forEach((x) => x.setAttribute('aria-pressed', x === b ? 'true' : 'false'));
		if (dev) { dev.setUsb(mode); tick(); }
		recEvent('usb', mode);
		midiNote();
	});
});
$('batt').addEventListener('input', (e) => {
	const mv = Number(e.target.value);
	$('batt-read').textContent = `${(mv / 1000).toFixed(2)} V`;
	if (dev) { dev.setBattery(mv); }
	recEvent('batt', mv);
});

// MIDI: an on-screen octave, and Web MIDI inputs where the browser allows them
function midiNote() {
	const usb = dev ? dev.inputs.usb : 0;
	$('midi-note').textContent = usb === USB.COMPUTER
		? 'Wakes is enumerated as a USB-MIDI port: notes play Plaits, CCs move parameters.'
		: 'Notes reach Wakes only while USB is set to Computer, as on the device.';
}
function sendMidi(bytes) {
	if (!dev) { return; }
	dev.sendMidi(bytes);
	if (dev.inputs.usb === USB.COMPUTER) {
		recEvent('midi', bytes.map((b) => b.toString(16).padStart(2, '0')).join(','));
	}
}
const piano = $('piano');
const WHITE = [0, 2, 4, 5, 7, 9, 11, 12];
const BLACK = [[1, 0], [3, 1], [6, 3], [8, 4], [10, 5]];
function pianoKey(semi, black, leftPct) {
	const b = document.createElement('button');
	b.type = 'button';
	b.className = black ? 'black' : '';
	b.setAttribute('aria-label', `MIDI note ${60 + semi}`);
	if (black) { b.style.left = `${leftPct}%`; }
	const on = (e) => { e.preventDefault(); b.setPointerCapture(e.pointerId); b.dataset.down = '1'; sendMidi([0x90, 60 + semi, 100]); };
	const off = () => { if (b.dataset.down === '1') { b.dataset.down = '0'; sendMidi([0x80, 60 + semi, 0]); } };
	b.addEventListener('pointerdown', on);
	b.addEventListener('pointerup', off);
	b.addEventListener('pointercancel', off);
	piano.appendChild(b);
}
WHITE.forEach((s) => pianoKey(s, false));
BLACK.forEach(([s, after]) => pianoKey(s, true, ((after + 1) / WHITE.length) * 100 - 3));

let midiAccess = null;
$('midi-connect').addEventListener('click', async () => {
	if (!navigator.requestMIDIAccess) {
		$('midi-note').textContent = 'This browser has no Web MIDI. The on-screen keys still work.';
		return;
	}
	try {
		midiAccess = await navigator.requestMIDIAccess({ sysex: false });
	} catch (e) {
		$('midi-note').textContent = 'MIDI access was refused here. The on-screen keys still work; a page served from your own machine can use a keyboard.';
		return;
	}
	const sel = $('midi-in');
	const fill = () => {
		const cur = sel.value;
		sel.innerHTML = '<option value="">No MIDI input</option>';
		for (const inp of midiAccess.inputs.values()) {
			const o = document.createElement('option');
			o.value = inp.id; o.textContent = inp.name;
			sel.appendChild(o);
		}
		sel.value = cur;
	};
	fill();
	midiAccess.onstatechange = fill;
	if (sel.options.length > 1 && !sel.value) { sel.selectedIndex = 1; sel.dispatchEvent(new Event('change')); }
});
$('midi-in').addEventListener('change', () => {
	if (!midiAccess) { return; }
	for (const inp of midiAccess.inputs.values()) {
		inp.onmidimessage = inp.id === $('midi-in').value
			? (m) => { if (m.data[0] !== 0xFE) { sendMidi(Array.from(m.data)); } } : null;
	}
});

// console buttons
$('clear-log').addEventListener('click', () => { term.textContent = ''; lines = 0; });
$('copy-log').addEventListener('click', async () => {
	try { await navigator.clipboard.writeText(term.textContent); $('copy-log').textContent = 'Copied'; }
	catch { const r = document.createRange(); r.selectNodeContents(term); const s = getSelection(); s.removeAllRanges(); s.addRange(r); $('copy-log').textContent = 'Selected'; }
	setTimeout(() => { $('copy-log').textContent = 'Copy'; }, 1500);
});

// ------------------------------------------------------------------ the WAKES drive
let selected = null;
let uploadDir = null;        // the folder "Add files…" writes into (a folder row clicked)
let dirty = false;
const dec = new TextDecoder();

function saveDrive() {
	if (!dev) { return; }
	try {
		let json = driveToJson(dev.drive);
		if (json.length > 4e6) {               // keep within localStorage: drop the logs
			const m = new Map([...dev.drive].filter(([p]) => !/\/LOGS\//i.test(p)));
			json = driveToJson(m);
		}
		localStorage.setItem(DRIVE_KEY, json);
		$('drive-note').textContent = 'Kept in this browser between visits.';
	} catch {
		$('drive-note').textContent = 'This browser is not keeping the drive between visits (storage is blocked).';
	}
}

function loadSavedDrive() {
	try {
		const j = localStorage.getItem(DRIVE_KEY);
		return j ? driveFromJson(j) : null;
	} catch {
		return null;
	}
}

function fmtSize(n) { return n < 1024 ? `${n} B` : `${(n / 1024).toFixed(n < 10240 ? 1 : 0)} KB`; }

function uploadTarget() {
	return uploadDir || (selected ? selected.slice(0, selected.lastIndexOf('/')) : '/WAKES/CNFG');
}

function renderFiles() {
	if (!dev) { return; }
	const box = $('files');
	const all = [...dev.drive.entries()];
	const entries = all.filter(([, f]) => !f.dir).sort((a, b) => a[0].localeCompare(b[0]));
	// Empty folders get a row too (WAKES/MIDI/1 .. 4 are made empty, for files to go in).
	for (const [p, f] of all) {
		if (f.dir && !all.some(([q]) => q.startsWith(p + '/'))) {
			entries.push([p, f]);
		}
	}
	entries.sort((a, b) => (a[1].dir ? a[0] + '/' : a[0]).localeCompare(b[1].dir ? b[0] + '/' : b[0]));
	const target = uploadTarget();
	box.textContent = '';
	if (!entries.length) {
		const e = document.createElement('div');
		e.className = 'empty';
		e.textContent = 'Empty. Switch Wakes on once and it writes WAKES/CNFG with its defaults.';
		box.appendChild(e);
	}
	let lastDir = null;
	const dirRow = (dir, empty) => {
		const d = document.createElement('button');
		d.type = 'button';
		d.className = 'dir';
		d.dataset.dir = dir;
		d.setAttribute('aria-pressed', dir === target ? 'true' : 'false');
		d.title = 'Add files here';
		d.textContent = dir.replace(/^\//, '') + '/' + (empty ? '  (empty)' : '');
		d.addEventListener('click', () => { uploadDir = dir; renderFiles(); });
		box.appendChild(d);
	};
	for (const [p, f] of entries) {
		if (f.dir) {
			dirRow(p, true);
			lastDir = p;
			continue;
		}
		const dir = p.slice(0, p.lastIndexOf('/')) || '/';
		if (dir !== lastDir) {
			dirRow(dir, false);
			lastDir = dir;
		}
		const r = document.createElement('button');
		r.type = 'button';
		r.className = 'row';
		r.setAttribute('role', 'option');
		r.setAttribute('aria-selected', p === selected ? 'true' : 'false');
		r.innerHTML = '<span class="nm"></span><span class="sz"></span>';
		r.querySelector('.nm').textContent = p.slice(p.lastIndexOf('/') + 1);
		r.querySelector('.sz').textContent = fmtSize(f.data.length);
		r.addEventListener('click', () => openFile(p));
		box.appendChild(r);
	}
	const files = entries.filter(([, f]) => !f.dir);
	const total = files.reduce((s, [, f]) => s + f.data.length, 0);
	$('drive-sub').textContent = `${files.length} files · ${fmtSize(total)}`;
	$('upload-label').textContent = `Add files to ${target.replace(/^\//, '')}/…`;
	if (selected && !dirty && dev.drive.has(selected)) { showFile(selected); }
}

function showFile(p) {
	const f = dev.drive.get(p);
	const ed = $('file-editor');
	if (!f) { return; }
	const text = dec.decode(f.data);
	const binary = text.includes('\u0000');
	const atBottom = ed.scrollTop + ed.clientHeight >= ed.scrollHeight - 4;
	ed.value = binary ? `(binary, ${f.data.length} bytes)` : text;
	ed.readOnly = binary;
	if (/\/LOGS\//i.test(p) && atBottom) { ed.scrollTop = ed.scrollHeight; }
	$('file-path').textContent = p.replace(/^\//, '');
	$('file-save').disabled = binary || !dirty;
	$('file-copy').disabled = false;
}

function openFile(p) {
	selected = p;
	uploadDir = null;
	dirty = false;
	showFile(p);
	renderFiles();
}

$('file-editor').addEventListener('input', () => {
	if (!selected) { return; }
	dirty = true;
	$('file-save').disabled = false;
	$('file-path').textContent = `${selected.replace(/^\//, '')} (edited)`;
});
$('file-save').addEventListener('click', () => {
	if (!selected || !dev) { return; }
	dev.writeFile(selected, $('file-editor').value);
	dirty = false;
	$('file-save').disabled = true;
	$('file-path').textContent = selected.replace(/^\//, '');
	hostNote(`drive: saved ${selected.replace(/^\//, '')}${dev.state === 'on' ? ' (Wakes reads CNFG at the next ON)' : ''}`);
	saveDrive();
	renderFiles();
});
$('file-copy').addEventListener('click', async () => {
	try { await navigator.clipboard.writeText($('file-editor').value); $('file-copy').textContent = 'Copied'; }
	catch { $('file-editor').select(); $('file-copy').textContent = 'Selected'; }
	setTimeout(() => { $('file-copy').textContent = 'Copy'; }, 1500);
});
$('upload').addEventListener('change', async (e) => {
	const folder = uploadTarget();
	for (const file of e.target.files) {
		const data = new Uint8Array(await file.arrayBuffer());
		dev.writeFile(`${folder}/${file.name}`, data);
		hostNote(`drive: added ${folder.replace(/^\//, '')}/${file.name}`);
	}
	e.target.value = '';
	saveDrive();
	renderFiles();
});
let eraseArmed = null;
$('erase').addEventListener('click', () => {
	const b = $('erase');
	if (!eraseArmed) {
		b.textContent = 'Click again to erase';
		eraseArmed = setTimeout(() => { eraseArmed = null; b.textContent = 'Erase drive'; }, 3000);
		return;
	}
	clearTimeout(eraseArmed);
	eraseArmed = null;
	b.textContent = 'Erase drive';
	dev.eraseDrive();
	selected = null;
	uploadDir = null;
	$('file-editor').value = '';
	$('file-path').textContent = 'No file selected';
	hostNote('drive: erased (Wakes writes fresh CNFG defaults at the next ON)');
	saveDrive();
	renderFiles();
});

// Refresh the drive view while Wakes writes to it (logs, presets).
setInterval(() => {
	if (dev && dev.snapshotDrive()) {
		renderFiles();
	}
}, 1000);
setInterval(saveDrive, 5000);

// ------------------------------------------------------------------ CPU meter
// The wasm build adds every basic block's estimated Cortex-M4F cost to DWT->CYCCNT, the
// counter Wakes times its audio with (tools/wasm_cycles.py, calibrated by
// tools/calibrate.sh against Wakes' hardware measurements). The kernel charges each thread
// what it counted while it ran; this samples the audio thread's cost per block.
const cpu = { mean: null, peak: null, hist: [], model: null };
const CPU_HIST = 240;                  // 60 s at 4 samples a second

function cpuLevel(mean) {
	if (mean === null) { return ['idle', 'off']; }
	if (mean >= 100) { return ['over', 'would drop audio']; }
	if (mean >= 85) { return ['high', 'little headroom']; }
	if (mean >= 70) { return ['tight', 'tight']; }
	return ['ok', 'fits'];
}

function sampleCpu() {
	const ex = dev && dev.state === 'on' ? dev.ex : null;
	if (!ex || !ex.emu_cpu) {
		cpu.mean = cpu.peak = null;
		renderCpu();
		return;
	}
	const blocks = ex.emu_cpu(1);
	const frames = ex.emu_audio_block_frames();
	const wall = ex.emu_cpu(4);
	if (blocks < 1 || !frames || wall <= 0) {
		cpu.mean = cpu.peak = null;
		ex.emu_cpu_window_reset();
		renderCpu();
		return;
	}
	const budget = frames * 64e6 / 48000;
	cpu.mean = 100 * ex.emu_cpu(0) / blocks / budget;
	cpu.peak = 100 * ex.emu_cpu(2) / budget;
	cpu.blockMs = frames / 48;
	const share = (k) => 100 * ex.emu_cpu(k) / wall;
	cpu.threads = { audio: share(0), main: share(5), store: share(6), all: share(3) };
	ex.emu_cpu_window_reset();
	cpu.hist.push([cpu.mean, cpu.peak]);
	if (cpu.hist.length > CPU_HIST) { cpu.hist.shift(); }
	renderCpu();
}

function renderCpu() {
	const [level, word] = cpuLevel(cpu.mean);
	$('cpu-body').dataset.level = level;
	$('cpu-pill').dataset.level = level;
	$('cpu-pill').textContent = word;
	if (cpu.mean === null) {
		$('cpu-mean').textContent = '–';
		$('cpu-peak').textContent = '–';
		$('cpu-fill').style.width = '0';
		$('cpu-pk').style.left = '0';
		$('cpu-threads').textContent = dev && dev.state === 'on' ? 'waiting for audio' : 'the device is off';
	} else {
		const pos = (v) => `${Math.min(100, v / 1.2).toFixed(2)}%`;      // the bar spans 0-120 %
		$('cpu-mean').textContent = cpu.mean.toFixed(0);
		$('cpu-peak').textContent = `${cpu.peak.toFixed(0)} %`;
		$('cpu-block').textContent = `${+cpu.blockMs.toFixed(2)} ms`;
		$('cpu-fill').style.width = pos(cpu.mean);
		$('cpu-pk').style.left = pos(cpu.peak);
		const t = cpu.threads;
		$('cpu-threads').textContent = `audio ${t.audio.toFixed(0)} · main ${t.main.toFixed(1)} · storage ${t.store.toFixed(1)} · idle ${Math.max(0, 100 - t.all).toFixed(0)} (%)`;
	}
	drawSpark();
}

function drawSpark() {
	const c = $('cpu-spark');
	const dpr = window.devicePixelRatio || 1;
	const w = c.clientWidth, h = c.clientHeight;
	if (!w || !h) { return; }
	if (c.width !== Math.round(w * dpr)) { c.width = Math.round(w * dpr); c.height = Math.round(h * dpr); }
	const g = c.getContext('2d');
	g.setTransform(dpr, 0, 0, dpr, 0, 0);
	g.clearRect(0, 0, w, h);
	const css = getComputedStyle(document.documentElement);
	const tok = (n) => css.getPropertyValue(n).trim();
	const y = (v) => h - 2 - (Math.min(v, 120) / 120) * (h - 4);
	g.lineWidth = 1;
	g.setLineDash([3, 3]);
	for (const [v, col] of [[85, tok('--warn')], [100, tok('--signal')]]) {
		g.strokeStyle = col; g.globalAlpha = 0.7;
		g.beginPath(); g.moveTo(0, y(v)); g.lineTo(w, y(v)); g.stroke();
	}
	g.setLineDash([]);
	g.globalAlpha = 1;
	const n = cpu.hist.length;
	if (n < 2) { return; }
	const x = (i) => w - (n - 1 - i) * (w / (CPU_HIST - 1));
	g.beginPath();
	g.moveTo(x(0), h);
	cpu.hist.forEach(([m], i) => g.lineTo(x(i), y(m)));
	g.lineTo(x(n - 1), h);
	g.closePath();
	g.fillStyle = tok('--ok'); g.globalAlpha = 0.22; g.fill();
	g.globalAlpha = 1;
	g.beginPath();
	cpu.hist.forEach(([m], i) => (i ? g.lineTo(x(i), y(m)) : g.moveTo(x(i), y(m))));
	g.strokeStyle = tok('--ok'); g.lineWidth = 1.5; g.stroke();
	g.beginPath();
	cpu.hist.forEach(([, p], i) => (i ? g.lineTo(x(i), y(p)) : g.moveTo(x(i), y(p))));
	g.strokeStyle = tok('--ink-2'); g.lineWidth = 1; g.stroke();
	const last = cpu.hist[n - 1][0];
	g.fillStyle = tok('--ink');
	g.beginPath(); g.arc(x(n - 1) - 2, y(last), 2.5, 0, Math.PI * 2); g.fill();
}

// Wakes' own 5-second report: its AUD line (the figure the hardware table quotes) and its
// CPU line, which splits the block into the engine, Marbles, routing, after and output.
function cpuLine(line) {
	let m;
	if ((m = /^AUD run=\d+ .*?cyc avg=([\d.]+)% max=([\d.]+)%/.exec(line))) {
		$('cpu-wakes').textContent = `AUD: mean ${(+m[1]).toFixed(1)} %, worst block ${(+m[2]).toFixed(1)} % (last 5 s)`;
	} else if ((m = /^CPU eng=([\d.]+)\/[\d.]+ mrb=([\d.]+)\/[\d.]+ rte=([\d.]+)\/[\d.]+ post=([\d.]+)\/[\d.]+ out=([\d.]+)/.exec(line))) {
		$('cpu-sections').textContent = `engine ${m[1]} · Marbles ${m[2]} · routing ${m[3]} · after ${m[4]} · output ${m[5]} (% of the block)`;
	}
}

async function loadCpuModel() {
	try {
		const r = await fetch('cpu.json');
		if (r.ok) { cpu.model = await r.json(); }
	} catch { /* shown below */ }
	const body = $('cpu-acc-body');
	body.textContent = '';
	const m = cpu.model;
	const p = (text) => { const e = document.createElement('p'); e.textContent = text; body.appendChild(e); };
	if (!m || !m.check) {
		$('cpu-acc-sum').textContent = 'How close is this to the device? (uncalibrated)';
		p('This build has no calibration (emulator/cpu.json): the figures are raw instruction-cost estimates. Run emulator/tools/calibrate.sh.');
		return;
	}
	const c = m.check, f = m.fit;
	$('cpu-acc-sum').textContent = `How close is this to the device? Typically within ±${Math.round(f.loo_rms_points)} points.`;
	p(`Each piece of code adds its estimated cost on the SP-1's Cortex-M4F to the cycle counter Wakes times its audio with, so Wakes' own AUD, CPU and THREADS lines read it too. The weights were fitted to ${f.engines} engines measured on an SP-1 (docs/PLAITS-ENGINES.md, with Marbles running): ${c.rms_points} points RMS across them, ${f.loo_rms_points} on an engine left out of the fit, worst ${c.worst.name} at ${c.worst.error > 0 ? '+' : ''}${c.worst.error}.`);
	p('Read it as ± that much. Code is not timed here, only counted: interrupts, flash wait states and the cache are averaged into the weights, not modelled. Before you rely on less than ~15 points of headroom, measure on the device.');
	const tbl = document.createElement('div');
	tbl.className = 'tbl';
	const rows = c.engines.map((e) => `<tr class="${e.fitted ? '' : 'out'}"><td>${e.name}${e.fitted ? '' : ' *'}</td><td>${e.hardware.toFixed(1)}</td><td>${e.emulator.toFixed(1)}</td><td>${e.error > 0 ? '+' : ''}${e.error.toFixed(1)}</td></tr>`).join('');
	tbl.innerHTML = `<table><thead><tr><th>engine</th><th>SP-1 %</th><th>meter %</th><th>error</th></tr></thead><tbody>${rows}</tbody></table>`;
	body.appendChild(tbl);
	p(`Measured as the hardware was: ${c.measured_with}. * Not fitted: ${Object.entries(m.excluded || {}).map(([k, v]) => `${k}, ${v}`).join('; ')}.`);
}

setInterval(sampleCpu, 250);
window.addEventListener('resize', drawSpark);

// ------------------------------------------------------------------ manual + reference
const UPSTREAM = 'https://github.com/Worldwave/Wakes/blob/main/';
const manual = { pages: [], cur: 0, docs: [], doc: null, module: 'plaits', side: 't', layer: 'base' };
const prefs = { labels: true, play: false };
try { Object.assign(prefs, JSON.parse(localStorage.getItem('wakes-emu-prefs') || '{}')); } catch { /* defaults */ }
function savePrefs() { try { localStorage.setItem('wakes-emu-prefs', JSON.stringify(prefs)); } catch { /* not kept */ } }
const PAGE_RE = {
	plaits: /plaits.*base/i, plaitsSettings: /plaits.*settings/i,
	marblesT: /marbles.*\bt\b/i, marblesX: /marbles.*\bx\b/i, marblesSettings: /marbles.*settings/i,
};

function pageNo(key) {
	const p = manual.pages.find((x) => PAGE_RE[key].test(x.title));
	return p ? p.n : 0;
}

function showPage(n, why) {
	const p = manual.pages.find((x) => x.n === n);
	if (!p) { return; }
	manual.cur = n;
	const img = $('manual-img');
	img.src = `manual/${p.img}`;
	img.alt = `Wakes manual, page ${n}: ${p.title}`;
	img.hidden = false;
	document.querySelectorAll('#manual-tabs button').forEach((b) => {
		b.setAttribute('aria-selected', Number(b.dataset.n) === n ? 'true' : 'false');
	});
	$('manual-caption').textContent = `Pg. ${n} · ${p.title}${why ? ` · ${why}` : ''}`;
	renderHotspots();
	renderDeviceLabels();
}

// Where Wakes is (module, Marbles side, layer), from what it prints on its console.
// Returns the manual page that goes with it, when the line moved it.
function trackDevice(line) {
	let m;
	if ((m = /^SYN plaits .*module=(\w+)/.exec(line))) {
		manual.module = m[1]; manual.layer = 'base';
		return [m[1] === 'marbles' ? 'marblesT' : 'plaits', `Wakes starts on ${m[1] === 'marbles' ? 'Marbles' : 'Plaits'}`];
	}
	if (/^MODULE plaits/.test(line)) {
		manual.module = 'plaits'; manual.layer = 'base';
		return ['plaits', 'Wakes switched to Plaits'];
	}
	if ((m = /^(?:MODULE marbles \(|MARBLES )(t|X) page/.exec(line))) {
		manual.module = 'marbles'; manual.side = m[1]; manual.layer = 'base';
		return [m[1] === 'X' ? 'marblesX' : 'marblesT', `Marbles, ${m[1]} side`];
	}
	if (/^PAGE settings/.test(line)) {
		manual.layer = 'settings';
		return [manual.module === 'marbles' ? 'marblesSettings' : 'plaitsSettings', 'SETTINGS'];
	}
	if (/^(PAGE base|SHIFT released -> base)/.test(line) && manual.module === 'plaits') {
		const left = manual.layer === 'settings';
		manual.layer = 'base';
		return left ? ['plaits', 'back to Plaits BASE'] : null;
	}
	return null;
}

function followManual(line) {
	const hit = trackDevice(line);
	if (!hit || !manual.pages.length || !$('manual-follow').checked) { return; }
	const n = pageNo(hit[0]);
	if (n && n !== manual.cur) { showPage(n, `following Wakes: ${hit[1]}`); }
}

// ---- the manual's names on the emulated device ----
const HW = { top: ['VOL−', 'VOL+'], rocker: { up: 'FFWD', down: 'RWD' }, edge: { play: 'PLAY', fnc: '••' } };
function renderDeviceLabels() {
	const p = manual.pages.find((x) => x.n === manual.cur);
	const shift = !!(dev && dev.inputs.fnc && p && p.shift);
	const g = prefs.labels && p ? (shift ? p.shift : p.base) : null;
	$('sp1').dataset.layer = g && shift ? 'shift' : 'base';
	faders.forEach((f, i) => { f.querySelector('.flab').textContent = (g && g.faders[i].label) || ''; });
	document.querySelectorAll('.key.tk').forEach((k, i) => {
		const t = g && g.keys[i].label;
		k.querySelector('.klab').textContent = t === '-' ? '–' : (t || '');
	});
	const set = (sel, text) => { const e = document.querySelector(sel); if (e) { e.textContent = text; } };
	set('.ek.top[data-btn="VOL_DOWN"] .elabel', (g && g.top[0] && g.top[0].label) || HW.top[0]);
	set('.ek.top[data-btn="VOL_UP"] .elabel', (g && g.top[1] && g.top[1].label) || HW.top[1]);
	set('.ek.left.up .elabel', (g && g.rocker.up && g.rocker.up.label) || HW.rocker.up);
	set('.ek.left.down .elabel', (g && g.rocker.down && g.rocker.down.label) || HW.rocker.down);
	set('.ek.right[data-btn="PLAY"] .elabel', (g && g.edge.play && g.edge.play.label) || HW.edge.play);
	set('.ek.right[data-btn="FNC"] .elabel', (g && g.edge.fnc && g.edge.fnc.label) || HW.edge.fnc);
	$('led-note').textContent = (g && g.leds) || '';
}

// ---- clicks on the manual play the device (off unless chosen) ----
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
let performing = false;

function playToast(text) {
	$('play-toast').textContent = text;
	clearTimeout(playToast.t);
	playToast.t = setTimeout(() => { $('play-toast').textContent = ''; }, 4000);
}

function deviceReady() {
	if (dev && dev.state === 'on') { return true; }
	playToast('Switch Wakes on first: hold •• for 1.5 s.');
	return false;
}

async function tapKey(name, ms = 110) {
	if (latched.has(name)) { return; }
	press(name, true);
	await wait(ms);
	press(name, false);
}

async function withShift(fn) {
	const held = latched.has(FNC);
	if (!held) { press(FNC, true); await wait(90); }
	await fn();
	if (!held) { await wait(90); press(FNC, false); }
}

// One gesture: { shift, ctl } with ctl a data-btn name.
async function perform(g) {
	if (performing || !deviceReady()) { return; }
	performing = true;
	try {
		if (g.shift) { await withShift(() => tapKey(g.ctl)); } else { await tapKey(g.ctl); }
	} finally {
		performing = false;
	}
}

async function doubleTapFnc() {
	await tapKey(FNC, 80);
	await wait(120);
	await tapKey(FNC, 80);
}

// Walk the device to a page of the manual, the way a player would.
async function goTo(key) {
	if (performing || !deviceReady()) { return; }
	performing = true;
	try {
		const leaveSettings = async () => {
			if (manual.layer === 'settings') { await tapKey(FNC); await wait(450); manual.layer = 'base'; }
		};
		const toModule = async (mod) => {
			await leaveSettings();
			if (manual.module !== mod) { await tapKey('T4'); await wait(450); }
		};
		if (key === 'plaits') {
			await toModule('plaits');
		} else if (key === 'plaitsSettings') {
			await toModule('plaits');
			await doubleTapFnc();
			manual.layer = 'settings';
		} else if (key === 'marblesT' || key === 'marblesX') {
			await toModule('marbles');
			const want = key === 'marblesX' ? 'X' : 't';
			if (manual.side !== want) { await tapKey(want === 'X' ? 'T3' : 'T2'); await wait(300); }
		} else if (key === 'marblesSettings') {
			await toModule('marbles');
			await doubleTapFnc();
			manual.layer = 'settings';   // Wakes does not print this one: keep it ourselves
		}
	} finally {
		performing = false;
	}
}

function pageKey(n) {
	const p = manual.pages.find((x) => x.n === n);
	if (!p) { return null; }
	for (const k of Object.keys(PAGE_RE)) {
		if (PAGE_RE[k].test(p.title)) { return k; }
	}
	return null;
}

const CTL_NAME = { PLAY: 'PLAY', T1: 'T1', T2: 'T2', T3: 'T3', T4: 'T4', FFWD: 'FFWD', RWD: 'RWD',
	'VOL+': 'VOL_UP', 'VOL−': 'VOL_DOWN', 'VOL-': 'VOL_DOWN', '••': FNC };

function renderHotspots() {
	const layer = $('manual-hot');
	layer.textContent = '';
	const p = manual.pages.find((x) => x.n === manual.cur);
	if (!prefs.play || !p) { return; }
	const pct = (v) => `${(v * 100).toFixed(3)}%`;
	const add = (cls, b, label, onDown) => {
		const h = document.createElement('button');
		h.type = 'button';
		h.className = `hs ${cls}`;
		Object.assign(h.style, { left: pct(b[0]), top: pct(b[1]), width: pct(b[2] - b[0]), height: pct(b[3] - b[1]) });
		h.title = label;
		h.setAttribute('aria-label', label);
		h.addEventListener('pointerdown', (e) => { e.preventDefault(); onDown(e, h); });
		h.addEventListener('contextmenu', (e) => e.preventDefault());
		layer.appendChild(h);
		return h;
	};
	for (const which of ['base', 'shift']) {
		const g = p[which];
		if (!g) { continue; }
		const shift = which === 'shift';
		const cls = shift ? 'shift' : '';
		const say = (ctl, name) => `${shift ? 'Hold •• and press' : 'Press'} ${keyName(ctl)}${name && name !== '-' ? ` (${name})` : ''}`;
		const tap = (ctl) => () => perform({ shift, ctl });
		g.keys.forEach((k, i) => add(cls, [k.x - 0.0095, k.y0, k.x + 0.0095, k.y1], say(`T${i + 1}`, k.label), tap(`T${i + 1}`)));
		(g.top || []).forEach((t, i) => {
			const ctl = i === 0 ? 'VOL_DOWN' : 'VOL_UP';
			add(cls, t.box, say(ctl, t.label), tap(ctl));
		});
		if (g.rocker.up) { add(cls, g.rocker.up.box, say('FFWD', g.rocker.up.label), tap('FFWD')); }
		if (g.rocker.down) { add(cls, g.rocker.down.box, say('RWD', g.rocker.down.label), tap('RWD')); }
		if (g.edge.play) { add(cls, g.edge.play.box, say('PLAY', g.edge.play.label), tap('PLAY')); }
		if (g.edge.fnc && !shift) { add(cls, g.edge.fnc.box, 'Tap ••', tap(FNC)); }
		// Faders: press and drag on the manual's fader to move the device's.
		g.faders.forEach((f, i) => {
			const name = f.label ? ` (${f.label})` : '';
			add(`${cls} fdr`, [f.x - 0.009, f.y0, f.x + 0.009, f.y1],
				`${shift ? 'Hold •• and move' : 'Move'} fader ${i + 1}${name}`, (e, h) => {
					if (!deviceReady()) { return; }
					const r = h.getBoundingClientRect();
					const at = (y) => setFader(i, 1 - Math.min(1, Math.max(0, (y - r.top) / r.height)));
					const held = latched.has(FNC);
					if (shift && !held) { press(FNC, true); }
					h.setPointerCapture(e.pointerId);
					at(e.clientY);
					const move = (ev) => at(ev.clientY);
					const done = () => {
						h.removeEventListener('pointermove', move);
						if (shift && !held) { setTimeout(() => press(FNC, false), 60); }
					};
					h.addEventListener('pointermove', move);
					h.addEventListener('pointerup', done, { once: true });
					h.addEventListener('pointercancel', done, { once: true });
				});
		});
	}
}

// Gestures named in the reference tables ("•• + T2", "VOL− / VOL+", "T1–T4"...)
function parseGesture(text) {
	let t = text.replace(/\s+/g, ' ').trim()
		.replace(/^(PLAITS|MARBLES|SETTINGS)( (t|X) page)? /i, '').replace(/, either page$/i, '');
	let shift = false;
	const m = /^••\s*\+\s*(.+)$/.exec(t);
	if (m) { shift = true; t = m[1]; }
	const ctls = [];
	for (const part of t.split(/\s*\/\s*/)) {
		const r = /^T([1-4])\s*[–-]\s*T([1-4])$/.exec(part);
		if (r) {
			for (let k = Number(r[1]); k <= Number(r[2]); k++) { ctls.push(`T${k}`); }
		} else if (CTL_NAME[part] && !(shift && part === '••')) {
			ctls.push(CTL_NAME[part]);
		} else {
			return null;
		}
	}
	return ctls.length ? { shift, ctls } : null;
}

function decorateGestures(root) {
	root.querySelectorAll('table tr td:first-child').forEach((td) => {
		const g = parseGesture(td.textContent);
		if (!g) { return; }
		const row = document.createElement('span');
		row.className = 'try-row';
		for (const ctl of g.ctls) {
			const b = document.createElement('button');
			b.type = 'button';
			b.className = 'try';
			b.textContent = `▶ ${g.shift ? '•• + ' : ''}${keyName(ctl)}`;
			b.title = `Play ${g.shift ? '•• + ' : ''}${keyName(ctl)} on the device`;
			b.addEventListener('click', () => perform({ shift: g.shift, ctl }));
			row.appendChild(b);
		}
		td.appendChild(row);
	});
}

// ---- the session recorder: a run you can replay with wakes_headless ----
const rec = { armed: false, on: false, events: [], resetreas: 0, end: 0, seen: [] };
// Lines worth checking on a replay: where Wakes went and what it did with the keys.
// (Wakes' key handlers print these in main.c; its boot-time dumps are left out.)
const REC_EXPECT = /^(MODULE |MARBLES [tX] page|PAGE |SHIFT released|ENGINE |MODEL |SCALE |QUANTIZE |OUTPUT |ROUTE |RANGE |DIVISION |DIVERSITY |DEJA VU |TRIG |MIDI port enabled)/;
function recLine(line) {
	if (!rec.on) { return; }
	const t = line.replace(/\s+$/, '');
	if (REC_EXPECT.test(t) && !rec.seen.includes(t) && rec.seen.length < 12) { rec.seen.push(t); }
}

function recEvent(what, val) {
	if (!rec.on || !dev || dev.state !== 'on' || !what) { return; }
	rec.events.push([dev.nowMs(), what, String(val)]);
	recStatus();
}

function recStart(resetreas) {
	rec.on = true;
	rec.armed = false;
	rec.resetreas = resetreas;
	const i = dev.inputs;
	rec.events = [];
	rec.seen = [];
	const at0 = (w, v) => rec.events.push([0, w, String(v)]);
	if (i.fnc) { at0('fnc', 1); }
	for (const [n, bit] of Object.entries(BTN)) { if (i.buttons & (1 << bit)) { at0(REC_NAME[n], 1); } }
	i.faders.forEach((v, k) => at0(`f${k + 1}`, v.toFixed(3)));
	if (i.usb) { at0('usb', i.usb); }
	if (i.battMv !== 3900) { at0('batt', i.battMv); }
	$('rec-btn').textContent = 'Stop';
	recStatus();
}

function recStop(why) {
	if (!rec.on && !rec.armed) { return; }
	const wasOn = rec.on;
	rec.end = wasOn && dev && dev.state === 'on' ? dev.nowMs() : (rec.events.length ? rec.events[rec.events.length - 1][0] + 500 : 0);
	rec.on = false;
	rec.armed = false;
	$('rec-btn').textContent = 'Record';
	$('rec-status').classList.remove('live');
	if (!wasOn) {
		$('rec-status').textContent = 'Not recording.';
		return;
	}
	// Faders: keep one point per 20 ms of each drag, and always its last.
	const out = [];
	const lastF = {};
	for (const e of rec.events) {
		if (/^f[1-4]$/.test(e[1]) && e[0] > 0) {
			const prev = lastF[e[1]];
			if (prev && e[0] - prev[0] < 20) { prev[0] = e[0]; prev[2] = e[2]; continue; }
			const copy = [...e];
			lastF[e[1]] = copy;
			out.push(copy);
			continue;
		}
		out.push(e);
	}
	const fmt = (t) => (t === 0 ? '0' : t.toFixed(2).replace(/\.?0+$/, ''));
	const args = out.map((e) => `${fmt(e[0])}:${e[1]}=${e[2]}`);
	const lines = [];
	for (let k = 0; k < args.length; k += 5) { lines.push('  ' + args.slice(k, k + 5).join(' ')); }
	const head = `build/emu/native/wakes_headless --ms ${Math.ceil(rec.end + 200)} ` +
		`--resetreas 0x${(rec.resetreas >>> 0).toString(16)} --wav session.wav`;
	const q = (t) => `'${t.replace(/'/g, `'\\''`)}'`;
	const expect = rec.seen.map((t) => `  --expect ${q(t)}`);
	$('rec-out').textContent = [head, ...expect, ...lines].join(' \\\n');
	$('rec-out').hidden = false;
	$('rec-copy').disabled = false;
	$('rec-status').textContent = `${out.length} events over ${(rec.end / 1000).toFixed(1)} s` +
		`${rec.seen.length ? `, ${rec.seen.length} console line${rec.seen.length === 1 ? '' : 's'} to check` : ''}${why ? ` (${why})` : ''}. ` +
		'Run it from the repository after emulator/build.sh native; it exits non-zero if Wakes no longer prints them.';
}

function recStatus() {
	const st = $('rec-status');
	if (rec.on) {
		st.classList.add('live');
		st.textContent = `● Recording · ${rec.events.length} events · ${(dev.nowMs() / 1000).toFixed(1)} s`;
	} else if (rec.armed) {
		st.classList.add('live');
		st.textContent = dev && dev.state === 'on'
			? 'Armed: switch Wakes off and on again (hold ••); recording starts at the power-on.'
			: 'Armed: switch Wakes on (hold ••); recording starts at the power-on.';
	}
}

$('rec-btn').addEventListener('click', () => {
	if (rec.on || rec.armed) {
		recStop();
	} else {
		rec.armed = true;
		$('rec-btn').textContent = 'Cancel';
		$('rec-out').hidden = true;
		recStatus();
	}
});
$('rec-copy').addEventListener('click', async () => {
	try { await navigator.clipboard.writeText($('rec-out').textContent); $('rec-copy').textContent = 'Copied'; }
	catch { const r = document.createRange(); r.selectNodeContents($('rec-out')); const sel = getSelection(); sel.removeAllRanges(); sel.addRange(r); $('rec-copy').textContent = 'Selected'; }
	setTimeout(() => { $('rec-copy').textContent = 'Copy script'; }, 1500);
});
setInterval(() => { if (rec.on) { recStatus(); } }, 500);

function linkFor(href) {
	if (/^[a-z]+:/i.test(href)) { return { href }; }
	if (href.startsWith('#')) { return { anchor: href.slice(1) }; }
	const [path, anchor] = href.split('#');
	const base = path.split('/').pop();
	if (manual.docs.some((d) => d.file === base)) { return { doc: base, anchor }; }
	return { href: new URL(href, `${UPSTREAM}docs/`).href };
}

async function openDoc(file, anchor) {
	const body = $('ref-body');
	manual.doc = file;
	document.querySelectorAll('#ref-tabs button').forEach((b) => {
		b.setAttribute('aria-selected', b.dataset.file === file ? 'true' : 'false');
	});
	$('ref-github').href = `${UPSTREAM}docs/${encodeURIComponent(file)}`;
	try {
		const r = await fetch(`manual/${file}`);
		if (!r.ok) { throw new Error(`HTTP ${r.status}`); }
		body.innerHTML = renderMarkdown(await r.text(), { link: linkFor });
		if (prefs.play) { decorateGestures(body); }
	} catch {
		body.innerHTML = '';
		const p = document.createElement('p');
		p.className = 'empty';
		p.append('This document is copied next to the page by emulator/build.sh web. Read it on ');
		const a = document.createElement('a');
		a.href = `${UPSTREAM}docs/${encodeURIComponent(file)}`;
		a.target = '_blank'; a.rel = 'noopener'; a.textContent = 'GitHub';
		p.append(a, '.');
		body.append(p);
	}
	const target = anchor ? body.querySelector(`#${CSS.escape(anchor)}`) : null;
	body.scrollTop = target ? target.offsetTop - body.offsetTop - 8 : 0;
}

$('ref-body').addEventListener('click', (e) => {
	const a = e.target.closest('a[data-doc], a[data-anchor]');
	if (!a) { return; }
	e.preventDefault();
	if (a.dataset.doc) {
		openDoc(a.dataset.doc, a.dataset.anchor);
	} else {
		const t = $('ref-body').querySelector(`#${CSS.escape(a.dataset.anchor)}`);
		if (t) { $('ref-body').scrollTop = t.offsetTop - $('ref-body').offsetTop - 8; }
	}
});

function setZoom(on) {
	$('manual-view').classList.toggle('zoomed', on);
	$('manual-zoom').setAttribute('aria-pressed', on ? 'true' : 'false');
	$('manual-zoom').textContent = on ? 'Fit' : 'Zoom';
}
$('manual-zoom').addEventListener('click', () => setZoom(!$('manual-view').classList.contains('zoomed')));
$('manual-img').addEventListener('click', () => setZoom(!$('manual-view').classList.contains('zoomed')));

$('manual-labels').checked = prefs.labels;
$('manual-play').checked = prefs.play;
$('manual-labels').addEventListener('change', (e) => { prefs.labels = e.target.checked; savePrefs(); renderDeviceLabels(); });
$('manual-play').addEventListener('change', (e) => {
	prefs.play = e.target.checked;
	savePrefs();
	renderHotspots();
	if (manual.doc) { openDoc(manual.doc); }
	playToast(prefs.play ? 'Clicks on the manual\'s drawings, its page tabs and ▶ in the reference now play the device.' : '');
});

const DEFAULT_DOCS = [
	{ file: 'UI-PAGES.md', tab: 'Controls' }, { file: 'PLAITS-ENGINES.md', tab: 'Plaits engines' },
	{ file: 'MARBLES-SETTINGS.md', tab: 'Marbles' }, { file: 'MIDI.md', tab: 'MIDI' },
	{ file: 'DEFAULTS.md', tab: 'Defaults' }, { file: 'SAFETY.md', tab: 'Safety' },
];

async function loadManual() {
	let info = null;
	try {
		const r = await fetch('manual/manual.json');
		if (r.ok) { info = await r.json(); }
	} catch { /* shown below */ }
	manual.pages = (info && info.pages) || [];
	manual.docs = (info && info.docs && info.docs.length) ? info.docs : DEFAULT_DOCS;
	if (!info || !info.pdf) {
		$('manual-pdf').href = `${UPSTREAM}docs/Wakes%20-%20Manual.pdf`;
	}
	const tabs = $('manual-tabs');
	for (const p of manual.pages) {
		const b = document.createElement('button');
		b.type = 'button';
		b.dataset.n = String(p.n);
		b.setAttribute('role', 'tab');
		b.innerHTML = '<span class="n"></span><span class="t"></span>';
		b.querySelector('.n').textContent = String(p.n);
		b.querySelector('.t').textContent = p.title.replace(/\s*\((left|right)\)\s*$/i, '');
		b.addEventListener('click', () => {
			showPage(p.n);
			const key = pageKey(p.n);
			if (prefs.play && key) { goTo(key); }
		});
		tabs.appendChild(b);
	}
	if (manual.pages.length) {
		showPage(1);
	} else {
		const e = $('manual-empty');
		e.hidden = false;
		e.textContent = 'The manual pages are added by emulator/build.sh web (poppler-utils renders them). The PDF button opens the manual on GitHub.';
		$('manual-zoom').disabled = true;
		$('manual-follow').disabled = true;
	}
	const rtabs = $('ref-tabs');
	for (const d of manual.docs) {
		const b = document.createElement('button');
		b.type = 'button';
		b.dataset.file = d.file;
		b.setAttribute('role', 'tab');
		b.textContent = d.tab;
		b.addEventListener('click', () => openDoc(d.file));
		rtabs.appendChild(b);
	}
	openDoc(manual.docs[0].file);
}

// ------------------------------------------------------------------ start
async function start() {
	for (let i = 0; i < 4; i++) { setFader(i, 0.5); }
	loadManual();
	loadCpuModel();
	let module;
	try {
		module = await loadWasm();
	} catch (e) {
		const b = $('blocked');
		b.hidden = false;
		b.textContent = `The emulator could not load: ${e.message} ` +
			'If this page is open somewhere that blocks WebAssembly, serve emulator/web/ from your own machine (python3 -m http.server) and open it there.';
		setChip('reset', 'not loaded');
		return;
	}
	dev = new WakesDevice(module, { onConsole: consoleOut, onState });
	const saved = loadSavedDrive();
	if (saved) { dev.drive = saved; }
	fv.forEach((v, i) => dev.setFader(i, v));
	dev.setBattery(Number($('batt').value));
	renderFiles();
	// Power is applied: the app boots cold, as right after a flash. On battery with
	// nothing held, Wakes says so and goes to SYSTEM_OFF, waiting for "••".
	await dev.boot(RESETREAS.COLD);
	setInterval(tick, 8);
	requestAnimationFrame(frame);
	midiNote();
}

start();
