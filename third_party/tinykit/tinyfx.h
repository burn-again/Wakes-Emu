/* tinyfx: cheap master-bus effects for the SP-1 drum firmware (stereo, 48 kHz, float32).
 *
 *   TFX_FILTER  DJ filter: 0.5 = off, below = low-pass sweep, above = high-pass sweep
 *   TFX_FOLD    triangle wavefolder: 0 = off, up = more folds (gain 1..9)
 *   TFX_CHEBY   Chebyshev waveshaper: 0 = off, sweeps harmonic 2 -> 3 -> 4 -> 5
 *   TFX_FM      through-zero FM: the drums push a sine carrier's frequency through zero;
 *               an envelope follower keeps the drum dynamics. 0 = off, up = deeper + wetter
 *   TFX_CRUSH   bit crusher + sample-rate reducer: 0 = off, up = fewer bits, lower rate
 *
 * Amounts are 0..1 and are ramped across each block, so moving a fader never zips.
 */
#ifndef TINYFX_H
#define TINYFX_H
#include <stdint.h>

enum { TFX_FILTER, TFX_FOLD, TFX_CHEBY, TFX_FM, TFX_CRUSH, TFX_COUNT };

typedef struct {
  float amt[TFX_COUNT], prev[TFX_COUNT];
  float ic1[2], ic2[2];          /* filter state, per channel */
  uint32_t fm_phase; float fm_env;
  float crush_hold[2], crush_ctr;
} tfx_state;

void tfx_init(tfx_state *s);
void tfx_set(tfx_state *s, unsigned fx, float amount);
void tfx_process(tfx_state *s, float *l, float *r, unsigned n);   /* in place; r may be NULL (mono) */
unsigned tfx_state_size(void);

#endif
