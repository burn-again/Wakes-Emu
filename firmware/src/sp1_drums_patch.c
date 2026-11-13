/* wakes-sp1 drum firmware: the [drums] section of a PRST slot, and drums.cnfg. See
 * sp1_drums_patch.h. No floating-point printf or strtod: numbers are written as fixed-point
 * integers and read by hand, as sp1_prst.c does. */
#include "sp1_drums_patch.h"

#include "sp1_drums.h"           /* the kits, the model list, the map bank */
#include "sp1_acid.h"            /* the acid patterns' names, the user line */

#include <stdio.h>
#include <string.h>

/* ---- numbers ---- */
static bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static const char *skip(const char *s, const char *e)
{
	while (s < e && is_space(*s)) {
		s++;
	}
	return s;
}

/* A plain decimal number: optional sign, digits, optional point and digits. No exponent, no
 * NaN or infinity. Returns where it stopped, or NULL if there was no number. */
static const char *num(const char *s, const char *e, float *out)
{
	bool neg = false;
	if (s < e && (*s == '-' || *s == '+')) {
		neg = *s == '-';
		s++;
	}
	float v = 0.0f;
	int digits = 0;
	while (s < e && *s >= '0' && *s <= '9' && digits < 9) {
		v = v * 10.0f + (float)(*s - '0');
		s++;
		digits++;
	}
	if (s < e && *s == '.') {
		s++;
		float k = 0.1f;
		while (s < e && *s >= '0' && *s <= '9') {
			v += k * (float)(*s - '0');
			k *= 0.1f;
			s++;
			digits++;
		}
	}
	if (digits == 0) {
		return NULL;
	}
	*out = neg ? -v : v;
	return s;
}

static bool key_is(const char *k, size_t n, const char *name)
{
	return strlen(name) == n && memcmp(k, name, n) == 0;
}

static bool lower_eq(const char *s, size_t n, const char *word)
{
	if (strlen(word) != n) {
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		char c = s[i];
		if (c >= 'A' && c <= 'Z') {
			c = (char)(c - 'A' + 'a');
		}
		if (c != word[i]) {
			return false;
		}
	}
	return true;
}

/* Up to `max` numbers separated by spaces or commas; returns how many were read, or -1 if
 * anything else was on the line. */
static int nums(const char *s, const char *e, float *out, int max)
{
	int n = 0;
	for (;;) {
		s = skip(s, e);
		while (s < e && *s == ',') {
			s = skip(s + 1, e);
		}
		if (s >= e) {
			return n;
		}
		if (n == max) {
			return -1;
		}
		const char *t = num(s, e, &out[n]);
		if (!t) {
			return -1;
		}
		n++;
		s = t;
	}
}

/* ---- the patch ---- */
static const char *const kLayerKey[SP1_DRUMS_LAYERS] = {
	"kit_faders", "kit_shift", "drum1_faders", "drum1_shift", "drum2_faders", "drum2_shift",
	"drum3_faders", "drum3_shift", "drum4_faders", "drum4_shift", "settings_faders",
	"settings_shift", "lanes_faders", "lanes_shift",
};
static const char *const kBassName[3] = { "off", "y", "rests" };

const char *sp1_drums_bass_name(unsigned mode)
{
	if (mode < 3u) {
		return kBassName[mode];
	}
	return mode < (unsigned)SP1_BASS_MODES ? sp1_acid_preset((int)mode - SP1_BASS_ACID)->name : kBassName[0];
}

static float len_value(int steps)
{
	static const int kLen[5] = { 8, 12, 16, 24, 32 };
	for (int i = 0; i < 5; i++) {
		if (kLen[i] >= steps) {
			return ((float)i + 0.5f) / 5.0f;
		}
	}
	return 0.9f;
}

void sp1_drums_patch_default(struct sp1_drums_patch *p, int kit)
{
	memset(p, 0, sizeof *p);
	kit = ((kit % SP1_DRUMS_KITS) + SP1_DRUMS_KITS) % SP1_DRUMS_KITS;
	p->kit = kit;
	for (int i = 0; i < 4; i++) {
		p->v[0][i] = 0.5f;                       /* KIT: centred offsets, fill 0  */
	}
	p->v[1][0] = 0.5f;                               /* the DJ filter: off            */
	for (int n = 0; n < 4; n++) {
		struct sp1_drums_voice_def d;
		sp1_drums_kit_voice(kit, n, &d);
		p->eng[n] = d.eng;
		p->model[n] = d.model;
		p->gain[n] = d.gain;
		if (d.melodic) {
			p->melodic |= (uint8_t)(1u << n);
		}
		float *b = p->v[2 + 2 * n], *sh = p->v[3 + 2 * n];
		b[0] = d.note / 120.0f;
		b[1] = d.tone;
		b[2] = d.decay;
		b[3] = d.mod;
		sh[0] = 0.5f;                                /* density                       */
		sh[1] = 0.5f;                                /* level: the kit's own          */
		sh[2] = len_value(sp1_drums_map_len(kit, n)); /* length: the map's own       */
		sh[3] = d.excite;
		p->pat[n] = (uint8_t)kit;
	}
	p->v[10][0] = 0.0f;                              /* swing                         */
	p->v[10][1] = 0.0f;                              /* morph: map A                  */
	p->v[10][2] = 0.5f;                              /* FM carrier: G3                */
	p->v[10][3] = 0.0f;                              /* bus drive: unity              */
	p->v[11][0] = 0.5f;                              /* bass octave: 0                */
	p->v[11][1] = 0.0f;                              /* bass shape: saw               */
	p->v[11][2] = 0.4f;                              /* bass cutoff                   */
	p->v[11][3] = 0.4f;                              /* bass decay                    */
	for (int i = 0; i < 4; i++) {
		p->v[12][i] = 0.5f;                          /* the acid lanes' densities: as written */
		p->v[13][i] = 1.0f;                          /* their lengths: the pattern's own      */
	}
	p->fx_on = 0x0Fu;
	p->rest_choke = 1u;
	p->bass_mode = 0u;
}

static int put(char *buf, size_t size, size_t *at, const char *fmt, int a, int b, int c, int d)
{
	if (*at >= size) {
		return -1;
	}
	const int n = snprintf(buf + *at, size - *at, fmt, a, b, c, d);
	if (n < 0 || (size_t)n >= size - *at) {
		return -1;
	}
	*at += (size_t)n;
	return 0;
}

static int put4(char *buf, size_t size, size_t *at, const char *key, const float v[4])
{
	const int n = snprintf(buf + *at, size - *at, "%s =", key);
	if (n < 0 || (size_t)n >= size - *at) {
		return -1;
	}
	*at += (size_t)n;
	for (int i = 0; i < 4; i++) {
		const float x = v[i] < 0.0f ? 0.0f : (v[i] > 1.0f ? 1.0f : v[i]);
		const int k = (int)(x * 10000.0f + 0.5f);
		if (put(buf, size, at, " %d.%04d", k / 10000, k % 10000, 0, 0) < 0) {
			return -1;
		}
	}
	return put(buf, size, at, "\n", 0, 0, 0, 0);
}

int sp1_drums_patch_write(const struct sp1_drums_patch *p, char *buf, size_t size)
{
	size_t at = 0;
	if (size == 0u) {
		return -1;
	}
	buf[0] = '\0';
	if (put(buf, size, &at, "\n[drums]\nkit = %d\nvoices =", p->kit + 1, 0, 0, 0) < 0) {
		return -1;
	}
	for (int n = 0; n < 4; n++) {
		if (put(buf, size, &at, " %d.%d", p->eng[n], p->model[n], 0, 0) < 0) {
			return -1;
		}
	}
	if (put(buf, size, &at, "\ngains =", 0, 0, 0, 0) < 0) {
		return -1;
	}
	for (int n = 0; n < 4; n++) {
		const int k = (int)(p->gain[n] * 10000.0f + 0.5f);
		if (put(buf, size, &at, " %d.%04d", k / 10000, k % 10000, 0, 0) < 0) {
			return -1;
		}
	}
	if (put(buf, size, &at, "\nmelodic = %d\n", p->melodic, 0, 0, 0) < 0) {
		return -1;
	}
	for (int l = 0; l < SP1_DRUMS_LAYERS; l++) {
		if (put4(buf, size, &at, kLayerKey[l], p->v[l]) < 0) {
			return -1;
		}
	}
	if (put(buf, size, &at, "fx = %d\nmute = %d\n", p->fx_on, p->mute, 0, 0) < 0) {
		return -1;
	}
	const int n1 = snprintf(buf + at, size - at, "rests = %s\nbass = %s\n",
				p->rest_choke ? "choke" : "ring",
				sp1_drums_bass_name(p->bass_mode));
	if (n1 < 0 || (size_t)n1 >= size - at) {
		return -1;
	}
	at += (size_t)n1;
	if (put(buf, size, &at, "patterns = %d %d %d %d\n", p->pat[0] + 1, p->pat[1] + 1,
		p->pat[2] + 1, p->pat[3] + 1) < 0 ||
	    put(buf, size, &at, "t_cables = %d %d %d %d\n", p->t_dest[0], p->t_dest[1],
		p->t_dest[2], p->t_dest[3]) < 0 ||
	    put(buf, size, &at, "x_cables = %d %d %d %d\n", p->x_dest[0], p->x_dest[1],
		p->x_dest[2], p->x_dest[3]) < 0) {
		return -1;
	}
	return (int)at;
}

/* May X output k (3 = Y) carry destination d? Y has the bass note and no own-slot four. */
static bool x_allowed(int k, int d)
{
	if (d < 0 || d >= SP1_DX_COUNT) {
		return false;
	}
	if (k == 3) {
		return d == SP1_DX_NONE || d == SP1_DX_BASS_NOTE ||
		       (d >= SP1_DX_KIT_TONE && d <= SP1_DX_FM);
	}
	return d != SP1_DX_BASS_NOTE;
}

static bool field(struct sp1_drums_patch *p, const char *k, size_t kn, const char *v, const char *e)
{
	float f[4];
	if (key_is(k, kn, "kit")) {
		if (nums(v, e, f, 1) != 1 || f[0] < 1.0f || f[0] > (float)SP1_DRUMS_KITS) {
			return false;
		}
		p->kit = (int)f[0] - 1;
		return true;
	}
	if (key_is(k, kn, "voices")) {
		/* "engine.model" four times, whole numbers each side of the point */
		uint8_t eng[4], mod[4];
		const char *s = v;
		for (int n = 0; n < 4; n++) {
			s = skip(s, e);
			int a = 0, b = 0, da = 0, db = 0;
			while (s < e && *s >= '0' && *s <= '9' && da < 3) {
				a = a * 10 + (*s++ - '0');
				da++;
			}
			if (da == 0 || s >= e || *s != '.') {
				return false;
			}
			s++;
			while (s < e && *s >= '0' && *s <= '9' && db < 3) {
				b = b * 10 + (*s++ - '0');
				db++;
			}
			if (db == 0 || (s < e && !is_space(*s) && *s != ',') ||
			    !sp1_drums_model_ok((uint8_t)a, (uint8_t)b)) {
				return false;
			}
			eng[n] = (uint8_t)a;
			mod[n] = (uint8_t)b;
			while (s < e && *s == ',') {
				s++;
			}
		}
		if (skip(s, e) != e) {
			return false;
		}
		memcpy(p->eng, eng, 4);
		memcpy(p->model, mod, 4);
		return true;
	}
	if (key_is(k, kn, "gains")) {
		if (nums(v, e, f, 4) != 4) {
			return false;
		}
		for (int n = 0; n < 4; n++) {
			if (f[n] < 0.0f || f[n] > 2.0f) {
				return false;
			}
		}
		memcpy(p->gain, f, sizeof f);
		return true;
	}
	for (int l = 0; l < SP1_DRUMS_LAYERS; l++) {
		if (key_is(k, kn, kLayerKey[l])) {
			if (nums(v, e, f, 4) != 4) {
				return false;
			}
			for (int i = 0; i < 4; i++) {
				if (f[i] < 0.0f || f[i] > 1.0f) {
					return false;
				}
			}
			memcpy(p->v[l], f, sizeof f);
			return true;
		}
	}
	if (key_is(k, kn, "melodic") || key_is(k, kn, "fx") || key_is(k, kn, "mute")) {
		if (nums(v, e, f, 1) != 1 || f[0] < 0.0f || f[0] > 15.0f || f[0] != (float)(int)f[0]) {
			return false;
		}
		uint8_t *dst = key_is(k, kn, "melodic") ? &p->melodic
			     : key_is(k, kn, "fx") ? &p->fx_on : &p->mute;
		*dst = (uint8_t)(int)f[0];
		return true;
	}
	const char *s = skip(v, e);
	size_t wn = 0;
	while (s + wn < e && !is_space(s[wn])) {
		wn++;
	}
	if (key_is(k, kn, "rests")) {
		if (lower_eq(s, wn, "choke") || lower_eq(s, wn, "ring")) {
			p->rest_choke = lower_eq(s, wn, "choke") ? 1u : 0u;
			return true;
		}
		return false;
	}
	if (key_is(k, kn, "bass")) {
		for (int m = 0; m < SP1_BASS_MODES; m++) {
			if (lower_eq(s, wn, sp1_drums_bass_name((unsigned)m))) {
				p->bass_mode = (uint8_t)m;
				return true;
			}
		}
		return false;
	}
	if (key_is(k, kn, "patterns")) {
		if (nums(v, e, f, 4) != 4) {
			return false;
		}
		for (int n = 0; n < 4; n++) {
			if (f[n] < 1.0f || f[n] > (float)SP1_DRUMS_KITS || f[n] != (float)(int)f[n]) {
				return false;
			}
		}
		for (int n = 0; n < 4; n++) {
			p->pat[n] = (uint8_t)((int)f[n] - 1);
		}
		return true;
	}
	if (key_is(k, kn, "t_cables") || key_is(k, kn, "x_cables")) {
		const bool t = key_is(k, kn, "t_cables");
		if (nums(v, e, f, 4) != 4) {
			return false;
		}
		for (int n = 0; n < 4; n++) {
			const int d = (int)f[n];
			if (f[n] != (float)d || (t ? (d < 0 || d >= SP1_DT_COUNT) : !x_allowed(n, d))) {
				return false;
			}
		}
		for (int n = 0; n < 4; n++) {
			(t ? p->t_dest : p->x_dest)[n] = (uint8_t)(int)f[n];
		}
		return true;
	}
	return true;                       /* a key this firmware does not know: ignored */
}

int sp1_drums_patch_parse(const char *text, size_t n, struct sp1_drums_patch *p)
{
	const char *s = text, *end = text + n;
	bool in = false;
	int bad = 0;
	p->present = false;
	while (s < end) {
		const char *eol = s;
		while (eol < end && *eol != '\n') {
			eol++;
		}
		const char *a = skip(s, eol), *b = eol;
		while (b > a && is_space(b[-1])) {
			b--;
		}
		if (a < b && *a == '[') {
			in = (b - a == 7) && memcmp(a, "[drums]", 7) == 0;
			p->present = p->present || in;
		} else if (in && a < b && *a != ';' && *a != '#') {
			const char *eq = a;
			while (eq < b && *eq != '=') {
				eq++;
			}
			if (eq < b) {
				const char *ke = eq;
				while (ke > a && is_space(ke[-1])) {
					ke--;
				}
				if (!field(p, a, (size_t)(ke - a), eq + 1, b)) {
					bad++;
				}
			}
		}
		s = eol + 1;
	}
	/* A classic voice on two drums at once is one LXR instance: the later one goes to FM. */
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < i; j++) {
			if (sp1_drums_model_unique(p->eng[i]) && p->eng[i] == p->eng[j] &&
			    p->model[i] == p->model[j]) {
				p->eng[i] = 7u;                /* TK_ENG_FM */
				p->model[i] = 0u;
				bad++;
			}
		}
	}
	return bad;
}

/* ---- drums.cnfg ---- */
const char SP1_DRUMS_CNFG_TEMPLATE[] =
"# drums.cnfg -- the drum firmware's settings, read at every ON.\n"
"# A line that does not read keeps its default, and the log names it.\n"
"# Delete this file and the next ON writes it back as it is here.\n"
"\n"
"[drums]\n"
"# The kit at power-on, 1-8, when the preset slot has no drums of its own:\n"
"# 1 lich techno, 2 8-bit breaks, 3 modal gamelan, 4 idm rungler, 5 dub techno,\n"
"# 6 iron vibes, 7 glass rungler, 8 gong garden (classic in a classic build)\n"
"kit = 1\n"
"# Rests: choke (fade what rings) or ring (tails sound through). SETTINGS RWD / FFWD.\n"
"rest_style = choke\n"
"# A kit rotation (KIT, FFWD tap) lands on the next: step, beat or bar\n"
"rotate_quantize = beat\n"
"# Tape slow (KIT, RWD held while playing)\n"
"tape_floor = 0.33       ; the slowest speed, 0.1 .. 1\n"
"tape_fall = 0.7         ; seconds (a time constant) to sink towards it\n"
"tape_release = 1.2      ; seconds to speed back up; 0 = at once\n"
"\n"
"[bass]\n"
"# off, y (on the Y clock, Y picks the note) or rests (8th notes where every drum rests).\n"
"# SETTINGS \"••\" + FFWD / RWD changes it live; a preset keeps its own.\n"
"mode = off\n"
"root = 36               ; MIDI note of the lowest degree\n"
"octaves = 2             ; how far Y reaches, 1 .. 4\n"
"resonance = 0.4         ; 0 .. 1\n"
"envelope = 0.5          ; the filter envelope, 0 .. 1\n"
"glide = 0.15            ; 0 .. 1\n"
"sub = 0.4               ; the square an octave down, 0 .. 1\n"
"level = -15             ; dB in the mix, -40 .. 0\n"
"\n"
"[acid]\n"
"# The acid line plays in the bass's place when the bass mode is an acid pattern: step to\n"
"# one with SETTINGS \"••\" + FFWD / RWD, or name it above (mode = acid-home). The patterns:\n"
"# acid-home, campfire, sixth-form, octave-hopper, rubber, crawl, fold-garden,\n"
"# three-accents, jungle, raga, reggaeton, cumbia, thrash, ambient, markov,\n"
"# electro-house, french-house, and user: the line below.\n"
"# SETTINGS + SHIFT is then its cutoff, resonance, decay and drive, and two more \"••\" taps\n"
"# from SETTINGS open LANES: each lane's density, and with SHIFT its loop length.\n"
"level = -12             ; dB in the mix, -40 .. 0\n"
"duck = 0.5              ; octaves the kick dips its cutoff, 0 .. 3\n"
"transpose = 0           ; semitones, -24 .. 24\n"
"# Your own pattern, one token per 16th, up to 32: a note (C2, D#2, Bb1), . a rest,\n"
"# _ a tie; after a note ! accent, ~ slide into it, ^ or v an octave up or down, each\n"
"# with an optional weight 1-9 (C2!4), and /5 the note's own weight.\n"
"# line = C2! C2 C3~ C2 . D#2! C2 G2~ C2! . A#2! G2~ C2 C2 F2! D#2~\n";
const size_t SP1_DRUMS_CNFG_TEMPLATE_LEN = sizeof(SP1_DRUMS_CNFG_TEMPLATE) - 1u;

void sp1_drums_cfg_defaults(struct sp1_drums_cfg *c)
{
	c->kit = 0;
	c->rest_choke = 1;
	c->rotate_q = 1;
	c->tape_floor = 0.33f;
	c->tape_fall = 0.7f;
	c->tape_release = 1.2f;
	c->bass_mode = 0;
	c->bass_root = 36.0f;
	c->bass_octaves = 2;
	c->bass_res = 0.4f;
	c->bass_env = 0.5f;
	c->bass_glide = 0.15f;
	c->bass_sub = 0.4f;
	c->bass_level_db = -15.0f;
	c->acid_level_db = -12.0f;
	c->acid_duck = 0.5f;
	c->acid_transpose = 0.0f;
}

void sp1_drums_cfg_begin(struct sp1_drums_cfg_reader *r, struct sp1_drums_cfg *c,
			 sp1_drums_cfg_problem problem, void *ctx)
{
	memset(r, 0, sizeof *r);
	r->c = c;
	r->problem = problem;
	r->ctx = ctx;
	sp1_drums_cfg_defaults(c);
}

static void bad_line(struct sp1_drums_cfg_reader *r, const char *what, const char *why)
{
	r->bad++;
	if (r->problem) {
		r->problem(r->ctx, r->lineno, what, why);
	}
}

/* A number in [lo, hi], else the line is bad. */
static bool cfg_num(struct sp1_drums_cfg_reader *r, const char *key, const char *v, const char *e,
		    float lo, float hi, float *out)
{
	float f;
	if (nums(v, e, &f, 1) != 1) {
		bad_line(r, key, "not a number");
		return false;
	}
	if (f < lo || f > hi) {
		bad_line(r, key, "out of range");
		return false;
	}
	*out = f;
	return true;
}

static void cfg_line(struct sp1_drums_cfg_reader *r, const char *a, const char *b)
{
	struct sp1_drums_cfg *c = r->c;
	/* strip an inline comment */
	for (const char *p = a; p < b; p++) {
		if (*p == ';' || *p == '#') {
			b = p;
			break;
		}
	}
	a = skip(a, b);
	while (b > a && is_space(b[-1])) {
		b--;
	}
	if (a >= b) {
		return;
	}
	if (*a == '[') {
		const size_t n = (size_t)(b - a);
		if (n < sizeof r->section) {
			memcpy(r->section, a, n);
			r->section[n] = '\0';
		}
		if (strcmp(r->section, "[drums]") != 0 && strcmp(r->section, "[bass]") != 0 &&
		    strcmp(r->section, "[acid]") != 0) {
			bad_line(r, r->section, "not a section of this file");
		}
		return;
	}
	const char *eq = a;
	while (eq < b && *eq != '=') {
		eq++;
	}
	if (eq >= b) {
		bad_line(r, "line", "no '='");
		return;
	}
	const char *ke = eq;
	while (ke > a && is_space(ke[-1])) {
		ke--;
	}
	const size_t kn = (size_t)(ke - a);
	const char *v = skip(eq + 1, b);
	const size_t vn = (size_t)(b - v);
	const bool bass = strcmp(r->section, "[bass]") == 0;
	char key[24];
	snprintf(key, sizeof key, "%.*s", (int)(kn < 23u ? kn : 23u), a);
	float f;
	if (strcmp(r->section, "[acid]") == 0) {
		if (key_is(a, kn, "level")) {
			(void)cfg_num(r, key, v, b, -40.0f, 0.0f, &c->acid_level_db);
		} else if (key_is(a, kn, "duck")) {
			(void)cfg_num(r, key, v, b, 0.0f, 3.0f, &c->acid_duck);
		} else if (key_is(a, kn, "transpose")) {
			(void)cfg_num(r, key, v, b, -24.0f, 24.0f, &c->acid_transpose);
		} else if (key_is(a, kn, "line")) {
			if (sp1_acid_set_user(v, vn) < 0) {
				bad_line(r, key, "not a pattern");
			}
		} else {
			bad_line(r, key, "unknown key");
		}
		return;
	}
	if (!bass && key_is(a, kn, "kit")) {
		if (cfg_num(r, key, v, b, 1.0f, (float)SP1_DRUMS_KITS, &f)) {
			c->kit = (int)f - 1;
		}
	} else if (!bass && key_is(a, kn, "rest_style")) {
		if (lower_eq(v, vn, "choke") || lower_eq(v, vn, "ring")) {
			c->rest_choke = lower_eq(v, vn, "choke") ? 1 : 0;
		} else {
			bad_line(r, key, "choke or ring");
		}
	} else if (!bass && key_is(a, kn, "rotate_quantize")) {
		if (lower_eq(v, vn, "step")) {
			c->rotate_q = 0;
		} else if (lower_eq(v, vn, "beat")) {
			c->rotate_q = 1;
		} else if (lower_eq(v, vn, "bar")) {
			c->rotate_q = 2;
		} else {
			bad_line(r, key, "step, beat or bar");
		}
	} else if (!bass && key_is(a, kn, "tape_floor")) {
		(void)cfg_num(r, key, v, b, 0.1f, 1.0f, &c->tape_floor);
	} else if (!bass && key_is(a, kn, "tape_fall")) {
		(void)cfg_num(r, key, v, b, 0.05f, 10.0f, &c->tape_fall);
	} else if (!bass && key_is(a, kn, "tape_release")) {
		(void)cfg_num(r, key, v, b, 0.0f, 10.0f, &c->tape_release);
	} else if (bass && key_is(a, kn, "mode")) {
		if (lower_eq(v, vn, "off")) {
			c->bass_mode = 0;
		} else if (lower_eq(v, vn, "y")) {
			c->bass_mode = 1;
		} else if (lower_eq(v, vn, "rests")) {
			c->bass_mode = 2;
		} else {
			int m = SP1_BASS_ACID;
			while (m < SP1_BASS_MODES && !lower_eq(v, vn, sp1_drums_bass_name((unsigned)m))) {
				m++;
			}
			if (m < SP1_BASS_MODES) {
				c->bass_mode = m;
			} else {
				bad_line(r, key, "off, y, rests or an acid pattern's name");
			}
		}
	} else if (bass && key_is(a, kn, "root")) {
		(void)cfg_num(r, key, v, b, 12.0f, 72.0f, &c->bass_root);
	} else if (bass && key_is(a, kn, "octaves")) {
		if (cfg_num(r, key, v, b, 1.0f, 4.0f, &f)) {
			c->bass_octaves = (int)f;
		}
	} else if (bass && key_is(a, kn, "resonance")) {
		(void)cfg_num(r, key, v, b, 0.0f, 1.0f, &c->bass_res);
	} else if (bass && key_is(a, kn, "envelope")) {
		(void)cfg_num(r, key, v, b, 0.0f, 1.0f, &c->bass_env);
	} else if (bass && key_is(a, kn, "glide")) {
		(void)cfg_num(r, key, v, b, 0.0f, 1.0f, &c->bass_glide);
	} else if (bass && key_is(a, kn, "sub")) {
		(void)cfg_num(r, key, v, b, 0.0f, 1.0f, &c->bass_sub);
	} else if (bass && key_is(a, kn, "level")) {
		(void)cfg_num(r, key, v, b, -40.0f, 0.0f, &c->bass_level_db);
	} else {
		bad_line(r, key, "not a setting of this file");
	}
}

void sp1_drums_cfg_feed(struct sp1_drums_cfg_reader *r, const char *t, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		const char ch = t[i];
		if (ch == '\n') {
			r->lineno++;
			if (r->too_long) {
				bad_line(r, "line", "too long");
			} else {
				cfg_line(r, r->line, r->line + r->len);
			}
			r->len = 0;
			r->too_long = false;
		} else if (r->len < sizeof r->line - 1u) {
			r->line[r->len++] = ch;
		} else {
			r->too_long = true;
		}
	}
}

int sp1_drums_cfg_end(struct sp1_drums_cfg_reader *r)
{
	if (r->len > 0u || r->too_long) {             /* a last line with no newline */
		r->lineno++;
		if (r->too_long) {
			bad_line(r, "line", "too long");
		} else {
			cfg_line(r, r->line, r->line + r->len);
		}
		r->len = 0;
	}
	return r->bad;
}
