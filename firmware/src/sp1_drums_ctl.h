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
	uint32_t step_seq;            /* each increment while stopped: Marbles one 16th       */
	float speed;                  /* tape slow: the machine's speed, 1 = normal            */
	uint8_t rotate_q;             /* a rotation lands on the next: 0 step, 1 beat, 2 bar   */

	/* ---- the sequencer (step 3), per slot ---- */
	uint8_t pat[SP1_DRUMS_N];     /* the map pair (A, B) from the bank: a kit's index     */
	uint8_t len[SP1_DRUMS_N];     /* steps in the loop: 8, 12, 16, 24 or 32               */
	float density[SP1_DRUMS_N];   /* 0..1: which weights play                              */
	float fill;                   /* -0.5..+0.5, added to every slot's density             */
	float morph;                  /* 0..1: map A -> map B                                  */
	float swing;                  /* 0..1: odd 16ths late by up to a third of a step       */
	float jitter;                 /* Marbles' t JITTER: threshold jitter, per-hit wobble   */
	float deja;                   /* Marbles' t DEJA VU: 0.5 and up = the jitter repeats   */

	/* ---- Marbles' cables (step 3) ----
	 * t_dest: t1, t2, t3, the Y clock -> enum sp1_drums_tdest.
	 * x_dest: X1, X2, X3, Y -> enum sp1_drums_xdest; depth -1..+1 each (X SHIFT F1-F3,
	 * t SHIFT F1); lo / hi: the volts each of the four outputs spans now (its range). */
	uint8_t t_dest[4], x_dest[4];
	float depth[4], lo[4], hi[4];
	uint8_t y_ticks;              /* the Y clock: one tick per this many t2 ticks          */

	/* ---- the bass (step 4) ---- */
	uint8_t bass_mode;            /* enum sp1_drums_bass                                   */
	uint8_t bass_octaves;         /* how many octaves of the scale Y spreads over          */
	float bass_root;              /* MIDI note of its lowest degree                        */
	float bass_level;             /* linear gain                                           */
	float bass_p[8];              /* tinybass: shape, cutoff, res, env, decay, glide, sub, octave */
	uint8_t n_deg;                /* the scale: degrees in semitones above the root        */
	float degrees[12];
	float period;                 /* its period (12 for an octave)                         */

	/* ---- the acid line (bass_mode >= SP1_BASS_ACID): tinyacid in the bass's place ---- */
	uint8_t acid_chr;             /* tinyacid's character: 0 the 303, 1 the 101            */
	float acid_p[14];             /* tinyacid's parameters, in its order                   */
	float acid_level;             /* linear gain                                           */
	float acid_gate;              /* how far into its step a note's gate falls, 0..1       */
	float acid_duck;              /* octaves the cutoff dips on each hit of slot 1         */
	float acid_dens[4];           /* the lanes' densities: gate, accent, slide, octave     */
	uint8_t acid_len[4];          /* the lanes' loops, 0 = the pattern's                   */
	float acid_transpose;         /* semitones                                             */
	/* Recorded fader moves (cutoff, resonance, decay, drive). The kit always holds the last
	 * bar of them; each keep makes that bar a take that loops under the faders, up to four. */
	uint32_t acid_keep_seq;       /* each increment: the last bar becomes a take           */
	uint32_t acid_undo_seq;       /* each increment: the newest take goes                  */
	uint32_t acid_snap_seq;       /* each increment: back to acid_snap_to takes            */
	uint8_t acid_snap_to;
};

/* Where a t output (or the Y clock) goes. */
enum sp1_drums_tdest {
	SP1_DT_NONE = 0,
	SP1_DT_SLOT1, SP1_DT_SLOT2, SP1_DT_SLOT3, SP1_DT_SLOT4,
	SP1_DT_BASS,                  /* strike the bass                                       */
	SP1_DT_ACCENT,                /* whatever plays on that tick is louder                 */
	SP1_DT_COUNT
};

/* Where an X output (or Y) goes. The "own slot" four act on the slot that output's t
 * partner plays (X1 with t1 ...), sampled at each hit. */
enum sp1_drums_xdest {
	SP1_DX_NONE = 0,
	SP1_DX_PITCH, SP1_DX_TONE, SP1_DX_DECAY, SP1_DX_VEL,        /* own slot          */
	SP1_DX_KIT_PITCH, SP1_DX_KIT_TONE, SP1_DX_KIT_DECAY, SP1_DX_FILL,
	SP1_DX_FILTER, SP1_DX_FOLD, SP1_DX_CHEBY, SP1_DX_FM,
	SP1_DX_BASS_NOTE,             /* Y only                                                 */
	SP1_DX_COUNT
};

/* SP1_BASS_ACID + i plays acid pattern i (sp1_acid.h) on the acid voice; the bass is silent. */
enum sp1_drums_bass { SP1_BASS_OFF = 0, SP1_BASS_Y, SP1_BASS_RESTS, SP1_BASS_ACID };
#define SP1_BASS_MODES (SP1_BASS_ACID + 9)        /* + SP1_ACID_PRESETS */

#endif
