/*
 * emu_arch_native.c -- thread switching for a desktop build: ucontext coroutines.
 * Used by the native harness (emulator/native/) to test the emulator without a browser.
 */
#include "emu.h"
#include "emu_api.h"

#include <stdlib.h>
#include <ucontext.h>

#define NATIVE_STACK (1u << 20)

struct arch {
	ucontext_t ctx;
	void *stack;
};

static ucontext_t sched_ctx;
static void (*sink)(const char *s, size_t n);

void emu_set_console_sink(void (*fn)(const char *s, size_t n)) { sink = fn; }

void emu_console_write(const char *s, size_t n)
{
	if (sink) {
		sink(s, n);
	} else {
		fwrite(s, 1, n, stdout);
		fflush(stdout);
	}
}

static void trampoline(void)
{
	struct k_thread *t = emu_current;
	t->started = 1;
	t->entry(t->p1, t->p2, t->p3);
	emu_thread_exited(t);
}

void emu_arch_prepare(struct k_thread *t)
{
	struct arch *a = t->arch;
	if (!a) {
		a = calloc(1, sizeof(*a));
		a->stack = malloc(NATIVE_STACK);
		t->arch = a;
	}
	getcontext(&a->ctx);
	a->ctx.uc_stack.ss_sp = a->stack;
	a->ctx.uc_stack.ss_size = NATIVE_STACK;
	a->ctx.uc_link = &sched_ctx;
	makecontext(&a->ctx, trampoline, 0);
}

void emu_arch_switch_out(void)
{
	struct arch *a = emu_current->arch;
	swapcontext(&a->ctx, &sched_ctx);
}

FUNC_NORETURN void emu_stop(enum emu_stop why)
{
	if (emu_stop_reason == EMU_STOP_NONE) {
		emu_stop_reason = why;
	}
	if (emu_current) {
		struct arch *a = emu_current->arch;
		swapcontext(&a->ctx, &sched_ctx);  /* never resumed */
	}
	abort();
}

double emu_run_until_ms(double limit_ms)
{
	const uint64_t limit = (uint64_t)(limit_ms * (double)EMU_MS);
	while (emu_stop_reason == EMU_STOP_NONE) {
		struct k_thread *t = emu_sched_pick(limit);
		if (!t) {
			break;
		}
		emu_current = t;
		struct arch *a = t->arch;
		swapcontext(&sched_ctx, &a->ctx);
		emu_current = NULL;
	}
	return (double)emu_now / (double)EMU_MS;
}
