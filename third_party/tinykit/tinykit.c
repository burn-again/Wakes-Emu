/* tinykit.c -- see tinykit.h. MIT. Original code; algorithms are public. */
#include "tinykit.h"
#ifdef TK_WITH_LXR
#include "lxr/lxr_kit.h"
#endif
#include <math.h>
#include <string.h>

#define SR 48000.0f
#define TWO32 4294967296.0f

/* ------------------------------------------------------------------ names */
static const char *const kEngine[TK_NUM_ENGINES] = {
  "Bytebeat", "Chip", "1-bit", "Gendy", "KS drum",
  "Automaton", "Rungler", "FM (OPL2-ish)", "Shakers", "Modal", "Classic",
};
static const char *const kModels[TK_NUM_ENGINES][12] = {
  {"1/t kick", "bit-losing snare", "metal", "Sierpinski", "zap", "arp", "tom", "crunch"},
  {"NES noise", "NES short noise", "Game Boy 7-bit", "4-bit poly", "5-bit poly", "9-bit poly", "NES triangle kick", "12.5% pulse"},
  {"pulse kick", "noise clicks", "two pulses", "punk console"},
  {"3 breakpoints", "6 breakpoints", "10 breakpoints", "16 breakpoints"},
  {"noise burst", "+-1 burst", "fading burst", "impulses"},
  {"rule 30", "rule 45", "rule 73", "rule 90", "rule 110", "rule 150"},
  {"comparator", "XOR", "steps", "triangle"},
  {"bass drum", "snare", "tom", "hi-hat", "cymbal"},
  {"maraca", "cabasa", "sekere", "tambourine", "sleighbells"},
  {"membrane", "free bar", "bell", "harmonic", "marimba", "vibraphone", "glass", "wood block",
   "steel drum", "kalimba", "gong", "tuned drum"},
  {"classic kick", "classic tom", "classic perc", "classic snare", "classic hi-hat", "classic cymbal"},
};
#ifdef TK_WITH_LXR
static const uint8_t kNumModels[TK_NUM_ENGINES] = {8, 8, 4, 4, 4, 6, 4, 5, 5, 12, 6};
#else
static const uint8_t kNumModels[TK_NUM_ENGINES] = {8, 8, 4, 4, 4, 6, 4, 5, 5, 12, 0};
#endif

const char *tk_engine_name(unsigned e) { return e < TK_NUM_ENGINES ? kEngine[e] : "?"; }
unsigned tk_engine_models(unsigned e) { return e < TK_NUM_ENGINES ? kNumModels[e] : 0; }
const char *tk_model_name(unsigned e, unsigned m) {
  return (e < TK_NUM_ENGINES && m < kNumModels[e]) ? kModels[e][m] : "?";
}

/* --------------------------------------------------------------- helpers */
static inline uint32_t xs(uint32_t *s) {      /* xorshift32 */
  uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x;
}
static inline float rndf(uint32_t *s) {         /* -1..1 */
  return (float)(int32_t)xs(s) * (1.0f / 2147483648.0f);
}
static inline float clampf(float x, float a, float b) { return x < a ? a : (x > b ? b : x); }
static inline uint32_t popc(uint32_t x) {       /* M4 has no POPCNT */
  x = x - ((x >> 1) & 0x55555555u);
  x = (x & 0x33333333u) + ((x >> 2) & 0x33333333u);
  return (((x + (x >> 4)) & 0x0F0F0F0Fu) * 0x01010101u) >> 24;
}
/* phase increment for f cycles/sample, saturating below Nyquist-ish */
static inline uint32_t inc_of(float f) {
  f = clampf(f, 0.0f, 0.49f);
  return (uint32_t)(f * TWO32);
}
/* triangle from a 32-bit phase, -1..1 */
static inline float tri(uint32_t p) {
  int32_t s = (int32_t)(p ^ (uint32_t)((int32_t)p >> 31));   /* fold */
  return (float)s * (1.0f / 1073741824.0f) - 1.0f;
}

static float sine_tab[257];
static int sine_ready;
static void sine_init(void) {
  if (sine_ready) return;
  for (int i = 0; i <= 256; ++i) sine_tab[i] = sinf(6.2831853f * (float)i / 256.0f);
  sine_ready = 1;
}
static inline float sin_p(uint32_t p) {        /* linear-interp table sine */
  uint32_t i = p >> 24; float f = (float)(p & 0xFFFFFFu) * (1.0f / 16777216.0f);
  return sine_tab[i] + (sine_tab[i + 1] - sine_tab[i]) * f;
}

/* time in seconds -> per-sample multiplier reaching -60 dB */
static float k_of(float seconds) { return expf(-6.9078f / (seconds * SR)); }
static float note_hz(float pitch01) { return 440.0f * exp2f((pitch01 * 120.0f - 69.0f) / 12.0f); }

/* ------------------------------------------------------------------- API */
void tk_init(tk_voice *v, unsigned engine, unsigned model) {
  memset(v, 0, sizeof *v);
  sine_init();
  v->engine = (uint8_t)(engine % TK_NUM_ENGINES);
  if (!kNumModels[v->engine]) v->engine = TK_ENG_FM;          /* classic voices not built in */
  v->model = (uint8_t)(model % kNumModels[v->engine]);
#ifdef TK_WITH_LXR
  if (v->engine == TK_ENG_CLASSIC) { static int lx_ready; if (!lx_ready) { lx_init(); lx_ready = 1; } }
#endif
  for (int i = 0; i < TK_NCTL; ++i) v->ctl[i] = 0.5f;
  v->rng = 0x9E3779B9u ^ (engine * 7919u + model * 104729u);
  v->reg = 1;
}

void tk_set(tk_voice *v, unsigned c, float x) {
  if (c < TK_NCTL) v->ctl[c] = clampf(x, 0.0f, 1.0f);
}

int tk_active(const tk_voice *v) { return v->env > 1e-4f; }

static void reso_set(tk_reso *r, float hz, float q) {
  /* Chamberlin SVF: g = 2 sin(pi f / fs), r = damping */
  r->g = 2.0f * sinf(3.14159265f * clampf(hz, 20.0f, 14000.0f) / SR);
  r->r = 1.0f / q;
}
static inline float reso_bp(tk_reso *r, float x) {
  float hp = x - r->s1 * r->r - r->s2;
  r->s1 += r->g * hp;                 /* band */
  r->s2 += r->g * r->s1;              /* low  */
  return r->s1;
}

/* modal models: partial ratios, levels and relative decay times */
typedef struct { float r[TK_MODES], w[TK_MODES], d[TK_MODES]; } tk_modal_model;
static const tk_modal_model kModal[12] = {
  {{1.000f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f}, {1, .8f, .7f, .6f, .5f, .4f}, {1, .7f, .6f, .5f, .45f, .4f}},     /* membrane   */
  {{1.000f, 2.757f, 5.404f, 8.933f, 13.34f, 18.64f}, {1, .6f, .4f, .3f, .2f, .1f}, {1, .6f, .4f, .3f, .2f, .15f}},      /* free bar   */
  {{0.500f, 1.000f, 1.183f, 1.506f, 2.000f, 2.514f}, {.6f, 1, .7f, .5f, .6f, .4f}, {1.6f, 1, .8f, .7f, .6f, .5f}},      /* bell       */
  {{1.000f, 2.000f, 3.000f, 4.000f, 5.000f, 6.000f}, {1, .5f, .33f, .25f, .2f, .17f}, {1, .8f, .7f, .6f, .5f, .4f}},    /* harmonic   */
  {{1.000f, 3.990f, 10.65f, 18.00f, 1.0f, 1.0f},     {1, .35f, .12f, .05f, 0, 0}, {1, .5f, .25f, .15f, 1, 1}},          /* marimba    */
  {{1.000f, 4.000f, 10.00f, 20.00f, 1.0f, 1.0f},     {1, .3f, .1f, .05f, 0, 0}, {1.4f, .5f, .3f, .2f, 1, 1}},           /* vibraphone */
  {{1.000f, 2.320f, 4.250f, 6.630f, 9.380f, 12.00f}, {1, .7f, .5f, .35f, .25f, .15f}, {1, .9f, .8f, .7f, .6f, .5f}},    /* glass      */
  {{1.000f, 2.570f, 4.080f, 5.620f, 7.000f, 8.400f}, {1, .6f, .4f, .25f, .15f, .1f}, {.5f, .3f, .25f, .2f, .15f, .12f}},/* wood block */
  {{1.000f, 2.000f, 3.000f, 4.020f, 5.000f, 6.000f}, {1, .8f, .5f, .35f, .2f, .15f}, {1, .9f, .7f, .6f, .5f, .4f}},     /* steel drum */
  {{1.000f, 5.430f, 15.20f, 1.0f, 1.0f, 1.0f},       {1, .35f, .12f, 0, 0, 0}, {1, .4f, .2f, 1, 1, 1}},                 /* kalimba    */
  {{1.000f, 1.520f, 1.890f, 2.330f, 2.890f, 3.400f}, {1, .8f, .7f, .6f, .5f, .4f}, {1.2f, 1, .9f, .8f, .7f, .6f}},      /* gong       */
  {{1.000f, 2.000f, 3.010f, 4.030f, 5.050f, 2.300f}, {1, .6f, .4f, .3f, .2f, .3f}, {1, .6f, .5f, .4f, .35f, .25f}},     /* tuned drum */
};

void tk_trigger(tk_voice *v, float vel) {
#ifdef TK_WITH_LXR
  if (v->engine == TK_ENG_CLASSIC) {
    for (int c = 0; c < TK_NCTL; ++c) lx_set(v->model, c, v->ctl[c]);
    lx_trigger(v->model, vel);
    v->env = 1.0f;
    return;
  }
#endif
  const float P = v->ctl[TK_PITCH], T = v->ctl[TK_TONE], D = v->ctl[TK_DECAY];
  const float M = v->ctl[TK_MOD], X = v->ctl[TK_EXCITE];
  v->freq = note_hz(P) / SR;
  v->env = clampf(vel, 0.0f, 1.0f);
  v->env_k = k_of(0.02f * exp2f(D * 7.0f));   /* DECAY: 20 ms .. 2.6 s */
  v->pitch_env = 1.0f;
  v->pitch_k = k_of(0.004f + 0.4f * (1.0f - M) * (1.0f - M));   /* MOD = fall speed by default */
  v->t = 0; v->ctr = 0; v->phase = 0; v->phase2 = 0; v->lp = 0.0f; v->hold = 0;

  switch (v->engine) {
  case TK_ENG_CHIP:
    v->reg = (v->model == 2) ? 0x7Fu : 0x7FFFu;
    v->pitch_k = k_of(0.003f + 0.5f * (1.0f - M) * (1.0f - M));
    break;
  case TK_ENG_GENDY: {
    static const uint8_t nbp[4] = {3, 6, 10, 16};
    uint32_t n = nbp[v->model];
    float base = 1.0f / (v->freq * (float)n);
    for (uint32_t i = 0; i < n; ++i) {
      v->u.gendy.amp[i] = rndf(&v->rng) * (0.2f + 0.8f * X);
      v->u.gendy.dur[i] = (uint32_t)clampf(base, 1.0f, 4000.0f);
    }
    v->u.gendy.idx = 0; v->u.gendy.pos = 0;
    v->u.gendy.a0 = 0.0f; v->u.gendy.a1 = v->u.gendy.amp[0];
    break;
  }
  case TK_ENG_KS: {
    uint32_t len = (uint32_t)clampf(1.0f / v->freq, 4.0f, (float)(TK_KS_MAX - 2));
    v->u.ks.len = len; v->u.ks.w = 0;
    memset(v->u.ks.line, 0, sizeof v->u.ks.line);
    for (uint32_t i = 0; i < len; ++i) {
      float s;
      switch (v->model) {
      case 0: s = rndf(&v->rng); break;                                   /* noise burst */
      case 1: s = (xs(&v->rng) & 1u) ? 1.0f : -1.0f; break;               /* +-1 burst  */
      case 2: s = rndf(&v->rng) * (1.0f - (float)i / (float)len); break;  /* fading     */
      default: s = (i % (len / 3u + 1u)) == 0 ? 1.0f : 0.0f; break;       /* impulses   */
      }
      v->u.ks.line[i] = (int16_t)(s * 24000.0f);
    }
    v->u.ks.w = len;
    break;
  }
  case TK_ENG_AUTOMATON: {
    uint32_t seed = (uint32_t)(T * 65535.0f) * 2654435761u;
    v->reg = seed ? seed : 0x00010000u;
    break;
  }
  case TK_ENG_FM:
    v->u.fm.fb1 = v->u.fm.fb2 = 0.0f;
    break;
  case TK_ENG_SHAKER: {
    /* PhISEM-style: resonator centres (Hz) and Qs, scaled by PITCH around C4 */
    static const float hz[5][3] = {{3200, 0, 0}, {3000, 0, 0}, {5500, 0, 0}, {2300, 5600, 8100}, {2500, 5300, 6500}};
    static const float q[5][3] = {{12, 0, 0}, {2, 0, 0}, {3, 0, 0}, {10, 60, 60}, {60, 60, 60}};
    float sc = exp2f((P - 0.5f) * 2.0f) * (0.7f + 0.6f * X);
    for (int i = 0; i < 3; ++i) {
      reso_set(&v->u.shk.f[i], hz[v->model][i] * sc, q[v->model][i] > 0 ? q[v->model][i] : 1.0f);
      v->u.shk.f[i].s1 = v->u.shk.f[i].s2 = 0.0f;
      v->u.shk.gain[i] = q[v->model][i] > 0 ? (i ? 0.5f : 1.0f) : 0.0f;
    }
    v->u.shk.energy = (0.25f + 0.75f * T) * v->env;   /* TONE = shake strength */
    break;
  }
  case TK_ENG_MODAL: {
    const tk_modal_model *mm = &kModal[v->model];
    float wsum = 0.0f;
    for (unsigned i = 0; i < TK_MODES; ++i) wsum += mm->w[i];
    for (unsigned i = 0; i < TK_MODES; ++i) {
      float f = v->freq * mm->r[i];
      float audible = f < 0.45f ? 1.0f : 0.0f;          /* drop partials above ~21 kHz */
      f = clampf(f, 0.0f, 0.45f);
      float w = 6.2831853f * f;
      v->u.modal.cs[i] = cosf(w); v->u.modal.sn[i] = sinf(w);
      v->u.modal.c[i] = 0.0f; v->u.modal.s[i] = 1.0f;
      /* TONE = brightness: tilt of initial partial levels */
      float tilt = exp2f(-(float)i * (1.0f - T) * 1.6f);
      /* EXCITE = strike position: 0 = centre (all modes), 1 = near the edge (comb) */
      float pos = 0.5f - 0.45f * X;
      float strike = fabsf(sinf(3.14159265f * (float)(i + 1) * pos));
      v->u.modal.e[i] = audible * mm->w[i] * tilt * (0.2f + 0.8f * strike) / wsum * 1.4f * v->env;
      /* MOD = upper partials die faster */
      float tsec = 0.02f * exp2f(D * 7.0f) * mm->d[i] / (1.0f + (float)i * M * 3.0f);
      v->u.modal.k[i] = k_of(tsec);
    }
    v->env_k = 1.0f;                           /* the partials carry their own decay */
    v->lp = (0.15f + 0.6f * T) * v->env;       /* mallet click level: brighter = harder beater */
    break;
  }
  default: break;
  }
}

/* ------------------------------------------------------------- engines */
static uint32_t ca_next(uint32_t x, uint32_t rule) {
  const uint32_t l = (x << 1) | (x >> 31), r = (x >> 1) | (x << 31);
  uint32_t y = 0;
  /* neighbourhood value (l,c,r) = 4l+2c+r; OR in every pattern whose rule bit is set */
  if (rule & 0x01u) y |= ~l & ~x & ~r;
  if (rule & 0x02u) y |= ~l & ~x &  r;
  if (rule & 0x04u) y |= ~l &  x & ~r;
  if (rule & 0x08u) y |= ~l &  x &  r;
  if (rule & 0x10u) y |=  l & ~x & ~r;
  if (rule & 0x20u) y |=  l & ~x &  r;
  if (rule & 0x40u) y |=  l &  x & ~r;
  if (rule & 0x80u) y |=  l &  x &  r;
  return y ? y : 1u;
}

void tk_render_add(tk_voice *v, float *out, unsigned n) {
#ifdef TK_WITH_LXR
  if (v->engine == TK_ENG_CLASSIC) { lx_render_add(v->model, out, n); v->env = lx_active(v->model) ? 1.0f : 0.0f; return; }
#endif
  if (!tk_active(v)) return;
  const float T = v->ctl[TK_TONE], M = v->ctl[TK_MOD], X = v->ctl[TK_EXCITE];
  float env = v->env, pe = v->pitch_env;
  const float ek = v->env_k, pk = v->pitch_k, f0 = v->freq;
  uint32_t rng = v->rng;

  switch (v->engine) {
  /* ---------------------------------------------------------- bytebeat */
  case TK_ENG_BYTEBEAT: {
    const uint32_t a = 1u + (uint32_t)(T * 15.0f), b = 1u + (uint32_t)(M * 15.0f), c = (uint32_t)(X * 7.0f);
    const uint32_t inc = (uint32_t)(clampf(f0 * 32.0f, 0.0f, 4.0f) * 65536.0f);  /* t ticks at 32x the note (16.16) */
    uint32_t ph = v->phase;
    for (unsigned i = 0; i < n; ++i) {
      ph += inc; const uint32_t t = ph >> 16;
      uint32_t y;
      switch (v->model) {
      case 0: y = (a * 0x80000u) / (t + 512u); break;                        /* 1/t kick: falling saw */
      case 1: y = (xs(&rng) >> 24) & (0xFFu << ((t >> (7u + c)) & 7u)); break; /* bit-losing snare */
      case 2: y = ((t * a) & (t >> (4u + c))) ^ (t * b); break;              /* metal */
      case 3: y = (t & (t >> (c + 3u))) * a; break;                          /* Sierpinski */
      case 4: y = t * (a * 4096u / ((t >> 4) + 16u)); break;                 /* zap */
      case 5: y = t * ((0x5A3Cu >> (((t >> (9u + (c & 3u))) & 3u) * 4u)) & 15u) * b; break; /* arp */
      case 6: y = (a * 0x200000u) / (t + 2048u); break;                      /* tom */
      default: y = ((t * a) ^ (t >> (c + 2u))) | (t >> b); break;            /* crunch */
      }
      out[i] += (float)((int32_t)(y & 0xFFu) - 128) * (1.0f / 128.0f) * env;
      env *= ek;
    }
    v->phase = ph;
    break;
  }
  /* -------------------------------------------------------------- chip */
  case TK_ENG_CHIP: {
    /* TONE = noise period (clock), EXCITE = start pitch, MOD = pitch fall (in pitch_k) */
    const float clk = (v->model < 6) ? (200.0f * exp2f(T * 7.5f) / SR) : f0;
    uint32_t reg = v->reg, ph = v->phase;
    float y = (float)v->hold;
    for (unsigned i = 0; i < n; ++i) {
      const uint32_t inc = inc_of(clk * (1.0f + X * 12.0f * pe));
      ph += inc;
      if (v->model < 6) {
        if (ph < inc) {               /* clock edge */
          uint32_t fb;
          switch (v->model) {
          case 0: fb = (reg ^ (reg >> 1)) & 1u; reg = (reg >> 1) | (fb << 14); break;          /* 15-bit long */
          case 1: fb = (reg ^ (reg >> 6)) & 1u; reg = (reg >> 1) | (fb << 14); break;          /* 93-step short */
          case 2: fb = (reg ^ (reg >> 1)) & 1u; reg = ((reg >> 1) & ~0x40u) | (fb << 6); break; /* 7-bit */
          case 3: fb = ((reg >> 3) ^ (reg >> 2)) & 1u; reg = ((reg << 1) | fb) & 0xFu; break;
          case 4: fb = ((reg >> 4) ^ (reg >> 2)) & 1u; reg = ((reg << 1) | fb) & 0x1Fu; break;
          default: fb = ((reg >> 8) ^ (reg >> 4)) & 1u; reg = ((reg << 1) | fb) & 0x1FFu; break;
          }
          if (!reg) reg = 1;
          y = (reg & 1u) ? 1.0f : -1.0f;
        }
      } else if (v->model == 6) {     /* NES triangle: 32 steps, 4-bit */
        uint32_t step = ph >> 27; uint32_t q = step < 16 ? 15 - step : step - 16;
        y = (float)q * (2.0f / 15.0f) - 1.0f;
      } else {                        /* 12.5% pulse */
        y = (ph >> 29) == 0 ? 1.0f : -0.14f;
      }
      out[i] += y * 0.7f * env;
      env *= ek; pe *= pk;
    }
    v->reg = reg; v->phase = ph; v->hold = (int32_t)y;
    break;
  }
  /* ------------------------------------------------------------- 1-bit */
  case TK_ENG_ONEBIT: {
    uint32_t ph = v->phase, ph2 = v->phase2;
    const uint32_t pw = (uint32_t)((0.05f + 0.9f * X) * TWO32 * 0.999f);
    int32_t mono = v->hold;
    for (unsigned i = 0; i < n; ++i) {
      float y;
      switch (v->model) {
      case 0: {                        /* pulse kick: pitch falls from (1+8T)x */
        ph += inc_of(f0 * (1.0f + 8.0f * T * pe));
        y = ph < pw ? 1.0f : -1.0f; break;
      }
      case 1: {                        /* noise clicks: density = TONE, width = EXCITE */
        if (mono > 0) { --mono; y = 1.0f; }
        else { y = 0.0f; if ((xs(&rng) >> 16) < (uint32_t)(T * T * 3000.0f * (0.2f + pe))) mono = 1 + (int32_t)(X * 40.0f); }
        break;
      }
      case 2: {                        /* two pulses XOR, ratio = TONE */
        const float fa = f0 * (1.0f + 3.0f * pe * M);
        ph += inc_of(fa); ph2 += inc_of(fa * (1.0f + 3.0f * T));
        y = ((ph < pw) ^ (ph2 < 0x80000000u)) ? 1.0f : -1.0f; break;
      }
      default: {                       /* punk console: astable A fires a monostable */
        const uint32_t inc = inc_of(f0 * (1.0f + 4.0f * T * pe));
        ph += inc;
        if (ph < inc) mono = 1 + (int32_t)(X * X * 600.0f);
        if (mono > 0) { --mono; y = 1.0f; } else y = -1.0f;
        break;
      }
      }
      out[i] += y * 0.5f * env;
      env *= ek; pe *= pk;
    }
    v->phase = ph; v->phase2 = ph2; v->hold = mono;
    break;
  }
  /* ------------------------------------------------------------- gendy */
  case TK_ENG_GENDY: {
    static const uint8_t nbp[4] = {3, 6, 10, 16};
    const uint32_t nb = nbp[v->model];
    const float astep = T * T * 0.6f;                 /* TONE = amplitude step */
    const float base = clampf(1.0f / (f0 * (float)nb), 1.0f, 4000.0f);
    const float dstep = M * M * base * 0.7f;          /* MOD = duration step */
    uint32_t idx = v->u.gendy.idx, pos = v->u.gendy.pos;
    float a0 = v->u.gendy.a0, a1 = v->u.gendy.a1, y = v->lp;
    uint32_t dur = v->u.gendy.dur[idx];
    float slope = (a1 - a0) / (float)dur;
    y = a0 + slope * (float)pos;
    for (unsigned i = 0; i < n; ++i) {
      if (++pos >= dur) {               /* next breakpoint: random-walk it */
        pos = 0; a0 = a1;
        idx = (idx + 1u >= nb) ? 0u : idx + 1u;
        float a = v->u.gendy.amp[idx] + rndf(&rng) * astep;
        if (a > 1.0f) a = 2.0f - a;   /* mirror at the bounds */
        if (a < -1.0f) a = -2.0f - a;
        v->u.gendy.amp[idx] = a; a1 = a;
        float d = (float)v->u.gendy.dur[idx] + rndf(&rng) * dstep;
        d = clampf(d, base * 0.25f, base * 4.0f);
        dur = v->u.gendy.dur[idx] = (uint32_t)d + 1u;
        slope = (a1 - a0) / (float)dur;
        y = a0;
      } else y += slope;
      out[i] += y * 0.8f * env;
      env *= ek;
    }
    v->u.gendy.idx = idx; v->u.gendy.pos = pos; v->u.gendy.a0 = a0; v->u.gendy.a1 = a1; v->lp = y;
    break;
  }
  /* --------------------------------------------------------------- KS */
  case TK_ENG_KS: {
    /* TONE = roughness (sign-flip probability, 0.5 = snare), MOD = stretch,
       EXCITE = pitch drop (delay starts short and lengthens). */
    const uint32_t flip = (uint32_t)(T * 65535.0f), stretch = (uint32_t)(M * M * 60000.0f);
    const uint32_t L = v->u.ks.len;
    uint32_t w = v->u.ks.w;
    int16_t *line = v->u.ks.line;
    for (unsigned i = 0; i < n; ++i) {
      const uint32_t len = L - (uint32_t)((float)L * 0.5f * X * pe);
      const uint32_t ra = (w - len) & (TK_KS_MAX - 1u), rb = (ra - 1u) & (TK_KS_MAX - 1u);
      const uint32_t r = xs(&rng);
      int32_t s = ((r & 0xFFFFu) < stretch) ? line[ra] : ((int32_t)line[ra] + line[rb]) >> 1;
      if ((r >> 16) < flip) s = -s;
      line[w & (TK_KS_MAX - 1u)] = (int16_t)s;
      ++w;
      out[i] += (float)s * (1.0f / 24000.0f) * env;
      env *= ek; pe *= pk;
    }
    v->u.ks.w = w;
    break;
  }
  /* --------------------------------------------------------- automaton */
  case TK_ENG_AUTOMATON: {
    static const uint8_t rules[6] = {30, 45, 73, 90, 110, 150};
    const uint32_t rule = rules[v->model];
    const uint32_t width = 1u + (uint32_t)(X * 15.0f);          /* EXCITE = which cells you hear */
    const uint32_t mask = (width >= 32u) ? 0xFFFFFFFFu : (((1u << width) - 1u) << (16u - width / 2u));
    const float norm = 2.0f / (float)width;
    uint32_t reg = v->reg, ph = v->phase;
    float y = v->lp;
    for (unsigned i = 0; i < n; ++i) {
      const uint32_t inc = inc_of(f0 * 4.0f * (1.0f + 6.0f * pe));  /* MOD = rate fall (pitch_k) */
      ph += inc;
      if (ph < inc) { reg = ca_next(reg, rule); y = (float)popc(reg & mask) * norm - 1.0f; }
      out[i] += y * 0.6f * env;
      env *= ek; pe *= pk;
    }
    v->reg = reg; v->phase = ph; v->lp = y;
    break;
  }
  /* ----------------------------------------------------------- rungler */
  case TK_ENG_RUNGLER: {
    /* osc A at PITCH, osc B at TONE; 8-bit register clocked by B, data from A.
       MOD = how hard B (the rungler CV) bends A, EXCITE = how hard A bends B. */
    const float fb = 20.0f * exp2f(T * 9.0f) / SR;
    uint32_t pa = v->phase, pb = v->phase2, reg = v->reg & 0xFFu;
    float cv = (float)(reg >> 5) * (1.0f / 7.0f);
    for (unsigned i = 0; i < n; ++i) {
      const uint32_t ia = inc_of(f0 * (1.0f + 3.0f * M * cv));
      const uint32_t ib = inc_of(fb * (1.0f + 3.0f * X * cv));
      pa += ia;
      const uint32_t old_b = pb; pb += ib;
      if ((old_b ^ pb) & 0x80000000u && !(pb & 0x80000000u)) {   /* B square rising edge (wrap) */
        uint32_t data = pa >> 31;
        if (v->model == 1) data ^= (reg >> 7) & 1u;               /* XOR loop */
        reg = ((reg << 1) | data) & 0xFFu;
        cv = (float)(reg >> 5) * (1.0f / 7.0f);
      }
      float y;
      switch (v->model) {
      case 0: y = (tri(pa) > tri(pb)) ? 0.8f : -0.8f; break;
      case 1: y = ((pa ^ pb) >> 31) ? 0.8f : -0.8f; break;
      case 2: y = cv * 1.6f - 0.8f; break;
      default: y = tri(pa) * 0.9f; break;
      }
      out[i] += y * env;
      env *= ek;
    }
    v->phase = pa; v->phase2 = pb; v->reg = reg;
    break;
  }
  /* ---------------------------------------------------------------- FM */
  case TK_ENG_FM: {
    uint32_t p1 = v->phase, p2 = v->phase2;
    float fb1 = v->u.fm.fb1, fb2 = v->u.fm.fb2;
    for (unsigned i = 0; i < n; ++i) {
      float y;
      switch (v->model) {
      case 0: case 2: {                 /* BD / tom: modulator->carrier, feedback on mod, pitch drop */
        const float drop = (v->model == 0) ? 6.0f : 2.0f;
        const float f = f0 * (1.0f + drop * X * pe);
        p2 += inc_of(f); p1 += inc_of(f);
        const float m = sin_p(p2 + (uint32_t)(int32_t)(fb2 * T * 6e8f));
        fb2 = m;
        y = sin_p(p1 + (uint32_t)(int32_t)(m * M * pe * 1.2e9f));
        break;
      }
      case 1: {                         /* snare: tone op + noise gated by the tone op's phase bit */
        p1 += inc_of(f0 * (1.0f + 1.5f * X * pe));
        p2 += inc_of(f0 * (1.5f + 2.0f * M));
        const float tone = sin_p(p1 + (uint32_t)(int32_t)(fb1 * T * 4e8f));
        fb1 = tone;
        const float nz = ((p2 >> 31) ^ (xs(&rng) >> 31)) ? 1.0f : -1.0f;
        y = tone * (1.0f - T * 0.7f) + nz * (0.25f + 0.5f * T);
        break;
      }
      default: {                        /* hi-hat / cymbal: phase bits of two square ops (OPL-style) */
        p1 += inc_of(f0 * 8.0f * (1.0f + 3.0f * T));
        p2 += inc_of(f0 * 8.0f * (1.0f + 3.0f * M) * 1.4142f);
        const uint32_t b = ((p1 >> 29) ^ (p1 >> 27) ^ (p2 >> 30) ^ (p2 >> 28)) & 1u;
        float metal = b ? 1.0f : -1.0f;
        if (v->model == 3) {            /* hi-hat: noise in, high-passed */
          const float nz = rndf(&rng);
          float x = metal * (1.0f - X * 0.8f) + nz * X;
          v->lp += 0.35f * (x - v->lp); y = (x - v->lp) * 0.9f;
        } else {
          v->lp += 0.15f * (metal - v->lp); y = (metal - v->lp) * (0.6f + 0.4f * X);
        }
        break;
      }
      }
      out[i] += y * 0.8f * env;
      env *= ek; pe *= pk;
    }
    v->phase = p1; v->phase2 = p2; v->u.fm.fb1 = fb1; v->u.fm.fb2 = fb2;
    break;
  }
  /* ------------------------------------------------------------ shaker */
  case TK_ENG_SHAKER: {
    static const uint16_t beads[5] = {25, 512, 64, 32, 32};
    static const float sdecay[5] = {0.95f, 0.96f, 0.96f, 0.95f, 0.97f};
    /* MOD = number of beads (0.5 = the instrument's own count) */
    const uint32_t chance = (uint32_t)((float)beads[v->model] * exp2f((M - 0.5f) * 4.0f) * 2.5f);
    static const float outg[5] = {8.5f, 3.5f, 4.5f, 1.5f, 2.3f};   /* level-match the five instruments */
    const float sd = sdecay[v->model], og = outg[v->model] * 0.6f;
    const float g0 = v->u.shk.gain[0], g1 = v->u.shk.gain[1], g2 = v->u.shk.gain[2];
    float energy = v->u.shk.energy, level = v->lp;
    for (unsigned i = 0; i < n; ++i) {
      if ((xs(&rng) >> 16) < chance) level += energy * 0.5f;
      level *= sd;
      const float x = rndf(&rng) * level;
      float y = reso_bp(&v->u.shk.f[0], x) * g0;
      if (g1 > 0.0f) y += reso_bp(&v->u.shk.f[1], x) * g1 + reso_bp(&v->u.shk.f[2], x) * g2;
      out[i] += y * og;
      energy *= ek;                    /* DECAY = how long you keep shaking */
    }
    env = energy + level;              /* active while shaking or still ringing */
    v->u.shk.energy = energy; v->lp = level;
    break;
  }
  /* ------------------------------------------------------------- modal */
  case TK_ENG_MODAL: {
    /* coupled-form oscillators, one per partial; EXCITE = pitch drop applied as a
       per-block rotation tweak; renormalise once per block. */
    float *c = v->u.modal.c, *s = v->u.modal.s, *e = v->u.modal.e, *k = v->u.modal.k;
    const float *cs = v->u.modal.cs, *sn = v->u.modal.sn;
    float sum_e = 0.0f, click = v->lp;
    for (unsigned i = 0; i < n; ++i) {
      float y = rndf(&rng) * click;              /* ~1 ms beater click */
      click *= 0.93f;
      for (unsigned j = 0; j < TK_MODES; ++j) {
        const float cj = c[j] * cs[j] - s[j] * sn[j];
        const float sj = s[j] * cs[j] + c[j] * sn[j];
        c[j] = cj; s[j] = sj;
        y += sj * e[j];
        e[j] *= k[j];
      }
      out[i] += y;
    }
    for (unsigned j = 0; j < TK_MODES; ++j) {
      const float g = 1.5f - 0.5f * (c[j] * c[j] + s[j] * s[j]);
      c[j] *= g; s[j] *= g; sum_e += e[j];
    }
    v->lp = click < 1e-5f ? 0.0f : click;
    (void)pe;
    env = sum_e;                       /* active while any partial rings */
    break;
  }
  default: break;
  }
  v->env = env; v->pitch_env = pe; v->rng = rng;
}

unsigned tk_voice_size(void) { return (unsigned)sizeof(tk_voice); }

/* Silence a voice at once (used for rests: fade the output first, then choke). */
void tk_choke(tk_voice *v) {
#ifdef TK_WITH_LXR
  if (v->engine == TK_ENG_CLASSIC) lx_choke(v->model);
#endif
  v->env = 0.0f;
  v->lp = 0.0f;
  if (v->engine == TK_ENG_MODAL) for (unsigned j = 0; j < TK_MODES; ++j) v->u.modal.e[j] = 0.0f;
  if (v->engine == TK_ENG_SHAKER) v->u.shk.energy = 0.0f;
}
