/*
 * wakes-sp1 drum firmware, stage 1 (CONFIG_SP1_DRUMS): the four-slot drum kit, the bus FX
 * and the hit routing that replace the Plaits voice in sp1_synth.cc.
 *
 * ---- threads ----
 *   sp1_drums_init()          main thread, once, before audio starts
 *   sp1_drums_begin(), sp1_drums_render()   AUDIO THREAD ONLY (from sp1_synth_render)
 *
 * ---- stage 1 controls (on Wakes' existing PLAITS page) ----
 *   engine select (T2 / T3)   one of eight built-in kits (engine index mod 8)
 *   F1 FREQUENCY              kit pitch, semitones around C4 (clamped to +-12)
 *   F2 TIMBRE / F3 MORPH      kit tone / kit decay offsets (centre = the kit as stored)
 *   F4 HARMONICS              kit mod offset
 *   SHIFT F1..F4              FX: DJ filter (centre off), fold, Chebyshev, FM (|value|)
 *   RWD / burst / MIDI note   audition slot 1
 * ---- hits while Marbles runs ----
 *   t1 -> slot 1, t3 -> slot 2, t2 -> slot 3 (velocity from X2), every 4th t2 tick -> slot 4.
 *   X1 / X3 / X2 / Y nudge the tone of the slot they belong to, per hit; melodic slots take
 *   their pitch from that output instead, quantized to a major pentatonic over two octaves.
 * The full page layout (KIT, DRUM 1-4, SETTINGS, MARBLES) is stage 2.
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

void sp1_drums_init(void);

/* AUDIO THREAD, once per DMA block: take this block's parameters (kit, offsets, FX). */
void sp1_drums_begin(const struct sp1_synth_params *c);

/* AUDIO THREAD, once per Plaits block. `rise`: bit 0 t1, bit 1 t2, bit 2 t3 rising this
 * block (0 while Marbles is stopped). `volts`: X1, X2, X3, Y for this block, or NULL.
 * `audition`: a TRIG from RWD, the burst or MIDI. Renders `n` samples into `out` in the
 * 16-bit scale sp1_synth's output stage expects (the same scale as Plaits' OUT). */
void sp1_drums_render(uint8_t rise, const float *volts, bool audition, float *out, uint32_t n);

/* For the log: the kit playing and its name. */
int sp1_drums_kit(void);
const char *sp1_drums_kit_name(int kit);

#ifdef __cplusplus
}
#endif

#endif
