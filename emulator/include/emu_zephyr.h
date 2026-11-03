/*
 * emu_zephyr.h -- the slice of Zephyr, nRF and CMSIS that Wakes uses, implemented for
 * the emulator. Every fake header under emulator/include/ includes this one file.
 *
 * Nothing here is Zephyr's code. It is a re-statement of the API surface Wakes calls
 * (names, types, argument order), backed by emulator/src/emu_*.c. Behaviour follows
 * Zephyr's documented semantics closely enough that firmware/src compiles and runs
 * unchanged; where it differs, the difference is noted next to the definition.
 */
#ifndef EMU_ZEPHYR_H
#define EMU_ZEPHYR_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>

#include "emu_autoconf.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ toolchain */
#define __noinit
#define __packed            __attribute__((__packed__))
#define __aligned(x)        __attribute__((__aligned__(x)))
#define __maybe_unused      __attribute__((__unused__))
#define __unused            __attribute__((__unused__))
#define __used              __attribute__((__used__))
#define __weak              __attribute__((__weak__))
#define __printf_like(f, a) __attribute__((format(printf, f, a)))
#define FUNC_NORETURN       __attribute__((__noreturn__))
#define ALWAYS_INLINE       inline __attribute__((always_inline))
#define likely(x)           __builtin_expect(!!(x), 1)
#define unlikely(x)         __builtin_expect(!!(x), 0)
#define CODE_UNREACHABLE    __builtin_unreachable()
#define ARG_UNUSED(x)       (void)(x)

#ifdef __cplusplus
#define BUILD_ASSERT(EXPR, ...) static_assert(EXPR, "" __VA_ARGS__)
#else
#define BUILD_ASSERT(EXPR, ...) _Static_assert(EXPR, "" __VA_ARGS__)
#endif

#define __ASSERT(test, fmt, ...)    do { } while (0)
#define __ASSERT_NO_MSG(test)       do { } while (0)

/* ------------------------------------------------------------------ sys/util.h */
#define ARRAY_SIZE(a)       (sizeof(a) / sizeof((a)[0]))
#ifndef MIN
#define MIN(a, b)           (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b)           (((a) > (b)) ? (a) : (b))
#endif
#define CLAMP(v, lo, hi)    (((v) < (lo)) ? (lo) : (((v) > (hi)) ? (hi) : (v)))
#define BIT(n)              (1UL << (n))
#define BIT_MASK(n)         (BIT(n) - 1UL)
#define DIV_ROUND_UP(n, d)  (((n) + (d) - 1) / (d))
#define ROUND_UP(x, a)      ((((x) + ((a) - 1)) / (a)) * (a))
#define ROUND_DOWN(x, a)    (((x) / (a)) * (a))
#define CONTAINER_OF(ptr, type, field) ((type *)(((char *)(ptr)) - offsetof(type, field)))
#define STRINGIFY(s)        Z_STRINGIFY(s)
#define Z_STRINGIFY(s)      #s

/* Zephyr's IS_ENABLED / IF_ENABLED / COND_CODE_1 trick, restated. */
#define _XXXX1 _YYYY,
#define IS_ENABLED(config_macro)        Z_IS_ENABLED1(config_macro)
#define Z_IS_ENABLED1(config_macro)     Z_IS_ENABLED2(_XXXX##config_macro)
#define Z_IS_ENABLED2(one_or_two_args)  Z_IS_ENABLED3(one_or_two_args 1, 0)
#define Z_IS_ENABLED3(ignore_this, val, ...) val
#define COND_CODE_1(_flag, _if_1_code, _else_code) Z_COND_CODE_1(_flag, _if_1_code, _else_code)
#define Z_COND_CODE_1(_flag, _if_1_code, _else_code) \
	Z_COND_CODE(_XXXX##_flag, _if_1_code, _else_code)
#define Z_COND_CODE(one_or_two_args, _if_code, _else_code) \
	Z_GET_ARG2_DEBRACKET(one_or_two_args _if_code, _else_code)
#define Z_GET_ARG2_DEBRACKET(ignore_this, val, ...) Z_DEBRACKET val
#define Z_DEBRACKET(...) __VA_ARGS__
#define IF_ENABLED(_flag, _code) COND_CODE_1(_flag, _code, ())

/* ------------------------------------------------------------------ printk */
void printk(const char *fmt, ...) __printf_like(1, 2);
void vprintk(const char *fmt, va_list ap);
#define snprintk  snprintf
#define vsnprintk vsnprintf
typedef int (*printk_hook_fn_t)(int c);
void __printk_hook_install(printk_hook_fn_t fn);
printk_hook_fn_t __printk_get_hook(void);

/* ------------------------------------------------------------------ time */
/* Timeouts are carried in microseconds. K_FOREVER is -1, K_NO_WAIT 0. */
typedef struct { int64_t us; } k_timeout_t;
#define K_NO_WAIT       ((k_timeout_t){ 0 })
#define K_FOREVER       ((k_timeout_t){ -1 })
#define K_USEC(t)       ((k_timeout_t){ (int64_t)(t) })
#define K_MSEC(t)       ((k_timeout_t){ (int64_t)(t) * 1000 })
#define K_SECONDS(t)    ((k_timeout_t){ (int64_t)(t) * 1000000 })
#define K_TIMEOUT_EQ(a, b) ((a).us == (b).us)

int32_t  k_msleep(int32_t ms);
int32_t  k_usleep(int32_t us);
int32_t  k_sleep(k_timeout_t t);
int64_t  k_uptime_get(void);
uint32_t k_uptime_get_32(void);
int64_t  k_uptime_delta(int64_t *reftime);
uint32_t k_cycle_get_32(void);
uint64_t k_cycle_get_64(void);
void     k_busy_wait(uint32_t usec);
void     k_yield(void);
static inline uint32_t k_ms_to_cyc_ceil32(uint32_t ms)
{
	return (uint32_t)(((uint64_t)ms * CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC + 999u) / 1000u);
}
static inline uint32_t k_cyc_to_ms_floor32(uint32_t c)
{
	return (uint32_t)(((uint64_t)c * 1000u) / CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC);
}
static inline uint64_t k_cyc_to_us_floor64(uint64_t c)
{
	return (c * 1000000u) / CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
}
static inline uint32_t k_cyc_to_us_floor32(uint32_t c)
{
	return (uint32_t)k_cyc_to_us_floor64(c);
}

/* ------------------------------------------------------------------ threads */
typedef void (*k_thread_entry_t)(void *p1, void *p2, void *p3);

struct k_thread {
	/* Emulator bookkeeping; Wakes only ever takes the address of one. */
	int       prio;
	int       state;
	uint64_t  wake_at;        /* virtual time, CPU cycles; UINT64_MAX = no timeout */
	void     *wait_obj;       /* what it is pended on                              */
	int       wait_rc;        /* 0 = given, -EAGAIN = timed out                    */
	void     *wait_data;      /* a slab block handed over on wake                  */
	uint32_t  seq;            /* FIFO among equal priorities                        */
	k_thread_entry_t entry;
	void     *p1, *p2, *p3;
	char      name[32];
	uint8_t  *stack_mem;
	size_t    stack_size;
	uintptr_t stack_top;
	uintptr_t saved_sp;       /* wasm: shadow stack pointer at the last switch     */
	void     *arch;           /* native: ucontext; wasm: asyncify data             */
	int       started;
	uint64_t  run_switches;
	struct k_thread *next_all;
};
typedef struct k_thread *k_tid_t;

#define K_THREAD_STACK_DEFINE(sym, size)  uint8_t sym[(size)] __attribute__((aligned(16)))
#define K_KERNEL_STACK_DEFINE(sym, size)  K_THREAD_STACK_DEFINE(sym, size)
#define K_THREAD_STACK_SIZEOF(sym)        sizeof(sym)
typedef uint8_t k_thread_stack_t;
#define CONFIG_NUM_COOP_PRIORITIES 16
#define K_PRIO_COOP(x)    (-(CONFIG_NUM_COOP_PRIORITIES - (x)))
#define K_PRIO_PREEMPT(x) (x)
#define K_ESSENTIAL 0
#define K_FP_REGS   0

k_tid_t k_thread_create(struct k_thread *new_thread, k_thread_stack_t *stack,
			size_t stack_size, k_thread_entry_t entry,
			void *p1, void *p2, void *p3, int prio, uint32_t options,
			k_timeout_t delay);
int     k_thread_name_set(k_tid_t thread, const char *str);
const char *k_thread_name_get(k_tid_t thread);
void    k_thread_priority_set(k_tid_t thread, int prio);
int     k_thread_priority_get(k_tid_t thread);
k_tid_t k_current_get(void);
typedef void (*k_thread_user_cb_t)(const struct k_thread *thread, void *user_data);
void    k_thread_foreach_unlocked(k_thread_user_cb_t user_cb, void *user_data);
void    k_thread_foreach(k_thread_user_cb_t user_cb, void *user_data);
int     k_thread_stack_space_get(const struct k_thread *thread, size_t *unused_ptr);
void    k_sched_lock(void);
void    k_sched_unlock(void);

typedef struct {
	uint64_t execution_cycles;
	uint64_t total_cycles;
	uint64_t current_cycles;
	uint64_t peak_cycles;
	uint64_t average_cycles;
	uint64_t idle_cycles;
} k_thread_runtime_stats_t;
int k_thread_runtime_stats_get(k_tid_t thread, k_thread_runtime_stats_t *stats);
int k_thread_runtime_stats_all_get(k_thread_runtime_stats_t *stats);

/* ------------------------------------------------------------------ sync */
struct k_sem { uint32_t count; uint32_t limit; };
#define Z_SEM_INITIALIZER(obj, initial, lim) { (initial), (lim) }
#define K_SEM_DEFINE(name, initial, lim) struct k_sem name = { (initial), (lim) }
#define K_SEM_MAX_LIMIT UINT32_MAX
int  k_sem_init(struct k_sem *sem, unsigned int initial, unsigned int limit);
int  k_sem_take(struct k_sem *sem, k_timeout_t timeout);
void k_sem_give(struct k_sem *sem);
void k_sem_reset(struct k_sem *sem);
static inline unsigned int k_sem_count_get(struct k_sem *sem) { return sem->count; }

struct k_mutex { struct k_thread *owner; uint32_t lock_count; };
#define K_MUTEX_DEFINE(name) struct k_mutex name = { NULL, 0 }
int k_mutex_init(struct k_mutex *m);
int k_mutex_lock(struct k_mutex *m, k_timeout_t timeout);
int k_mutex_unlock(struct k_mutex *m);

struct k_spinlock { int dummy; };
typedef struct { int key; } k_spinlock_key_t;
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *l) { (void)l; return (k_spinlock_key_t){ 0 }; }
static inline void k_spin_unlock(struct k_spinlock *l, k_spinlock_key_t k) { (void)l; (void)k; }
static inline unsigned int irq_lock(void) { return 0; }
static inline void irq_unlock(unsigned int key) { (void)key; }
static inline bool k_is_in_isr(void) { return false; }

struct k_mem_slab {
	uint8_t *buffer;
	size_t   block_size;
	uint32_t num_blocks;
	uint32_t num_used;
	void    *free_list;
	int      inited;
};
#define K_MEM_SLAB_DEFINE_STATIC(name, slab_block_size, slab_num_blocks, slab_align) \
	static uint8_t name##_emu_buf[(slab_block_size) * (slab_num_blocks)]         \
		__attribute__((aligned(16)));                                        \
	static struct k_mem_slab name = { name##_emu_buf, (slab_block_size),         \
					  (slab_num_blocks), 0, NULL, 0 }
#define K_MEM_SLAB_DEFINE(name, bs, nb, al) \
	static uint8_t name##_emu_buf[(bs) * (nb)] __attribute__((aligned(16))); \
	struct k_mem_slab name = { name##_emu_buf, (bs), (nb), 0, NULL, 0 }
int  k_mem_slab_alloc(struct k_mem_slab *slab, void **mem, k_timeout_t timeout);
void k_mem_slab_free(struct k_mem_slab *slab, void *mem);
uint32_t k_mem_slab_num_free_get(struct k_mem_slab *slab);
uint32_t k_mem_slab_num_used_get(struct k_mem_slab *slab);

/* ------------------------------------------------------------------ atomics */
/* Threads are cooperative in the emulator: a plain access is atomic. */
typedef long atomic_t;
typedef long atomic_val_t;
#define ATOMIC_INIT(i) (i)
static inline atomic_val_t atomic_get(const atomic_t *t) { return *(const volatile atomic_t *)t; }
static inline atomic_val_t atomic_set(atomic_t *t, atomic_val_t v) { atomic_val_t o = *t; *t = v; return o; }
static inline atomic_val_t atomic_clear(atomic_t *t) { return atomic_set(t, 0); }
static inline atomic_val_t atomic_add(atomic_t *t, atomic_val_t v) { atomic_val_t o = *t; *t = o + v; return o; }
static inline atomic_val_t atomic_sub(atomic_t *t, atomic_val_t v) { atomic_val_t o = *t; *t = o - v; return o; }
static inline atomic_val_t atomic_inc(atomic_t *t) { return atomic_add(t, 1); }
static inline atomic_val_t atomic_dec(atomic_t *t) { return atomic_sub(t, 1); }
static inline atomic_val_t atomic_and(atomic_t *t, atomic_val_t v) { atomic_val_t o = *t; *t = o & v; return o; }
static inline atomic_val_t atomic_or(atomic_t *t, atomic_val_t v) { atomic_val_t o = *t; *t = o | v; return o; }
static inline atomic_val_t atomic_xor(atomic_t *t, atomic_val_t v) { atomic_val_t o = *t; *t = o ^ v; return o; }
static inline bool atomic_cas(atomic_t *t, atomic_val_t old, atomic_val_t nv)
{
	if (*t != old) { return false; }
	*t = nv;
	return true;
}
static inline bool atomic_test_bit(const atomic_t *t, int bit) { return ((*t >> bit) & 1) != 0; }
static inline void atomic_set_bit(atomic_t *t, int bit) { *t |= (1L << bit); }
static inline void atomic_clear_bit(atomic_t *t, int bit) { *t &= ~(1L << bit); }
static inline bool atomic_test_and_clear_bit(atomic_t *t, int bit)
{
	bool r = atomic_test_bit(t, bit);
	atomic_clear_bit(t, bit);
	return r;
}
static inline bool atomic_test_and_set_bit(atomic_t *t, int bit)
{
	bool r = atomic_test_bit(t, bit);
	atomic_set_bit(t, bit);
	return r;
}

/* ------------------------------------------------------------------ fatal / reboot */
struct arch_esf { struct { uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr; } basic; };
void k_sys_fatal_error_handler(unsigned int reason, const struct arch_esf *esf);
#define SYS_REBOOT_WARM 0
#define SYS_REBOOT_COLD 1
FUNC_NORETURN void sys_reboot(int type);
FUNC_NORETURN void sys_poweroff(void);

/* ------------------------------------------------------------------ devices + DT */
struct device {
	const char *name;
	int         kind;
	void       *data;
};
static inline bool device_is_ready(const struct device *dev) { return dev != NULL; }
static inline int  device_init(const struct device *dev) { return dev ? 0 : -ENODEV; }

#define DT_NODELABEL(label)   label
#define DT_PATH(...)          zephyr_user
#define DT_ALIAS(alias)       EMU_ALIAS_##alias
#define DT_CHOSEN(prop)       EMU_CHOSEN_##prop
#define EMU_ALIAS_watchdog0   wdt0
#define EMU_CHOSEN_zephyr_console cdc_acm_uart0
#define EMU_DEV(node)         EMU_DEV_(node)
#define EMU_DEV_(node)        emu_device_##node
#define DEVICE_DT_GET(node)   (&EMU_DEV(node))
#define DEVICE_DT_GET_OR_NULL(node) DEVICE_DT_GET(node)
#define DT_NODE_HAS_STATUS(node, st) 1
#define DT_NODE_HAS_STATUS_OKAY(node) 1
#define DT_NODE_EXISTS(node)  1

extern const struct device emu_device_i2s0;
extern const struct device emu_device_i2c0;
extern const struct device emu_device_pwm2;
extern const struct device emu_device_pwm3;
extern const struct device emu_device_wdt0;
extern const struct device emu_device_adc;
extern const struct device emu_device_cdc_acm_uart0;
extern const struct device emu_device_zephyr_udc0;

/* ------------------------------------------------------------------ ADC */
#define ADC_GAIN_1_6        0
#define ADC_REF_INTERNAL    0
#define ADC_ACQ_TIME_DEFAULT 0
struct adc_dt_spec {
	const struct device *dev;
	uint8_t channel_id;
	bool    channel_cfg_dt_node_exists;
	uint8_t resolution;
	uint8_t oversampling;
};
struct adc_sequence_options { uint32_t interval_us; void *callback; void *user_data; uint16_t extra_samplings; };
struct adc_sequence {
	const struct adc_sequence_options *options;
	uint32_t channels;
	void    *buffer;
	size_t   buffer_size;
	uint8_t  resolution;
	uint8_t  oversampling;
	bool     calibrate;
};
#define ADC_DT_SPEC_GET_BY_IDX(node, idx) \
	{ .dev = &emu_device_adc, .channel_id = (idx), .channel_cfg_dt_node_exists = true, \
	  .resolution = 12, .oversampling = 0 }
static inline bool adc_is_ready_dt(const struct adc_dt_spec *s) { return s && s->dev; }
static inline int  adc_channel_setup_dt(const struct adc_dt_spec *s) { (void)s; return 0; }
static inline int  adc_sequence_init_dt(const struct adc_dt_spec *s, struct adc_sequence *seq)
{
	seq->channels = BIT(s->channel_id);
	seq->resolution = s->resolution;
	seq->oversampling = s->oversampling;
	return 0;
}
int adc_read(const struct device *dev, const struct adc_sequence *seq);
int adc_read_dt(const struct adc_dt_spec *spec, const struct adc_sequence *seq);

/* ------------------------------------------------------------------ PWM */
typedef uint16_t pwm_flags_t;
int pwm_set_cycles(const struct device *dev, uint32_t channel, uint32_t period,
		   uint32_t pulse, pwm_flags_t flags);

/* ------------------------------------------------------------------ I2C */
#define I2C_BITRATE_STANDARD 100000u
#define I2C_BITRATE_FAST     400000u
int i2c_write(const struct device *dev, const uint8_t *buf, uint32_t num_bytes, uint16_t addr);
int i2c_read(const struct device *dev, uint8_t *buf, uint32_t num_bytes, uint16_t addr);
int i2c_write_read(const struct device *dev, uint16_t addr, const void *write_buf,
		   size_t num_write, void *read_buf, size_t num_read);
int i2c_reg_write_byte(const struct device *dev, uint16_t dev_addr, uint8_t reg_addr, uint8_t value);
int i2c_reg_read_byte(const struct device *dev, uint16_t dev_addr, uint8_t reg_addr, uint8_t *value);

/* ------------------------------------------------------------------ I2S */
enum i2s_dir { I2S_DIR_RX, I2S_DIR_TX, I2S_DIR_BOTH };
enum i2s_trigger_cmd { I2S_TRIGGER_START, I2S_TRIGGER_STOP, I2S_TRIGGER_DRAIN,
		       I2S_TRIGGER_DROP, I2S_TRIGGER_PREPARE };
enum i2s_state { I2S_STATE_NOT_READY, I2S_STATE_READY, I2S_STATE_RUNNING,
		 I2S_STATE_STOPPING, I2S_STATE_ERROR };
#define I2S_FMT_DATA_FORMAT_I2S        0
#define I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED 1
#define I2S_OPT_FRAME_CLK_SLAVE        BIT(2)
#define I2S_OPT_BIT_CLK_SLAVE          BIT(1)
#define I2S_OPT_FRAME_CLK_MASTER       0
#define I2S_OPT_BIT_CLK_MASTER         0
typedef uint8_t i2s_fmt_t;
typedef uint8_t i2s_opt_t;
struct i2s_config {
	uint8_t  word_size;
	uint8_t  channels;
	i2s_fmt_t format;
	i2s_opt_t options;
	uint32_t frame_clk_freq;
	struct k_mem_slab *mem_slab;
	size_t   block_size;
	int32_t  timeout;
};
int i2s_configure(const struct device *dev, enum i2s_dir dir, const struct i2s_config *cfg);
int i2s_trigger(const struct device *dev, enum i2s_dir dir, enum i2s_trigger_cmd cmd);
int i2s_write(const struct device *dev, void *mem_block, size_t size);

/* ------------------------------------------------------------------ UART (CDC ACM) */
enum uart_line_ctrl { UART_LINE_CTRL_BAUD_RATE = 1, UART_LINE_CTRL_RTS = 2,
		      UART_LINE_CTRL_DTR = 4, UART_LINE_CTRL_DCD = 8, UART_LINE_CTRL_DSR = 16 };
int uart_line_ctrl_get(const struct device *dev, uint32_t ctrl, uint32_t *val);
int uart_line_ctrl_set(const struct device *dev, uint32_t ctrl, uint32_t val);
int uart_poll_in(const struct device *dev, unsigned char *c);
void uart_poll_out(const struct device *dev, unsigned char c);

/* ------------------------------------------------------------------ watchdog */
typedef void (*wdt_callback_t)(const struct device *dev, int channel_id);
struct wdt_window { uint32_t min; uint32_t max; };
struct wdt_timeout_cfg {
	struct wdt_window window;
	wdt_callback_t callback;
	uint8_t flags;
};
#define WDT_FLAG_RESET_SOC        BIT(1)
#define WDT_OPT_PAUSE_HALTED_BY_DBG BIT(1)
int wdt_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg);
int wdt_setup(const struct device *dev, uint8_t options);
int wdt_feed(const struct device *dev, int channel_id);

/* ------------------------------------------------------------------ disk access */
#define DISK_STATUS_OK       0x00
#define DISK_STATUS_UNINIT   0x01
#define DISK_STATUS_NOMEDIA  0x02
#define DISK_STATUS_WR_PROTECT 0x04
#define DISK_IOCTL_GET_SECTOR_COUNT   1
#define DISK_IOCTL_GET_SECTOR_SIZE    2
#define DISK_IOCTL_GET_ERASE_BLOCK_SZ 4
#define DISK_IOCTL_CTRL_SYNC          5
#define DISK_IOCTL_CTRL_INIT          6
#define DISK_IOCTL_CTRL_DEINIT        7
struct disk_info;
struct disk_operations {
	int (*init)(struct disk_info *disk);
	int (*status)(struct disk_info *disk);
	int (*read)(struct disk_info *disk, uint8_t *data_buf, uint32_t start_sector, uint32_t num_sector);
	int (*write)(struct disk_info *disk, const uint8_t *data_buf, uint32_t start_sector, uint32_t num_sector);
	int (*ioctl)(struct disk_info *disk, uint8_t cmd, void *buff);
};
struct disk_info {
	const char *name;
	const struct disk_operations *ops;
	const struct device *dev;
	uint32_t refcnt;
	struct disk_info *emu_next;
};
int disk_access_register(struct disk_info *disk);
int disk_access_unregister(struct disk_info *disk);

/* ------------------------------------------------------------------ FatFs (ff.h) */
typedef struct { uint32_t database; uint32_t fsize; uint32_t csize; } FATFS;
typedef struct { uint8_t fmt; uint8_t n_fat; uint32_t align; uint32_t n_root; uint32_t au_size; } MKFS_PARM;
#define FM_FAT   0x01
#define FM_FAT32 0x02
#define FM_EXFAT 0x04
#define FM_ANY   0x07
#define FM_SFD   0x08

/* ------------------------------------------------------------------ file system */
#define FS_FATFS   0
#define FS_LITTLEFS 1
#define FS_O_READ    0x01
#define FS_O_WRITE   0x02
#define FS_O_RDWR    (FS_O_READ | FS_O_WRITE)
#define FS_O_MODE_MASK 0x03
#define FS_O_CREATE  0x10
#define FS_O_APPEND  0x20
#define FS_O_TRUNC   0x40
#define FS_O_FLAGS_MASK 0x70
#define FS_SEEK_SET  0
#define FS_SEEK_CUR  1
#define FS_SEEK_END  2
#define FS_MOUNT_FLAG_NO_FORMAT BIT(0)
#define FS_MOUNT_FLAG_READ_ONLY BIT(1)
#define MAX_FILE_NAME 255
typedef long off_t_emu;
enum fs_dir_entry_type { FS_DIR_ENTRY_FILE = 0, FS_DIR_ENTRY_DIR };
struct fs_dirent {
	enum fs_dir_entry_type type;
	char   name[MAX_FILE_NAME + 1];
	size_t size;
};
struct fs_mount_t {
	int         type;
	const char *mnt_point;
	void       *fs_data;
	void       *storage_dev;
	uint8_t     flags;
	size_t      mountp_len;
	const void *fs;
};
struct fs_file_t { void *filep; const struct fs_mount_t *mp; uint32_t flags; };
struct fs_dir_t  { void *dirp; const struct fs_mount_t *mp; };
struct fs_statvfs {
	unsigned long f_bsize;
	unsigned long f_frsize;
	unsigned long f_blocks;
	unsigned long f_bfree;
};
static inline void fs_file_t_init(struct fs_file_t *zfp) { zfp->filep = NULL; zfp->mp = NULL; zfp->flags = 0; }
static inline void fs_dir_t_init(struct fs_dir_t *zdp) { zdp->dirp = NULL; zdp->mp = NULL; }
int     fs_mount(struct fs_mount_t *mp);
int     fs_unmount(struct fs_mount_t *mp);
int     fs_mkfs(int fs_type, uintptr_t dev_id, void *cfg, int flags);
int     fs_open(struct fs_file_t *zfp, const char *file_name, uint32_t flags);
int     fs_close(struct fs_file_t *zfp);
ssize_t fs_read(struct fs_file_t *zfp, void *ptr, size_t size);
ssize_t fs_write(struct fs_file_t *zfp, const void *ptr, size_t size);
int     fs_seek(struct fs_file_t *zfp, long offset, int whence);
long    fs_tell(struct fs_file_t *zfp);
int     fs_truncate(struct fs_file_t *zfp, long length);
int     fs_sync(struct fs_file_t *zfp);
int     fs_unlink(const char *path);
int     fs_rename(const char *from, const char *to);
int     fs_mkdir(const char *path);
int     fs_stat(const char *path, struct fs_dirent *entry);
int     fs_opendir(struct fs_dir_t *zdp, const char *path);
int     fs_readdir(struct fs_dir_t *zdp, struct fs_dirent *entry);
int     fs_closedir(struct fs_dir_t *zdp);
int     fs_statvfs(const char *path, struct fs_statvfs *stat);

/* ------------------------------------------------------------------ USB device next */
enum usbd_speed { USBD_SPEED_FS = 1, USBD_SPEED_HS = 2 };
enum usbd_msg_type {
	USBD_MSG_VBUS_READY, USBD_MSG_VBUS_REMOVED, USBD_MSG_RESUME, USBD_MSG_SUSPEND,
	USBD_MSG_RESET, USBD_MSG_SOF, USBD_MSG_UDC_ERROR, USBD_MSG_STACK_ERROR,
	USBD_MSG_CDC_ACM_LINE_CODING, USBD_MSG_CDC_ACM_CONTROL_LINE_STATE,
	USBD_MSG_DFU_APP_DETACH, USBD_MSG_DFU_DOWNLOAD_COMPLETED, USBD_MSG_CONFIGURATION,
	USBD_MSG_MAX_NUMBER
};
struct usbd_context;
struct usbd_msg { enum usbd_msg_type type; int status; const struct device *dev; };
typedef void (*usbd_msg_cb_t)(struct usbd_context *const ctx, const struct usbd_msg *const msg);
struct usbd_context {
	const char *name;
	uint16_t vid, pid;
	usbd_msg_cb_t msg_cb;
	bool enabled;
	bool inited;
};
struct usbd_desc_node   { const char *str; int type; };
struct usbd_config_node { uint8_t attrib; uint8_t power; const struct usbd_desc_node *desc; };
#define USB_SCD_SELF_POWERED  0x40
#define USB_SCD_REMOTE_WAKEUP 0x20
#define USB_BCC_MISCELLANEOUS 0xEF
#define USBD_DEVICE_DEFINE(device_name, udc_dev, vid_, pid_) \
	struct usbd_context device_name = { #device_name, (vid_), (pid_), NULL, false, false }
#define USBD_DESC_LANG_DEFINE(name)               static struct usbd_desc_node name = { "lang", 0 }
#define USBD_DESC_MANUFACTURER_DEFINE(name, s)    static struct usbd_desc_node name = { (s), 1 }
#define USBD_DESC_PRODUCT_DEFINE(name, s)         static struct usbd_desc_node name = { (s), 2 }
#define USBD_DESC_SERIAL_NUMBER_DEFINE(name)      static struct usbd_desc_node name = { "SN", 3 }
#define USBD_DESC_CONFIG_DEFINE(name, s)          static struct usbd_desc_node name = { (s), 4 }
#define USBD_DESC_STRING_DEFINE(name, s, t)       static struct usbd_desc_node name = { (s), (t) }
#define USBD_CONFIGURATION_DEFINE(name, attrib, power, desc_nd) \
	static struct usbd_config_node name = { (attrib), (power), (desc_nd) }
int usbd_add_descriptor(struct usbd_context *uds_ctx, struct usbd_desc_node *dn);
int usbd_add_configuration(struct usbd_context *uds_ctx, enum usbd_speed speed,
			   struct usbd_config_node *cd);
int usbd_register_all_classes(struct usbd_context *uds_ctx, enum usbd_speed speed,
			      uint8_t cfg, const char *const blocklist[]);
int usbd_register_class(struct usbd_context *uds_ctx, const char *name,
			enum usbd_speed speed, uint8_t cfg);
int usbd_device_set_bcd_device(struct usbd_context *uds_ctx, uint16_t bcd);
int usbd_device_set_code_triple(struct usbd_context *uds_ctx, enum usbd_speed speed,
				uint8_t base_class, uint8_t subclass, uint8_t protocol);
int usbd_self_powered(struct usbd_context *uds_ctx, bool status);
int usbd_msg_register_cb(struct usbd_context *uds_ctx, usbd_msg_cb_t cb);
int usbd_init(struct usbd_context *uds_ctx);
int usbd_enable(struct usbd_context *uds_ctx);
int usbd_disable(struct usbd_context *uds_ctx);
#define USBD_DEFINE_MSC_LUN(id, disk_name, t10_vendor, t10_product, t10_revision) \
	__attribute__((unused)) static const char *const emu_msc_lun_##id = (disk_name)

/* ------------------------------------------------------------------ nRF + CMSIS */
typedef struct {
	volatile uint32_t OUT, OUTSET, OUTCLR, IN, DIR, DIRSET, DIRCLR, LATCH, DETECTMODE;
	volatile uint32_t PIN_CNF[32];
} NRF_GPIO_Type;
typedef struct {
	volatile uint32_t RESETREAS, SYSTEMOFF, USBREGSTATUS, GPREGRET, GPREGRET2, POFCON;
} NRF_POWER_Type;
typedef struct { volatile uint32_t RR[8]; volatile uint32_t CRV, RREN, CONFIG, RUNSTATUS; } NRF_WDT_Type;
typedef struct { volatile uint32_t ENABLE, TASKS_START, TASKS_STOP; } NRF_I2S_Type;
typedef struct { volatile uint32_t ICACHECNF, IHIT, IMISS; } NRF_NVMC_Type;
typedef struct { volatile uint32_t CTRL, CYCCNT; } DWT_Type;
typedef struct { volatile uint32_t DEMCR; } DCB_Type;
extern NRF_GPIO_Type  emu_nrf_p0, emu_nrf_p1;
extern NRF_POWER_Type emu_nrf_power;
extern NRF_WDT_Type   emu_nrf_wdt;
extern NRF_I2S_Type   emu_nrf_i2s;
extern NRF_NVMC_Type  emu_nrf_nvmc;
extern DWT_Type       emu_dwt;
extern DCB_Type       emu_dcb;
#define NRF_P0    (&emu_nrf_p0)
#define NRF_P1    (&emu_nrf_p1)
#define NRF_POWER (&emu_nrf_power)
#define NRF_WDT   (&emu_nrf_wdt)
#define NRF_I2S   (&emu_nrf_i2s)
#define NRF_NVMC  (&emu_nrf_nvmc)
#define DWT       (&emu_dwt)
#define DCB       (&emu_dcb)
#define CoreDebug (&emu_dcb)

#define GPIO_PIN_CNF_DIR_Pos        0
#define GPIO_PIN_CNF_DIR_Input      0UL
#define GPIO_PIN_CNF_DIR_Output     1UL
#define GPIO_PIN_CNF_INPUT_Pos      1
#define GPIO_PIN_CNF_INPUT_Connect  0UL
#define GPIO_PIN_CNF_INPUT_Disconnect 1UL
#define GPIO_PIN_CNF_PULL_Pos       2
#define GPIO_PIN_CNF_PULL_Disabled  0UL
#define GPIO_PIN_CNF_PULL_Pulldown  1UL
#define GPIO_PIN_CNF_PULL_Pullup    3UL
#define GPIO_PIN_CNF_DRIVE_Pos      8
#define GPIO_PIN_CNF_DRIVE_S0S1     0UL
#define GPIO_PIN_CNF_DRIVE_H0H1     3UL
#define GPIO_PIN_CNF_DRIVE_S0D1     6UL
#define GPIO_PIN_CNF_SENSE_Pos      16
#define GPIO_PIN_CNF_SENSE_Disabled 0UL
#define GPIO_PIN_CNF_SENSE_High     2UL
#define GPIO_PIN_CNF_SENSE_Low      3UL
#define WDT_RR_RR_Reload            0x6E524635UL
#define POWER_USBREGSTATUS_VBUSDETECT_Msk  0x1UL
#define POWER_USBREGSTATUS_OUTPUTRDY_Msk   0x2UL
#define POWER_RESETREAS_RESETPIN_Msk 0x1UL
#define POWER_RESETREAS_DOG_Msk      0x2UL
#define POWER_RESETREAS_SREQ_Msk     0x4UL
#define POWER_RESETREAS_LOCKUP_Msk   0x8UL
#define POWER_RESETREAS_OFF_Msk      0x10000UL
#define POWER_RESETREAS_VBUS_Msk     0x100000UL
#define DWT_CTRL_CYCCNTENA_Msk       0x1UL
#define DCB_DEMCR_TRCENA_Msk         (1UL << 24)
#define CoreDebug_DEMCR_TRCENA_Msk   (1UL << 24)
#define NVMC_ICACHECNF_CACHEPROFEN_Msk (1UL << 8)
#define NVMC_ICACHECNF_CACHEEN_Msk   1UL

/* A barrier is where the emulator notices a write it has to act on (SYSTEMOFF). */
void emu_dsb(void);
#define __DSB() emu_dsb()
#define __ISB() emu_dsb()
#define __DMB() do { } while (0)
#define __WFE() emu_dsb()
#define __WFI() emu_dsb()
#define __SEV() do { } while (0)
#define __NOP() do { } while (0)
FUNC_NORETURN void NVIC_SystemReset(void);

/* ------------------------------------------------------------------ app_version */
#include "emu_app_version.h"

#ifdef __cplusplus
}
#endif

#endif /* EMU_ZEPHYR_H */
