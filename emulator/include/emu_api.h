/* emu_api.h -- the host-facing API (emulator/src/emu_api.c). */
#ifndef EMU_API_H
#define EMU_API_H

#include <stdint.h>
#include <stddef.h>

/* The wasm build compiles everything -fvisibility=hidden and links --export-dynamic,
 * so exactly the functions marked here become exports. */
#define EMU_EXPORT __attribute__((visibility("default"), used))

#ifdef __cplusplus
extern "C" {
#endif

void   emu_boot(uint32_t resetreas);
double emu_now_ms(void);
int    emu_stopped(void);
void   emu_set_fader(int i, float v);
void   emu_set_buttons(uint32_t mask);
void   emu_set_fnc(int on);
void   emu_set_usb(int mode);
void   emu_set_batt_mv(int mv);
void   emu_set_console_open(int on);
float   *emu_leds(void);
int16_t *emu_audio_ring_ptr(void);
uint32_t emu_audio_ring_frames(void);
uint32_t emu_audio_written(void);
uint32_t emu_audio_underrun_count(void);
int      emu_audio_running(void);
uint8_t *emu_midi_buf(void);
void     emu_midi_send(uint32_t n);

/* Native only: run until virtual time reaches limit_ms or the device stops. */
double emu_run_until_ms(double limit_ms);
/* Native only: where console lines go (default stdout). */
void emu_set_console_sink(void (*fn)(const char *s, size_t n));

#ifdef __cplusplus
}
#endif

#endif
