// worklet.js -- the emulator's output stage: blocks of the I2S stream, queued by app.js,
// played in order; silence when it runs dry, the oldest dropped past 300 ms.
class WakesOut extends AudioWorkletProcessor {
	constructor() {
		super();
		this.q = []; this.off = 0; this.n = 0;
		this.port.onmessage = (e) => {
			if (e.data === 'flush') { this.q = []; this.n = 0; this.off = 0; return; }
			this.q.push(e.data); this.n += e.data.l.length;
			while (this.n > 14400 && this.q.length > 1) {      // > 300 ms: drop the oldest
				const c = this.q.shift(); this.n -= c.l.length - this.off; this.off = 0;
			}
		};
	}
	process(inputs, outputs) {
		const o = outputs[0], L = o[0], R = o[1] || o[0];
		let i = 0;
		while (i < L.length && this.q.length) {
			const c = this.q[0];
			const k = Math.min(L.length - i, c.l.length - this.off);
			L.set(c.l.subarray(this.off, this.off + k), i);
			R.set(c.r.subarray(this.off, this.off + k), i);
			i += k; this.off += k; this.n -= k;
			if (this.off >= c.l.length) { this.q.shift(); this.off = 0; }
		}
		if (i < L.length) { L.fill(0, i); R.fill(0, i); }
		return true;
	}
}
registerProcessor('wakes-out', WakesOut);
