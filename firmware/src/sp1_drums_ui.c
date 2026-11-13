/* wakes-sp1 drum firmware: the drum pages. See sp1_drums_ui.h.
 *
 * Pickup is the PLAITS page's (sp1_plaits_ui.c), which is Mutable Instruments Plaits'
 * catch-up (plaits/pot_controller.h, Emilie Gillet, MIT; attributed in NOTICE), with the
 * same thresholds, skew, lock and jump guard. */
#include "sp1_drums_ui.h"

#include "sp1_controls.h"    /* enum sp1_button: the bits of sp1_dui_keys' masks */
#include "sp1_drums.h"       /* the kits, the model list, the map bank            */
#include "sp1_drums_patch.h" /* the state a preset holds, drums.cnfg              */
#include "sp1_acid.h"        /* the acid patterns and their sounds                */
#include "sp1_ui_timing.h"   /* SP1_ENGINE_LED_FULL / HALF                        */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- tuning: the PLAITS page's (sp1_plaits_ui.c has the reasons) ---- */
#define FADER_FULL      3701.0f
#define FADER_LP        0.125f
#define CATCH_MOVE      0.005f
#define CATCH_JUMP      0.25f
#define CATCH_MATCH     0.005f
#define CATCH_LOCK      0.03f
#define DETENT_BIPOLAR  0.10f
#define TAP_MAX_MS      300u     /* a "••" press this short, untouched, is a tap          */
#define TAP_GAP_MS      400u     /* the next tap must START within this of the last ending */
#define BREATH_MS       1200u
#define CLEAR_TAP_MS    300u     /* an FFWD press this short is a tap: clear at the bar   */

/* Layers: two per page, base and SHIFT -- the same twelve a preset holds. */
#define LAYERS          SP1_DRUMS_LAYERS
#define L_KIT           0
#define L_KIT_SH        1
#define L_DRUM(n)       (2 + 2 * (n))
#define L_DRUM_SH(n)    (3 + 2 * (n))
#define L_SET           10
#define L_SET_SH        11       /* the bass */

/* Everything a preset holds (sp1_drums_patch.h): the kit, the voices, the twelve layers'
 * values, FX, mutes, rests, the bass mode, the patterns and the cables. */
static struct sp1_drums_patch pt;
#define stored pt.v
/* drums.cnfg */
static struct sp1_drums_cfg cfg;
static bool cfg_applied;
static bool  catching[LAYERS][4];
static float pos[4], prev[4], now01[4], lock_ref[4];
static bool  locked[4];
static int   active = L_KIT;

static enum sp1_dui_page page = SP1_DUI_KIT;
static enum sp1_dui_page came_from = SP1_DUI_KIT;   /* where a SETTINGS exit returns */

/* "••" taps */
static bool     fnc_was;
static bool     press_dirty;
static uint32_t press_ms;
static uint32_t since_tap_ms = 0xFFFFFFFFu;
static int      tap_count;
static uint32_t breath_ms;
static bool     shift_shown;

/* Marbles' side of the controls (sp1_dui_marbles) */
static struct sp1_dui_marbles mrb;

/* the rocker */
static uint32_t rest_clear_seq, rotate_seq, step_seq;
static uint8_t  rest_clear;
static uint8_t  rest_write, rest_erase;
static bool     ffwd_eaten, rwd_eaten;   /* pressed with "••", or on a page where it is a key */
static uint32_t ffwd_ms;
static bool     burst;
static float    speed = 1.0f;
static uint8_t  audition;

/* what main shows / prints */
static bool    flash_due;
static uint8_t flash_lv[4];
#define LOGS 4
static char    logq[LOGS][80];
static int     log_head, log_tail;

/* ---- helpers ---- */
static float clamp01(float x) { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
static float to01(uint16_t raw) { return clamp01((float)raw * (1.0f / FADER_FULL)); }

static float detent(float p, float w)
{
	const float half = 0.5f * w;
	const float d = clamp01(p) - 0.5f;
	const float a = fabsf(d);
	if (a <= half) {
		return 0.5f;
	}
	const float r = 0.5f * (a - half) / (0.5f - half);
	return d < 0.0f ? 0.5f - r : 0.5f + r;
}

static bool bipolar(int l, int i)
{
	return l == L_KIT || (l == L_KIT_SH && i == 0);   /* KIT's offsets and fill, the filter */
}

/* DRUM n + SHIFT F3 (the loop length) and the bass octave: zones of their faders. */
static const uint8_t kLen[5] = { 8, 12, 16, 24, 32 };
static int zone(float v, int n)
{
	int z = (int)(clamp01(v) * (float)n);
	return z < 0 ? 0 : (z > n - 1 ? n - 1 : z);
}
static float zone_value(int z, int n) { return ((float)z + 0.5f) / (float)n; }

__attribute__((format(printf, 1, 2)))
static void LOG(const char *fmt, ...)
{
	const int next = (log_head + 1) % LOGS;
	if (next == log_tail) {
		return;                          /* full: drop rather than block */
	}
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(logq[log_head], sizeof logq[0], fmt, ap);
	va_end(ap);
	log_head = next;
}

static void flash(const uint8_t lv[4])
{
	memcpy(flash_lv, lv, 4);
	flash_due = true;
}

static int layer_of(enum sp1_dui_page p, bool shift)
{
	if (p == SP1_DUI_SETTINGS) {
		return shift ? L_SET_SH : L_SET;
	}
	return 2 * (int)p + (shift ? 1 : 0);
}

static void activate(int l)
{
	active = l;
	for (int i = 0; i < 4; i++) {
		prev[i] = pos[i];
		catching[l][i] = fabsf(stored[l][i] - pos[i]) >= CATCH_MATCH;
		lock_ref[i] = now01[i];
		locked[i] = true;
	}
}

static void pickup_all(void)
{
	for (int l = 0; l < LAYERS; l++) {
		for (int i = 0; i < 4; i++) {
			catching[l][i] = fabsf(stored[l][i] - pos[i]) >= CATCH_MATCH;
		}
	}
	for (int i = 0; i < 4; i++) {
		prev[i] = pos[i];
	}
}

static void page_glyph(enum sp1_dui_page p)
{
	uint8_t lv[4] = { 0u, 0u, 0u, 0u };
	if (p >= SP1_DUI_DRUM1 && p <= SP1_DUI_DRUM4) {
		lv[p - SP1_DUI_DRUM1] = SP1_ENGINE_LED_FULL;
	} else if (p == SP1_DUI_KIT) {
		lv[0] = lv[1] = lv[2] = lv[3] = SP1_ENGINE_LED_HALF;
	}
	if (p != SP1_DUI_SETTINGS) {
		flash(lv);                       /* SETTINGS says so by breathing */
	}
}

static void go(enum sp1_dui_page p, bool fnc)
{
	page = p;
	activate(layer_of(p, fnc));
	page_glyph(p);
}

/* ---- the kit ---- */
void sp1_dui_load_kit(int k)
{
	/* A kit brings its voices, their drum pages and its maps; the KIT page, the FX, SETTINGS,
	 * the bass, the mutes and the cables are left as they are. */
	struct sp1_drums_patch d;
	sp1_drums_patch_default(&d, k);
	pt.kit = d.kit;
	pt.melodic = d.melodic;
	for (int v = 0; v < 4; v++) {
		pt.eng[v] = d.eng[v];
		pt.model[v] = d.model[v];
		pt.gain[v] = d.gain[v];
		pt.pat[v] = d.pat[v];
		memcpy(stored[L_DRUM(v)], d.v[L_DRUM(v)], sizeof stored[0]);
		memcpy(stored[L_DRUM_SH(v)], d.v[L_DRUM_SH(v)], sizeof stored[0]);
	}
	pickup_all();
}

static void defaults(int k)
{
	sp1_drums_patch_default(&pt, k);
	pt.rest_choke = (uint8_t)(cfg.rest_choke ? 1 : 0);
	pt.bass_mode = (uint8_t)cfg.bass_mode;
	pickup_all();
}

void sp1_dui_init(void)
{
	sp1_drums_cfg_defaults(&cfg);
	cfg_applied = false;
	defaults(0);
	page = came_from = SP1_DUI_KIT;
	active = L_KIT;
	speed = 1.0f;
}

void sp1_dui_rip(void)
{
	defaults(pt.kit);
	rest_clear = 0x0Fu;
	rest_clear_seq++;
}

void sp1_dui_set_cfg(const struct sp1_drums_cfg *c)
{
	cfg = *c;
	if (!cfg_applied) {
		/* The power-on choices, once per boot and before any preset: a preset with drums
		 * of its own then replaces them (sp1_dui_put). */
		cfg_applied = true;
		defaults(cfg.kit);
	}
}

void sp1_dui_get(struct sp1_drums_patch *p)
{
	*p = pt;
	p->present = true;
}

void sp1_dui_put(const struct sp1_drums_patch *p)
{
	if (!p->present) {
		return;                       /* a slot with no drums: they stay as they are */
	}
	pt = *p;
	pickup_all();
}

static void enter_common(const uint16_t raw[4], bool fnc)
{
	for (int i = 0; i < 4; i++) {
		pos[i] = to01(raw[i]);
		now01[i] = pos[i];
	}
	pickup_all();
	page = came_from = SP1_DUI_KIT;
	fnc_was = fnc;
	press_dirty = true;
	press_ms = 0u;
	since_tap_ms = 0xFFFFFFFFu;
	tap_count = 0;
	activate(layer_of(page, fnc));
	shift_shown = fnc;
}

void sp1_dui_enter(const uint16_t raw[4])
{
	enter_common(raw, true);      /* ON is entered holding "••": that press is not a tap */
}

void sp1_dui_resume(const uint16_t raw[4], bool fnc)
{
	enter_common(raw, fnc);
	page_glyph(SP1_DUI_KIT);
}

/* ---- one tick: taps, faders ---- */
uint32_t sp1_dui_tick(uint32_t elapsed_ms, const uint16_t raw[4], bool valid, bool fnc,
		      bool activity)
{
	uint32_t ev = 0u;

	if (valid) {
		for (int i = 0; i < 4; i++) {
			now01[i] = to01(raw[i]);
			pos[i] += (now01[i] - pos[i]) * FADER_LP;
		}
	}

	/* ---- "••" taps (a press <= 300 ms with nothing else touched) ----
	 *   2nd tap: SETTINGS, at once.   3rd tap: MARBLES (main swaps the module).
	 *   On SETTINGS, ONE tap leaves -- but only once the 400 ms a 2nd tap could start in
	 *   has passed, so a double or triple tap from SETTINGS is never read as its exit. */
	if (since_tap_ms != 0xFFFFFFFFu) {
		since_tap_ms += elapsed_ms;
	}
	if (fnc) {
		if (!fnc_was) {
			press_ms = 0u;
			press_dirty = false;
		}
		press_ms += elapsed_ms;
		if (activity) {
			press_dirty = true;
		}
	} else if (fnc_was) {
		const bool tap = !press_dirty && press_ms <= TAP_MAX_MS;
		if (!tap) {
			tap_count = 0;
			since_tap_ms = 0xFFFFFFFFu;
		} else {
			if (tap_count > 0 && since_tap_ms <= TAP_GAP_MS + press_ms) {
				tap_count++;
			} else {
				tap_count = 1;
			}
			since_tap_ms = 0u;
			if (tap_count == 2 && page != SP1_DUI_SETTINGS) {
				came_from = page;
				page = SP1_DUI_SETTINGS;
				ev |= SP1_DUI_EV_PAGE;
			} else if (tap_count == 3) {
				page = SP1_DUI_KIT;        /* coming back from MARBLES lands here */
				ev |= SP1_DUI_EV_PAGE | SP1_DUI_EV_TO_MARBLES;
				tap_count = 0;
				since_tap_ms = 0xFFFFFFFFu;
			}
		}
	}
	/* a lone tap on SETTINGS, once nothing followed it: back where we came from */
	if (!fnc && tap_count == 1 && since_tap_ms != 0xFFFFFFFFu && since_tap_ms > TAP_GAP_MS) {
		if (page == SP1_DUI_SETTINGS) {
			page = came_from;
			ev |= SP1_DUI_EV_PAGE;
			page_glyph(page);
		}
		tap_count = 0;
		since_tap_ms = 0xFFFFFFFFu;
	}
	fnc_was = fnc;

	const int want = layer_of(page, fnc);
	if (want != active) {
		activate(want);
	}
	if (fnc != shift_shown) {             /* SHIFT shown / hidden, not a page change */
		shift_shown = fnc;
		ev |= SP1_DUI_EV_LAYER;
	}

	/* ---- pickup, active layer only (sp1_plaits_ui.c) ---- */
	if (valid) {
		for (int i = 0; i < 4; i++) {
			float *const s = &stored[active][i];
			if (locked[i]) {
				prev[i] = pos[i];
				if (fabsf(now01[i] - lock_ref[i]) > CATCH_LOCK) {
					locked[i] = false;
				}
				continue;
			}
			if (!catching[active][i]) {
				*s = pos[i];
				prev[i] = pos[i];
				continue;
			}
			const float delta = pos[i] - prev[i];
			if (fabsf(delta) <= CATCH_MOVE) {
				continue;
			}
			if (fabsf(delta) >= CATCH_JUMP) {
				prev[i] = pos[i];
				continue;
			}
			float skew = delta > 0.0f
				? (1.001f - *s) / (1.001f - prev[i])
				: (0.001f + *s) / (0.001f + prev[i]);
			if (skew < 0.1f)  { skew = 0.1f; }
			if (skew > 10.0f) { skew = 10.0f; }
			*s = clamp01(*s + skew * delta);
			prev[i] = pos[i];
			if (fabsf(*s - pos[i]) < CATCH_MATCH) {
				catching[active][i] = false;
				ev |= (SP1_DUI_EV_CAUGHT & (0x10u << i));
			}
		}
	}

	breath_ms = (breath_ms + elapsed_ms) % BREATH_MS;
	return ev;
}

/* ---- buttons and the rocker ---- */
#define BIT(b) (1u << (b))

static void kit_glyph(int k, uint8_t lv[4])
{
	for (int i = 0; i < 4; i++) {
		lv[i] = (i == (k & 3)) ? SP1_ENGINE_LED_FULL : (k >= 4 ? SP1_ENGINE_LED_HALF : 0u);
	}
}

static void mask_glyph(uint8_t m, uint8_t lv[4])
{
	for (int i = 0; i < 4; i++) {
		lv[i] = (m & (1u << i)) ? SP1_ENGINE_LED_FULL : 0u;
	}
}

static void model_step(int v, int dir)
{
	const int n = sp1_drums_models();
	int i = sp1_drums_model_index(pt.eng[v], pt.model[v]);
	for (int tries = 0; tries < n; tries++) {
		i = (i < 0) ? (dir > 0 ? 0 : n - 1) : (i + (dir > 0 ? 1 : n - 1)) % n;
		uint8_t e, m;
		sp1_drums_model_at(i, &e, &m);
		bool taken = false;
		if (sp1_drums_model_unique(e)) {
			for (int o = 0; o < 4; o++) {
				taken = taken || (o != v && pt.eng[o] == e && pt.model[o] == m);
			}
		}
		if (!taken) {
			pt.eng[v] = e;
			pt.model[v] = m;
			break;
		}
	}
	/* where on the list, as a bar from T1 (first) to T4 (last) */
	uint8_t lv[4];
	const float x = (float)(i < 0 ? 0 : i) / (float)(n > 1 ? n - 1 : 1) * 4.0f;
	for (int k = 0; k < 4; k++) {
		const float f = x - (float)k;
		lv[k] = f >= 1.0f ? SP1_ENGINE_LED_FULL : (f > 0.0f ? SP1_ENGINE_LED_HALF : 0u);
	}
	lv[0] = lv[0] ? lv[0] : SP1_ENGINE_LED_HALF;
	flash(lv);
	LOG("MODEL drum %d: %s (%d of %d)\n", v + 1, sp1_drums_model_name(pt.eng[v], pt.model[v]),
	    (i < 0 ? 0 : i) + 1, n);
}

/* SETTINGS "••" + Tn: drum n's maps from the next kit's pattern for that slot. The loop
 * length follows the new map's own (pickup catches DRUM n + SHIFT F3 up). */
static void pattern_step(int n)
{
	pt.pat[n] = (uint8_t)((pt.pat[n] + 1u) % SP1_DRUMS_KITS);
	const int len = sp1_drums_map_len(pt.pat[n], n);
	int z = 2;
	for (int i = 0; i < 5; i++) {
		if (kLen[i] == len) {
			z = i;
		}
	}
	stored[L_DRUM_SH(n)][2] = zone_value(z, 5);
	catching[L_DRUM_SH(n)][2] = fabsf(stored[L_DRUM_SH(n)][2] - pos[2]) >= CATCH_MATCH;
	uint8_t lv[4];
	kit_glyph(pt.pat[n], lv);
	flash(lv);
	LOG("PATTERN drum %d: %s's (%d steps)\n", n + 1, sp1_drums_kit_name(pt.pat[n]), len);
}

/* ---- Marbles' cables ----
 * out 0..3: t1, t2, t3, the Y clock (MARBLES t page, "••" + T1-T4); 4..7: X1, X2, X3, Y (the X
 * page). Each output steps round its own ring. */
static const uint8_t kTRing[] = { SP1_DT_NONE, SP1_DT_SLOT1, SP1_DT_SLOT2, SP1_DT_SLOT3,
				  SP1_DT_SLOT4, SP1_DT_BASS, SP1_DT_ACCENT };
static const uint8_t kXRing[] = { SP1_DX_NONE, SP1_DX_PITCH, SP1_DX_TONE, SP1_DX_DECAY,
				  SP1_DX_VEL, SP1_DX_KIT_PITCH, SP1_DX_KIT_TONE, SP1_DX_KIT_DECAY,
				  SP1_DX_FILL, SP1_DX_FILTER, SP1_DX_FOLD, SP1_DX_CHEBY, SP1_DX_FM };
static const uint8_t kYRing[] = { SP1_DX_NONE, SP1_DX_BASS_NOTE, SP1_DX_KIT_TONE,
				  SP1_DX_KIT_DECAY, SP1_DX_FILL, SP1_DX_FILTER, SP1_DX_FOLD,
				  SP1_DX_CHEBY, SP1_DX_FM };

static uint8_t *cable(int out)
{
	return out < 4 ? &pt.t_dest[out & 3] : &pt.x_dest[(out - 4) & 3];
}

static uint8_t ring_next(const uint8_t *ring, int n, uint8_t cur)
{
	for (int i = 0; i < n; i++) {
		if (ring[i] == cur) {
			return ring[(i + 1) % n];
		}
	}
	return ring[0];
}

void sp1_dui_cable_step(int out)
{
	if (out < 0 || out > 7) {
		return;
	}
	uint8_t *c = cable(out);
	if (out < 4) {
		*c = ring_next(kTRing, (int)sizeof kTRing, *c);
	} else if (out < 7) {
		*c = ring_next(kXRing, (int)sizeof kXRing, *c);
	} else {
		*c = ring_next(kYRing, (int)sizeof kYRing, *c);
	}
}

int sp1_dui_cable_unpatch(int out)
{
	if (out < 0 || out > 7) {
		return 0;
	}
	uint8_t *c = cable(out);
	const int had = *c != 0u;
	*c = 0u;
	return had;
}

uint8_t sp1_dui_cable(int out)
{
	return (out >= 0 && out <= 7) ? *cable(out) : 0u;
}

void sp1_dui_cable_glyph(int out, uint8_t lv[4])
{
	const uint8_t F = SP1_ENGINE_LED_FULL, H = SP1_ENGINE_LED_HALF;
	for (int i = 0; i < 4; i++) {
		lv[i] = 0u;
	}
	const uint8_t d = sp1_dui_cable(out);
	if (out < 4) {
		if (d >= SP1_DT_SLOT1 && d <= SP1_DT_SLOT4) {
			lv[d - SP1_DT_SLOT1] = F;                  /* the slot's LED            */
		} else if (d == SP1_DT_BASS) {
			lv[0] = lv[3] = F;                         /* ends                      */
		} else if (d == SP1_DT_ACCENT) {
			lv[0] = lv[1] = lv[2] = lv[3] = H;         /* all four at half          */
		}
		return;
	}
	if (d >= SP1_DX_PITCH && d <= SP1_DX_VEL) {
		lv[d - SP1_DX_PITCH] = H;                          /* own slot: one at half     */
	} else if (d >= SP1_DX_KIT_PITCH && d <= SP1_DX_FILL) {
		lv[d - SP1_DX_KIT_PITCH] = F;                      /* KIT: one full             */
	} else if (d >= SP1_DX_FILTER && d <= SP1_DX_FM) {
		for (int i = 0; i < 4; i++) {
			lv[i] = H;                                 /* FX: one full, rest half   */
		}
		lv[d - SP1_DX_FILTER] = F;
	} else if (d == SP1_DX_BASS_NOTE) {
		lv[0] = lv[3] = F;
	}
}

const char *sp1_dui_cable_name(int out)
{
	static const char *const tn[SP1_DT_COUNT] = { "none", "slot 1", "slot 2", "slot 3",
						      "slot 4", "bass", "accent" };
	static const char *const xn[SP1_DX_COUNT] = {
		"none", "its slot's pitch", "its slot's tone", "its slot's decay",
		"its slot's velocity", "KIT pitch", "KIT tone", "KIT decay", "fill",
		"FX filter", "FX fold", "FX Chebyshev", "FX FM", "bass note" };
	const uint8_t d = sp1_dui_cable(out);
	if (out < 4) {
		return d < SP1_DT_COUNT ? tn[d] : "?";
	}
	return d < SP1_DX_COUNT ? xn[d] : "?";
}

const char *sp1_dui_output_name(int out)
{
	static const char *const on[8] = { "t1", "t2", "t3", "Y clock", "X1", "X2", "X3", "Y" };
	return (out >= 0 && out <= 7) ? on[out] : "?";
}

/* The Plaits destination whose INTELLIGENT range suits what output k (X1..X3, Y) is cabled
 * to: a pitch two octaves (V/Oct, 0..2 V), a centred amount +-5 V (FM), a push 0..5 V
 * (LEVEL). Main hands it to Marbles in place of the PLAITS routing. */
uint8_t sp1_dui_range_dest(int k)
{
	switch (sp1_dui_cable(4 + (k & 3))) {
	case SP1_DX_PITCH: case SP1_DX_BASS_NOTE:
		return 5u;                                       /* SP1_DEST_VOCT  */
	case SP1_DX_TONE: case SP1_DX_DECAY: case SP1_DX_KIT_PITCH: case SP1_DX_KIT_TONE:
	case SP1_DX_KIT_DECAY: case SP1_DX_FILTER:
		return 1u;                                       /* SP1_DEST_FM    */
	case SP1_DX_VEL: case SP1_DX_FILL: case SP1_DX_FOLD: case SP1_DX_CHEBY: case SP1_DX_FM:
		return 6u;                                       /* SP1_DEST_LEVEL */
	default:
		return 0u;                                       /* SP1_DEST_NONE  */
	}
}

void sp1_dui_marbles(const struct sp1_dui_marbles *m)
{
	mrb = *m;
}

uint32_t sp1_dui_keys(uint32_t dt, const struct sp1_dui_keys *k)
{
	uint32_t act = 0u;
	const bool fnc = k->fnc;
	const bool run = k->running;
	const bool drum_page = !k->marbles && page >= SP1_DUI_DRUM1 && page <= SP1_DUI_DRUM4;
	const int dn = drum_page ? (int)page - SP1_DUI_DRUM1 : -1;
	const bool on_kit = !k->marbles && page == SP1_DUI_KIT;
	const bool on_set = !k->marbles && page == SP1_DUI_SETTINGS;
	const bool ffwd_down = (k->held & BIT(SP1_BTN_FFWD)) != 0u;
	const bool rwd_down = (k->held & BIT(SP1_BTN_RWD)) != 0u;
	const bool ffwd_press = (k->pressed & BIT(SP1_BTN_FFWD)) != 0u;
	const bool rwd_press = (k->pressed & BIT(SP1_BTN_RWD)) != 0u;

	audition = drum_page ? (uint8_t)(1u << dn) : 0u;

	/* ---- T1-T4: pages, FX, mutes (drum module only; MARBLES has its own) ---- */
	if (!k->marbles) {
		for (int n = 0; n < 4; n++) {
			if (!(k->pressed & BIT(SP1_BTN_T1 + n))) {
				continue;
			}
			if (fnc && on_set) {
				pattern_step(n);
			} else if (fnc) {
				{
					pt.fx_on ^= (uint8_t)(1u << n);
					uint8_t lv[4];
					mask_glyph(pt.fx_on, lv);
					flash(lv);
					static const char *const fxn[4] = { "DJ filter", "fold",
									    "Chebyshev", "FM" };
					LOG("FX %s %s\n", fxn[n], (pt.fx_on & (1u << n)) ? "on" : "off");
				}
			} else if (on_set) {
				pt.mute ^= (uint8_t)(1u << n);
				uint8_t lv[4];
				mask_glyph((uint8_t)~pt.mute, lv);
				flash(lv);
				LOG("MUTE drum %d %s\n", n + 1, (pt.mute & (1u << n)) ? "muted" : "playing");
			} else {
				const enum sp1_dui_page to = ((int)page == SP1_DUI_DRUM1 + n)
					? SP1_DUI_KIT : (enum sp1_dui_page)(SP1_DUI_DRUM1 + n);
				go(to, fnc);
				LOG("PAGE %s\n", sp1_dui_page_name(to));
			}
		}
	}

	/* ---- the rocker with "••": kit / model select (drum pages); never transport ---- */
	if (fnc && (ffwd_press || rwd_press)) {
		const int dir = ffwd_press ? 1 : -1;
		if (on_kit) {
			sp1_dui_load_kit(pt.kit + dir);
			uint8_t lv[4];
			kit_glyph(pt.kit, lv);
			flash(lv);
			LOG("KIT %d: %s\n", pt.kit + 1, sp1_drums_kit_name(pt.kit));
		} else if (drum_page) {
			model_step(dn, dir);
		} else if (on_set) {
			const bool was_acid = pt.bass_mode >= SP1_BASS_ACID;
			pt.bass_mode = (uint8_t)((pt.bass_mode + (dir > 0 ? 1 : SP1_BASS_MODES - 1)) %
						 SP1_BASS_MODES);
			if (pt.bass_mode >= SP1_BASS_ACID) {
				/* An acid pattern brings its sound: SETTINGS + SHIFT becomes its cutoff,
				 * resonance, decay and drive, and the faders catch up from there. The row
				 * shows the pattern's number in binary, T1 the low bit, over a half-lit row. */
				const int i = pt.bass_mode - SP1_BASS_ACID;
				const struct sp1_acid_preset *ap = sp1_acid_preset(i);
				stored[L_SET_SH][0] = ap->p[0];        /* cutoff    */
				stored[L_SET_SH][1] = ap->p[1];        /* resonance */
				stored[L_SET_SH][2] = ap->p[3];        /* decay     */
				stored[L_SET_SH][3] = ap->p[5];        /* drive     */
				pickup_all();
				uint8_t lv[4];
				for (int b = 0; b < 4; b++) {
					lv[b] = ((i + 1) >> b) & 1 ? SP1_ENGINE_LED_FULL : SP1_ENGINE_LED_HALF;
				}
				flash(lv);
				LOG("BASS acid %d: %s (%s)\n", i + 1, ap->name, ap->chr ? "101" : "303");
			} else {
				if (was_acid) {
					/* back to the bass: its own octave, shape, cutoff and decay */
					struct sp1_drums_patch d;
					sp1_drums_patch_default(&d, pt.kit);
					memcpy(stored[L_SET_SH], d.v[L_SET_SH], sizeof stored[0]);
					pickup_all();
				}
				static const char *const bn[3] = {
					"off", "on the Y clock (Y picks the note)", "only in the rests" };
				const uint8_t lv[4] = { pt.bass_mode == 1 ? SP1_ENGINE_LED_FULL : 0u,
							pt.bass_mode == 2 ? SP1_ENGINE_LED_FULL : 0u,
							pt.bass_mode == 2 ? SP1_ENGINE_LED_FULL : 0u,
							pt.bass_mode == 1 ? SP1_ENGINE_LED_FULL : 0u };
				flash(lv);
				LOG("BASS %s\n", bn[pt.bass_mode]);
			}
		}
	}
	if (ffwd_press) {
		ffwd_eaten = fnc || on_set;
		ffwd_ms = 0u;
	}
	if (rwd_press) {
		rwd_eaten = fnc || on_set;
	}
	if (ffwd_down) {
		ffwd_ms += dt;
	}

	/* ---- SETTINGS: the rest style ---- */
	if (on_set && !fnc && (ffwd_press || rwd_press)) {
		pt.rest_choke = rwd_press ? 1u : 0u;
		const uint8_t lv[4] = { SP1_ENGINE_LED_FULL, pt.rest_choke ? SP1_ENGINE_LED_FULL : 0u,
					pt.rest_choke ? SP1_ENGINE_LED_FULL : 0u, SP1_ENGINE_LED_FULL };
		flash(lv);
		LOG("RESTS %s\n", pt.rest_choke ? "choke" : "ring out");
	}

	/* ---- playing ---- */
	const uint8_t lane = k->marbles ? 0x0Fu : (drum_page ? (uint8_t)(1u << dn) : 0u);
	rest_write = 0u;
	rest_erase = 0u;
	bool tape = false;
	if (run && !on_set) {
		if (on_kit) {
			tape = rwd_down && !rwd_eaten && !fnc;
			if (ffwd_press && !fnc) {
				rotate_seq++;
				LOG("ROTATE the kit, on the next %s\n",
				    cfg.rotate_q == 0 ? "step" : (cfg.rotate_q == 2 ? "bar" : "beat"));
			}
		} else if (lane) {
			if (rwd_down && !rwd_eaten && !fnc) {
				rest_write = lane;
			}
			if (ffwd_down && !ffwd_eaten && !fnc) {
				rest_erase = lane;
			}
			if ((k->released & BIT(SP1_BTN_FFWD)) && !ffwd_eaten && ffwd_ms <= CLEAR_TAP_MS) {
				rest_clear = lane;
				rest_clear_seq++;
				LOG("RESTS clear %s at the next bar\n", k->marbles ? "every drum" : "this drum");
			}
			if (rwd_press && !fnc) {
				LOG("RESTS writing %s\n", k->marbles ? "every drum" : "this drum");
			}
		}
	}

	/* ---- stopped ---- */
	burst = false;
	if (!run && !on_set) {
		if (rwd_press && !fnc) {
			if (drum_page) {
				act |= SP1_DUI_ACT_TRIG;
				LOG("AUDITION drum %d\n", dn + 1);
			} else {
				step_seq++;
				LOG("STEP one 16th\n");
			}
		}
		burst = ffwd_down && !ffwd_eaten && !fnc;
	}

	/* ---- tape slow: sink while held, ease back after ---- */
	const float floor_ = cfg.tape_floor;
	const float fall_ms = 1000.0f * cfg.tape_fall;
	const float release_ms = 1000.0f * cfg.tape_release;
	static bool tape_was;
	if (tape != tape_was) {
		tape_was = tape;
		if (tape) {
			LOG("TAPE slow: sinking to x%d.%02d\n", (int)floor_, (int)(floor_ * 100.0f) % 100);
		} else {
			LOG("TAPE released at x%d.%02d: back over %d ms\n", (int)speed,
			    (int)(speed * 100.0f) % 100, (int)release_ms);
		}
	}
	if (tape) {
		speed += (floor_ - speed) * (1.0f - expf(-(float)dt / (fall_ms > 1.0f ? fall_ms : 1.0f)));
	} else if (speed < 1.0f) {
		if (release_ms <= 0.0f) {
			speed = 1.0f;                        /* tape_release = 0: at once */
		} else {
			speed += (1.0f - floor_) * (float)dt / release_ms;
		}
		if (speed > 1.0f) {
			speed = 1.0f;
		}
	}
	if (speed < floor_) {
		speed = floor_;
	}
	return act;
}

bool sp1_dui_burst(void) { return burst; }

bool sp1_dui_flash(uint8_t lv[4])
{
	if (!flash_due) {
		return false;
	}
	flash_due = false;
	memcpy(lv, flash_lv, 4);
	return true;
}

const char *sp1_dui_log(void)
{
	if (log_tail == log_head) {
		return NULL;
	}
	const char *s = logq[log_tail];
	log_tail = (log_tail + 1) % LOGS;
	return s;
}

/* ---- what the kit plays ---- */
void sp1_dui_ctl(struct sp1_drums_ctl *o)
{
	memset(o, 0, sizeof *o);
	const float *kb = stored[L_KIT];
	const float semis_f = (2.0f * detent(kb[0], DETENT_BIPOLAR) - 1.0f) * 12.0f;
	const float semis = (float)(int)(semis_f + (semis_f >= 0.0f ? 0.5f : -0.5f));
	const float d_tone = (detent(kb[1], DETENT_BIPOLAR) - 0.5f) * 0.8f;
	const float d_decay = (detent(kb[2], DETENT_BIPOLAR) - 0.5f) * 0.8f;
	const float fill = detent(kb[3], DETENT_BIPOLAR) - 0.5f;          /* +-0.5 density */
	for (int v = 0; v < 4; v++) {
		const float *b = stored[L_DRUM(v)];
		const float *sh = stored[L_DRUM_SH(v)];
		o->eng[v] = pt.eng[v];
		o->model[v] = pt.model[v];
		o->note[v] = b[0] * 120.0f + semis;
		o->tone[v] = clamp01(b[1] + d_tone);
		o->decay[v] = clamp01(b[2] + d_decay);
		o->mod[v] = clamp01(b[3]);
		o->excite[v] = clamp01(sh[3]);
		o->level[v] = pt.gain[v] * 2.0f * clamp01(sh[1]);
		o->density[v] = clamp01(sh[0]);
		o->len[v] = kLen[zone(sh[2], 5)];
		o->pat[v] = pt.pat[v];
		o->t_dest[v] = pt.t_dest[v];
		o->x_dest[v] = pt.x_dest[v];
	}
	o->fill = fill;
	o->morph = clamp01(stored[L_SET][1]);
	o->swing = clamp01(stored[L_SET][0]);
	o->jitter = clamp01(mrb.jitter);
	o->deja = clamp01(mrb.deja);
	o->rotate_q = (uint8_t)cfg.rotate_q;
	/* the cables' depth and the volts each output spans now ([J], INTELLIGENT per cable) */
	static const uint8_t kYDiv[12] = { 64, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2, 1 };
	o->y_ticks = kYDiv[(mrb.y_div >= 0 && mrb.y_div < 12) ? mrb.y_div : 6];
	for (int k = 0; k < 4; k++) {
		o->depth[k] = mrb.depth[k] < -1.0f ? -1.0f : (mrb.depth[k] > 1.0f ? 1.0f : mrb.depth[k]);
		const int r = k < 3 ? mrb.x_range : mrb.y_range;
		float lo = -5.0f, hi = 5.0f;
		if (r == 0) {                                     /* 0..2 V   */
			lo = 0.0f; hi = 2.0f;
		} else if (r == 1) {                              /* 0..5 V   */
			lo = 0.0f; hi = 5.0f;
		} else if (r == 3) {                              /* INTELLIGENT: per cable */
			const uint8_t d = sp1_dui_range_dest(k);
			lo = d == 1u || d == 0u ? -5.0f : 0.0f;
			hi = d == 5u ? 2.0f : 5.0f;
		}
		o->lo[k] = lo;
		o->hi[k] = hi;
	}
	/* the bass */
	const float *bs = stored[L_SET_SH];
	o->bass_mode = pt.bass_mode;
	o->bass_octaves = (uint8_t)cfg.bass_octaves;
	o->bass_root = cfg.bass_root;
	o->bass_level = powf(10.0f, cfg.bass_level_db / 20.0f) * 2.5f;
	o->bass_p[0] = clamp01(bs[1]);                       /* shape   */
	o->bass_p[1] = clamp01(bs[2]);                       /* cutoff  */
	o->bass_p[2] = cfg.bass_res;
	o->bass_p[3] = cfg.bass_env;
	o->bass_p[4] = clamp01(bs[3]);                       /* decay   */
	o->bass_p[5] = cfg.bass_glide;
	o->bass_p[6] = cfg.bass_sub;
	o->bass_p[7] = zone_value(zone(bs[0], 5), 5);        /* octave: five steps */
	/* the acid line: the pattern's sound, with SETTINGS + SHIFT as cutoff, resonance, decay, drive */
	if (pt.bass_mode >= SP1_BASS_ACID && pt.bass_mode < SP1_BASS_MODES) {
		const struct sp1_acid_preset *ap = sp1_acid_preset(pt.bass_mode - SP1_BASS_ACID);
		o->acid_chr = ap->chr;
		for (int i = 0; i < SP1_ACID_NPARAM; i++) {
			o->acid_p[i] = ap->p[i];
		}
		o->acid_p[0] = clamp01(bs[0]);
		o->acid_p[1] = clamp01(bs[1]);
		o->acid_p[3] = clamp01(bs[2]);
		o->acid_p[5] = clamp01(bs[3]);
		o->acid_gate = ap->gate;
		o->acid_level = powf(10.0f, cfg.acid_level_db / 20.0f) * 2.5f;
		o->acid_duck = cfg.acid_duck;
		o->acid_transpose = cfg.acid_transpose;
		for (int l = 0; l < 4; l++) {
			o->acid_dens[l] = cfg.acid_dens[l];
			o->acid_len[l] = (uint8_t)cfg.acid_len[l];
		}
	}
	o->n_deg = (uint8_t)(mrb.n_deg > 12 ? 12 : (mrb.n_deg < 0 ? 0 : mrb.n_deg));
	for (int i = 0; i < o->n_deg; i++) {
		o->degrees[i] = mrb.degrees[i];
	}
	o->period = mrb.period > 0.0f ? mrb.period : 12.0f;
	o->melodic = pt.melodic;
	o->mute = pt.mute;
	o->fx_on = pt.fx_on;
	o->fx[0] = detent(stored[L_KIT_SH][0], DETENT_BIPOLAR);
	o->fx[1] = clamp01(stored[L_KIT_SH][1]);
	o->fx[2] = clamp01(stored[L_KIT_SH][2]);
	o->fx[3] = clamp01(stored[L_KIT_SH][3]);
	o->fm_hz = 196.0f * exp2f((clamp01(stored[L_SET][2]) - 0.5f) * 6.0f);   /* +-3 octaves */
	o->bus_gain = 1.0f + 3.0f * clamp01(stored[L_SET][3]);
	o->audition = audition;
	o->rest_write = rest_write;
	o->rest_erase = rest_erase;
	o->rest_clear = rest_clear;
	o->rest_choke = pt.rest_choke;
	o->rest_clear_seq = rest_clear_seq;
	o->rotate_seq = rotate_seq;
	o->step_seq = step_seq;
	o->speed = speed;
}

float sp1_dui_speed(void) { return speed; }

/* ---- LEDs ---- */
static void leds_of(int l, uint8_t out[4])
{
	uint32_t k = 256u;
	if (l == L_SET) {
		const uint32_t half = BREATH_MS / 2u;
		const uint32_t tri = breath_ms < half ? breath_ms : BREATH_MS - breath_ms;
		k = 90u + (166u * tri) / half;
	}
	for (int i = 0; i < 4; i++) {
		const float x = bipolar(l, i)
			? 2.0f * fabsf(detent(stored[l][i], DETENT_BIPOLAR) - 0.5f)
			: clamp01(stored[l][i]);
		const uint32_t v = (uint32_t)(x * 255.0f + 0.5f);
		out[i] = (uint8_t)((v * k) >> 8);
	}
}

void sp1_dui_leds(uint8_t out[4])      { leds_of(active, out); }
void sp1_dui_page_leds(uint8_t out[4]) { leds_of(layer_of(page, false), out); }

enum sp1_dui_page sp1_dui_page(void) { return page; }
bool sp1_dui_shift(void) { return (active & 1) && active != L_SET; }
int  sp1_dui_kit(void) { return pt.kit; }

const char *sp1_dui_page_name(enum sp1_dui_page p)
{
	switch (p) {
	case SP1_DUI_KIT:      return "kit";
	case SP1_DUI_DRUM1:    return "drum 1";
	case SP1_DUI_DRUM2:    return "drum 2";
	case SP1_DUI_DRUM3:    return "drum 3";
	case SP1_DUI_DRUM4:    return "drum 4";
	case SP1_DUI_SETTINGS: return "settings";
	default:               return "?";
	}
}
