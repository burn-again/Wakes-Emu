/*
 * wakes-sp1 — MIDI files (experimental, CONFIG_SP1_MIDIFILE). See sp1_midifile.h and
 * docs/MIDIFILE.md.
 */
#include "sp1_midifile.h"

#include <string.h>

/* ================================================================ the file format */

static uint32_t be32(const uint8_t *d)
{
	return ((uint32_t)d[0] << 24) | ((uint32_t)d[1] << 16) | ((uint32_t)d[2] << 8) | d[3];
}

/* A variable-length quantity, at most four bytes, inside [*p, end). */
static bool vlq(const uint8_t *d, uint32_t *p, uint32_t end, uint32_t *v)
{
	uint32_t x = 0u;
	for (int i = 0; i < 4; i++) {
		if (*p >= end) {
			return false;
		}
		const uint8_t b = d[(*p)++];
		x = (x << 7) | (b & 0x7Fu);
		if ((b & 0x80u) == 0u) {
			*v = x;
			return true;
		}
	}
	return false;
}

static uint8_t data_bytes(uint8_t status)
{
	const uint8_t t = status & 0xF0u;
	return (t == 0xC0u || t == 0xD0u) ? 1u : 2u;
}

/* The tracks: [start, end) of each MTrk's data. Unknown chunks are skipped, as the format
 * asks. Returns the count, or -1 (too many / a chunk runs past the file). */
static int find_tracks(const uint8_t *d, size_t n, uint32_t start[], uint32_t end[])
{
	uint32_t p = 8u + be32(d + 4);
	int k = 0;
	while (p + 8u <= n) {
		const uint32_t len = be32(d + p + 4);
		const uint32_t s = p + 8u;
		if (len > n - s) {
			return -1;
		}
		if (memcmp(d + p, "MTrk", 4) == 0) {
			if (k == SP1_MF_TRACKS) {
				return -1;
			}
			start[k] = s;
			end[k] = s + len;
			k++;
		}
		p = s + len;
	}
	return k;
}

static void count_poly(void *ctx, const uint8_t msg[3], uint8_t len)
{
	(void)len;
	struct sp1_mf_player *p = ctx;
	if ((msg[0] & 0xF0u) == 0x90u) {
		int on = 0;
		for (int i = 0; i < 16; i++) {
			for (uint8_t b = p->on[i]; b; b &= (uint8_t)(b - 1u)) {
				on++;
			}
		}
		if (on > p->info.poly) {
			p->info.poly = (uint8_t)(on > 255 ? 255 : on);
		}
	}
}

const char *sp1_mf_check(const uint8_t *d, size_t n, struct sp1_mf_info *info)
{
	static uint32_t start[SP1_MF_TRACKS], end[SP1_MF_TRACKS];
	memset(info, 0, sizeof(*info));
	info->lo = 127u;
	if (n < 14u || memcmp(d, "MThd", 4) != 0 || be32(d + 4) < 6u || be32(d + 4) > n - 8u) {
		return "not a MIDI file (no MThd header)";
	}
	info->format = (uint16_t)((d[8] << 8) | d[9]);
	const uint16_t division = (uint16_t)((d[12] << 8) | d[13]);
	if (info->format > 1u) {
		return "format 2 (independent patterns) is not played";
	}
	if (division & 0x8000u) {
		return "SMPTE time is not played, only ticks per quarter note";
	}
	if (division == 0u) {
		return "0 ticks per quarter note";
	}
	info->ppq = division;
	const int k = find_tracks(d, n, start, end);
	if (k < 0) {
		return "more than 16 tracks, or a chunk runs past the end of the file";
	}
	if (k == 0) {
		return "no tracks";
	}
	info->tracks = (uint16_t)k;
	info->size = (uint32_t)n;
	for (int t = 0; t < k; t++) {
		uint32_t p = start[t], tick = 0u;
		uint8_t rs = 0u;
		while (p < end[t]) {
			uint32_t delta;
			if (!vlq(d, &p, end[t], &delta)) {
				return "a delta time runs past its track";
			}
			tick += delta;
			if (p >= end[t]) {
				return "a track ends after a delta time";
			}
			uint8_t st = d[p];
			if (st & 0x80u) {
				p++;
			} else if (rs) {
				st = rs;
			} else {
				return "running status with no status before it";
			}
			if (st == 0xFFu) {
				uint32_t len;
				if (p >= end[t]) {
					return "a meta event runs past its track";
				}
				const uint8_t type = d[p++];
				if (!vlq(d, &p, end[t], &len) || len > end[t] - p) {
					return "a meta event runs past its track";
				}
				p += len;
				if (type == 0x2Fu) {
					break;                      /* end of track */
				}
				continue;
			}
			if (st == 0xF0u || st == 0xF7u) {
				uint32_t len;
				if (!vlq(d, &p, end[t], &len) || len > end[t] - p) {
					return "a SysEx runs past its track";
				}
				p += len;
				continue;
			}
			if (st >= 0xF0u) {
				return "a system message inside a track";
			}
			rs = st;
			const uint8_t nd = data_bytes(st);
			if (nd > end[t] - p || d[p] > 0x7Fu || (nd == 2u && d[p + 1] > 0x7Fu)) {
				return "a channel message is cut short or has a byte above 127";
			}
			const uint8_t ty = st & 0xF0u;
			if (ty == 0x90u && d[p + 1] > 0u) {
				info->notes++;
				info->channels |= (uint16_t)(1u << (st & 0x0Fu));
				info->lo = d[p] < info->lo ? d[p] : info->lo;
				info->hi = d[p] > info->hi ? d[p] : info->hi;
			}
			p += nd;
		}
		if (tick > info->end_tick) {
			info->end_tick = tick;
		}
	}
	if (info->notes == 0u) {
		return "no notes";
	}
	if (info->end_tick == 0u) {
		info->end_tick = info->ppq;
	}
	/* Every byte is good. Play it once, merged, to learn how many notes overlap. */
	static struct sp1_mf_player probe;
	sp1_mf_player_init(&probe, d, info);
	sp1_mf_play_to(&probe, UINT32_MAX - 1u, count_poly, &probe);
	info->poly = probe.info.poly;
	return NULL;
}

/* ================================================================ the player */

static void next_delta(struct sp1_mf_player *p, struct sp1_mf_track *t)
{
	uint32_t delta;
	if (t->pos >= t->end || !vlq(p->d, &t->pos, t->end, &delta)) {
		t->done = true;
		return;
	}
	t->next += delta;
}

void sp1_mf_rewind(struct sp1_mf_player *p)
{
	for (int t = 0; t < SP1_MF_TRACKS; t++) {
		struct sp1_mf_track *r = &p->tr[t];
		r->done = t >= p->info.tracks;
		if (r->done) {
			continue;
		}
		r->pos = r->start;
		r->next = 0u;
		r->rs = 0u;
		next_delta(p, r);
	}
}

void sp1_mf_player_init(struct sp1_mf_player *p, const uint8_t *d, const struct sp1_mf_info *i)
{
	uint32_t start[SP1_MF_TRACKS], end[SP1_MF_TRACKS];   /* storage and audio both call it */
	memset(p, 0, sizeof(*p));
	p->d = d;
	p->info = *i;
	p->info.poly = 0u;
	/* The file was checked (sp1_mf_check): the same tracks it found then. */
	const int k = find_tracks(d, i->size, start, end);
	p->info.tracks = (uint16_t)(k < 0 ? 0 : k);
	for (int t = 0; t < p->info.tracks; t++) {
		p->tr[t].start = start[t];
		p->tr[t].end = end[t];
	}
	sp1_mf_rewind(p);
}

/* One event of track `t` (its tick has come): a note goes out, anything else is skipped. */
static void step(struct sp1_mf_player *p, struct sp1_mf_track *t, sp1_mf_emit_fn emit, void *ctx)
{
	const uint8_t *d = p->d;
	uint8_t st = d[t->pos];
	if (st & 0x80u) {
		t->pos++;
	} else {
		st = t->rs;
	}
	if (st == 0xFFu) {
		uint32_t len = 0u;
		const uint8_t type = d[t->pos++];
		(void)vlq(d, &t->pos, t->end, &len);
		t->pos += len;
		if (type == 0x2Fu) {
			t->done = true;
			return;
		}
	} else if (st == 0xF0u || st == 0xF7u) {
		uint32_t len = 0u;
		(void)vlq(d, &t->pos, t->end, &len);
		t->pos += len;
	} else {
		t->rs = st;
		const uint8_t nd = data_bytes(st);
		const uint8_t ty = st & 0xF0u;
		const uint8_t note = d[t->pos];
		const uint8_t vel = nd > 1u ? d[t->pos + 1] : 0u;
		t->pos += nd;
		const uint8_t bit = (uint8_t)(1u << (note & 7u));
		if (ty == 0x90u && vel > 0u) {
			p->on[note >> 3] |= bit;
			const uint8_t m[3] = { st, note, vel };
			emit(ctx, m, 3u);
		} else if (ty == 0x80u || ty == 0x90u) {
			p->on[note >> 3] &= (uint8_t)~bit;
			const uint8_t m[3] = { (uint8_t)(0x80u | (st & 0x0Fu)), note, 0u };
			emit(ctx, m, 3u);
		}
	}
	next_delta(p, t);
}

void sp1_mf_play_to(struct sp1_mf_player *p, uint32_t tick, sp1_mf_emit_fn emit, void *ctx)
{
	for (;;) {
		struct sp1_mf_track *best = NULL;
		for (int i = 0; i < SP1_MF_TRACKS; i++) {
			struct sp1_mf_track *t = &p->tr[i];
			if (!t->done && t->next <= tick && (!best || t->next < best->next)) {
				best = t;
			}
		}
		if (!best) {
			return;
		}
		step(p, best, emit, ctx);
	}
}

void sp1_mf_all_off(struct sp1_mf_player *p, sp1_mf_emit_fn emit, void *ctx)
{
	for (int i = 0; i < 128; i++) {
		if (p->on[i >> 3] & (1u << (i & 7))) {
			const uint8_t m[3] = { 0x80u, (uint8_t)i, 0u };
			emit(ctx, m, 3u);
		}
	}
	memset(p->on, 0, sizeof(p->on));
}

/* ================================================================ the device */
#if defined(CONFIG_SP1_MIDIFILE)

#include <errno.h>
#include <zephyr/sys/printk.h>

#define BUF_SIZE ((size_t)CONFIG_SP1_MIDIFILE_MAX_KB * 1024u)
static uint8_t buf[BUF_SIZE];

/* The hand-over (sp1_midifile.h): each transition belongs to one thread. */
static volatile int state = SP1_MF_EMPTY;
static volatile int want = -1;           /* main: load this slot when the buffer is free */
static volatile int sel = -1;            /* main: the slot selected (-1 none)           */
static volatile int load_slot = -1;      /* main sets it for the job, storage reads it   */
static volatile uint32_t ready_gen;      /* storage: +1 per file handed over             */

/* What the last job found, for main's log line (storage writes, then bumps res_gen). */
enum { RES_OK = 1, RES_MISSING, RES_BAD, RES_TOOBIG, RES_ERROR };
static volatile uint32_t res_gen;
static uint32_t res_seen;
static int res_kind, res_slot, res_err;
static size_t res_n;
static const char *res_why;
static char res_name[64];
static struct sp1_mf_info res_info;

uint8_t *sp1_mf_buffer(size_t *size)
{
	*size = BUF_SIZE;
	return buf;
}

bool sp1_mf_loading(int *slot)
{
	if (state != SP1_MF_LOADING) {
		return false;
	}
	*slot = load_slot;
	return true;
}

static void result(int kind, const char *name, size_t n, int err, const char *why)
{
	res_kind = kind;
	res_slot = load_slot;
	res_n = n;
	res_err = err;
	res_why = why;
	snprintk(res_name, sizeof(res_name), "%s", name ? name : "");
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	res_gen = res_gen + 1u;
}

void sp1_mf_loaded(const char *name, size_t n, int err)
{
	if (state != SP1_MF_LOADING) {
		return;
	}
	if (load_slot != sel) {                    /* deselected while it loaded */
		state = SP1_MF_EMPTY;
		return;
	}
	if (err == -EFBIG) {
		result(RES_TOOBIG, name, n, err, NULL);
		state = SP1_MF_EMPTY;
		return;
	}
	if (err < 0) {
		result(RES_ERROR, name, n, err, NULL);
		state = SP1_MF_EMPTY;
		return;
	}
	if (n == 0u) {
		result(RES_MISSING, name, 0u, 0, NULL);
		state = SP1_MF_EMPTY;
		return;
	}
	const char *why = sp1_mf_check(buf, n, &res_info);
	if (why) {
		result(RES_BAD, name, n, 0, why);
		state = SP1_MF_EMPTY;
		return;
	}
	result(RES_OK, name, n, 0, NULL);
	ready_gen = ready_gen + 1u;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	state = SP1_MF_READY;
}

void sp1_mf_select(int slot)
{
	if (slot < 0 || slot >= SP1_MF_SLOTS) {
		return;
	}
	if (slot == sel) {
		sel = -1;                              /* the same button again: off */
		want = -1;
		printk("MIDIFILE off\n");
	} else {
		sel = slot;
		want = slot;
	}
	if (state == SP1_MF_READY) {
		state = SP1_MF_RELEASE;                /* the audio thread lets go first */
	}
}

int sp1_mf_slot(void)
{
	return sel;
}

void sp1_mf_main_tick(void (*queue)(void))
{
	if (want >= 0 && state == SP1_MF_EMPTY) {
		load_slot = want;
		want = -1;
		state = SP1_MF_LOADING;
		queue();
	}
	if (res_gen == res_seen) {
		return;
	}
	res_seen = res_gen;
	__atomic_thread_fence(__ATOMIC_SEQ_CST);
	const int s = res_slot + 1;
	switch (res_kind) {
	case RES_OK: {
		const struct sp1_mf_info *i = &res_info;
		printk("MIDIFILE %d: %s (%u B) -- format %u, %u track%s, %u notes %u..%u, "
		       "up to %u at once, %u.%02u beats; plays with the clock\n",
		       s, res_name, (unsigned)res_n, i->format, i->tracks, i->tracks == 1u ? "" : "s",
		       (unsigned)i->notes, i->lo, i->hi, i->poly,
		       (unsigned)(i->end_tick / i->ppq),
		       (unsigned)((i->end_tick % i->ppq) * 100u / i->ppq));
		return;
	}
	case RES_MISSING:
		printk("MIDIFILE %d: no .mid in WAKES/MIDI/%d\n", s, s);
		break;
	case RES_TOOBIG:
		printk("MIDIFILE %d: %s is %u B, more than the %u KB this build plays\n", s,
		       res_name, (unsigned)res_n, (unsigned)CONFIG_SP1_MIDIFILE_MAX_KB);
		break;
	case RES_BAD:
		printk("MIDIFILE %d: %s -- %s\n", s, res_name, res_why);
		break;
	default:
		printk("MIDIFILE %d: the card failed (%d)\n", s, res_err);
		break;
	}
	if (sel == res_slot) {
		sel = -1;                              /* nothing to play: deselect */
	}
}

void sp1_mf_reset(void)
{
	sel = -1;
	want = -1;
	if (state != SP1_MF_LOADING) {
		state = SP1_MF_EMPTY;
	}
}

/* ---- audio thread ---- */
static struct sp1_mf_player player;
static uint32_t a_gen;
static bool a_have;          /* `player` is the READY file */
static bool a_running;       /* following the clock */
static int32_t a_beats;      /* whole beats since beat 1 (-1: waiting for it) */
static float a_prev;
static uint32_t a_base;      /* tick where this pass of the loop began */

bool sp1_mf_audio_live(sp1_mf_emit_fn emit, void *ctx)
{
	const int s = state;
	if (s == SP1_MF_RELEASE) {
		if (a_have) {
			sp1_mf_all_off(&player, emit, ctx);
		}
		a_have = false;
		a_running = false;
		__atomic_thread_fence(__ATOMIC_SEQ_CST);
		state = SP1_MF_EMPTY;
		return true;                           /* this block carries the note-offs */
	}
	if (s != SP1_MF_READY) {
		return false;
	}
	if (!a_have || a_gen != ready_gen) {
		a_gen = ready_gen;
		sp1_mf_player_init(&player, buf, &res_info);
		a_have = true;
		a_running = false;                     /* picked while running: the next beat */
	}
	return true;
}

void sp1_mf_audio_block(bool running, float ramp, sp1_mf_emit_fn emit, void *ctx)
{
	if (!a_have) {
		return;
	}
	if (!running) {
		if (a_running) {
			sp1_mf_all_off(&player, emit, ctx);
			a_running = false;
		}
		return;
	}
	if (!a_running) {
		/* PLAY: Marbles parks its ramp at the end of a cycle, so the first wrap is
		 * beat 1, t2's first tick. A file picked while the clock runs starts on the
		 * next beat the same way. */
		a_running = true;
		a_beats = -1;
		a_prev = ramp;
		a_base = 0u;
		sp1_mf_rewind(&player);
		memset(player.on, 0, sizeof(player.on));
		return;
	}
	if (ramp < a_prev) {
		a_beats++;
	}
	a_prev = ramp;
	if (a_beats < 0) {
		return;
	}
	const uint32_t ppq = player.info.ppq;
	const uint32_t tick = (uint32_t)a_beats * ppq + (uint32_t)(ramp * (float)ppq);
	uint32_t rel = tick - a_base;
	if (rel >= player.info.end_tick) {
		/* The end: the last note-offs, then the top again. */
		sp1_mf_play_to(&player, player.info.end_tick, emit, ctx);
		sp1_mf_all_off(&player, emit, ctx);
		sp1_mf_rewind(&player);
		a_base += player.info.end_tick;
		rel -= player.info.end_tick;
	}
	sp1_mf_play_to(&player, rel, emit, ctx);
}

#endif /* CONFIG_SP1_MIDIFILE */
