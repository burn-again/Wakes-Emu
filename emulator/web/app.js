// app.js -- the bench page: draws the SP-1, wires its controls to the emulated board,
// plays the I2S stream, shows the CDC console and the WAKES drive.
import { WakesDevice, BTN, USB, RESETREAS, STOP, STOP_NAME, driveToJson, driveFromJson }
	from './emu-core.js';

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
			setChip('on', `running · ${(dev.nowMs() / 1000).toFixed(1)} s`);
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
const latched = new Set();
function press(name, down) {
	if (!dev) { return; }
	if (!audio.ctx && down) {
		startAudio().then(() => setSound(true)).catch(() => {});
	}
	if (name === FNC) { dev.setFnc(down); } else { dev.setButton(BTN[name], down); }
	document.querySelectorAll(`[data-btn="${name}"]`).forEach((k) => { k.dataset.down = down ? '1' : '0'; });
	if (down) { tick(); }
}

document.querySelectorAll('[data-btn]').forEach((el) => {
	const name = el.dataset.btn;
	el.addEventListener('pointerdown', (e) => {
		e.preventDefault();
		if (e.altKey) {                         // latch / unlatch
			if (latched.has(name)) {
				latched.delete(name); el.dataset.latched = '0'; press(name, false);
			} else {
				latched.add(name); el.dataset.latched = '1'; press(name, true);
			}
			return;
		}
		if (latched.has(name)) { return; }
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
		if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); e.stopPropagation(); press(name, false); }
	});
	el.addEventListener('contextmenu', (e) => e.preventDefault());
});

const KEYMAP = { f: FNC, F: FNC, '1': 'T1', '2': 'T2', '3': 'T3', '4': 'T4', ' ': 'PLAY',
	ArrowLeft: 'RWD', ArrowRight: 'FFWD', ArrowUp: 'VOL_UP', ArrowDown: 'VOL_DOWN' };
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
	f.innerHTML = '<span class="slot"></span><span class="knob"></span><span class="val"></span>';
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
}

// bench supply
document.querySelectorAll('#usb-seg button').forEach((b) => {
	b.addEventListener('click', () => {
		const mode = Number(b.dataset.usb);
		document.querySelectorAll('#usb-seg button').forEach((x) => x.setAttribute('aria-pressed', x === b ? 'true' : 'false'));
		if (dev) { dev.setUsb(mode); tick(); }
		midiNote();
	});
});
$('batt').addEventListener('input', (e) => {
	const mv = Number(e.target.value);
	$('batt-read').textContent = `${(mv / 1000).toFixed(2)} V`;
	if (dev) { dev.setBattery(mv); }
});

// MIDI: an on-screen octave, and Web MIDI inputs where the browser allows them
function midiNote() {
	const usb = dev ? dev.inputs.usb : 0;
	$('midi-note').textContent = usb === USB.COMPUTER
		? 'Wakes is enumerated as a USB-MIDI port: notes play Plaits, CCs move parameters.'
		: 'Notes reach Wakes only while USB is set to Computer, as on the device.';
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
	const on = (e) => { e.preventDefault(); b.setPointerCapture(e.pointerId); b.dataset.down = '1'; dev && dev.sendMidi([0x90, 60 + semi, 100]); };
	const off = () => { if (b.dataset.down === '1') { b.dataset.down = '0'; dev && dev.sendMidi([0x80, 60 + semi, 0]); } };
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
			? (m) => { if (dev && m.data[0] !== 0xFE) { dev.sendMidi(Array.from(m.data)); } } : null;
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

function renderFiles() {
	if (!dev) { return; }
	const box = $('files');
	const entries = [...dev.drive.entries()].filter(([, f]) => !f.dir).sort((a, b) => a[0].localeCompare(b[0]));
	box.textContent = '';
	if (!entries.length) {
		const e = document.createElement('div');
		e.className = 'empty';
		e.textContent = 'Empty. Switch Wakes on once and it writes WAKES/CNFG with its defaults.';
		box.appendChild(e);
	}
	let lastDir = null;
	for (const [p, f] of entries) {
		const dir = p.slice(0, p.lastIndexOf('/')) || '/';
		if (dir !== lastDir) {
			const d = document.createElement('div');
			d.className = 'dir';
			d.textContent = dir.replace(/^\//, '') + '/';
			box.appendChild(d);
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
	const total = entries.reduce((s, [, f]) => s + f.data.length, 0);
	$('drive-sub').textContent = `${entries.length} files · ${fmtSize(total)}`;
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
	const folder = selected ? selected.slice(0, selected.lastIndexOf('/')) : '/WAKES/CNFG';
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

// ------------------------------------------------------------------ start
async function start() {
	for (let i = 0; i < 4; i++) { setFader(i, 0.5); }
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
