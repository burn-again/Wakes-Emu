/* lxr_kit: the "classic" voices -- drum, snare, hi-hat and cymbal DSP ported from the Erica Synths
 * LXR firmware (GPL-3.0-only, see LICENSE and NOTICE-upstream.md) -- behind the same five controls
 * as tinykit. Built only with TK_WITH_LXR; delete this folder and drop the flag to remove it.
 *
 * Instances are fixed by the LXR code: three drum voices, one snare, one hi-hat, one cymbal.
 *   0 classic kick   1 classic tom   2 classic perc   3 classic snare   4 classic hi-hat   5 classic cymbal
 * Controls (0..1): 0 pitch, 1 tone, 2 decay, 3 mod, 4 excite.
 */
#ifndef LXR_KIT_H
#define LXR_KIT_H
#define LX_COUNT 6
void lx_init(void);
void lx_set(int id, int ctl, float v);
void lx_trigger(int id, float vel);
void lx_render_add(int id, float *out, unsigned n);
void lx_choke(int id);
int  lx_active(int id);
const char *lx_name(int id);
#endif
