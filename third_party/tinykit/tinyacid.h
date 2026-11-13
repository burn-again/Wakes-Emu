/* tinyacid: a mono acid / lead voice for the SP-1 drum firmware (48 kHz, float32).
 *
 * One voice core, two characters (ta_character):
 *   TA_CHAR_303  saw / square into a four-pole ladder whose first pole sits an octave up and whose
 *                feedback is high-passed (the bass thins as RES rises, and it never self-oscillates);
 *                ACCENT charges a capacitor, so accents in a row stack the cutoff higher, and an
 *                accented note's filter decay is short whatever DECAY says.
 *   TA_CHAR_101  saw + pulse (PW) + sub + noise into four equal poles that self-oscillate; one
 *                envelope with a sustain (HOLD) for the filter and the amp; ACCENT is velocity.
 *
 * Oscillator: one phase gives the PolyBLEP saw and the pulse (the saw minus a shifted copy of
 * itself, so PW is the shift); the sub is a square one octave down.
 * Filter: zero-delay-feedback ladder, one saturator at its input, run TA_OS times per sample.
 * FM is the oscillator into the filter's cutoff.
 * A SLIDE note does not retrigger the envelopes and glides in pitch (log domain).
 * Notes are float MIDI numbers, so microtonal scales play exactly.
 */
#ifndef TINYACID_H
#define TINYACID_H
#include <stdint.h>

enum {
  TA_CUTOFF,   /* 50 Hz .. 4.5 kHz before the envelope */
  TA_RES,
  TA_ENVMOD,   /* how far the envelope opens the filter */
  TA_DECAY,    /* the filter envelope (and, on the 101, the amp): 0.2 .. 2.5 s */
  TA_ACCENT,   /* how much an accented note adds */
  TA_DRIVE,    /* into the filter, and the clip after the amp */
  TA_WAVE,     /* 0 saw .. 1 pulse, continuous */
  TA_PW,       /* pulse width: 0 square .. 1 thin */
  TA_SUB,
  TA_NOISE,
  TA_FM,       /* oscillator -> cutoff */
  TA_SLIDE,    /* glide time: 15 .. 240 ms, 0.5 = 60 ms */
  TA_HOLD,     /* 303: how long a held note rings. 101: the sustain level */
  TA_OSC,      /* the saw / pulse's level in the mixer (0 leaves sub, noise and the filter) */
  TA_NPARAM
};

enum { TA_CHAR_303, TA_CHAR_101, TA_NCHAR };

#define TA_F_ACCENT 1u
#define TA_F_SLIDE  2u    /* legato: no retrigger, glide. Ignored when nothing is sounding */

typedef struct {
  float p[TA_NPARAM];
  uint8_t chr, gate, accented, att, dirty;
  uint32_t blk_n;                       /* the block length the per-block coefficients are for */
  uint32_t phase, sub_phase, inc, rng;
  float note, target;                   /* MIDI numbers; note glides to target */
  float amp, fenv, cap, duck, accg, accg_t;
  float s1, s2, s3, s4, hp, x1;         /* ladder state, feedback high-pass, last input */
  float g, k;                           /* the ladder's coefficients at the end of the last block */
  float cG, cG1, cia, cib, cD;
  float fc;                             /* the cutoff the last block ended on, Hz (for tests) */
  /* per-sample and per-block rates, worked out when dirty */
  float atk_step, amp_kd, amp_kr, amp_sus;
  float fenv_kb, fenv_krb, fenv_sus, cap_kb, duck_kb, glide_kb;
} ta_voice;

void ta_init(ta_voice *v);
void ta_character(ta_voice *v, unsigned chr);
void ta_set(ta_voice *v, unsigned param, float value);      /* 0..1 */
void ta_note(ta_voice *v, float note, unsigned flags);       /* gate on */
void ta_gate_off(ta_voice *v);
void ta_duck(ta_voice *v, float octaves);                    /* a kick: the cutoff dips and comes back */
void ta_render_mix(ta_voice *v, float *out, unsigned n, float gain);   /* adds into out */
int  ta_active(const ta_voice *v);
unsigned ta_voice_size(void);

#endif
