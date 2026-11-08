/*
 * wakes-sp1 drum firmware (CONFIG_SP1_DRUMS): the four-voice kit, its bus FX, rests, kit
 * rotation, tape slow and step-through, in place of the Plaits voice in sp1_synth.cc. The
 * pages that drive it are sp1_drums_ui (main thread); what they send is sp1_drums_ctl.h.
 *
 * ---- threads ----
 *   sp1_drums_init()                          main thread, once, before audio starts
 *   sp1_drums_begin/_clock/_render()          AUDIO THREAD ONLY (from sp1_synth_render)
 *   the kit and model tables                  any thread (constant data)
 *   sp1_drums_status()                        main thread (a snapshot the audio thread writes)
 *
 * ---- hits ----
 * Slot 1 <- t1, slot 2 <- t3, slot 3 <- t2 (velocity from X2), slot 4 <- every 4th t2 tick.
 * X1 / X3 / X2 / Y nudge the tone of what their slot plays, per hit; a melodic voice takes
 * its pitch from that output instead (major pentatonic, two octaves). Slot i plays voice
 * (i - r) mod 4, r = the kit rotation.
 *
 * ---- rests (rocker, while playing) ----
 * One bar of 16ths per slot, on Marbles' master ramp: four t2 ticks are a bar, four 16ths a
 * tick. The rocker acts on a step if it is down at the step's start or within 30 ms after,
 * so an early press lands on the step it was aimed at. A rested step skips its hits; with
 * choke on, whatever that slot's voice is ringing fades over 12 ms and stops.
 */
#ifndef SP1_DRUMS_H
#define SP1_DRUMS_H

#include <stdbool.h>
#include <stdint.h>

#include "sp1_synth.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SP1_DRUMS_KITS 8

/* ---- the built-in kits and the model list (constant data, any thread) ---- */
struct sp1_drums_voice_def {
	uint8_t eng, model;
	float note, tone, decay, mod, excite, gain;
	bool melodic;
};
void sp1_drums_kit_voice(int kit, int voice, struct sp1_drums_voice_def *out);
const char *sp1_drums_kit_name(int kit);
/* The models DRUM n's "••" + FFWD / RWD steps through, in order. */
int  sp1_drums_models(void);
void sp1_drums_model_at(int i, uint8_t *eng, uint8_t *model);
int  sp1_drums_model_index(uint8_t eng, uint8_t model);       /* -1 if not on the list */
const char *sp1_drums_model_name(uint8_t eng, uint8_t model);
bool sp1_drums_model_unique(uint8_t eng);                     /* one instance per kit (classic) */
bool sp1_drums_model_ok(uint8_t eng, uint8_t model);          /* this build has it          */
/* The natural length (16 or 32 steps) of the maps kit `bank` gives slot `slot`. */
int  sp1_drums_map_len(int bank, int slot);

void sp1_drums_init(void);

/* AUDIO THREAD, once per DMA block, first: take this block's controls. */
void sp1_drums_begin(const struct sp1_synth_params *c);

/* AUDIO THREAD, once per DMA block, after MIDI has set Marbles' clock and before Marbles
 * renders: step-through. Each step press while stopped feeds Marbles one beat of external
 * clock over this block (one tick, the DEJA VU loop advancing), then stops it again on the
 * next. `frames` = Marbles frames this block; `external` = MIDI owns the clock (no steps). */
void sp1_drums_clock(uint32_t frames, bool external);

/* AUDIO THREAD, once per Plaits block. */
struct sp1_drums_frame {
	uint8_t rise;          /* bit 0 t1, bit 1 t2, bit 2 t3 rising this block (0 if stopped) */
	const float *volts;    /* X1, X2, X3, Y this block, or NULL                         */
	float ramp;            /* Marbles' master ramp, 0..1 per t2 tick                   */
	bool running;          /* Marbles' clock runs                                       */
	bool audition;         /* a TRIG from RWD, the burst or MIDI                       */
};
/* Renders `n` (<= 24) samples into `out`, in the 16-bit scale sp1_synth's output stage
 * expects (the scale of Plaits' OUT). */
void sp1_drums_render(const struct sp1_drums_frame *f, float *out, uint32_t n);

/* ---- for the main thread: the log and the LEDs ---- */
struct sp1_drums_status {
	int8_t   step;            /* 0..15, -1 while stopped                           */
	uint8_t  rotation;        /* r                                                 */
	uint8_t  last_hits;       /* voices struck on the last step that struck any   */
	uint16_t rests[SP1_DRUMS_N];   /* per slot, bit = 16th of the bar             */
	uint32_t hits;            /* voices struck since boot                          */
	uint32_t bass_notes;      /* bass notes since boot                             */
};
void sp1_drums_status(struct sp1_drums_status *out);

#ifdef __cplusplus
}
#endif

#endif
