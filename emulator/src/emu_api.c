/*
 * emu_api.c -- what a host (the browser, or the native harness) drives the device with.
 * In the wasm build every EMU_EXPORT function is a wasm export.
 *
 * One boot per instance: Wakes' statics are only fresh in a fresh instance, exactly as
 * RAM is only fresh after a reset. The browser makes a new instance for every reset,
 * power-off and wake, and carries the WAKES drive across (emu_fs_*).
 */
#include "emu.h"
#include "emu_api.h"
#include "sp1_controls.h"

#include <stdlib.h>

int wakes_main(void);     /* firmware/src/main.c's main(), renamed at compile time */

EMU_EXPORT void emu_boot(uint32_t resetreas)
{
	emu_kernel_reset();
	emu_board_reset(resetreas);
	emu_fs_reset_mounts();
	emu_emmc_reset();
	emu_board_inputs_changed();
	emu_start_main(wakes_main);
}

EMU_EXPORT double emu_now_ms(void) { return (double)emu_now / (double)EMU_MS; }
EMU_EXPORT int    emu_stopped(void) { return emu_stop_reason; }

/* ---- inputs ---- */
EMU_EXPORT void emu_set_fader(int i, float v)
{
	if (i >= 0 && i < 4) {
		emu_in.fader[i] = v;
	}
}
EMU_EXPORT void emu_set_buttons(uint32_t mask) { emu_in.buttons = mask; }
EMU_EXPORT void emu_set_fnc(int on) { emu_in.fnc = on; emu_board_inputs_changed(); }
EMU_EXPORT void emu_set_usb(int mode) { emu_in.usb = mode; emu_board_inputs_changed(); }
EMU_EXPORT void emu_set_batt_mv(int mv) { emu_in.batt_mv = mv; emu_board_inputs_changed(); }
EMU_EXPORT void emu_set_console_open(int on) { emu_in.console_open = on; }

/* ---- outputs ---- */
EMU_EXPORT float   *emu_leds(void) { return &emu_led[0][0]; }
EMU_EXPORT int16_t *emu_audio_ring_ptr(void) { return emu_audio_ring; }
EMU_EXPORT uint32_t emu_audio_ring_frames(void) { return EMU_AUDIO_RING_FRAMES; }
EMU_EXPORT uint32_t emu_audio_written(void) { return emu_audio_wr; }
EMU_EXPORT uint32_t emu_audio_underrun_count(void) { return emu_audio_underruns; }
EMU_EXPORT int      emu_audio_running(void) { return emu_i2s_running; }

/* ---- MIDI from the host (USB-MIDI, only while USB is a computer) ---- */
static uint8_t midi_stage[256];
EMU_EXPORT uint8_t *emu_midi_buf(void) { return midi_stage; }
EMU_EXPORT void emu_midi_send(uint32_t n)
{
	emu_midi_in(midi_stage, n > sizeof(midi_stage) ? sizeof(midi_stage) : n);
}

/* ---- the WAKES drive ---- */
EMU_EXPORT void *emu_alloc(uint32_t n) { return malloc(n ? n : 1); }
EMU_EXPORT void  emu_free(void *p) { free(p); }
EMU_EXPORT int   emu_drive_slots(void) { return emu_fs_count(); }
EMU_EXPORT const char *emu_drive_path(int i) { return emu_fs_path(i); }
EMU_EXPORT int   emu_drive_is_dir(int i) { return emu_fs_is_dir(i); }
EMU_EXPORT uint32_t emu_drive_size(int i) { return emu_fs_size(i); }
EMU_EXPORT const uint8_t *emu_drive_data(int i) { return emu_fs_data(i); }
EMU_EXPORT int   emu_drive_put(const char *path, const uint8_t *data, uint32_t n, int is_dir)
{
	return emu_fs_put(path, data, n, is_dir);
}
EMU_EXPORT int   emu_drive_remove(const char *path) { return emu_fs_remove(path); }
EMU_EXPORT void  emu_drive_clear(void) { emu_fs_clear(); }
EMU_EXPORT uint32_t emu_drive_generation(void) { return emu_fs_generation(); }
EMU_EXPORT int   emu_drive_formatted(void) { return emu_fs_formatted(); }
EMU_EXPORT void  emu_drive_set_formatted(int on) { emu_fs_set_formatted(on); }
