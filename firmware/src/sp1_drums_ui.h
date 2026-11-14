/*
 * wakes-sp1 drum firmware: the drum pages (CONFIG_SP1_DRUMS), in place of the PLAITS page.
 * Pure C, no Zephyr, like sp1_plaits_ui: main.c feeds it fader counts, the "••" state and
 * the button edges once per control tick, and it hands the kit a finished sp1_drums_ctl.
 *
 * ---- pages ----
 *   KIT        the page the device boots into
 *   DRUM n     Tn from KIT or another DRUM page; Tn again returns to KIT
 *   SETTINGS   "••" double tap; one "••" tap returns to the page you came from
 *   MARBLES    "••" triple tap (main swaps the module; triple tap there comes back to KIT)
 * Holding "••" shows the page's SHIFT layer.
 *
 *   page             F1                F2             F3            F4
 *   KIT              kit pitch +-12 st kit tone       kit decay     fill           (all centred)
 *   KIT + SHIFT      DJ filter         fold           Chebyshev     FM             (FX amounts)
 *   DRUM n           pitch             tone           decay         mod
 *   DRUM n + SHIFT   density           level          length        excite
 *   SETTINGS         swing             morph          FM carrier    bus drive
 *   SETTINGS + SHIFT bass octave       bass shape     bass cutoff   bass decay
 *
 * ---- buttons ----
 *   T1-T4            KIT / DRUM: open DRUM n (same T = back to KIT). SETTINGS: mute drum n
 *   "••" + T1-T4     KIT / DRUM: FX 1-4 on / off (filter, fold, Chebyshev, FM).
 *                    SETTINGS: drum n's maps from the next kit
 *   "••" + FFWD/RWD  KIT: next / previous built-in kit. DRUM n: next / previous model.
 *                    SETTINGS: the bass mode (off / on the Y clock / only in the rests)
 *   MARBLES "••" + T1-T4 (main.c): the cables, sp1_dui_cable_step below
 *
 * ---- the rocker (FFWD up, RWD down); MARBLES acts on every drum ----
 *   page         playing: RWD          playing: FFWD                  stopped: RWD     stopped: FFWD
 *   KIT          held: tape slow       tap: rotate the kit            step one 16th    held: burst
 *   DRUM n       held: rest drum n     held: release; tap: clear      audition drum n  held: burst drum n
 *   SETTINGS     rests choke           rests ring out                 (the same)
 *   MARBLES      held: rest all        held: release all; tap: clear  step one 16th    held: burst
 */
#ifndef SP1_DRUMS_UI_H
#define SP1_DRUMS_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "sp1_drums_ctl.h"
#include "sp1_drums_patch.h"

#ifdef __cplusplus
extern "C" {
#endif

enum sp1_dui_page {
	SP1_DUI_KIT = 0,
	SP1_DUI_DRUM1, SP1_DUI_DRUM2, SP1_DUI_DRUM3, SP1_DUI_DRUM4,
	SP1_DUI_SETTINGS,
	SP1_DUI_LANES,                /* T3: the acid line's lanes                               */
	SP1_DUI_SOUND,                /* T1: the acid voice (the page at power-on)               */
	SP1_DUI_PATTERN,              /* T2: which pattern, transpose, octave, sparse / dense    */
	SP1_DUI_PAGES
};

/* Boot: kit 1, every page at its defaults, FX on at zero, nothing muted, rests choke. */
void sp1_dui_init(void);
/* Every entry to ON: page KIT, pickup against where the faders are. */
void sp1_dui_enter(const uint16_t raw[4]);
/* Back from MARBLES: page KIT. `fnc` = "••" is down now (that press is not a tap). */
void sp1_dui_resume(const uint16_t raw[4], bool fnc);

/* One control tick: faders, pickup and the "••" taps. */
#define SP1_DUI_EV_PAGE        0x001u   /* the page changed                                */
#define SP1_DUI_EV_LAYER       0x002u   /* SHIFT shown / hidden                            */
#define SP1_DUI_EV_CAUGHT      0x0F0u   /* bit 4+i: fader i just caught up                 */
#define SP1_DUI_EV_TO_MARBLES  0x100u   /* "••" triple tap: main swaps to MARBLES          */
uint32_t sp1_dui_tick(uint32_t elapsed_ms, const uint16_t raw[4], bool valid, bool fnc,
		      bool activity);

/* One control tick: the buttons and the rocker. Button masks are bit (enum sp1_button). */
struct sp1_dui_keys {
	bool fnc;              /* "••" down                                                 */
	bool running;          /* Marbles' clock runs                                       */
	bool marbles;          /* the MARBLES module is on show (its rocker = every drum)   */
	uint16_t pressed, held, released;
};
#define SP1_DUI_ACT_TRIG       0x01u    /* main: sp1_synth_trigger() (an audition)          */
uint32_t sp1_dui_keys(uint32_t elapsed_ms, const struct sp1_dui_keys *k);
/* What main should do this tick, after sp1_dui_keys. */
bool sp1_dui_burst(void);                 /* FFWD burst wanted                          */
bool sp1_dui_flash(uint8_t lv[4]);        /* a glyph to show once (sp1_display_engine)  */
const char *sp1_dui_log(void);            /* a line to print, or NULL; call until NULL  */

/* What the kit plays. */
void  sp1_dui_ctl(struct sp1_drums_ctl *out);
float sp1_dui_speed(void);                /* tape slow: Marbles' clock follows it         */

/* Model-row LEDs: the active layer's four values (SETTINGS breathes). */
void sp1_dui_leds(uint8_t out[4]);
void sp1_dui_page_leds(uint8_t out[4]);   /* the page, ignoring SHIFT (end of a rip)     */

enum sp1_dui_page sp1_dui_page(void);
bool sp1_dui_shift(void);
const char *sp1_dui_page_name(enum sp1_dui_page p);
int  sp1_dui_kit(void);

/* ---- Marbles' side: what main reads from the MARBLES module every tick ---- */
struct sp1_dui_marbles {
	float depth[4];               /* X1..X3 (X SHIFT F1-F3), Y (t SHIFT F1): -1..+1      */
	float jitter, deja;           /* t JITTER, t DEJA VU, 0..1                            */
	int   y_div;                  /* Y's divider, 0..11 (Marbles' table)                  */
	int   x_range, y_range;       /* [J], enum sp1_mrb_range                              */
	int   n_deg;                  /* Marbles' selected scale, for the bass                */
	float degrees[12];
	float period;
};
void sp1_dui_marbles(const struct sp1_dui_marbles *m);

/* ---- the cables (MARBLES' "••" + T1-T4) ----
 * out 0..3 = t1, t2, t3, the Y clock (t page); 4..7 = X1, X2, X3, Y (X page). */
void sp1_dui_cable_step(int out);
int  sp1_dui_cable_unpatch(int out);          /* 1 if something was patched           */
uint8_t sp1_dui_cable(int out);
void sp1_dui_cable_glyph(int out, uint8_t lv[4]);
const char *sp1_dui_cable_name(int out);
const char *sp1_dui_output_name(int out);
/* The PLAITS destination (enum sp1_mui_dest) whose INTELLIGENT range suits output k
 * (X1..X3, Y): main hands these to Marbles in place of the PLAITS routing. */
uint8_t sp1_dui_range_dest(int k);

/* ---- drums.cnfg and presets ---- */
void sp1_dui_set_cfg(const struct sp1_drums_cfg *c);   /* each ON, before the preset    */
void sp1_dui_get(struct sp1_drums_patch *p);
void sp1_dui_put(const struct sp1_drums_patch *p);     /* no [drums]: nothing changes   */

/* "••" + PLAY held 3 s on the drum pages: the current kit as stored (its own maps), FX at zero
 * (on), SETTINGS and the bass at defaults, nothing muted, every rest and cable cleared, the
 * rest style and bass mode from drums.cnfg. */
void sp1_dui_rip(void);
void sp1_dui_load_kit(int kit);

#ifdef __cplusplus
}
#endif

#endif
