/*
 * emu.h -- the emulator's own interfaces: virtual time, the scheduler, the board model.
 * Wakes never includes this; it sees only emu_zephyr.h.
 */
#ifndef EMU_H
#define EMU_H

#include "emu_zephyr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- virtual time ----
 * The master clock is the nRF52840's CPU clock, 64 MHz. Code takes no virtual time to
 * run; time only moves when every thread is blocked, which is when the scheduler jumps
 * to the next event (a sleep ending, an I2S block draining, a timeout). */
#define EMU_CPU_HZ   64000000ull
#define EMU_MS       (EMU_CPU_HZ / 1000u)
#define EMU_US       (EMU_CPU_HZ / 1000000u)
#define EMU_NEVER    UINT64_MAX
extern uint64_t emu_now;
void emu_set_now(uint64_t t);

/* ---- threads / scheduler (emu_kernel.c) ---- */
enum { EMU_T_NEW, EMU_T_READY, EMU_T_SLEEP, EMU_T_PEND, EMU_T_DEAD };
extern struct k_thread *emu_current;
extern struct k_thread *emu_threads;          /* every thread ever created */
void emu_kernel_reset(void);
void emu_start_main(int (*main_fn)(void));
/* Next thread to run, or NULL once virtual time has reached `limit`. Processes every
 * event due on the way. */
struct k_thread *emu_sched_pick(uint64_t limit);
/* The current thread gives up the CPU (it has set its own state first). */
void emu_switch(void);
/* Around every run of a thread: makes it current, and charges it the CPU cycles counted
 * on DWT->CYCCNT while it ran (the wasm build counts them; see tools/wasm_cycles.py). */
void emu_run_begin(struct k_thread *t);
void emu_run_end(void);
/* The CPU meter's window: 0 audio-thread cycles, 1 audio blocks they made, 2 the dearest
 * single block, 3 all threads' cycles, 4 the window's length in CPU cycles, 5 main's,
 * 6 the storage thread's; 10-17 the audio thread's cycles per kind of work (profiling
 * builds only). */
double emu_cpu_stat(int which);
void   emu_cpu_reset(void);
/* Called by the arch layer when a thread's entry function returns. */
void emu_thread_exited(struct k_thread *t);
/* Wake every thread pended on obj, or only the best one; returns how many. */
int  emu_wake(void *obj, int all, int rc, void *data);
/* Pend the current thread on obj until woken or `timeout` passes; returns wait_rc. */
int  emu_pend(void *obj, k_timeout_t timeout);
/* Give up the CPU if a thread that outranks the current one is ready. */
void emu_preempt_check(void);

/* ---- arch (emu_arch_native.c / emu_arch_wasm.c) ---- */
void emu_arch_prepare(struct k_thread *t);    /* allocate stacks for a new thread */
void emu_arch_switch_out(void);               /* current thread -> scheduler       */

/* ---- events the scheduler drives (emu_board.c) ---- */
uint64_t emu_board_next_event(void);
void     emu_board_run_events(uint64_t now);
void     emu_board_on_switch(void);           /* GPIO latch, watchdog check        */

/* ---- resets: never return; the host restarts the device ---- */
enum emu_stop {
	EMU_STOP_NONE = 0,
	EMU_STOP_SYSTEM_OFF,     /* SYSTEM_OFF: dark until "••" or USB wakes it   */
	EMU_STOP_SOFT_RESET,     /* NVIC_SystemReset / sys_reboot                  */
	EMU_STOP_WATCHDOG,       /* the watchdog expired                           */
	EMU_STOP_FAULT,          /* a trap: k_sys_fatal_error_handler's path       */
	EMU_STOP_MAIN_RETURNED,
};
FUNC_NORETURN void emu_stop(enum emu_stop why);
extern volatile int emu_stop_reason;

/* ---- the board (emu_board.c) ---- */
struct emu_inputs {
	float    fader[4];        /* 0..1, bottom to top                              */
	uint32_t buttons;         /* bit per enum sp1_button (PLAY, T1..T4, VOL+...)  */
	int      fnc;             /* "••" held                                        */
	int      usb;             /* 0 unplugged, 1 charger, 2 computer               */
	int      batt_mv;         /* cell voltage                                     */
	int      console_open;    /* a terminal has the CDC port open (DTR)           */
};
extern struct emu_inputs emu_in;
void emu_board_reset(uint32_t resetreas);
void emu_board_inputs_changed(void);

/* LEDs as the PWM peripherals hold them: [row][channel] = pulse / period, 0..1. */
extern float emu_led[2][4];

/* Audio the I2S peripheral has clocked out: stereo int16, a ring the host drains. */
#define EMU_AUDIO_RING_FRAMES 16384u
extern int16_t  emu_audio_ring[EMU_AUDIO_RING_FRAMES * 2];
extern uint32_t emu_audio_wr;           /* frames written, total (wraps)      */
extern uint32_t emu_audio_underruns;
extern int      emu_i2s_running;
extern uint32_t emu_i2s_writes;         /* blocks accepted by i2s_write, total */
uint32_t emu_i2s_block_frames(void);    /* frames per audio block, as Wakes configured I2S */

/* Console output (printk, after Wakes' own hook). */
void emu_console_write(const char *s, size_t n);

/* USB events into Wakes' own msg callback, and MIDI into its receive path. */
void emu_usb_update(void);
void emu_midi_in(const uint8_t *bytes, size_t n);

/* ---- file system (emu_fs.c) ---- */
void emu_fs_reset_mounts(void);
int  emu_fs_formatted(void);
void emu_fs_set_formatted(int on);
int  emu_fs_count(void);
const char *emu_fs_path(int i);
int  emu_fs_is_dir(int i);
uint32_t emu_fs_size(int i);
const uint8_t *emu_fs_data(int i);
int  emu_fs_put(const char *path, const uint8_t *data, uint32_t n, int is_dir);
int  emu_fs_remove(const char *path);
void emu_fs_clear(void);
uint32_t emu_fs_generation(void);

/* ---- eMMC blocks (emu_emmc.c) ---- */
void emu_emmc_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* EMU_H */
