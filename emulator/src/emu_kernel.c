/*
 * emu_kernel.c -- a cooperative, virtual-time stand-in for the Zephyr kernel.
 *
 * Threads are real coroutines with their own stacks (emu_arch_*.c), scheduled by
 * Zephyr's rule: the highest-priority ready thread runs (lower number wins; FIFO among
 * equals). A thread runs until it blocks -- sleep, semaphore, mutex, slab, I2S queue --
 * or until it wakes a thread that outranks it, which then preempts it at that point.
 *
 * Running code takes no virtual time. When every thread is blocked the clock jumps to
 * the next event. So timing is exact and repeatable (a sleep of 8 ms is 8 ms of audio),
 * and CPU cost is not modelled: see emulator/README.md, "What it cannot tell you".
 */
#include "emu.h"

#include <stdlib.h>

uint64_t emu_now;
struct k_thread *emu_current;
struct k_thread *emu_threads;
volatile int emu_stop_reason;

static uint32_t seq;
static int sched_locked;
static struct k_thread main_thread;
static int (*main_fn)(void);

void emu_set_now(uint64_t t)
{
	emu_now = t;
	emu_dwt.CYCCNT = (uint32_t)t;   /* DWT counts CPU cycles: Wakes' synth reads it */
}

void emu_kernel_reset(void)
{
	emu_threads = NULL;
	emu_current = NULL;
	seq = 0;
	sched_locked = 0;
	emu_stop_reason = EMU_STOP_NONE;
	emu_set_now(0);
}

/* ------------------------------------------------------------------ scheduling */

static void make_ready(struct k_thread *t)
{
	t->state = EMU_T_READY;
	t->wake_at = EMU_NEVER;
	t->wait_obj = NULL;
	t->seq = ++seq;
}

static bool outranks(const struct k_thread *a, const struct k_thread *b)
{
	return a->prio < b->prio || (a->prio == b->prio && a->seq < b->seq);
}

struct k_thread *emu_sched_pick(uint64_t limit)
{
	for (;;) {
		emu_board_on_switch();
		if (emu_stop_reason != EMU_STOP_NONE) {
			return NULL;
		}

		struct k_thread *best = NULL;
		for (struct k_thread *t = emu_threads; t; t = t->next_all) {
			if (t->state == EMU_T_READY && (!best || outranks(t, best))) {
				best = t;
			}
		}
		if (best) {
			return best;
		}

		/* Nothing can run: jump to the next thing that will happen. */
		uint64_t next = emu_board_next_event();
		for (struct k_thread *t = emu_threads; t; t = t->next_all) {
			if ((t->state == EMU_T_SLEEP || t->state == EMU_T_PEND) && t->wake_at < next) {
				next = t->wake_at;
			}
		}
		if (next > limit) {
			if (limit > emu_now) {
				emu_set_now(limit);
			}
			return NULL;
		}
		if (next > emu_now) {
			emu_set_now(next);
		}
		emu_board_run_events(emu_now);
		for (struct k_thread *t = emu_threads; t; t = t->next_all) {
			if ((t->state == EMU_T_SLEEP || t->state == EMU_T_PEND) &&
			    t->wake_at <= emu_now) {
				if (t->state == EMU_T_PEND) {
					t->wait_rc = -EAGAIN;
				}
				make_ready(t);
			}
		}
	}
}

void emu_switch(void)
{
	struct k_thread *t = emu_current;
	if (!t) {
		return;      /* not in a thread: nothing to switch away from */
	}
	t->run_switches++;
	emu_arch_switch_out();
}

void emu_preempt_check(void)
{
	struct k_thread *cur = emu_current;
	if (!cur || cur->state != EMU_T_READY || sched_locked || cur->prio < 0) {
		return;      /* ISR context, cooperative thread, or scheduler locked */
	}
	for (struct k_thread *t = emu_threads; t; t = t->next_all) {
		if (t != cur && t->state == EMU_T_READY && t->prio < cur->prio) {
			cur->seq = ++seq;        /* back of its own priority's queue */
			emu_switch();
			return;
		}
	}
}

static struct k_thread *best_waiter(void *obj)
{
	struct k_thread *best = NULL;
	for (struct k_thread *t = emu_threads; t; t = t->next_all) {
		if (t->state == EMU_T_PEND && t->wait_obj == obj && (!best || outranks(t, best))) {
			best = t;
		}
	}
	return best;
}

static void wake_one(struct k_thread *t, int rc, void *data)
{
	t->wait_rc = rc;
	t->wait_data = data;
	make_ready(t);
}

int emu_wake(void *obj, int all, int rc, void *data)
{
	int n = 0;
	struct k_thread *t;
	while ((t = best_waiter(obj)) != NULL) {
		wake_one(t, rc, data);
		n++;
		if (!all) {
			break;
		}
	}
	return n;
}

int emu_pend(void *obj, k_timeout_t timeout)
{
	struct k_thread *t = emu_current;
	if (!t) {
		return -EAGAIN;   /* cannot block outside a thread */
	}
	t->wait_obj = obj;
	t->wait_rc = -EAGAIN;
	t->wait_data = NULL;
	t->wake_at = (timeout.us < 0) ? EMU_NEVER : emu_now + (uint64_t)timeout.us * EMU_US;
	t->state = EMU_T_PEND;
	emu_switch();
	return t->wait_rc;
}

void emu_thread_exited(struct k_thread *t)
{
	t->state = EMU_T_DEAD;
	emu_switch();          /* never resumed */
	for (;;) {
		emu_stop(EMU_STOP_FAULT);
	}
}

/* ------------------------------------------------------------------ threads */

static void main_entry(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	(void)main_fn();
	emu_stop(EMU_STOP_MAIN_RETURNED);
}

void emu_start_main(int (*fn)(void))
{
	main_fn = fn;
	memset(&main_thread, 0, sizeof(main_thread));
	(void)k_thread_create(&main_thread, NULL, 0, main_entry, NULL, NULL, NULL,
			      CONFIG_MAIN_THREAD_PRIORITY, 0, K_NO_WAIT);
	(void)k_thread_name_set(&main_thread, "main");
}

k_tid_t k_thread_create(struct k_thread *t, k_thread_stack_t *stack, size_t stack_size,
			k_thread_entry_t entry, void *p1, void *p2, void *p3, int prio,
			uint32_t options, k_timeout_t delay)
{
	ARG_UNUSED(stack); ARG_UNUSED(options);
	bool listed = false;
	for (struct k_thread *x = emu_threads; x; x = x->next_all) {
		if (x == t) { listed = true; break; }
	}
	t->entry = entry;
	t->p1 = p1; t->p2 = p2; t->p3 = p3;
	t->prio = prio;
	t->stack_size = stack_size;
	t->started = 0;
	t->run_switches = 0;
	if (t->name[0] == '\0') {
		snprintf(t->name, sizeof(t->name), "thread%p", (void *)t);
	}
	if (!listed) {
		t->next_all = emu_threads;
		emu_threads = t;
	}
	emu_arch_prepare(t);
	if (delay.us > 0) {
		t->state = EMU_T_SLEEP;
		t->wake_at = emu_now + (uint64_t)delay.us * EMU_US;
		t->wait_obj = NULL;
	} else {
		make_ready(t);
	}
	if (emu_current) {
		emu_current->state = EMU_T_READY;
		emu_preempt_check();
	}
	return t;
}

int k_thread_name_set(k_tid_t t, const char *str)
{
	if (!t) { t = emu_current; }
	if (!t) { return -EINVAL; }
	snprintf(t->name, sizeof(t->name), "%s", str ? str : "");
	return 0;
}

const char *k_thread_name_get(k_tid_t t) { return t ? t->name : NULL; }
void k_thread_priority_set(k_tid_t t, int prio) { if (t) { t->prio = prio; } }
int  k_thread_priority_get(k_tid_t t) { return t ? t->prio : 0; }
k_tid_t k_current_get(void) { return emu_current; }

void k_thread_foreach_unlocked(k_thread_user_cb_t cb, void *user)
{
	for (struct k_thread *t = emu_threads; t; t = t->next_all) {
		if (t->state != EMU_T_DEAD) {
			cb(t, user);
		}
	}
}

void k_thread_foreach(k_thread_user_cb_t cb, void *user) { k_thread_foreach_unlocked(cb, user); }

int k_thread_stack_space_get(const struct k_thread *t, size_t *unused)
{
	/* The emulator's stacks are not the device's: report half free rather than guess. */
	*unused = t ? t->stack_size / 2u : 0u;
	return 0;
}

int k_thread_runtime_stats_get(k_tid_t t, k_thread_runtime_stats_t *s)
{
	ARG_UNUSED(t);
	memset(s, 0, sizeof(*s));   /* CPU time is not modelled */
	return 0;
}

int k_thread_runtime_stats_all_get(k_thread_runtime_stats_t *s)
{
	memset(s, 0, sizeof(*s));
	s->execution_cycles = emu_now;
	s->total_cycles = emu_now;
	return 0;
}

void k_sched_lock(void) { sched_locked++; }
void k_sched_unlock(void)
{
	if (sched_locked > 0) {
		sched_locked--;
	}
	if (emu_current) {
		emu_current->state = EMU_T_READY;
		emu_preempt_check();
	}
}

/* ------------------------------------------------------------------ time */

int32_t k_msleep(int32_t ms) { return k_sleep(K_MSEC(ms)); }
int32_t k_usleep(int32_t us) { return k_sleep(K_USEC(us)); }

int32_t k_sleep(k_timeout_t timeout)
{
	struct k_thread *t = emu_current;
	if (!t) {
		return 0;
	}
	if (timeout.us == 0) {
		k_yield();
		return 0;
	}
	t->state = EMU_T_SLEEP;
	t->wait_obj = NULL;
	t->wake_at = (timeout.us < 0) ? EMU_NEVER : emu_now + (uint64_t)timeout.us * EMU_US;
	emu_switch();
	return 0;
}

void k_yield(void)
{
	struct k_thread *t = emu_current;
	if (!t) {
		return;
	}
	make_ready(t);
	emu_switch();
}

int64_t  k_uptime_get(void)    { return (int64_t)(emu_now / EMU_MS); }
uint32_t k_uptime_get_32(void) { return (uint32_t)(emu_now / EMU_MS); }

int64_t k_uptime_delta(int64_t *ref)
{
	const int64_t now = k_uptime_get();
	const int64_t d = now - *ref;
	*ref = now;
	return d;
}

uint64_t k_cycle_get_64(void)
{
	return (emu_now * (uint64_t)CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC) / EMU_CPU_HZ;
}

uint32_t k_cycle_get_32(void) { return (uint32_t)k_cycle_get_64(); }

void k_busy_wait(uint32_t usec)
{
	/* The CPU is busy and nothing else runs; events catch up at the next switch. */
	emu_set_now(emu_now + (uint64_t)usec * EMU_US);
}

/* ------------------------------------------------------------------ semaphores */

int k_sem_init(struct k_sem *s, unsigned int initial, unsigned int limit)
{
	s->count = initial;
	s->limit = limit;
	return 0;
}

int k_sem_take(struct k_sem *s, k_timeout_t timeout)
{
	if (s->count > 0u) {
		s->count--;
		return 0;
	}
	if (timeout.us == 0) {
		return -EBUSY;
	}
	return emu_pend(s, timeout) == 0 ? 0 : -EAGAIN;
}

void k_sem_give(struct k_sem *s)
{
	if (emu_wake(s, 0, 0, NULL) == 0 && s->count < s->limit) {
		s->count++;
	}
	if (emu_current) {
		emu_current->state = EMU_T_READY;
		emu_preempt_check();
	}
}

void k_sem_reset(struct k_sem *s)
{
	s->count = 0;
	(void)emu_wake(s, 1, -EAGAIN, NULL);
}

/* ------------------------------------------------------------------ mutexes */

int k_mutex_init(struct k_mutex *m)
{
	m->owner = NULL;
	m->lock_count = 0;
	return 0;
}

int k_mutex_lock(struct k_mutex *m, k_timeout_t timeout)
{
	struct k_thread *cur = emu_current;
	if (m->owner == NULL) {
		m->owner = cur;
		m->lock_count = 1;
		return 0;
	}
	if (m->owner == cur) {
		m->lock_count++;
		return 0;
	}
	if (timeout.us == 0) {
		return -EBUSY;
	}
	/* The unlocking thread hands ownership over before waking us. */
	return emu_pend(m, timeout) == 0 ? 0 : -EAGAIN;
}

int k_mutex_unlock(struct k_mutex *m)
{
	if (m->owner != emu_current) {
		return -EPERM;
	}
	if (--m->lock_count > 0u) {
		return 0;
	}
	struct k_thread *w = best_waiter(m);
	if (w) {
		m->owner = w;
		m->lock_count = 1;
		wake_one(w, 0, NULL);
	} else {
		m->owner = NULL;
	}
	if (emu_current) {
		emu_current->state = EMU_T_READY;
		emu_preempt_check();
	}
	return 0;
}

/* ------------------------------------------------------------------ memory slabs */

static void slab_init(struct k_mem_slab *s)
{
	if (s->inited) {
		return;
	}
	s->free_list = NULL;
	for (uint32_t i = s->num_blocks; i > 0u; i--) {
		void **b = (void **)(s->buffer + (i - 1u) * s->block_size);
		*b = s->free_list;
		s->free_list = b;
	}
	s->num_used = 0;
	s->inited = 1;
}

int k_mem_slab_alloc(struct k_mem_slab *s, void **mem, k_timeout_t timeout)
{
	slab_init(s);
	if (s->free_list) {
		void **b = (void **)s->free_list;
		s->free_list = *b;
		s->num_used++;
		*mem = b;
		return 0;
	}
	if (timeout.us == 0) {
		*mem = NULL;
		return -ENOMEM;
	}
	if (emu_pend(s, timeout) == 0) {
		*mem = emu_current->wait_data;   /* handed over by k_mem_slab_free */
		return 0;
	}
	*mem = NULL;
	return -EAGAIN;
}

void k_mem_slab_free(struct k_mem_slab *s, void *mem)
{
	slab_init(s);
	struct k_thread *w = best_waiter(s);
	if (w) {
		wake_one(w, 0, mem);          /* straight to the waiter; still "used" */
	} else {
		void **b = (void **)mem;
		*b = s->free_list;
		s->free_list = b;
		s->num_used--;
	}
	if (emu_current) {
		emu_current->state = EMU_T_READY;
		emu_preempt_check();
	}
}

uint32_t k_mem_slab_num_free_get(struct k_mem_slab *s)
{
	slab_init(s);
	return s->num_blocks - s->num_used;
}

uint32_t k_mem_slab_num_used_get(struct k_mem_slab *s)
{
	slab_init(s);
	return s->num_used;
}
