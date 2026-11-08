/*
 * wakes-sp1 drum firmware: what the drum pages (sp1_drums_ui, main thread) hand the kit
 * (sp1_drums, audio thread). It rides inside sp1_synth_params, so it reaches the audio
 * thread whole, through the same double buffer as everything else (sp1_synth.h).
 *
 * A VOICE is a sound (DRUM n's page sets voice n). A SLOT is a rhythm lane: its trigger,
 * its Marbles output and its rests. Kit rotation moves voices between slots; slot i plays
 * voice (i - r) mod 4. Everything below indexed [4] is per voice unless it says slot.
 *
 * Counters (*_seq) are compared, never reset: a press is one increment, so the audio thread
 * can never lose or double one, whatever the timing (the same rule as sp1_synth_trigger).
 */
#ifndef SP1_DRUMS_CTL_H
#define SP1_DRUMS_CTL_H

#include <stdint.h>

#define SP1_DRUMS_N 4

struct sp1_drums_ctl {
	/* ---- the voices (step 1) ---- */
	uint8_t eng[SP1_DRUMS_N], model[SP1_DRUMS_N];   /* tinykit engine / model           */
	uint8_t melodic;              /* bit n: voice n takes its pitch from its Marbles output */
	uint8_t mute;                 /* bit n: voice n is muted (SETTINGS T1-T4)              */
	float note[SP1_DRUMS_N];      /* semitones, kit pitch applied                          */
	float tone[SP1_DRUMS_N], decay[SP1_DRUMS_N], mod[SP1_DRUMS_N], excite[SP1_DRUMS_N];
	float level[SP1_DRUMS_N];     /* mix gain                                               */
	/* ---- the bus ---- */
	uint8_t fx_on;                /* bit k: FX k on (filter, fold, Chebyshev, FM)          */
	float fx[4];                  /* amounts: filter 0.5 = off, the rest 0 = off           */
	float fm_hz;                  /* the FM carrier (SETTINGS F3)                          */
	float bus_gain;               /* into the soft clip (SETTINGS F4), 1 = unity           */
	/* ---- what an RWD / burst / MIDI TRIG plays: voices, 0 = the last step's hits ---- */
	uint8_t audition;
	/* ---- the rocker (step 2) ---- */
	uint8_t rest_write;           /* slots whose rests RWD is writing (held)              */
	uint8_t rest_erase;           /* slots whose rests FFWD is erasing (held)             */
	uint8_t rest_clear;           /* slots cleared at the next bar line, per clear_seq    */
	uint8_t rest_choke;           /* 1: a rest chokes what rings; 0: it rings out          */
	uint32_t rest_clear_seq;
	uint32_t rotate_seq;          /* each increment: rotate the kit one slot, on the beat */
	uint32_t step_seq;            /* each increment while stopped: Marbles one tick       */
	float speed;                  /* tape slow: the machine's speed, 1 = normal            */
};

#endif
