/* tinybass: a light mono bass / lead voice for the SP-1 drum firmware (48 kHz, float32).
 *
 * Oscillator: PolyBLEP saw -> square -> triangle (SHAPE, continuous), plus a square one octave down (SUB).
 * OCTAVE shifts every note by -2 .. +2 octaves.
 * Filter: resonant state-variable low-pass, swept by its own envelope (ENV) from CUTOFF.
 * Amp: fast attack, exponential decay (DECAY). GLIDE slides between notes.
 * Notes are float MIDI numbers, so microtonal scales (pelog, slendro) play exactly.
 */
#ifndef TINYBASS_H
#define TINYBASS_H
#include <stdint.h>

enum { TB_SHAPE, TB_CUTOFF, TB_RES, TB_ENV, TB_DECAY, TB_GLIDE, TB_SUB, TB_OCTAVE, TB_NPARAM };
/* SHAPE: 0 saw -> 0.5 square -> 1 triangle, continuous. OCTAVE: five steps, -2 .. +2 octaves (0.5 = 0). */

typedef struct {
  float p[TB_NPARAM];
  uint32_t phase, sub_phase;
  float freq, target, glide_k;
  float amp, amp_k, attack, fenv, fenv_k, vel;
  float lp, bp;                /* Chamberlin SVF state */
  float out_lp;
  float tri;                   /* leaky-integrated square = band-limited triangle */
} tb_voice;

void tb_init(tb_voice *v);
void tb_set(tb_voice *v, unsigned param, float value);      /* 0..1 */
void tb_note(tb_voice *v, float note, float velocity);       /* glides if GLIDE > 0 and a note is sounding */
void tb_render_add(tb_voice *v, float *out, unsigned n);
int  tb_active(const tb_voice *v);
unsigned tb_voice_size(void);

#endif
