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

void tb_render_add(tb_voice *v, float *out, unsigned n) { tb_render_mix(v, out, n, 1.0f); }

/* tb_render_add at a gain (after the warmth), so a caller needs no scratch buffer. */
void tb_render_mix(tb_voice *restrict v, float *restrict out, unsigned n, float gain) {
  if (!tb_active(v)) return;
  const float shape = v->p[TB_SHAPE], sub = v->p[TB_SUB] * 0.7f;
  /* saw -> square over the first half of SHAPE, square -> triangle over the second */
  const float m_sq = shape < 0.5f ? shape * 2.0f : 1.0f, m_tri = shape < 0.5f ? 0.0f : (shape - 0.5f) * 2.0f;
  /* Only what SHAPE lets you hear is computed: the saw below the square, the square (and the
     triangle integrated from it) from just above the saw. The triangle still leaks along
     while it is silent, so it is settled if SHAPE comes back up. */
  const int need_saw = m_sq < 1.0f, need_sq = m_sq > 0.0f || m_tri > 0.0f;
  float tri = v->tri;
  const float cut = 30.0f * exp2f(v->p[TB_CUTOFF] * 7.0f);           /* 30 Hz .. 3.8 kHz */
  const float envamt = v->p[TB_ENV] * 6.0f;                          /* up to +6 octaves (approx.) */
  const float damp = 2.0f - 1.85f * v->p[TB_RES];
  const float atk_step = 1.0f / (0.003f * SR);
  const float target = v->target, glide_k = v->glide_k, amp_k = v->amp_k, fenv_k = v->fenv_k;
  const float vel = v->vel * 0.5f, gk = 6.2831853f / SR;
  float freq = v->freq, amp = v->amp, fenv = v->fenv, lp = v->lp, bp = v->bp, att = v->attack;
  uint32_t ph = v->phase, sph = v->sub_phase;
  /* a finished glide: the frequency, and so the phase step, are constant for the block.
     The one-pole only approaches its target, so within a millionth (0.002 cent) it snaps. */
  if (fabsf(target - freq) < target * 1e-6f) freq = target;
  const int gliding = freq != target;
  uint32_t inc = (uint32_t)(freq * TWO32);
  for (unsigned i = 0; i < n; ++i) {
    if (gliding) {
      freq += (target - freq) * glide_k;
      inc = (uint32_t)(freq * TWO32);
    }
    ph += inc; sph += inc >> 1;
    const float dt = freq, t = (float)ph * (1.0f / TWO32);
    /* saw and square, both band-limited; their shared edge's residual once */
    const float bl = blep(t, dt);
    float osc;
    if (need_sq) {
      float t2 = t + 0.5f; t2 -= (float)(int32_t)t2;
      const float sq = (t < 0.5f ? 1.0f : -1.0f) + bl - blep(t2, dt);
      tri = tri * 0.9995f + sq * 4.0f * dt;                              /* integrate the square */
      const float wav = need_saw ? (1.0f - 2.0f * t + bl) + (sq - (1.0f - 2.0f * t + bl)) * m_sq : sq;
      osc = wav + (tri * 0.95f - wav) * m_tri;
    } else {
      osc = 1.0f - 2.0f * t + bl;                                        /* falling saw */
    }
    osc += (sph >> 31) ? sub : -sub;
    /* filter cutoff follows its envelope; g ~ 2 pi fc / fs is fine below ~5 kHz */
    float fc = cut * (1.0f + envamt * fenv * fenv);
    if (fc > 6000.0f) fc = 6000.0f;
    const float g = gk * fc;
    const float hp = osc - lp - damp * bp;
    bp += g * hp;
    lp += g * bp;
    /* amp: 3 ms attack ramp, then exponential decay */
    if (att > 0.0f) { amp += atk_step; if (amp >= 1.0f) { amp = 1.0f; att = 0.0f; } }
    else amp *= amp_k;
    fenv *= fenv_k;
    float y = lp * amp * vel;
    y = y * (1.5f - 0.5f * clampf(y * y, 0.0f, 1.0f));                 /* gentle warmth */
    out[i] += y * gain;
  }
  if (!need_sq) {
    /* the triangle was not integrated: leave it where the square would have put it at this
       phase (rising from -1 over the square's high half), so turning SHAPE up later starts
       it in place rather than from zero */
    const float t = (float)ph * (1.0f / TWO32);
    tri = t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t;
  }
  v->freq = freq; v->amp = amp; v->fenv = fenv; v->lp = lp; v->bp = bp; v->attack = att;
  v->phase = ph; v->sub_phase = sph; v->tri = tri;
}
