/*
 * wakes-sp1 drum firmware: the drums' part of a PRST slot, and drums.cnfg. Pure C, no Zephyr,
 * no storage (sp1_store.c does the files) -- the host suite drives both with damaged text.
 *
 * ---- in a preset: a [drums] section of the slot's .prst ----
 * Appended after Wakes' own sections, so the same file still loads in stock Wakes (it ignores
 * a section it does not know) and a stock slot loads here with the drums as they were. Every
 * field is checked on its own, as PRST does: a bad or missing one keeps its default.
 *
 *     [drums]
 *     kit = 3
 *     voices = 9.0 9.4 8.2 9.2        engine.model per voice
 *     gains = 0.7000 0.4500 0.3000 0.3500
 *     melodic = 10                     bit n: voice n is melodic
 *     kit_faders = 0.5000 0.5000 0.5000 0.5000
 *     kit_shift = ...                  one line per page and layer: kit, drum1-4, settings
 *                                      (_faders and _shift), four values 0..1 each
 *     fx = 15   mute = 0   rests = choke   bass = off
 *     patterns = 3 3 3 3               the bank (a kit) each slot's maps come from
 *     t_cables = 0 0 0 0               t1, t2, t3, the Y clock
 *     x_cables = 0 0 0 0               X1, X2, X3, Y
 *
 * ---- drums.cnfg: WAKES/CNFG/drums.cnfg, read at every ON ----
 * Line by line over the built-in values (SP1_DRUMS_CNFG_TEMPLATE, which is also what is
 * written to the drive when the file is missing). A bad line keeps its default and is named.
 */
#ifndef SP1_DRUMS_PATCH_H
#define SP1_DRUMS_PATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SP1_DRUMS_LAYERS 12      /* KIT, KIT+SHIFT, DRUM 1-4 (+SHIFT), SETTINGS (+SHIFT) */

struct sp1_drums_patch {
	bool present;                 /* the slot had a [drums] section                    */
	int kit;                      /* 0..7: which built-in kit the voices came from     */
	uint8_t eng[4], model[4];
	float gain[4];
	uint8_t melodic, fx_on, mute, rest_choke, bass_mode;
	uint8_t pat[4];               /* 0..7 */
	uint8_t t_dest[4], x_dest[4];
	float v[SP1_DRUMS_LAYERS][4]; /* the pages' stored values, 0..1                    */
};

/* Defaults: kit `kit`'s voices and maps, every page as stored, FX on, nothing muted,
 * choke, bass off, no cables. `present` false. */
void sp1_drums_patch_default(struct sp1_drums_patch *p, int kit);
/* Append the [drums] section to buf (NUL-terminated); returns its length or -1. */
int  sp1_drums_patch_write(const struct sp1_drums_patch *p, char *buf, size_t size);
/* Find the [drums] section in a slot's text and read it over `p` (which holds the defaults).
 * Returns the number of bad fields; p->present says whether the section was there. */
int  sp1_drums_patch_parse(const char *text, size_t n, struct sp1_drums_patch *p);

/* ---- drums.cnfg ---- */
struct sp1_drums_cfg {
	int   kit;                    /* 0..7, the kit at power-on                          */
	int   rest_choke;             /* 1 choke, 0 ring out                                */
	int   rotate_q;               /* 0 step, 1 beat, 2 bar                              */
	float tape_floor;             /* 0.1..1                                             */
	float tape_fall;              /* seconds, 0.05..10                                  */
	float tape_release;           /* seconds, 0..10 (0 = at once)                       */
	int   bass_mode;              /* enum sp1_drums_bass                                */
	float bass_root;              /* MIDI note 12..72                                   */
	int   bass_octaves;           /* 1..4                                               */
	float bass_res, bass_env, bass_glide, bass_sub;   /* 0..1                           */
	float bass_level_db;          /* -40..0                                             */
};
void sp1_drums_cfg_defaults(struct sp1_drums_cfg *c);

typedef void (*sp1_drums_cfg_problem)(void *ctx, int line, const char *what, const char *why);
struct sp1_drums_cfg_reader {
	struct sp1_drums_cfg *c;
	sp1_drums_cfg_problem problem;
	void *ctx;
	char line[128];
	size_t len;
	int lineno, bad;
	bool too_long;
	char section[16];
};
void sp1_drums_cfg_begin(struct sp1_drums_cfg_reader *r, struct sp1_drums_cfg *c,
			 sp1_drums_cfg_problem problem, void *ctx);
void sp1_drums_cfg_feed(struct sp1_drums_cfg_reader *r, const char *t, size_t n);
int  sp1_drums_cfg_end(struct sp1_drums_cfg_reader *r);   /* the number of bad lines */

extern const char SP1_DRUMS_CNFG_TEMPLATE[];
extern const size_t SP1_DRUMS_CNFG_TEMPLATE_LEN;

#ifdef __cplusplus
}
#endif

#endif
