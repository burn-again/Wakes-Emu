/* tinyfx.c -- see tinyfx.h. MIT. Float32 only; per-block maths (tanf) runs once per block.
 * Chain order: FM -> FOLD -> CHEBY -> CRUSH -> FILTER (the DJ filter sits last, as on a mixer). */
#include "tinyfx.h"
#include <math.h>
#include <string.h>

#define SR 48000.0f

static float sine_tab[257];
static int sine_ready;

static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline float sin_p(uint32_t p) {
  uint32_t i = p >> 24; float f = (float)(p & 0xFFFFFFu) * (1.0f / 16777216.0f);
  return sine_tab[i] + (sine_tab[i + 1] - sine_tab[i]) * f;
}

void tfx_init(tfx_state *s) {
  memset(s, 0, sizeof *s);
  s->amt[TFX_FILTER] = s->prev[TFX_FILTER] = 0.5f;
  if (!sine_ready) {
    for (int i = 0; i <= 256; ++i) sine_tab[i] = sinf(6.2831853f * (float)i / 256.0f);
    sine_ready = 1;
  }
}

void tfx_set(tfx_state *s, unsigned fx, float a) { if (fx < TFX_COUNT) s->amt[fx] = clampf(a, 0.0f, 1.0f); }
unsigned tfx_state_size(void) { return (unsigned)sizeof(tfx_state); }

/* DJ filter coefficients for an amount: TPT state-variable filter (Zavalishin). */
typedef struct { float a1, a2, a3, k; int mode; } svf_c;   /* mode 0 = bypass, 1 = LP, 2 = HP */
static svf_c svf_for(float a) {
  svf_c c = {0, 0, 0, 2.0f, 0};
  float d = a - 0.5f;
  if (fabsf(d) < 0.02f) return c;
  float depth = (fabsf(d) - 0.02f) / 0.48f;                  /* 0..1 away from centre */
  float fc = d < 0 ? 18000.0f * exp2f(-depth * 7.8f)        /* LP: 18 kHz -> ~80 Hz  */
                   : 25.0f * exp2f(depth * 8.3f);           /* HP: 25 Hz -> ~8 kHz   */
  float g = tanf(3.14159265f * fc / SR);
  c.k = 2.0f - 1.35f * depth;                                /* resonance rises as you sweep */
  c.a1 = 1.0f / (1.0f + g * (g + c.k)); c.a2 = g * c.a1; c.a3 = g * c.a2;
  c.mode = d < 0 ? 1 : 2;
  return c;
}

void tfx_process(tfx_state *s, float *l, float *r, unsigned n) {
  if (!n) return;
  const float inv = 1.0f / (float)n;
  float *ch[2] = {l, r};

  /* ---------------------------------------------------- through-zero FM (mono, both channels) */
  if (s->amt[TFX_FM] > 0.0f || s->prev[TFX_FM] > 0.0f) {
    float a = s->prev[TFX_FM], da = (s->amt[TFX_FM] - a) * inv;
    uint32_t ph = s->fm_phase; float env = s->fm_env;
    const float fc = 196.0f / SR * 4294967296.0f;            /* carrier G3, in phase units */
    for (unsigned i = 0; i < n; ++i, a += da) {
      const float x = r ? 0.5f * (l[i] + r[i]) : l[i];
      const float ax = fabsf(x);
      env += (ax > env ? 0.5f : 0.0015f) * (ax - env);
      const float depth = a * a * 2600.0f / SR * 4294967296.0f;   /* Hz per unit of input */
      ph += (uint32_t)(int32_t)(fc + depth * x * 2.0f);      /* signed: the phase runs backwards through zero */
      const float y = sin_p(ph) * env * 1.6f;
      const float wet = clampf(a * 2.0f, 0.0f, 1.0f);
      l[i] = l[i] * (1.0f - 0.85f * wet) + y * wet;
      if (r) r[i] = r[i] * (1.0f - 0.85f * wet) + y * wet;
    }
    s->fm_phase = ph; s->fm_env = env;
  }

  /* ------------------------------------------------------------------- wavefolder */
  if (s->amt[TFX_FOLD] > 0.0f || s->prev[TFX_FOLD] > 0.0f) {
    for (int c = 0; c < 2; ++c) {
      if (!ch[c]) continue;
      float a = s->prev[TFX_FOLD], da = (s->amt[TFX_FOLD] - a) * inv;
      float *x = ch[c];
      for (unsigned i = 0; i < n; ++i, a += da) {
        const float g = 1.0f + 8.0f * a;
        float t = x[i] * g * 0.25f + 64.25f;                  /* +64 keeps it positive for the cheap floor */
        t -= (float)(int32_t)t;
        const float trim = 1.0f - 0.1f * clampf(a * 10.0f, 0.0f, 1.0f);   /* no level jump when it engages */
        x[i] = (1.0f - 4.0f * fabsf(t - 0.5f)) * trim;       /* triangle fold; identity at gain 1 */
      }
    }
  }

  /* --------------------------------------------------------------- Chebyshev shaper */
  if (s->amt[TFX_CHEBY] > 0.0f || s->prev[TFX_CHEBY] > 0.0f) {
    for (int c = 0; c < 2; ++c) {
      if (!ch[c]) continue;
      float a = s->prev[TFX_CHEBY], da = (s->amt[TFX_CHEBY] - a) * inv;
      float *x = ch[c];
      for (unsigned i = 0; i < n; ++i, a += da) {
        const float xs = clampf(x[i] * 1.4f, -1.0f, 1.0f);
        const float t2 = 2.0f * xs * xs - 1.0f, t3 = 2.0f * xs * t2 - xs;
        const float t4 = 2.0f * xs * t3 - t2, t5 = 2.0f * xs * t4 - t3;
        const float h = a * 2.999f;                             /* 0..3: T2 -> T3 -> T4 -> T5 */
        const int hi = (int)h; const float f = h - (float)hi;
        float y;
        switch (hi) {                                           /* even orders shifted so silence stays silent */
        case 0: y = (t2 + 1.0f) + ((t3) - (t2 + 1.0f)) * f; break;
        case 1: y = t3 + ((t4 - 1.0f) - t3) * f; break;
        default: y = (t4 - 1.0f) + (t5 - (t4 - 1.0f)) * f; break;
        }
        const float wet = clampf(a * 5.0f, 0.0f, 1.0f);
        x[i] = x[i] * (1.0f - wet) + y * 0.8f * wet;
      }
    }
  }

  /* ------------------------------------------------------------------ bit crusher */
  if (s->amt[TFX_CRUSH] > 0.0f || s->prev[TFX_CRUSH] > 0.0f) {
    float a = s->prev[TFX_CRUSH], da = (s->amt[TFX_CRUSH] - a) * inv;
    float ctr = s->crush_ctr, hl = s->crush_hold[0], hr = s->crush_hold[1];
    float levels = exp2f(15.0f - a * 12.0f);                   /* 15 bits -> 3 bits, ramped per block */
    const float dlev = (exp2f(15.0f - s->amt[TFX_CRUSH] * 12.0f) - levels) * inv;
    for (unsigned i = 0; i < n; ++i, a += da, levels += dlev) {
      const float hold_n = 1.0f + a * a * 23.0f;               /* 48 kHz -> 2 kHz */
      ctr += 1.0f;
      if (ctr >= hold_n) {
        ctr -= hold_n;
        const float ql = l[i] * levels, qr = r ? r[i] * levels : 0.0f;
        hl = (float)(int32_t)(ql + (ql > 0 ? 0.5f : -0.5f)) / levels;
        hr = (float)(int32_t)(qr + (qr > 0 ? 0.5f : -0.5f)) / levels;
      }
      const float wet = clampf(a * 8.0f, 0.0f, 1.0f);
      l[i] = l[i] * (1.0f - wet) + hl * wet;
      if (r) r[i] = r[i] * (1.0f - wet) + hr * wet;
    }
    s->crush_ctr = ctr; s->crush_hold[0] = hl; s->crush_hold[1] = hr;
  }

  /* ------------------------------------------------------------------- DJ filter */
  {
    const svf_c c0 = svf_for(s->prev[TFX_FILTER]), c1 = svf_for(s->amt[TFX_FILTER]);
    if (c0.mode || c1.mode) {
      /* if one end is bypass, borrow the other end's mode with a transparent cutoff */
      svf_c A = c0.mode ? c0 : svf_for(c1.mode == 1 ? 0.479f : 0.521f);
      svf_c B = c1.mode ? c1 : svf_for(c0.mode == 1 ? 0.479f : 0.521f);
      const int mode = B.mode;
      for (int c = 0; c < 2; ++c) {
        if (!ch[c]) continue;
        float a1 = A.a1, a2 = A.a2, a3 = A.a3, k = A.k;
        const float d1 = (B.a1 - a1) * inv, d2 = (B.a2 - a2) * inv, d3 = (B.a3 - a3) * inv, dk = (B.k - k) * inv;
        float ic1 = s->ic1[c], ic2 = s->ic2[c];
        float *x = ch[c];
        for (unsigned i = 0; i < n; ++i) {
          const float v3 = x[i] - ic2;
          const float v1 = a1 * ic1 + a2 * v3;
          const float v2 = ic2 + a2 * ic1 + a3 * v3;
          ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
          x[i] = mode == 1 ? v2 : x[i] - k * v1 - v2;
          a1 += d1; a2 += d2; a3 += d3; k += dk;
        }
        s->ic1[c] = ic1; s->ic2[c] = ic2;
      }
    } else {
      s->ic1[0] = s->ic1[1] = s->ic2[0] = s->ic2[1] = 0.0f;
    }
  }

  memcpy(s->prev, s->amt, sizeof s->prev);
}
