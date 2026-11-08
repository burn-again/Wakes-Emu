/* wakes-sp1 drum firmware: the drum pages. See sp1_drums_ui.h.
 *
 * Pickup is the PLAITS page's (sp1_plaits_ui.c), which is Mutable Instruments Plaits'
 * catch-up (plaits/pot_controller.h, Emilie Gillet, MIT; attributed in NOTICE), with the
 * same thresholds, skew, lock and jump guard. */
#include "sp1_drums_ui.h"

#include "sp1_controls.h"    /* enum sp1_button: the bits of sp1_dui_keys' masks */
#include "sp1_drums.h"       /* the kits and the model list                       */
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
/* Tape slow (drums.cnfg in step 4): the floor, the fall's time constant, the way back. */
#define TAPE_FLOOR      0.33f
#define TAPE_FALL_MS    700.0f
#define TAPE_RELEASE_MS 1200.0f

/* Layers: two per page, base and SHIFT. SETTINGS + SHIFT shows KIT + SHIFT (the FX). */
#define LAYERS          (2 * SP1_DUI_PAGES)
#define L_KIT           0
#define L_KIT_SH        1
#define L_DRUM(n)       (2 + 2 * (n))
#define L_DRUM_SH(n)    (3 + 2 * (n))
#define L_SET           10

static float stored[LAYERS][4];
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

/* the kit */
static int     kit;
static uint8_t eng[4], model[4];
static float   def_gain[4];
static uint8_t melodic, mute;
static uint8_t fx_on = 0x0Fu;

/* the rocker */
static uint8_t  rest_choke = 1u;
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
	return l == L_KIT || (l == L_KIT_SH && i == 0);
}

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
		return shift ? L_KIT_SH : L_SET;
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
static void defaults_set(void)
{
	stored[L_SET][0] = 0.0f;        /* swing                  */
	stored[L_SET][1] = 0.0f;        /* morph                  */
	stored[L_SET][2] = 0.5f;        /* FM carrier: G3         */
	stored[L_SET][3] = 0.0f;        /* bus drive: unity       */
}

static void defaults_fx(void)
{
	stored[L_KIT_SH][0] = 0.5f;     /* DJ filter: centre = off */
	stored[L_KIT_SH][1] = 0.0f;
	stored[L_KIT_SH][2] = 0.0f;
	stored[L_KIT_SH][3] = 0.0f;
	fx_on = 0x0Fu;
}

static void defaults_kit_page(void)
{
	for (int i = 0; i < 4; i++) {
		stored[L_KIT][i] = 0.5f;
	}
}

void sp1_dui_load_kit(int k)
{
	kit = ((k % SP1_DRUMS_KITS) + SP1_DRUMS_KITS) % SP1_DRUMS_KITS;
	melodic = 0u;
	for (int v = 0; v < 4; v++) {
		struct sp1_drums_voice_def d;
		sp1_drums_kit_voice(kit, v, &d);
		eng[v] = d.eng;
		model[v] = d.model;
		def_gain[v] = d.gain;
		if (d.melodic) {
			melodic |= (uint8_t)(1u << v);
		}
		stored[L_DRUM(v)][0] = clamp01(d.note / 120.0f);
		stored[L_DRUM(v)][1] = d.tone;
		stored[L_DRUM(v)][2] = d.decay;
		stored[L_DRUM(v)][3] = d.mod;
		stored[L_DRUM_SH(v)][0] = 0.5f;      /* density (step 3)      */
		stored[L_DRUM_SH(v)][1] = 0.5f;      /* level: the kit's gain */
		stored[L_DRUM_SH(v)][2] = 0.5f;      /* length (step 3)       */
		stored[L_DRUM_SH(v)][3] = d.excite;
	}
	pickup_all();
}

void sp1_dui_init(void)
{
	defaults_kit_page();
	defaults_fx();
	defaults_set();
	sp1_dui_load_kit(0);
	mute = 0u;
	rest_choke = 1u;
	page = came_from = SP1_DUI_KIT;
	active = L_KIT;
	speed = 1.0f;
}

void sp1_dui_rip(void)
{
	defaults_kit_page();
	defaults_fx();
	defaults_set();
	sp1_dui_load_kit(kit);
	mute = 0u;
	rest_choke = 1u;
	rest_clear = 0x0Fu;
	rest_clear_seq++;
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
	int i = sp1_drums_model_index(eng[v], model[v]);
	for (int tries = 0; tries < n; tries++) {
		i = (i < 0) ? (dir > 0 ? 0 : n - 1) : (i + (dir > 0 ? 1 : n - 1)) % n;
		uint8_t e, m;
		sp1_drums_model_at(i, &e, &m);
		bool taken = false;
		if (sp1_drums_model_unique(e)) {
			for (int o = 0; o < 4; o++) {
				taken = taken || (o != v && eng[o] == e && model[o] == m);
			}
		}
		if (!taken) {
			eng[v] = e;
			model[v] = m;
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
	LOG("MODEL drum %d: %s (%d of %d)\n", v + 1, sp1_drums_model_name(eng[v], model[v]),
	    (i < 0 ? 0 : i) + 1, n);
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
			if (fnc) {
				if (!on_set) {
					fx_on ^= (uint8_t)(1u << n);
					uint8_t lv[4];
					mask_glyph(fx_on, lv);
					flash(lv);
					static const char *const fxn[4] = { "DJ filter", "fold",
									    "Chebyshev", "FM" };
					LOG("FX %s %s\n", fxn[n], (fx_on & (1u << n)) ? "on" : "off");
				}
			} else if (on_set) {
				mute ^= (uint8_t)(1u << n);
				uint8_t lv[4];
				mask_glyph((uint8_t)~mute, lv);
				flash(lv);
				LOG("MUTE drum %d %s\n", n + 1, (mute & (1u << n)) ? "muted" : "playing");
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
			sp1_dui_load_kit(kit + dir);
			uint8_t lv[4];
			kit_glyph(kit, lv);
			flash(lv);
			LOG("KIT %d: %s\n", kit + 1, sp1_drums_kit_name(kit));
		} else if (drum_page) {
			model_step(dn, dir);
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
		rest_choke = rwd_press ? 1u : 0u;
		const uint8_t lv[4] = { SP1_ENGINE_LED_FULL, rest_choke ? SP1_ENGINE_LED_FULL : 0u,
					rest_choke ? SP1_ENGINE_LED_FULL : 0u, SP1_ENGINE_LED_FULL };
		flash(lv);
		LOG("RESTS %s\n", rest_choke ? "choke" : "ring out");
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
				LOG("ROTATE the kit, on the next beat\n");
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
				LOG("STEP Marbles one tick\n");
			}
		}
		burst = ffwd_down && !ffwd_eaten && !fnc;
	}

	/* ---- tape slow: sink while held, ease back after ---- */
	static bool tape_was;
	if (tape != tape_was) {
		tape_was = tape;
		if (tape) {
			LOG("TAPE slow: sinking to x%d.%02d\n", (int)TAPE_FLOOR,
			    (int)(TAPE_FLOOR * 100.0f) % 100);
		} else {
			LOG("TAPE released at x%d.%02d: back over %d ms\n", (int)speed,
			    (int)(speed * 100.0f) % 100, (int)TAPE_RELEASE_MS);
		}
	}
	if (tape) {
		speed += (TAPE_FLOOR - speed) * (1.0f - expf(-(float)dt / TAPE_FALL_MS));
	} else if (speed < 1.0f) {
		speed += (1.0f - TAPE_FLOOR) * (float)dt / TAPE_RELEASE_MS;
		if (speed > 1.0f) {
			speed = 1.0f;
		}
	}
	if (speed < TAPE_FLOOR) {
		speed = TAPE_FLOOR;
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
	const float d_mod = (detent(kb[3], DETENT_BIPOLAR) - 0.5f) * 0.8f;
	for (int v = 0; v < 4; v++) {
		const float *b = stored[L_DRUM(v)];
		const float *sh = stored[L_DRUM_SH(v)];
		o->eng[v] = eng[v];
		o->model[v] = model[v];
		o->note[v] = b[0] * 120.0f + semis;
		o->tone[v] = clamp01(b[1] + d_tone);
		o->decay[v] = clamp01(b[2] + d_decay);
		o->mod[v] = clamp01(b[3] + d_mod);
		o->excite[v] = clamp01(sh[3]);
		o->level[v] = def_gain[v] * 2.0f * clamp01(sh[1]);
	}
	o->melodic = melodic;
	o->mute = mute;
	o->fx_on = fx_on;
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
	o->rest_choke = rest_choke;
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
int  sp1_dui_kit(void) { return kit; }

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
