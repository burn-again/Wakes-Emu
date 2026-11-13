/* tinyacid.c -- see tinyacid.h. MIT. Original code; float32 only, libm only when a setting changes. */
#include "tinyacid.h"
#include <math.h>
#include <string.h>

#ifndef TA_OS
#define TA_OS 2          /* ladder passes per output sample (1 or 2) */
#endif

#define SR 48000.0f
#define TWO32 4294967296.0f

/* What makes a character: constants only, one render path. */
typedef struct {
  float pole1;       /* the first pole's cutoff, as a multiple of the other three */
  float kmax;        /* feedback at RES = 1 (four equal poles oscillate at 4) */
  float fb_hp;       /* high-pass in the feedback path, Hz (0 = none) */
  float comp;        /* how much of the passband RES gives back */
  float env_oct;     /* ENVMOD = 1 opens the filter this many octaves */
  float acc_oct;     /* the accent capacitor's reach, octaves */
  float acc_amp;     /* an accent's extra level */
  float acc_env;     /* an accent's extra envelope depth when there is no capacitor */
  float keytrack;    /* octaves of cutoff per octave of pitch */
  float drive_pre, drive_post;
  int adsr;          /* 1: one envelope with a sustain for filter and amp. 0: the 303's two */
} ta_row;

static const ta_row ROWS[TA_NCHAR] = {
  /* 303 */ { 2.0f, 3.9f, 150.0f, 0.35f, 4.5f, 2.5f, 1.0f, 0.0f, 0.0f, 2.0f, 6.0f, 0 },
  /* 101 */ { 1.0f, 4.2f,   0.0f, 0.15f, 5.0f, 0.0f, 0.6f, 0.5f, 0.5f, 4.0f, 3.0f, 1 },
};

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

/* PolyBLEP residual for a discontinuity at phase 0, t = phase in 0..1, dt = increment */
static inline float blep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}

/* 2^x within 2e-5 (0.03 cent), no libm: the integer part into the exponent, a polynomial for the rest */
static inline float exp2_fast(float x) {
  x = clampf(x, -60.0f, 60.0f);
  int i = (int)x; if ((float)i > x) --i;
  const float f = x - (float)i;
  const float m = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f + f * (0.00961813f + f * (0.00133336f + f * 0.00015404f)))));
  union { uint32_t u; float f; } s; s.u = (uint32_t)(i + 127) << 23;
  return m * s.f;
}

/* soft clip: linear near 0, flat at +-1 from +-1.5 */
static inline float sat(float x) {
  x = clampf(x, -1.5f, 1.5f);
  return x - x * x * x * (4.0f / 27.0f);
}

typedef struct { float G, G1, ia, ib, D; } ta_coef;

/* The ladder's coefficients for a cutoff g = tan(pi fc / fs) and feedback k. */
static inline ta_coef coef(float g, float pole1, float k) {
  ta_coef c;
  const float a = 1.0f + g, b = 1.0f + g * pole1;
  const float inv = 1.0f / (a * b);
  c.ia = b * inv; c.ib = a * inv;
  c.G = g * c.ia; c.G1 = g * pole1 * c.ib;
  c.D = 1.0f / (1.0f + k * c.G1 * c.G * c.G * c.G);
  return c;
}

/* v->dirty: which of ta_update's rates a change has made stale. Cutoff, drive and the mixer
   are read straight from p[] each block, so a fader sweeping them costs no libm at all. */
enum { D_ENV = 1, D_CAP = 2, D_GLIDE = 4, D_ALL = 7 };

void ta_init(ta_voice *v) {
  memset(v, 0, sizeof *v);
  v->p[TA_CUTOFF] = 0.35f; v->p[TA_RES] = 0.6f; v->p[TA_ENVMOD] = 0.5f; v->p[TA_DECAY] = 0.3f;
  v->p[TA_ACCENT] = 0.6f; v->p[TA_SLIDE] = 0.5f; v->p[TA_HOLD] = 0.5f; v->p[TA_OSC] = 1.0f;
  v->note = v->target = 36.0f;
  v->rng = 0x2545F491u;
  v->accg = v->accg_t = 1.0f;
  v->dirty = D_ALL;
}

void ta_character(ta_voice *v, unsigned chr) {
  if (chr < TA_NCHAR && chr != v->chr) { v->chr = (uint8_t)chr; v->dirty = D_ALL; }
}

void ta_set(ta_voice *v, unsigned k, float x) {
  if (k >= TA_NPARAM) return;
  x = clampf(x, 0.0f, 1.0f);
  if (x == v->p[k]) return;
  v->p[k] = x;
  if (k == TA_DECAY || k == TA_HOLD) v->dirty |= D_ENV;
  else if (k == TA_RES) v->dirty |= D_CAP;
  else if (k == TA_SLIDE) v->dirty |= D_GLIDE;
}

int ta_active(const ta_voice *v) { return v->gate || v->att || v->amp > 1e-4f; }
unsigned ta_voice_size(void) { return (unsigned)sizeof(ta_voice); }

void ta_note(ta_voice *v, float note, unsigned flags) {
  const ta_row *r = &ROWS[v->chr];
  const int sounding = v->gate && v->amp > 0.02f;
  const int acc = (flags & TA_F_ACCENT) != 0;
  v->target = clampf(note, 0.0f, 127.0f);
  v->accented = (uint8_t)acc;                      /* both decays are kept ready: no libm here */
  v->accg_t = 1.0f + (acc ? v->p[TA_ACCENT] * r->acc_amp : 0.0f);
  v->gate = 1;
  if ((flags & TA_F_SLIDE) && sounding) return;     /* legato: the envelopes carry on, the pitch glides */
  v->note = v->target;
  v->att = 1;                                        /* ramp from the current level */
  v->fenv = 1.0f;
}

void ta_gate_off(ta_voice *v) { v->gate = 0; }

void ta_duck(ta_voice *v, float oct) { v->duck = clampf(v->duck + oct, 0.0f, 6.0f); }

/* The rates that only change with a setting, an accent or the block length. */
static void ta_update(ta_voice *v, unsigned n) {
  const ta_row *r = &ROWS[v->chr];
  const float fn = (float)n;
  const unsigned what = n != v->blk_n ? (unsigned)D_ALL : v->dirty;
  if (what & D_ENV) {
    /* times are to -60 dB, so tau = T / 6.9 */
    const float dec = 0.2f * exp2f(v->p[TA_DECAY] * 3.65f);                 /* 0.2 .. 2.5 s */
    v->fenv_kb = expf(-6.9f * fn / (dec * SR));
    v->fenv_kb_acc = r->adsr ? v->fenv_kb : expf(-6.9f * fn / (0.2f * SR));
    v->atk_step = 1.0f / ((r->adsr ? 0.0015f : 0.003f) * SR);
    if (r->adsr) {
      v->amp_sus = v->fenv_sus = v->p[TA_HOLD];
      v->amp_kd = expf(-6.9f / (dec * SR));
      v->amp_kr = expf(-6.9f / (dec * 0.6f * SR));
      v->fenv_krb = expf(-6.9f * fn / (dec * 0.6f * SR));
    } else {
      v->amp_sus = v->fenv_sus = 0.0f;
      v->amp_kd = expf(-6.9f / (0.3f * exp2f(v->p[TA_HOLD] * 3.7f) * SR));  /* 0.3 .. 4 s while held */
      v->amp_kr = expf(-6.9f / (0.016f * SR));
      v->fenv_krb = 0.0f;                                                    /* unused: the filter's carries on */
    }
  }
  if (what & D_CAP) v->cap_kb = expf(-fn / ((0.02f + 0.12f * v->p[TA_RES]) * SR));
  if (what & D_GLIDE) v->glide_kb = expf(-fn / (0.015f * exp2f(v->p[TA_SLIDE] * 4.0f) * SR)); /* tau 15 .. 240 ms */
  if (n != v->blk_n) v->duck_kb = expf(-fn / (0.12f * SR));
  v->blk_n = n;
  v->dirty = 0;
}

/* One pass of the ladder. u is the input after the feedback, solved without a delay. */
#define LADDER(X) do {                                                             \
    const float S = ((s1 * c.ib * c.G + s2 * c.ia) * c.G + s3 * c.ia) * c.G + s4 * c.ia; \
    float u = sat(((X) * xg - k * (S - hp)) * c.D);                                \
    float q;                                                                       \
    q = (u - s1) * c.G1; u = q + s1; s1 = u + q;                                   \
    q = (u - s2) * c.G;  u = q + s2; s2 = u + q;                                   \
    q = (u - s3) * c.G;  u = q + s3; s3 = u + q;                                   \
    q = (u - s4) * c.G;  u = q + s4; s4 = u + q;                                   \
    hp += hp_a * (u - hp);                                                         \
    y = u;                                                                         \
  } while (0)

void ta_render_mix(ta_voice *restrict v, float *restrict out, unsigned n, float gain) {
  if (n == 0u) return;
  if (v->dirty || n != v->blk_n) ta_update(v, n);
  if (!ta_active(v)) {
    /* at rest nothing is rendered and the filter empties, so the next note starts clean. The
       envelope, the accent capacitor and the duck run on at their own rates: a 303's gate is
       half a step, and accents stack across the gap. */
    v->s1 = v->s2 = v->s3 = v->s4 = v->hp = v->x1 = 0.0f; v->amp = 0.0f;
    v->fenv *= ROWS[v->chr].adsr ? v->fenv_krb : (v->accented ? v->fenv_kb_acc : v->fenv_kb);
    v->cap = (v->accented && ROWS[v->chr].acc_oct > 0.0f ? v->fenv : 0.0f) * (1.0f - v->cap_kb) + v->cap * v->cap_kb;
    v->duck *= v->duck_kb;
    return;
  }
  const ta_row *r = &ROWS[v->chr];
  const float fn = (float)n, rn = 1.0f / fn;

  /* ---- once per block: pitch, envelopes, the cutoff as a sum of octaves ---- */
  v->note = v->target + (v->note - v->target) * v->glide_kb;
  if (fabsf(v->note - v->target) < 0.001f) v->note = v->target;
  const float freq = 8.1757989f * exp2_fast(v->note * (1.0f / 12.0f)) * (1.0f / SR);
  const uint32_t inc1 = (uint32_t)(clampf(freq, 1.0f / SR, 0.2f) * TWO32);
  uint32_t inc = v->inc ? v->inc : inc1;                               /* ramps to inc1 over the block */
  const int32_t dinc = ((int32_t)inc1 - (int32_t)inc) / (int32_t)n;        /* both are under 2^30 */

  const int held = v->gate || !r->adsr;
  if (held) v->fenv = v->fenv_sus + (v->fenv - v->fenv_sus) * (v->accented ? v->fenv_kb_acc : v->fenv_kb);
  else v->fenv *= v->fenv_krb;
  const float cap_in = (v->accented && r->acc_oct > 0.0f) ? v->fenv : 0.0f;
  v->cap = cap_in + (v->cap - cap_in) * v->cap_kb;
  v->duck *= v->duck_kb;

  const float envd = v->p[TA_ENVMOD] * r->env_oct * (v->accented ? 1.0f + r->acc_env * v->p[TA_ACCENT] : 1.0f);
  const float oct = v->p[TA_CUTOFF] * 6.5f + envd * v->fenv + v->p[TA_ACCENT] * r->acc_oct * v->cap
                  + r->keytrack * (v->note - 48.0f) * (1.0f / 12.0f) - v->duck;
  const float fc = clampf(50.0f * exp2_fast(oct), 20.0f, 16000.0f);
  v->fc = fc;
  const float x = fc * (3.14159265f / (SR * (float)TA_OS)), x2 = x * x;
  const float g1 = x * (1.0f + x2 * (0.33333333f + x2 * (0.13333333f + x2 * 0.05396825f)));   /* tan */
  const float k = v->p[TA_RES] * r->kmax;
  const ta_coef c1 = coef(g1, r->pole1, k);
  if (v->g == 0.0f) { v->g = g1; v->cG = c1.G; v->cG1 = c1.G1; v->cia = c1.ia; v->cib = c1.ib; v->cD = c1.D; }
  ta_coef c = { v->cG, v->cG1, v->cia, v->cib, v->cD };
  const ta_coef dc = { (c1.G - c.G) * rn, (c1.G1 - c.G1) * rn, (c1.ia - c.ia) * rn, (c1.ib - c.ib) * rn, (c1.D - c.D) * rn };
  float g = v->g; const float dg = (g1 - g) * rn;

  /* ---- what the settings let you hear ---- */
  const float wave = v->p[TA_WAVE], lvl = v->p[TA_OSC];
  const float m_saw = (1.0f - wave) * lvl, m_pul = wave * lvl;
  const int need_pul = m_pul > 0.0f;
  const float pw = 0.5f + 0.45f * v->p[TA_PW];
  const float sub = v->p[TA_SUB] * 0.7f, noise = v->p[TA_NOISE] * 0.6f;
  const int need_noise = noise > 0.0f;
  const float fm = v->p[TA_FM] * 0.9f;
  const int need_fm = fm > 0.0f;
  const float drive = v->p[TA_DRIVE];
  const float xg = 0.5f * (1.0f + drive * r->drive_pre) * (1.0f + r->comp * k);
  const float post = 1.0f + drive * r->drive_post;
  const float og = gain * 0.9f / (1.0f + 0.35f * drive * r->drive_post);
  const float pole1 = r->pole1;
  const float hp_a = r->fb_hp * (6.2831853f / (SR * (float)TA_OS));
  const float atk_step = v->atk_step, amp_sus = v->gate ? v->amp_sus : 0.0f;
  const float amp_k = v->gate ? v->amp_kd : v->amp_kr;
  const float accg_t = v->accg_t;

  float amp = v->amp, accg = v->accg, s1 = v->s1, s2 = v->s2, s3 = v->s3, s4 = v->s4, hp = v->hp, x1 = v->x1;
  uint32_t ph = v->phase, sph = v->sub_phase, rng = v->rng;
  int att = v->att;

  for (unsigned i = 0; i < n; ++i) {
    inc += (uint32_t)dinc;
    ph += inc; sph += inc >> 1;
    const float dt = (float)inc * (1.0f / TWO32), t = (float)ph * (1.0f / TWO32);
    const float saw = 1.0f - 2.0f * t + blep(t, dt);                    /* falling saw */
    float osc = saw * m_saw;
    float raw = saw;
    if (need_pul) {
      /* the saw minus itself pw later: a pulse with no DC, band-limited at both edges */
      float t2 = t + pw; if (t2 >= 1.0f) t2 -= 1.0f;
      const float pul = saw - (1.0f - 2.0f * t2 + blep(t2, dt));
      osc += pul * m_pul;
      raw = saw + (pul - saw) * wave;
    }
    osc += (sph >> 31) ? sub : -sub;
    if (need_noise) {
      rng = rng * 1664525u + 1013904223u;
      osc += (float)(int32_t)rng * (noise / 2147483648.0f);
    }
    float y;
    if (need_fm) {
      /* the oscillator moves the cutoff: the coefficients are worked out for this sample */
      g += dg;
      const float gf = g * (1.0f + fm * raw);
      c = coef(gf < 1e-4f ? 1e-4f : gf, pole1, k);
    } else {
      c.G += dc.G; c.G1 += dc.G1; c.ia += dc.ia; c.ib += dc.ib; c.D += dc.D;
    }
#if TA_OS == 2
    { const float xm = 0.5f * (x1 + osc); float ya; LADDER(xm); ya = y; LADDER(osc); y = 0.5f * (ya + y); }
#else
    LADDER(osc);
#endif
    x1 = osc;
    /* amp: an attack ramp, then towards the sustain while held, or the release */
    if (att) { amp += atk_step; if (amp >= 1.0f) { amp = 1.0f; att = 0; } }
    else amp = amp_sus + (amp - amp_sus) * amp_k;
    accg += (accg_t - accg) * 0.002f;
    out[i] += sat(y * amp * accg * post) * og;
  }

  v->cG = c1.G; v->cG1 = c1.G1; v->cia = c1.ia; v->cib = c1.ib; v->cD = c1.D; v->g = g1; v->k = k;
  v->amp = amp; v->accg = accg; v->att = (uint8_t)att;
  v->s1 = s1; v->s2 = s2; v->s3 = s3; v->s4 = s4; v->hp = hp; v->x1 = x1;
  v->phase = ph; v->sub_phase = sph; v->rng = rng; v->inc = inc1;
}
