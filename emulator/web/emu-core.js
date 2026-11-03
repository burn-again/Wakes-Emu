// emu-core.js -- runs wakes.wasm: the device lifecycle, the thread dispatch loop, the
// WAKES drive, and the board's inputs and outputs. No DOM: the page (app.js) and the
// Node test (test-node.mjs) both drive it.
//
// One wasm instance is one power-on of the nRF52840. A reset, a watchdog, a fault or
// SYSTEM_OFF ends the instance; the next boot is a fresh instance (fresh RAM), with the
// WAKES drive carried across, exactly as the eMMC keeps it.

export const STOP = Object.freeze({
	NONE: 0, SYSTEM_OFF: 1, SOFT_RESET: 2, WATCHDOG: 3, FAULT: 4, MAIN_RETURNED: 5,
});
export const STOP_NAME = ['running', 'SYSTEM_OFF', 'soft reset', 'watchdog reset',
	'fault', 'main() returned'];

// nRF52840 RESETREAS bits, as Wakes' banner decodes them.
export const RESETREAS = Object.freeze({
	COLD: 0x0, DOG: 0x2, SREQ: 0x4, LOCKUP: 0x8, OFF: 0x10000, VBUS: 0x100000,
});

// Bit per enum sp1_button (firmware/src/sp1_controls.h).
export const BTN = Object.freeze({
	PLAY: 0, T1: 1, T2: 2, T3: 3, T4: 4, VOL_UP: 5, VOL_DOWN: 6, FFWD: 7, RWD: 8,
});

export const USB = Object.freeze({ UNPLUGGED: 0, CHARGER: 1, COMPUTER: 2 });

class EmuStop extends Error {
	constructor(reason) { super('device stopped: ' + STOP_NAME[reason]); this.reason = reason; }
}

const utf8 = new TextDecoder();
const enc = new TextEncoder();

export class WakesDevice {
	constructor(module, opts = {}) {
		this.module = module;                 // a compiled WebAssembly.Module
		this.onConsole = opts.onConsole || (() => {});
		this.onState = opts.onState || (() => {});
		this.inputs = { faders: [0.5, 0.5, 0.5, 0.5], buttons: 0, fnc: 0,
			usb: USB.UNPLUGGED, battMv: 3900, consoleOpen: 1 };
		this.drive = new Map();               // path -> { dir, data: Uint8Array }
		this.driveFormatted = true;
		this.state = 'off';                   // 'off' | 'on'
		this.lastStop = STOP.NONE;
		this.ex = null;
		this.audioRead = 0;
		this.boots = 0;
		this.driveGen = -1;
		this.autoReboot = true;
	}

	// ---- lifecycle ----

	async boot(resetreas = RESETREAS.COLD) {
		const self = this;
		let ex = null;
		const env = {
			emu_host_switch() {
				if (ex.asyncify_get_state() === 2) {        // rewinding: arrived back
					ex.asyncify_stop_rewind();
					return;
				}
				const t = ex.emu_get_current();
				ex.emu_t_save_sp(t, ex.__stack_pointer.value);
				ex.asyncify_start_unwind(ex.emu_t_async(t));
			},
			emu_host_stop(why) { throw new EmuStop(why); },
			emu_host_console(p, n) {
				self.onConsole(utf8.decode(new Uint8Array(ex.memory.buffer, p, n)));
			},
		};
		const wasi = {
			fd_write(fd, iovs, iovsLen, nwrittenPtr) {
				const dv = new DataView(ex.memory.buffer);
				let total = 0;
				for (let i = 0; i < iovsLen; i++) {
					const p = dv.getUint32(iovs + 8 * i, true);
					const n = dv.getUint32(iovs + 8 * i + 4, true);
					self.onConsole(utf8.decode(new Uint8Array(ex.memory.buffer, p, n)));
					total += n;
				}
				dv.setUint32(nwrittenPtr, total, true);
				return 0;
			},
			fd_close() { return 0; },
			fd_seek() { return 70; },             // ESPIPE: the console is not seekable
		};
		const inst = await WebAssembly.instantiate(this.module,
			{ env, wasi_snapshot_preview1: wasi });
		ex = inst.exports;
		this.ex = ex;
		ex._initialize();
		this.baseSp = ex.__stack_pointer.value;
		this.restoreDrive();
		this.pushInputs();
		ex.emu_boot(resetreas >>> 0);
		this.audioRead = ex.emu_audio_written();
		this.state = 'on';
		this.lastStop = STOP.NONE;
		this.boots++;
		this.onState('on', { resetreas });
	}

	// Run the device until its clock reads `ms` (device time since this boot).
	runUntil(ms) {
		if (this.state !== 'on') {
			return;
		}
		const ex = this.ex;
		try {
			for (;;) {
				const t = ex.emu_pick(ms);
				if (!t) {
					break;
				}
				ex.emu_set_current(t);
				if (!ex.emu_t_started(t)) {
					ex.emu_t_mark_started(t);
					ex.__stack_pointer.value = ex.emu_t_stack_top(t);
				} else {
					ex.__stack_pointer.value = ex.emu_t_saved_sp(t);
					ex.asyncify_start_rewind(ex.emu_t_async(t));
				}
				ex.emu_thread_enter(t);
				if (ex.asyncify_get_state() === 1) {
					ex.asyncify_stop_unwind();
				}
				ex.__stack_pointer.value = this.baseSp;
				ex.emu_set_current(0);
			}
			const why = ex.emu_stopped();
			if (why) {
				this.stopped(why);
			}
		} catch (e) {
			if (e instanceof EmuStop) {
				this.stopped(e.reason);
			} else {
				this.onConsole(`\n!! FAULT (${e.message}) -- the device resets\n`);
				this.stopped(STOP.FAULT, e);
			}
		}
	}

	nowMs() {
		return this.state === 'on' ? this.ex.emu_now_ms() : 0;
	}

	stopped(why) {
		this.snapshotDrive();
		this.state = 'off';
		this.lastStop = why;
		this.ex = null;
		this.onState('off', { why });
		if (!this.autoReboot) {
			return;
		}
		// What the nRF does next. SYSTEM_OFF waits for a wake edge; the rest restart now.
		if (why === STOP.SOFT_RESET || why === STOP.FAULT || why === STOP.MAIN_RETURNED) {
			this.pendingBoot = RESETREAS.SREQ;
		} else if (why === STOP.WATCHDOG) {
			this.pendingBoot = RESETREAS.DOG;
		}
	}

	// The host loop calls this between runs: performs a reset or a wake that is due.
	async service() {
		if (this.state === 'off' && this.pendingBoot !== undefined) {
			const r = this.pendingBoot;
			this.pendingBoot = undefined;
			await this.boot(r);
		}
	}

	// ---- inputs ----

	pushInputs() {
		const ex = this.ex, i = this.inputs;
		if (!ex) {
			return;
		}
		i.faders.forEach((v, k) => ex.emu_set_fader(k, v));
		ex.emu_set_buttons(i.buttons >>> 0);
		ex.emu_set_usb(i.usb);
		ex.emu_set_batt_mv(i.battMv);
		ex.emu_set_console_open(i.consoleOpen);
		ex.emu_set_fnc(i.fnc);
	}

	setFader(k, v) {
		this.inputs.faders[k] = Math.min(1, Math.max(0, v));
		if (this.ex) { this.ex.emu_set_fader(k, this.inputs.faders[k]); }
	}

	setButton(bit, down) {
		this.inputs.buttons = down ? (this.inputs.buttons | (1 << bit))
					   : (this.inputs.buttons & ~(1 << bit));
		if (this.ex) { this.ex.emu_set_buttons(this.inputs.buttons >>> 0); }
	}

	setFnc(down) {
		const edge = down && !this.inputs.fnc;
		this.inputs.fnc = down ? 1 : 0;
		if (this.ex) { this.ex.emu_set_fnc(this.inputs.fnc); }
		// "••" is wired to the nRF's GPIO sense: a press wakes it from SYSTEM_OFF.
		if (edge && this.state === 'off' && this.lastStop === STOP.SYSTEM_OFF) {
			this.pendingBoot = RESETREAS.OFF;
		}
	}

	setUsb(mode) {
		const plug = mode > 0 && this.inputs.usb === 0;
		this.inputs.usb = mode;
		if (this.ex) { this.ex.emu_set_usb(mode); }
		// VBUS rising wakes it from SYSTEM_OFF too.
		if (plug && this.state === 'off' && this.lastStop === STOP.SYSTEM_OFF) {
			this.pendingBoot = RESETREAS.VBUS;
		}
	}

	setBattery(mv) {
		this.inputs.battMv = mv | 0;
		if (this.ex) { this.ex.emu_set_batt_mv(this.inputs.battMv); }
	}

	// MIDI 1.0 bytes from the host, over USB (only reach Wakes while USB is a computer).
	sendMidi(bytes) {
		if (!this.ex || bytes.length === 0) {
			return;
		}
		const n = Math.min(bytes.length, 256);
		new Uint8Array(this.ex.memory.buffer, this.ex.emu_midi_buf(), n).set(bytes.slice(0, n));
		this.ex.emu_midi_send(n);
	}

	// ---- outputs ----

	leds() {
		if (!this.ex) {
			return new Float32Array(8);
		}
		return new Float32Array(this.ex.memory.buffer, this.ex.emu_leds(), 8).slice();
	}

	// New audio since the last call, as Float32 L and R. The ring holds ~340 ms.
	readAudio() {
		const ex = this.ex;
		if (!ex) {
			return { left: new Float32Array(0), right: new Float32Array(0) };
		}
		const ringFrames = ex.emu_audio_ring_frames();
		const wr = ex.emu_audio_written() >>> 0;
		let n = (wr - this.audioRead) >>> 0;
		if (n > ringFrames) {
			this.audioRead = (wr - ringFrames) >>> 0;
			n = ringFrames;
		}
		const ring = new Int16Array(ex.memory.buffer, ex.emu_audio_ring_ptr(), ringFrames * 2);
		const left = new Float32Array(n), right = new Float32Array(n);
		for (let k = 0; k < n; k++) {
			const at = ((this.audioRead + k) >>> 0) % ringFrames;
			left[k] = ring[2 * at] / 32768;
			right[k] = ring[2 * at + 1] / 32768;
		}
		this.audioRead = wr;
		return { left, right };
	}

	audioRunning() { return this.ex ? !!this.ex.emu_audio_running() : false; }

	// ---- the WAKES drive ----

	cstr(p) {
		const m = new Uint8Array(this.ex.memory.buffer);
		let e = p;
		while (m[e]) { e++; }
		return utf8.decode(m.subarray(p, e));
	}

	// Copy the drive out of the running instance (cheap when nothing changed).
	snapshotDrive() {
		const ex = this.ex;
		if (!ex) {
			return false;
		}
		const gen = ex.emu_drive_generation();
		if (gen === this.driveGen) {
			return false;
		}
		this.driveGen = gen;
		const d = new Map();
		const slots = ex.emu_drive_slots();
		for (let i = 0; i < slots; i++) {
			const p = ex.emu_drive_path(i);
			if (!p) {
				continue;
			}
			const dir = !!ex.emu_drive_is_dir(i);
			const size = ex.emu_drive_size(i);
			const ptr = ex.emu_drive_data(i);
			const data = dir || !ptr ? new Uint8Array(0)
				: new Uint8Array(ex.memory.buffer, ptr, size).slice();
			d.set(this.cstr(p), { dir, data });
		}
		this.drive = d;
		this.driveFormatted = !!ex.emu_drive_formatted();
		return true;
	}

	restoreDrive() {
		const ex = this.ex;
		ex.emu_drive_set_formatted(this.driveFormatted ? 1 : 0);
		const entries = [...this.drive.entries()].sort((a, b) => a[0].length - b[0].length);
		for (const [path, f] of entries) {
			this.putInto(path, f.data, f.dir);
		}
		this.driveGen = ex.emu_drive_generation();
	}

	putInto(path, data, dir) {
		const ex = this.ex;
		const pb = enc.encode(path + '\0');
		const pp = ex.emu_alloc(pb.length);
		new Uint8Array(ex.memory.buffer, pp, pb.length).set(pb);
		const n = dir ? 0 : data.length;
		const dp = ex.emu_alloc(Math.max(1, n));
		if (n) {
			new Uint8Array(ex.memory.buffer, dp, n).set(data);
		}
		const rc = ex.emu_drive_put(pp, dp, n, dir ? 1 : 0);
		ex.emu_free(pp);
		ex.emu_free(dp);
		return rc;
	}

	// Host-side edits, as a computer writing the drive. Applied to the running instance
	// too, so a file saved while the device sits in STANDBY is read at the next ON.
	writeFile(path, data) {
		if (!path.startsWith('/')) { path = '/' + path; }
		const bytes = typeof data === 'string' ? enc.encode(data) : data;
		this.drive.set(path, { dir: false, data: bytes });
		const parts = path.split('/').filter(Boolean);
		for (let k = 1; k < parts.length; k++) {
			const dp = '/' + parts.slice(0, k).join('/');
			if (!this.drive.has(dp)) { this.drive.set(dp, { dir: true, data: new Uint8Array(0) }); }
		}
		this.driveFormatted = true;
		if (this.ex) {
			this.putInto(path, bytes, false);
			this.driveGen = -1;
		}
	}

	removeFile(path) {
		for (const p of [...this.drive.keys()]) {
			if (p === path || p.startsWith(path + '/')) { this.drive.delete(p); }
		}
		if (this.ex) {
			const pb = enc.encode(path + '\0');
			const pp = this.ex.emu_alloc(pb.length);
			new Uint8Array(this.ex.memory.buffer, pp, pb.length).set(pb);
			this.ex.emu_drive_remove(pp);
			this.ex.emu_free(pp);
			this.driveGen = -1;
		}
	}

	eraseDrive() {
		this.drive = new Map();
		if (this.ex) {
			this.ex.emu_drive_clear();
			this.driveGen = -1;
		}
	}
}

// Drive <-> JSON (base64 file bodies), for keeping it in the browser between visits.
export function driveToJson(drive) {
	const out = {};
	for (const [p, f] of drive) {
		let s = '';
		for (let i = 0; i < f.data.length; i += 0x8000) {
			s += String.fromCharCode.apply(null, f.data.subarray(i, i + 0x8000));
		}
		out[p] = { d: f.dir ? 1 : 0, b: f.dir ? '' : btoa(s) };
	}
	return JSON.stringify(out);
}

export function driveFromJson(json) {
	const m = new Map();
	const o = JSON.parse(json);
	for (const p of Object.keys(o)) {
		const bin = atob(o[p].b || '');
		const u = new Uint8Array(bin.length);
		for (let i = 0; i < bin.length; i++) { u[i] = bin.charCodeAt(i); }
		m.set(p, { dir: !!o[p].d, data: u });
	}
	return m;
}
