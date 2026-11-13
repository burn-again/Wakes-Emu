/*
 * wakes-sp1 drum firmware: the acid line. A pattern of up to 32 steps, its text form, the
 * built-in patterns, and the resolver that turns four lanes into what the voice is told.
 * Plain C with no Zephyr in it, so tools/host-tests/acidseqtest.c runs it as it is.
 *
 * ---- a pattern, as text ----
 * One token per 16th, separated by spaces ('|' between bars is ignored):
 *
 *     C2      a note: letter, # or b, octave (C2 = MIDI 36)
 *     .       a rest
 *     _       a tie: the note before it holds through this step
 *     !       after a note: accent              C2!
 *     ~       after a note: slide INTO it       D#2~     (legato: no retrigger, the pitch glides)
 *     ^  v    after a note: an octave up / down G2^      (the octave lane)
 *     /5      after a note: the note's weight   C2/5     (the gate lane)
 *
 * A mark may carry a weight, 1-9, as the drums' maps do: "C2!4" is an accent that only sounds
 * with the accent lane's density turned up, "C2/5" a note the gate lane's density thins out
 * first. No digit is 9: there at every density above zero.
 *
 * ---- lanes ----
 * Gate, accent, slide and octave are four lanes over the same steps. Each has its own loop
 * length (0 = the pattern's) and density (0.5 = as written with weights 5 and up; 1 = every
 * weight; 0 = nothing), so with all four at the pattern's length this is a plain 16-step
 * line, and shortening one lane makes it drift against the others. The pitches and ties
 * always loop at the pattern's length.
 *
 * ---- threads ----
 * The pattern tables are constant; the user pattern is written by the main thread
 * (sp1_acid_set_user, when drums.cnfg is read) and read by the audio thread. A step is a
 * few bytes read one at a time, so the worst a change mid-bar can do is one odd step.
 */
#ifndef SP1_ACID_H
#define SP1_ACID_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SP1_ACID_STEPS 32

enum { SP1_ACID_L_GATE, SP1_ACID_L_ACCENT, SP1_ACID_L_SLIDE, SP1_ACID_L_OCTAVE, SP1_ACID_LANES };

struct sp1_acid_step {
	uint8_t note;                 /* MIDI                                               */
	uint8_t w[SP1_ACID_LANES];    /* each lane's weight here, 0-9                       */
	int8_t  oct;                  /* the octave lane's direction, +1 or -1              */
	uint8_t tie;                  /* 1: the note before holds through this step         */
};
struct sp1_acid_pattern {
	struct sp1_acid_step s[SP1_ACID_STEPS];
	uint8_t n;                    /* steps, 1..32                                       */
};

/* Text -> pattern. Returns the number of steps, or -(column + 1) of the first thing that
 * does not read (the pattern is then left as it was). */
int sp1_acid_parse(struct sp1_acid_pattern *p, const char *text, size_t len);
/* Pattern -> text, in the form above. Returns the length, or -1 if it does not fit. */
int sp1_acid_format(const struct sp1_acid_pattern *p, char *buf, size_t size);

/* ---- the resolver ---- */
struct sp1_acid_perf {
	float   dens[SP1_ACID_LANES];   /* 0..1; 0.5 = as written                           */
	uint8_t len[SP1_ACID_LANES];    /* each lane's loop, 0 = the pattern's              */
	float   transpose;              /* semitones                                         */
};
void sp1_acid_perf_default(struct sp1_acid_perf *f);

enum { SP1_ACID_REST, SP1_ACID_NOTE, SP1_ACID_SLIDE, SP1_ACID_TIE };
struct sp1_acid_event {
	uint8_t kind;                 /* REST: gate off. NOTE: a new note. SLIDE: legato to  */
	                              /* `note`. TIE: nothing new, the note carries on.      */
	uint8_t accent;
	uint8_t hold;                 /* 1: the gate stays up to the next step (a slide or a */
	                              /* tie follows); 0: it falls part-way through this one */
	float   note;
};
struct sp1_acid_seq {
	int32_t next_at;              /* the step `next` was decided for (-1: none)         */
	uint8_t next_gate, next_tie, next_acc, next_slide;
	float   next_note;
	uint8_t sounding;             /* the gate is up as this step begins                 */
	float   last_note;
};
void sp1_acid_seq_reset(struct sp1_acid_seq *q);
/* One 16th. `g16` counts from 0 at PLAY. Each step is decided once, a step ahead (a slide
 * needs the note before it held), so a density moved between two steps cannot leave a
 * note stuck or cut. */
struct sp1_acid_event sp1_acid_seq_step(struct sp1_acid_seq *q, const struct sp1_acid_pattern *p,
					const struct sp1_acid_perf *f, int32_t g16);

/* ---- the built-in patterns, each with its sound ---- */
#define SP1_ACID_BUILTIN 17
#define SP1_ACID_PRESETS (SP1_ACID_BUILTIN + 1)     /* the last is the user's (drums.cnfg) */
#define SP1_ACID_NPARAM 14                           /* tinyacid's TA_NPARAM                */
struct sp1_acid_preset {
	const char *name;             /* no spaces: it is a word in a preset file           */
	const char *line;
	uint8_t chr;                  /* tinyacid character: 0 the 303, 1 the 101            */
	float p[SP1_ACID_NPARAM];     /* tinyacid's parameters, in its order                 */
	float gate;                   /* how far into its step a note's gate falls, 0..1     */
	float fx[4];                  /* a suggestion for the bus: filter (0.5 off), fold,   */
	                              /* Chebyshev, FM. The firmware leaves the FX faders     */
	                              /* alone; the host renders use it.                      */
	float bpm;                    /* the tempo it was written at (the host renders)      */
	uint8_t len[SP1_ACID_LANES];  /* the lanes' loops it starts with, 0 = the pattern's  */
};
/* The lanes' loop lengths a fader can choose (the LANES page, SHIFT): eight zones, the top
 * one the pattern's own length. */
#define SP1_ACID_LEN_ZONES 8
uint8_t sp1_acid_len_zone(int zone);            /* 2 3 4 5 6 8 12, then 0 = the pattern's */
int     sp1_acid_len_zone_of(uint8_t len);      /* the zone nearest a length              */
const struct sp1_acid_preset *sp1_acid_preset(int i);          /* 0 .. SP1_ACID_PRESETS-1 */
const struct sp1_acid_pattern *sp1_acid_pattern_of(int i);     /* its parsed pattern       */
/* drums.cnfg's [acid] line: the user pattern. Returns as sp1_acid_parse. */
int sp1_acid_set_user(const char *text, size_t len);
/* Parses the built-in lines; call once before audio starts. Returns 0, or the 1-based
 * index of a built-in line that does not read (a build error, caught by the host test). */
int sp1_acid_init(void);

#ifdef __cplusplus
}
#endif

#endif
