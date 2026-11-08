/* tinybass.c -- see tinybass.h. MIT. Original code; float32 only, libm only at note-on. */
#include "tinybass.h"
#include <math.h>
#include <string.h>

#define SR 48000.0f
#define TWO32 4294967296.0f

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }

/* PolyBLEP residual for a discontinuity at phase 0, t = phase in 0..1, dt = increment */
static inline float blep(float t, float dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
  if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
  return 0.0f;
}

void tb_init(tb_voice *v) {
  memset(v, 0, sizeof *v);
  v->p[TB_SHAPE] = 0.3f; v->p[TB_CUTOFF] = 0.35f; v->p[TB_RES] = 0.4f; v->p[TB_ENV] = 0.5f;
  v->p[TB_DECAY] = 0.4f; v->p[TB_GLIDE] = 0.15f; v->p[TB_SUB] = 0.4f; v->p[TB_OCTAVE] = 0.5f;
  v->freq = v->target = 55.0f / SR;
}

void tb_set(tb_voice *v, unsigned k, float x) { if (k < TB_NPARAM) v->p[k] = clampf(x, 0.0f, 1.0f); }
int tb_active(const tb_voice *v) { return v->amp > 1e-4f || v->attack > 0.0f; }
unsigned tb_voice_size(void) { return (unsigned)sizeof(tb_voice); }

void tb_note(tb_voice *v, float note, float vel) {
  const float oct = (float)((int)(v->p[TB_OCTAVE] * 4.0f + 0.5f) - 2);   /* -2 .. +2 */
  const float f = 440.0f * exp2f((note + 12.0f * oct - 69.0f) / 12.0f) / SR;
  const int sounding = v->amp > 0.05f;
  v->target = clampf(f, 1.0f / SR, 0.2f);
  if (!sounding || v->p[TB_GLIDE] < 0.02f) v->freq = v->target;
  /* glide: one-pole on frequency, 5 ms .. 400 ms */
  v->glide_k = 1.0f - expf(-1.0f / ((0.005f + 0.4f * v->p[TB_GLIDE] * v->p[TB_GLIDE]) * SR));
  const float dec = 0.05f * exp2f(v->p[TB_DECAY] * 5.3f);           /* 50 ms .. 2 s to -60 dB */
  v->amp_k = expf(-6.9f / (dec * SR));
  v->fenv_k = expf(-6.9f / (dec * 0.55f * SR));
  v->vel = clampf(vel, 0.0f, 1.0f);
  v->attack = 1.0f;                                                   /* 3 ms ramp from the current level */
  v->fenv = 1.0f;
}

void tb_render_add(tb_voice *v, float *out, unsigned n) {
  if (!tb_active(v)) return;
  const float shape = v->p[TB_SHAPE], sub = v->p[TB_SUB] * 0.7f;
  /* saw -> square over the first half of SHAPE, square -> triangle over the second */
  const float m_sq = shape < 0.5f ? shape * 2.0f : 1.0f, m_tri = shape < 0.5f ? 0.0f : (shape - 0.5f) * 2.0f;
  float tri = v->tri;
  const float cut = 30.0f * exp2f(v->p[TB_CUTOFF] * 7.0f);           /* 30 Hz .. 3.8 kHz */
  const float envamt = v->p[TB_ENV] * 6.0f;                          /* up to +6 octaves (approx.) */
  const float damp = 2.0f - 1.85f * v->p[TB_RES];
  const float atk_step = 1.0f / (0.003f * SR);
  float freq = v->freq, amp = v->amp, fenv = v->fenv, lp = v->lp, bp = v->bp, att = v->attack;
  uint32_t ph = v->phase, sph = v->sub_phase;
  for (unsigned i = 0; i < n; ++i) {
    freq += (v->target - freq) * v->glide_k;
    const uint32_t inc = (uint32_t)(freq * TWO32);
    ph += inc; sph += inc >> 1;
    const float dt = freq, t = (float)ph * (1.0f / TWO32);
    /* saw and square, both band-limited */
    const float saw = 1.0f - 2.0f * t + blep(t, dt);                     /* falling saw: its fundamental is in phase with the square's */
    float t2 = t + 0.5f; t2 -= (float)(int32_t)t2;
    const float sq = (t < 0.5f ? 1.0f : -1.0f) + blep(t, dt) - blep(t2, dt);
    tri = tri * 0.9995f + sq * 4.0f * dt;                                /* integrate the square */
    const float wav = saw + (sq - saw) * m_sq;
    const float osc = wav + (tri * 0.95f - wav) * m_tri + ((sph >> 31) ? sub : -sub);
    /* filter cutoff follows its envelope; g ~ 2 pi fc / fs is fine below ~5 kHz */
    float fc = cut * (1.0f + envamt * fenv * fenv);
    if (fc > 6000.0f) fc = 6000.0f;
    const float g = 6.2831853f * fc / SR;
    const float hp = osc - lp - damp * bp;
    bp += g * hp;
    lp += g * bp;
    /* amp: 3 ms attack ramp, then exponential decay */
    if (att > 0.0f) { amp += atk_step; if (amp >= 1.0f) { amp = 1.0f; att = 0.0f; } }
    else amp *= v->amp_k;
    fenv *= v->fenv_k;
    float y = lp * amp * v->vel * 0.5f;
    y = y * (1.5f - 0.5f * clampf(y * y, 0.0f, 1.0f));                 /* gentle warmth */
    out[i] += y;
  }
  v->freq = freq; v->amp = amp; v->fenv = fenv; v->lp = lp; v->bp = bp; v->attack = att;
  v->phase = ph; v->sub_phase = sph; v->tri = tri;
}
