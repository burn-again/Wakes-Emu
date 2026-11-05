/*
 * wakes-sp1 — MIDI files (experimental, CONFIG_SP1_MIDIFILE; docs/MIDIFILE.md).
 *
 * A Standard MIDI File, format 0 or 1, played into the Plaits voice through the same note
 * path as USB-MIDI (sp1_midi.h): note priority, legato and glide are the MIDI script's. The
 * file's position is Marbles' clock -- one quarter note per beat of the master ramp -- so
 * PLAY starts it from the top, RATE and the t range set its tempo, and it loops at its end.
 * The file's own tempo is not used.
 *
 * Framework-agnostic, like sp1_midi.cc: no Zephyr header.
 *
 * ---- threads, and who owns the buffer ----
 *   main      sp1_mf_select() (the gesture), sp1_mf_main_tick() (every control tick: the
 *             hand-over and the log line).
 *   storage   fills sp1_mf_buffer() and calls sp1_mf_loaded() (sp1_store.c, JOB_MIDI).
 *   audio     sp1_mf_audio_*() (sp1_synth_render), the only reader while a file is READY.
 * The buffer changes hands through `state`: the audio thread lets go of a READY file
 * (RELEASE -> EMPTY, its notes off) before main asks storage for another, and storage hands
 * a file over only after sp1_mf_check() has walked every byte of it -- so the player itself
 * never reads past a track.
 */
#ifndef SP1_MIDIFILE_H
#define SP1_MIDIFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SP1_MF_SLOTS   4
#define SP1_MF_TRACKS  16      /* tracks played; a file with more is refused */

/* What sp1_mf_check() learns, for the log. */
struct sp1_mf_info {
	uint16_t format;
	uint16_t tracks;
	uint16_t ppq;
	uint32_t notes;        /* note-ons */
	uint32_t end_tick;     /* the longest track's end: the loop point */
	uint32_t size;         /* the file's bytes */
	uint16_t channels;     /* bit per channel with a note */
	uint8_t  poly;         /* most notes sounding at once */
	uint8_t  lo, hi;       /* the lowest and highest note */
};

/* Walk the whole file. NULL if it can be played (info filled), else why not. */
const char *sp1_mf_check(const uint8_t *d, size_t n, struct sp1_mf_info *info);

/* ---- the player (pure: no state outside the struct; the host test drives it too) ---- */
struct sp1_mf_track {
	uint32_t start, pos, end;   /* byte offsets into the file */
	uint32_t next;         /* absolute tick of the event at `pos` */
	uint8_t  rs;           /* running status */
	bool     done;
};
struct sp1_mf_player {
	const uint8_t *d;
	struct sp1_mf_info info;
	struct sp1_mf_track tr[SP1_MF_TRACKS];
	uint8_t on[16];        /* notes sounding, a bit each, for "all off" */
};
typedef void (*sp1_mf_emit_fn)(void *ctx, const uint8_t msg[3], uint8_t len);

/* After sp1_mf_check() said yes. */
void sp1_mf_player_init(struct sp1_mf_player *p, const uint8_t *d, const struct sp1_mf_info *i);
void sp1_mf_rewind(struct sp1_mf_player *p);
/* Every note-on / note-off up to and including `tick`, in time order across tracks. */
void sp1_mf_play_to(struct sp1_mf_player *p, uint32_t tick, sp1_mf_emit_fn emit, void *ctx);
/* A note-off for every note still sounding. */
void sp1_mf_all_off(struct sp1_mf_player *p, sp1_mf_emit_fn emit, void *ctx);

/* ---- the device side ---- */
enum sp1_mf_state {
	SP1_MF_EMPTY = 0,      /* no file: storage may fill the buffer             */
	SP1_MF_LOADING,        /* storage is filling it                            */
	SP1_MF_READY,          /* checked: the audio thread plays it               */
	SP1_MF_RELEASE,        /* main wants it back: audio lets go -> EMPTY       */
};

/* Storage thread. */
uint8_t *sp1_mf_buffer(size_t *size);
/* JOB_MIDI is starting: false if it must not touch the buffer (nothing was asked). */
bool sp1_mf_loading(int *slot);
/* JOB_MIDI's answer: `n` bytes in the buffer from `name` (n == 0: no file; `err` < 0: the
 * card or the file system failed). Checks the file and hands it to the audio thread. */
void sp1_mf_loaded(const char *name, size_t n, int err);

/* Main thread. */
/* The gesture: rocker down + T<slot+1>. The slot playing -> off; another -> load it. */
void sp1_mf_select(int slot);
/* -1: no file selected, else its slot (0..3), loaded or loading. */
int  sp1_mf_slot(void);
/* Every control tick: hands a released buffer to storage, prints what a load found.
 * `queue` asks the storage thread to run JOB_MIDI (sp1_store_midi_load). */
void sp1_mf_main_tick(void (*queue)(void));
/* Power-off / ON entry: forget the file (the next ON starts with none). */
void sp1_mf_reset(void);

/* Audio thread, once per audio block before sp1_midi_audio_begin: is a file READY? (The
 * MIDI path then runs every Plaits block.) Also takes a RELEASE. */
bool sp1_mf_audio_live(sp1_mf_emit_fn emit, void *ctx);
/* Audio thread, Plaits block `j`, before sp1_midi_audio_block(j): `running` is Marbles',
 * `ramp` its master ramp at this block (0..1, one beat). Emits the block's notes. */
void sp1_mf_audio_block(bool running, float ramp, sp1_mf_emit_fn emit, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* SP1_MIDIFILE_H */
