/*
 * emu_arch_wasm.c -- thread switching in WebAssembly, with Binaryen's Asyncify.
 *
 * Each thread gets its own shadow stack (linear memory) and its own Asyncify buffer.
 * Switching out calls the host import emu_host_switch(): the host starts an unwind,
 * which returns through every wasm frame to the host's dispatch loop (web/emu-core.js),
 * remembering the shadow stack pointer. Resuming sets that pointer back and rewinds
 * into the same call, which then returns normally. Only the host's loop and these
 * exports know about Asyncify; Wakes' code is unchanged.
 */
#include "emu.h"
#include "emu_api.h"

#include <stdlib.h>

#define WASM_STACK  (256u * 1024u)    /* shadow stack per thread (Wakes' are 3-4 KB on the
				       * device; wasm frames are laid out differently)     */
#define ASYNC_BUF   (64u * 1024u)     /* Asyncify's saved locals per thread               */

__attribute__((import_module("env"), import_name("emu_host_switch")))
void emu_host_switch(void);
__attribute__((import_module("env"), import_name("emu_host_stop")))
void emu_host_stop(int why);
__attribute__((import_module("env"), import_name("emu_host_console")))
void emu_host_console(const char *s, size_t n);

void emu_console_write(const char *s, size_t n) { emu_host_console(s, n); }

void emu_arch_prepare(struct k_thread *t)
{
	if (!t->stack_mem) {
		t->stack_mem = malloc(WASM_STACK);
		t->stack_top = ((uintptr_t)t->stack_mem + WASM_STACK) & ~(uintptr_t)15u;
	}
	if (!t->arch) {
		t->arch = malloc(ASYNC_BUF);
	}
	uint32_t *d = (uint32_t *)t->arch;
	d[0] = (uint32_t)(uintptr_t)(d + 2);            /* Asyncify: current position */
	d[1] = (uint32_t)(uintptr_t)t->arch + ASYNC_BUF; /* Asyncify: end              */
	t->started = 0;
	t->saved_sp = 0;
}

void emu_arch_switch_out(void) { emu_host_switch(); }

FUNC_NORETURN void emu_stop(enum emu_stop why)
{
	if (emu_stop_reason == EMU_STOP_NONE) {
		emu_stop_reason = why;
	}
	emu_host_stop(why);         /* throws in the host; never returns */
	__builtin_trap();
}

/* ---- the host's dispatch loop uses these ---- */
EMU_EXPORT struct k_thread *emu_pick(double limit_ms)
{
	if (emu_stop_reason != EMU_STOP_NONE) {
		return NULL;
	}
	return emu_sched_pick((uint64_t)(limit_ms * (double)EMU_MS));
}
EMU_EXPORT void      emu_set_current(struct k_thread *t) { emu_current = t; }
EMU_EXPORT struct k_thread *emu_get_current(void) { return emu_current; }
EMU_EXPORT int       emu_t_started(struct k_thread *t) { return t->started; }
EMU_EXPORT void      emu_t_mark_started(struct k_thread *t) { t->started = 1; }
EMU_EXPORT uintptr_t emu_t_stack_top(struct k_thread *t) { return t->stack_top; }
EMU_EXPORT uintptr_t emu_t_async(struct k_thread *t) { return (uintptr_t)t->arch; }
EMU_EXPORT uintptr_t emu_t_saved_sp(struct k_thread *t) { return t->saved_sp; }
EMU_EXPORT void      emu_t_save_sp(struct k_thread *t, uintptr_t sp) { t->saved_sp = sp; }
EMU_EXPORT const char *emu_t_name(struct k_thread *t) { return t->name; }

EMU_EXPORT void emu_thread_enter(struct k_thread *t)
{
	t->entry(t->p1, t->p2, t->p3);
	emu_thread_exited(t);
}

/* wasi-libc's reactor model wants this for static constructors; the host calls it. */
EMU_EXPORT int emu_ready(void) { return 1; }
