/* tinykit: ten tiny percussion engines for the SP-1 (Cortex-M4F, 48 kHz).
 *
 * Original implementations of public algorithms (bytebeat, LFSR chip noise,
 * 1-bit pulses, Xenakis' GENDY, Karplus-Strong drum, elementary cellular
 * automata, Hordijk's Rungler, OPL2-style 2-op FM, Cook's PhISEM shakers,
 * modal/additive), inspired by the "TINY for Lich" model list. Single
 * precision and 32-bit integers only: no double, no libm in render().
 *
 * Controls are 0..1:
 *   PITCH  note = pitch*120 (MIDI-ish; 0.5 = C4-ish)
 *   TONE   engine-specific timbre      (TINY's "Tone")
 *   DECAY  amplitude decay time         (TINY's "Decay")
 *   MOD    engine-specific second axis  (TINY's "Mod")
 *   EXCITE engine-specific third axis   (TINY's "Excite")
 * On the SP-1: F1 PITCH, F2 TONE, F3 DECAY, F4 MOD, and SHIFT+F4 EXCITE.
 */
#ifndef TINYKIT_H
#define TINYKIT_H
#include <stdint.h>

enum { TK_PITCH, TK_TONE, TK_DECAY, TK_MOD, TK_EXCITE, TK_NCTL };

enum {
  TK_ENG_BYTEBEAT, TK_ENG_CHIP, TK_ENG_ONEBIT, TK_ENG_GENDY, TK_ENG_KS,
  TK_ENG_AUTOMATON, TK_ENG_RUNGLER, TK_ENG_FM, TK_ENG_SHAKER, TK_ENG_MODAL,
  TK_ENG_CLASSIC,            /* LXR-derived voices (lxr/, GPL-3): only with TK_WITH_LXR, else 0 models */
  TK_NUM_ENGINES
};

#define TK_KS_MAX 1024u      /* Karplus-Strong delay: lowest pitch ~47 Hz */
#define TK_MODES 6u          /* modal partials */
#define TK_GENDY_BP 16u

typedef struct {
  float g, r, s1, s2;        /* 2-pole resonator (SVF band-pass) */
} tk_reso;

typedef struct tk_voice {
  uint8_t engine, model;     /* model = variant within the engine */
  float ctl[TK_NCTL];
  /* shared */
  float env, env_k;          /* amp envelope and per-sample multiplier */
  float pitch_env, pitch_k;  /* pitch envelope (1 -> 0) */
  float freq;                /* base frequency, cycles per sample */
  uint32_t phase, phase2, rng, t;
  int32_t hold;              /* sample-and-hold value / last output */
  uint32_t reg;              /* LFSR / CA / rungler register */
  uint32_t ctr;              /* clock counter for clocked engines */
  float lp;                  /* one-pole state */
  /* engine-specific */
  union {
    struct { float amp[TK_GENDY_BP]; uint32_t dur[TK_GENDY_BP]; uint32_t idx, pos; float a0, a1; } gendy;
    struct { int16_t line[TK_KS_MAX]; uint32_t len, w; uint16_t flo, fhi; } ks;   /* [flo, fhi): excitation written */
    /* per partial: (c, s) carries phase AND amplitude; (a, b) = decay x (cos, sin) of the step,
       so every sample is one damped rotation. `live`: bit j set while partial j is audible. */
    struct { float c[TK_MODES], s[TK_MODES], a[TK_MODES], b[TK_MODES]; uint32_t live; } modal;
    struct { tk_reso f[3]; float energy, gain[3]; } shk;
    struct { float fb1, fb2; } fm;
  } u;
} tk_voice;

void  tk_init(tk_voice *v, unsigned engine, unsigned model);
void  tk_set(tk_voice *v, unsigned ctl, float value);   /* latched at the next trigger where needed */
void  tk_trigger(tk_voice *v, float velocity);
/* Adds the voice into out[] (mono), so a kit is several calls on one buffer. */
void  tk_render_add(tk_voice *v, float *out, unsigned n);
int   tk_active(const tk_voice *v);
unsigned tk_voice_size(void);
void  tk_choke(tk_voice *v);                 /* silence at once (rests) */

const char *tk_engine_name(unsigned engine);
unsigned    tk_engine_models(unsigned engine);
const char *tk_model_name(unsigned engine, unsigned model);

#endif
