/* wakes-sp1 drum firmware: the acid line's patterns and resolver -- see sp1_acid.h. */
#include "sp1_acid.h"

#include <string.h>

/* ---- the built-in patterns ----
 * p[] is tinyacid's order: cutoff, res, envmod, decay, accent, drive, wave, pw, sub, noise,
 * fm, slide, hold, osc. */
static const struct sp1_acid_preset kPresets[SP1_ACID_PRESETS] = {
	/* 1: the plain thing. C minor, accents on the ones, two slides up. */
	{ "acid-home",
	  "C2! C2 C3~ C2 . D#2! C2 G2~ | C2! . A#2! G2~ C2 C2 F2! D#2~",
	  0, { 0.32f, 0.75f, 0.60f, 0.30f, 0.70f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.50f, 0.50f, 1.0f },
	  0.50f, { 0.5f, 0.0f, 0.0f, 0.0f }, 126.0f },
	/* 2: slow and held. D mixolydian (the flat seventh under a major third), long ties, a
	 * soft pulse with a little noise, and a decay long enough to trail off under each tie. */
	{ "campfire",
	  "D2 _ _ A2 _ C3~ B2 _ | G2 _ _ D3 _ A2~ F#2 _",
	  1, { 0.38f, 0.35f, 0.35f, 0.75f, 0.30f, 0.10f, 0.55f, 0.35f, 0.50f, 0.08f, 0.0f, 0.70f, 0.55f, 1.0f },
	  0.90f, { 0.42f, 0.0f, 0.0f, 0.0f }, 86.0f },
	/* 3: A dorian (the raised sixth), rests that let the release ring, one octave lift. */
	{ "sixth-form",
	  "A1 . E2 _ G2~ F#2 _ . | A1^ _ E2~ D2 . C2 _ E2~",
	  1, { 0.30f, 0.50f, 0.50f, 0.85f, 0.40f, 0.0f, 0.20f, 0.60f, 0.60f, 0.12f, 0.0f, 0.80f, 0.40f, 1.0f },
	  0.80f, { 0.5f, 0.15f, 0.0f, 0.0f }, 92.0f },
	/* 4: the octave lane doing the work: one note, every other step an octave up. */
	{ "octave-hopper",
	  "E1! E1^ E1 E1^ . E1! E1^ G1~ | G1 G1^ . A1! A1^ A1~ B1 D2^3~",
	  0, { 0.40f, 0.70f, 0.55f, 0.20f, 0.80f, 0.25f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.50f, 0.50f, 1.0f },
	  0.45f, { 0.5f, 0.0f, 0.0f, 0.0f }, 132.0f },
	/* 5: nearly all slides and ties, with a long glide: the line never lets go. */
	{ "rubber",
	  "F1 _ G#1~ _ C2~ _ D#2~ F2~ | F1! . C2~ _ A#1~ _ G#1~ G1~",
	  0, { 0.30f, 0.80f, 0.70f, 0.50f, 0.60f, 0.10f, 0.0f, 0.0f, 0.20f, 0.0f, 0.0f, 0.85f, 0.80f, 1.0f },
	  0.50f, { 0.5f, 0.0f, 0.0f, 0.0f }, 118.0f },
	/* 6: sparse and filthy: sub, filter FM and drive, with Chebyshev on the bus. */
	{ "crawl",
	  "C1! . . C1 . D#1~ _ . | C1! . . G1~ F1 . A#0! .",
	  0, { 0.22f, 0.65f, 0.75f, 0.55f, 0.90f, 0.80f, 0.30f, 0.0f, 0.70f, 0.0f, 0.45f, 0.60f, 0.70f, 1.0f },
	  0.60f, { 0.5f, 0.0f, 0.30f, 0.0f }, 100.0f },
	/* 7: twelve steps, so it turns against a sixteen-step kit every three bars; a thin
	 * pulse through the wavefolder. */
	{ "fold-garden",
	  "G1 _ D2 B1~ . E2 _ D2~ . A1 B1~ D2",
	  1, { 0.45f, 0.45f, 0.40f, 0.60f, 0.30f, 0.20f, 0.70f, 0.50f, 0.30f, 0.0f, 0.0f, 0.50f, 0.50f, 1.0f },
	  0.70f, { 0.5f, 0.35f, 0.15f, 0.0f }, 104.0f },
	/* 8: accents in a row, which stack the cutoff, and marks with weights: the "!4" and
	 * "^5~3" only sound with the accent, octave and slide lanes' densities turned up. */
	{ "three-accents",
	  "A1! A1! A1! . A1 C2~ A1 . | E2! E2! D2!~ . A1 A1!4 G1 A1^5~3",
	  0, { 0.30f, 0.85f, 0.50f, 0.35f, 1.00f, 0.10f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.50f, 0.50f, 1.0f },
	  0.50f, { 0.5f, 0.0f, 0.0f, 0.0f }, 124.0f },
	/* 9: the user's line from drums.cnfg, with the plain sound. */
	{ "user", NULL,
	  0, { 0.32f, 0.75f, 0.60f, 0.30f, 0.70f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.50f, 0.50f, 1.0f },
	  0.50f, { 0.5f, 0.0f, 0.0f, 0.0f }, 126.0f },
};

static struct sp1_acid_pattern pats[SP1_ACID_PRESETS];

static int is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '|'; }

int sp1_acid_parse(struct sp1_acid_pattern *out, const char *t, size_t len)
{
	static const int8_t kPc[7] = { 9, 11, 0, 2, 4, 5, 7 };      /* A B C D E F G */
	struct sp1_acid_pattern p;
	size_t i = 0;
	int have_note = 0;
	uint8_t last = 36;

	memset(&p, 0, sizeof p);
	for (;;) {
		while (i < len && is_space(t[i])) {
			i++;
		}
		if (i >= len || t[i] == '\0') {
			break;
		}
		if (p.n >= SP1_ACID_STEPS) {
			return -(int)(i + 1);
		}
		struct sp1_acid_step *s = &p.s[p.n];
		s->oct = 1;
		if (t[i] == '.') {
			i++;
		} else if (t[i] == '_') {
			s->tie = 1;
			i++;
		} else if (t[i] >= 'A' && t[i] <= 'G') {
			int note = kPc[t[i] - 'A'];
			int oct = 0, neg = 0;
			i++;
			if (i < len && t[i] == '#') {
				note++;
				i++;
			} else if (i < len && t[i] == 'b') {
				note--;
				i++;
			}
			if (i < len && t[i] == '-') {
				neg = 1;
				i++;
			}
			if (i >= len || t[i] < '0' || t[i] > '9') {
				return -(int)(i + 1);
			}
			oct = t[i] - '0';
			i++;
			note += 12 * ((neg ? -oct : oct) + 1);
			if (note < 0 || note > 127) {
				return -(int)i;
			}
			s->note = (uint8_t)note;
			s->w[SP1_ACID_L_GATE] = 9;
			while (i < len && !is_space(t[i]) && t[i] != '\0') {
				const char m = t[i];
				int lane;
				if (m == '!') {
					lane = SP1_ACID_L_ACCENT;
				} else if (m == '~') {
					lane = SP1_ACID_L_SLIDE;
				} else if (m == '^' || m == 'v') {
					lane = SP1_ACID_L_OCTAVE;
					s->oct = m == '^' ? 1 : -1;
				} else if (m == '/') {
					lane = SP1_ACID_L_GATE;
				} else {
					return -(int)(i + 1);
				}
				i++;
				if (i < len && t[i] >= '0' && t[i] <= '9') {
					s->w[lane] = (uint8_t)(t[i] - '0');
					i++;
				} else if (m == '/') {
					return -(int)(i + 1);          /* a weight needs its digit */
				} else {
					s->w[lane] = 9;
				}
			}
			last = s->note;
			have_note = 1;
		} else {
			return -(int)(i + 1);
		}
		if (i < len && !is_space(t[i]) && t[i] != '\0') {
			return -(int)(i + 1);                  /* "._", ".C2": one token per step */
		}
		p.n++;
	}
	if (p.n == 0 || !have_note) {
		return -1;
	}
	/* A rest or a tie carries the pitch before it (the last one round the loop for a
	 * leading rest), so a gate lane of another length always finds a note to play. */
	for (int k = 0; k < 2 * p.n; k++) {
		struct sp1_acid_step *s = &p.s[k % p.n];
		if (s->w[SP1_ACID_L_GATE] > 0 && !s->tie) {
			last = s->note;
		} else {
			s->note = last;
		}
	}
	*out = p;
	return p.n;
}

int sp1_acid_format(const struct sp1_acid_pattern *p, char *buf, size_t size)
{
	static const char *const kName[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
	static const char kMark[SP1_ACID_LANES] = { '/', '!', '~', '^' };
	size_t at = 0;
#define PUT(c) do { if (at + 1 >= size) return -1; buf[at++] = (c); } while (0)
	for (int k = 0; k < p->n; k++) {
		const struct sp1_acid_step *s = &p->s[k];
		if (k) {
			PUT(' ');
		}
		if (s->tie) {
			PUT('_');
		} else if (s->w[SP1_ACID_L_GATE] == 0) {
			PUT('.');
		} else {
			const char *nm = kName[s->note % 12];
			const int oct = s->note / 12 - 1;
			PUT(nm[0]);
			if (nm[1]) {
				PUT(nm[1]);
			}
			if (oct < 0) {
				PUT('-');
			}
			PUT((char)('0' + (oct < 0 ? -oct : oct)));
			for (int l = SP1_ACID_L_ACCENT; l < SP1_ACID_LANES + 1; l++) {
				const int lane = l == SP1_ACID_LANES ? SP1_ACID_L_GATE : l;   /* the weight last */
				const uint8_t w = s->w[lane];
				if (lane == SP1_ACID_L_GATE ? w == 9 : w == 0) {
					continue;
				}
				PUT(lane == SP1_ACID_L_OCTAVE && s->oct < 0 ? 'v' : kMark[lane]);
				if (w != 9 || lane == SP1_ACID_L_GATE) {
					PUT((char)('0' + w));
				}
			}
		}
	}
#undef PUT
	buf[at] = '\0';
	return (int)at;
}

void sp1_acid_perf_default(struct sp1_acid_perf *f)
{
	for (int l = 0; l < SP1_ACID_LANES; l++) {
		f->dens[l] = 0.5f;
		f->len[l] = 0;
	}
	f->transpose = 0.0f;
}

void sp1_acid_seq_reset(struct sp1_acid_seq *q)
{
	memset(q, 0, sizeof *q);
	q->next_at = -1;
	q->last_note = 36.0f;
}

/* Does lane l sound on step g? Its own loop over the pattern's steps, its own density. */
static int fires(const struct sp1_acid_pattern *p, const struct sp1_acid_perf *f, int l, int32_t g,
		 const struct sp1_acid_step **at)
{
	int len = f->len[l] ? f->len[l] : p->n;
	if (len > SP1_ACID_STEPS) {
		len = SP1_ACID_STEPS;
	}
	const struct sp1_acid_step *s = &p->s[(g % len) % p->n];
	*at = s;
	const uint8_t w = s->w[l];
	return w > 0 && (float)w * (1.0f / 9.0f) > 1.0f - f->dens[l];
}

static void decide(struct sp1_acid_seq *q, const struct sp1_acid_pattern *p,
		   const struct sp1_acid_perf *f, int32_t g)
{
	const struct sp1_acid_step *pitch = &p->s[g % p->n], *s;
	q->next_at = g;
	q->next_tie = pitch->tie;
	q->next_gate = (uint8_t)fires(p, f, SP1_ACID_L_GATE, g, &s);
	q->next_acc = (uint8_t)fires(p, f, SP1_ACID_L_ACCENT, g, &s);
	q->next_slide = (uint8_t)fires(p, f, SP1_ACID_L_SLIDE, g, &s);
	float note = (float)pitch->note + f->transpose;
	if (fires(p, f, SP1_ACID_L_OCTAVE, g, &s)) {
		note += 12.0f * (float)s->oct;
	}
	q->next_note = note < 0.0f ? 0.0f : (note > 127.0f ? 127.0f : note);
}

struct sp1_acid_event sp1_acid_seq_step(struct sp1_acid_seq *q, const struct sp1_acid_pattern *p,
					const struct sp1_acid_perf *f, int32_t g16)
{
	struct sp1_acid_event e = { SP1_ACID_REST, 0, 0, q->last_note };
	if (p->n == 0 || g16 < 0) {
		q->sounding = 0;
		return e;
	}
	if (q->next_at != g16) {
		decide(q, p, f, g16);
	}
	const uint8_t gate = q->next_gate, tie = q->next_tie, acc = q->next_acc, slide = q->next_slide;
	const float note = q->next_note;
	decide(q, p, f, g16 + 1);                     /* the step after, decided once, now */

	if (tie) {
		e.kind = q->sounding ? SP1_ACID_TIE : SP1_ACID_REST;
	} else if (gate) {
		e.kind = (slide && q->sounding) ? SP1_ACID_SLIDE : SP1_ACID_NOTE;
		e.note = note;
		e.accent = acc;
		q->last_note = note;
	}
	/* The gate stays up for what follows: a tie, or a note that slides in. */
	e.hold = (uint8_t)(e.kind != SP1_ACID_REST && (q->next_tie || (q->next_gate && q->next_slide)));
	q->sounding = e.hold;
	return e;
}

const struct sp1_acid_preset *sp1_acid_preset(int i)
{
	return &kPresets[i >= 0 && i < SP1_ACID_PRESETS ? i : 0];
}

const struct sp1_acid_pattern *sp1_acid_pattern_of(int i)
{
	return &pats[i >= 0 && i < SP1_ACID_PRESETS ? i : 0];
}

int sp1_acid_set_user(const char *text, size_t len)
{
	return sp1_acid_parse(&pats[SP1_ACID_BUILTIN], text, len);
}

int sp1_acid_init(void)
{
	int bad = 0;
	for (int i = 0; i < SP1_ACID_BUILTIN; i++) {
		if (sp1_acid_parse(&pats[i], kPresets[i].line, strlen(kPresets[i].line)) < 0 && !bad) {
			bad = i + 1;
		}
	}
	pats[SP1_ACID_BUILTIN] = pats[0];              /* until drums.cnfg gives one */
	return bad;
}
