/*
 * emu_board.c -- the SP-1 around the nRF52840: registers, ADC (faders, both button
 * ladders, battery), PWM (the two LED rows), I2C (CS42L42 + TAS2505), I2S (the
 * codec-clocked audio stream), the watchdog, the CDC console and USB.
 *
 * The analog values are the ones Wakes' own tables were measured from (firmware/src/
 * sp1_controls.c, Adara's unit, 2026-09-19), including the rail sag a ladder press puts
 * on the faders, so the real decode, debounce and sag rejection run unchanged.
 */
#include "emu.h"
#include "sp1_controls.h"
#include "sp1_midi_usb.h"

/* ------------------------------------------------------------------ registers */
NRF_GPIO_Type  emu_nrf_p0, emu_nrf_p1;
NRF_POWER_Type emu_nrf_power;
NRF_WDT_Type   emu_nrf_wdt;
NRF_I2S_Type   emu_nrf_i2s;
NRF_NVMC_Type  emu_nrf_nvmc;
DWT_Type       emu_dwt;
DCB_Type       emu_dcb;

const struct device emu_device_i2s0          = { "i2s0", 1, NULL };
const struct device emu_device_i2c0          = { "i2c0", 2, NULL };
const struct device emu_device_pwm2          = { "pwm2", 3, NULL };
const struct device emu_device_pwm3          = { "pwm3", 4, NULL };
const struct device emu_device_wdt0          = { "wdt0", 5, NULL };
const struct device emu_device_adc           = { "adc", 6, NULL };
const struct device emu_device_cdc_acm_uart0 = { "cdc_acm_uart0", 7, NULL };
const struct device emu_device_zephyr_udc0   = { "zephyr_udc0", 8, NULL };

struct emu_inputs emu_in = { { 0.5f, 0.5f, 0.5f, 0.5f }, 0, 0, 0, 3900, 1 };
float emu_led[2][4];

/* Pin numbers, as firmware/src/sp1_board.h has them. */
#define PIN_FNC     27u   /* P0, "••", active low            */
#define PIN_NCE     21u   /* P0, charger enable, active low  */
#define PIN_NCHG    22u   /* P0, charging, active low         */
#define PIN_NPGOOD  24u   /* P0, USB power good, active low   */
#define PIN_TAS_RST 9u    /* P0                               */
#define PIN_OSC_EN  13u   /* P0, 3.072 MHz audio oscillator   */
#define PIN_CS42_RST 15u  /* P0                               */
#define PIN_BTN_COM 10u   /* P1, rail for ladders + faders    */

/* OUTSET/OUTCLR are write-only strobes on the chip. Here they are memory, so the
 * board folds them into OUT whenever it looks (each switch, each ADC/I2C access). */
static void latch_port(NRF_GPIO_Type *p)
{
	if (p->OUTSET) { p->OUT |= p->OUTSET; p->OUTSET = 0; }
	if (p->OUTCLR) { p->OUT &= ~p->OUTCLR; p->OUTCLR = 0; }
}

static bool pin_out(NRF_GPIO_Type *p, uint32_t pin)
{
	latch_port(p);
	return (p->OUT >> pin) & 1u;
}

static bool charging_now(void)
{
	/* BQ24232: charges while USB is present, nCE is low, and the cell is not full. */
	return emu_in.usb > 0 && !pin_out(&emu_nrf_p0, PIN_NCE) && emu_in.batt_mv < 4150;
}

static void update_inputs(void)
{
	uint32_t in0 = 0xFFFFFFFFu;
	if (emu_in.fnc)      { in0 &= ~(1u << PIN_FNC); }
	if (emu_in.usb > 0)  { in0 &= ~(1u << PIN_NPGOOD); }
	if (charging_now())  { in0 &= ~(1u << PIN_NCHG); }
	emu_nrf_p0.IN = in0;
	emu_nrf_p1.IN = 0xFFFFFFFFu;
	emu_nrf_power.USBREGSTATUS = (emu_in.usb > 0)
		? (POWER_USBREGSTATUS_VBUSDETECT_Msk | POWER_USBREGSTATUS_OUTPUTRDY_Msk) : 0u;
}

/* ------------------------------------------------------------------ console */

static char line[512];
static size_t line_n;

static int default_hook(int c)
{
	line[line_n++] = (char)c;
	if (c == '\n' || line_n == sizeof(line)) {
		emu_console_write(line, line_n);
		line_n = 0;
	}
	return c;
}

static printk_hook_fn_t hook = default_hook;
void __printk_hook_install(printk_hook_fn_t fn) { hook = fn ? fn : default_hook; }
printk_hook_fn_t __printk_get_hook(void) { return hook; }

void vprintk(const char *fmt, va_list ap)
{
	char buf[1024];
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	if (n < 0) {
		return;
	}
	if ((size_t)n >= sizeof(buf)) {
		n = (int)sizeof(buf) - 1;
	}
	for (int i = 0; i < n; i++) {
		hook((unsigned char)buf[i]);
	}
}

void printk(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintk(fmt, ap);
	va_end(ap);
}

/* ------------------------------------------------------------------ ADC */

/* io-channels on zephyr_user, as firmware/src/sp1_controls.c and sp1_batt.c index them. */
enum { CH_BATT = 0, CH_LADDER0 = 1, CH_LADDER1 = 2, CH_FADER1 = 3 };

/* Ladder 0 (T1-T4 + PLAY): centre reading per pressed set, mask bits T1 0x01, T2 0x02,
 * T3 0x04, T4 0x08, PLAY 0x10. Measured/modelled values from sp1_controls.c. */
static const uint16_t ladder0[32] = {
	[0x01] = 210,  [0x02] = 399,  [0x03] = 570,  [0x04] = 728,  [0x05] = 865,
	[0x06] = 990,  [0x07] = 1104, [0x08] = 1215, [0x09] = 1311, [0x0A] = 1398,
	[0x0B] = 1480, [0x0C] = 1558, [0x0D] = 1628, [0x0E] = 1693, [0x0F] = 1755,
	[0x10] = 1813, [0x11] = 1866, [0x12] = 1917, [0x13] = 1965, [0x14] = 2013,
	[0x15] = 2055, [0x16] = 2096, [0x17] = 2134, [0x18] = 2170, [0x19] = 2209,
	[0x1A] = 2242, [0x1B] = 2273, [0x1C] = 2305, [0x1D] = 2333, [0x1E] = 2361,
	[0x1F] = 2387,
};
#define LADDER_IDLE 4u          /* nothing pressed: a few counts of noise floor  */
#define LADDER_A    3604.0f     /* sp1_controls.c: ladder model full scale        */
#define SAG_K       0.0178f     /* sp1_controls.c: rail droop per unit load       */

static uint16_t ladder0_value(void)
{
	const uint32_t b = emu_in.buttons;
	uint32_t m = 0;
	if (b & BIT(SP1_BTN_T1))   { m |= 0x01; }
	if (b & BIT(SP1_BTN_T2))   { m |= 0x02; }
	if (b & BIT(SP1_BTN_T3))   { m |= 0x04; }
	if (b & BIT(SP1_BTN_T4))   { m |= 0x08; }
	if (b & BIT(SP1_BTN_PLAY)) { m |= 0x10; }
	return m ? ladder0[m] : LADDER_IDLE;
}

static uint16_t ladder1_value(void)
{
	/* Single-press ladder: the lowest-resistance key wins if two are held. */
	const uint32_t b = emu_in.buttons;
	if (b & BIT(SP1_BTN_RWD))      { return 403; }
	if (b & BIT(SP1_BTN_VOL_DOWN)) { return 729; }
	if (b & BIT(SP1_BTN_FFWD))     { return 1216; }
	if (b & BIT(SP1_BTN_VOL_UP))   { return 1817; }
	return LADDER_IDLE;
}

static uint16_t adc_value(uint8_t ch)
{
	if (ch == CH_BATT) {
		/* Cell -> divider (x 2.005) -> pin -> 12 bits over 3.6 V, as sp1_batt.c reads it. */
		const uint32_t pin_mv = ((uint32_t)emu_in.batt_mv * 1000u) / 2005u;
		uint32_t raw = (pin_mv * 4095u) / 3600u;
		return (uint16_t)(raw > 4095u ? 4095u : raw);
	}
	/* Both ladders and all four faders hang off BTN_COM (P1.10). Unpowered, ~0. */
	if (!pin_out(&emu_nrf_p1, PIN_BTN_COM)) {
		return 0u;
	}
	const uint16_t l0 = ladder0_value();
	const uint16_t l1 = ladder1_value();
	if (ch == CH_LADDER0) { return l0; }
	if (ch == CH_LADDER1) { return l1; }
	if (ch >= CH_FADER1 && ch < CH_FADER1 + 4) {
		float f = emu_in.fader[ch - CH_FADER1];
		f = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
		float v = f * 4095.0f;
		/* A pressed ladder loads the shared rail; every fader reads low with it. */
		const uint16_t r = l0 > l1 ? l0 : l1;
		if (r > LADDER_IDLE) {
			const float x = (float)r / (LADDER_A - (float)r);
			v = v / (1.0f + SAG_K * x);
		}
		return (uint16_t)(v + 0.5f);
	}
	return 0u;
}

int adc_read(const struct device *dev, const struct adc_sequence *seq)
{
	if (dev != &emu_device_adc || !seq || !seq->buffer || seq->buffer_size < 2) {
		return -EINVAL;
	}
	int16_t *out = (int16_t *)seq->buffer;
	size_t n = 0;
	for (uint8_t ch = 0; ch < 32 && (n + 1) * 2 <= seq->buffer_size; ch++) {
		if (seq->channels & BIT(ch)) {
			out[n++] = (int16_t)adc_value(ch);
		}
	}
	return n ? 0 : -EINVAL;
}

int adc_read_dt(const struct adc_dt_spec *spec, const struct adc_sequence *seq)
{
	return adc_read(spec->dev, seq);
}

/* ------------------------------------------------------------------ PWM (LEDs) */

int pwm_set_cycles(const struct device *dev, uint32_t ch, uint32_t period, uint32_t pulse,
		   pwm_flags_t flags)
{
	ARG_UNUSED(flags);
	const int row = (dev == &emu_device_pwm2) ? 0 : (dev == &emu_device_pwm3) ? 1 : -1;
	if (row < 0 || ch > 3u || period == 0u) {
		return -EINVAL;
	}
	emu_led[row][ch] = (float)(pulse > period ? period : pulse) / (float)period;
	return 0;
}

/* ------------------------------------------------------------------ I2C codecs */

#define CS42_ADDR 0x48u
#define TAS_ADDR  0x18u
static uint8_t cs42_page;
static uint8_t cs42_reg[0x80][0x100];      /* [page & 0x7F][register] */
static uint8_t tas_page;
static uint8_t tas_reg[4][0x80];

static bool chip_out_of_reset(uint16_t addr)
{
	if (addr == CS42_ADDR) { return pin_out(&emu_nrf_p0, PIN_CS42_RST); }
	if (addr == TAS_ADDR)  { return pin_out(&emu_nrf_p0, PIN_TAS_RST); }
	return false;
}

static uint8_t cs42_read(uint8_t page, uint8_t reg)
{
	const uint16_t r = (uint16_t)(page << 8) | reg;
	if (r == 0x1001) { return 0x42; }      /* device ID: CS42L42                 */
	if (r == 0x1109) { return 0x02; }      /* "SCLK selected": the osc is running */
	return cs42_reg[page & 0x7F][reg];
}

int i2c_write(const struct device *dev, const uint8_t *buf, uint32_t n, uint16_t addr)
{
	if (dev != &emu_device_i2c0 || !chip_out_of_reset(addr) || n == 0) {
		return -EIO;                         /* NAK: no chip, or held in reset */
	}
	if (addr == CS42_ADDR) {
		if (buf[0] == 0x00 && n >= 2) {
			cs42_page = buf[1];
		} else if (n >= 2) {
			cs42_reg[cs42_page & 0x7F][buf[0]] = buf[1];
		}
		return 0;
	}
	if (buf[0] == 0x00 && n >= 2) {
		tas_page = buf[1] & 3u;
	} else if (n >= 2 && buf[0] < 0x80) {
		tas_reg[tas_page][buf[0]] = buf[1];
	}
	return 0;
}

int i2c_read(const struct device *dev, uint8_t *buf, uint32_t n, uint16_t addr)
{
	if (dev != &emu_device_i2c0 || !chip_out_of_reset(addr)) {
		return -EIO;
	}
	memset(buf, 0, n);
	return 0;
}

int i2c_write_read(const struct device *dev, uint16_t addr, const void *wbuf, size_t nw,
		   void *rbuf, size_t nr)
{
	if (dev != &emu_device_i2c0 || !chip_out_of_reset(addr) || nw < 1) {
		return -EIO;
	}
	const uint8_t reg = ((const uint8_t *)wbuf)[0];
	uint8_t *out = (uint8_t *)rbuf;
	for (size_t i = 0; i < nr; i++) {
		out[i] = (addr == CS42_ADDR) ? cs42_read(cs42_page, (uint8_t)(reg + i))
					     : tas_reg[tas_page][(reg + i) & 0x7Fu];
	}
	return 0;
}

int i2c_reg_write_byte(const struct device *dev, uint16_t a, uint8_t reg, uint8_t v)
{
	const uint8_t b[2] = { reg, v };
	return i2c_write(dev, b, 2, a);
}

int i2c_reg_read_byte(const struct device *dev, uint16_t a, uint8_t reg, uint8_t *v)
{
	return i2c_write_read(dev, a, &reg, 1, v, 1);
}

/* ------------------------------------------------------------------ I2S */

/* The SP-1's I2S is a SLAVE: the 3.072 MHz oscillator and the CS42L42 clock it. A block
 * drains every block_size/4 frames at 48 kHz, but only while that clock runs -- with
 * the oscillator off, nothing drains and i2s_write times out, as on the device. */
#define I2S_QMAX 8
static struct i2s_config i2s_cfg;
static bool     i2s_configured;
static void    *i2s_q[I2S_QMAX];
static unsigned i2s_qn;
static uint64_t i2s_t0, i2s_ticks, i2s_next;
static uint32_t i2s_frames;          /* per block */
int      emu_i2s_running;
uint32_t emu_i2s_writes;
int16_t  emu_audio_ring[EMU_AUDIO_RING_FRAMES * 2];
uint32_t emu_audio_wr;
uint32_t emu_audio_underruns;
static int i2s_q_obj;                /* what a full i2s_write pends on */
uint32_t emu_i2s_block_frames(void) { return i2s_frames; }

static unsigned i2s_qcap(void)
{
	return (unsigned)CONFIG_I2S_NRFX_TX_BLOCK_COUNT + 2u;  /* queue + DMA current + next */
}

static uint64_t block_cycles(uint64_t n)
{
	return (n * i2s_frames * EMU_CPU_HZ) / 48000u;
}

static void i2s_drop_all(void)
{
	for (unsigned i = 0; i < i2s_qn; i++) {
		k_mem_slab_free(i2s_cfg.mem_slab, i2s_q[i]);
	}
	i2s_qn = 0;
	emu_i2s_running = 0;
	i2s_next = EMU_NEVER;
	(void)emu_wake(&i2s_q_obj, 1, 0, NULL);
}

int i2s_configure(const struct device *dev, enum i2s_dir dir, const struct i2s_config *cfg)
{
	if (dev != &emu_device_i2s0 || dir != I2S_DIR_TX || !cfg || !cfg->mem_slab) {
		return -EINVAL;
	}
	if (cfg->word_size != 16 || cfg->channels != 2) {
		return -EINVAL;      /* the only format Wakes (and this model) uses */
	}
	if (emu_i2s_running) {
		return -EIO;
	}
	i2s_cfg = *cfg;
	i2s_frames = (uint32_t)(cfg->block_size / 4u);
	i2s_configured = i2s_frames > 0;
	return i2s_configured ? 0 : -EINVAL;
}

int i2s_trigger(const struct device *dev, enum i2s_dir dir, enum i2s_trigger_cmd cmd)
{
	if (dev != &emu_device_i2s0 || dir != I2S_DIR_TX) {
		return -EINVAL;
	}
	switch (cmd) {
	case I2S_TRIGGER_START:
		if (!i2s_configured || emu_i2s_running || i2s_qn == 0) {
			return -EIO;
		}
		emu_i2s_running = 1;
		i2s_t0 = emu_now;
		i2s_ticks = 0;
		i2s_next = i2s_t0 + block_cycles(1);
		return 0;
	case I2S_TRIGGER_STOP:
	case I2S_TRIGGER_DRAIN:
	case I2S_TRIGGER_DROP:
		i2s_drop_all();
		return 0;
	case I2S_TRIGGER_PREPARE:
		return 0;
	}
	return -EINVAL;
}

int i2s_write(const struct device *dev, void *blk, size_t size)
{
	if (dev != &emu_device_i2s0 || !i2s_configured || size != i2s_cfg.block_size) {
		return -EIO;
	}
	while (i2s_qn >= i2s_qcap()) {
		if (emu_pend(&i2s_q_obj, K_MSEC(i2s_cfg.timeout)) != 0) {
			return -EAGAIN;
		}
	}
	i2s_q[i2s_qn++] = blk;
	emu_i2s_writes++;
	return 0;
}

static void i2s_tick(void)
{
	const bool clocked = pin_out(&emu_nrf_p0, PIN_OSC_EN);
	i2s_ticks++;
	i2s_next = i2s_t0 + block_cycles(i2s_ticks + 1u);
	if (!clocked) {
		return;                     /* no bit clock: nothing moves */
	}
	const uint32_t at = emu_audio_wr % EMU_AUDIO_RING_FRAMES;
	(void)at;
	if (i2s_qn == 0) {
		emu_audio_underruns++;
		for (uint32_t f = 0; f < i2s_frames; f++) {
			const uint32_t w = (emu_audio_wr++) % EMU_AUDIO_RING_FRAMES;
			emu_audio_ring[2 * w] = 0;
			emu_audio_ring[2 * w + 1] = 0;
		}
		return;
	}
	void *blk = i2s_q[0];
	memmove(&i2s_q[0], &i2s_q[1], (i2s_qn - 1) * sizeof(void *));
	i2s_qn--;
	const int16_t *s = (const int16_t *)blk;
	for (uint32_t f = 0; f < i2s_frames; f++) {
		const uint32_t w = (emu_audio_wr++) % EMU_AUDIO_RING_FRAMES;
		emu_audio_ring[2 * w] = s[2 * f];
		emu_audio_ring[2 * w + 1] = s[2 * f + 1];
	}
	k_mem_slab_free(i2s_cfg.mem_slab, blk);
	(void)emu_wake(&i2s_q_obj, 0, 0, NULL);
}

/* ------------------------------------------------------------------ watchdog */

static bool     wdt_on;
static uint32_t wdt_ms;
static wdt_callback_t wdt_cb;
static uint64_t wdt_fed;

int wdt_install_timeout(const struct device *dev, const struct wdt_timeout_cfg *cfg)
{
	if (dev != &emu_device_wdt0 || !cfg) {
		return -EINVAL;
	}
	wdt_ms = cfg->window.max;
	wdt_cb = cfg->callback;
	return 0;
}

int wdt_setup(const struct device *dev, uint8_t options)
{
	ARG_UNUSED(options);
	if (dev != &emu_device_wdt0 || wdt_ms == 0u) {
		return -EINVAL;
	}
	wdt_on = true;
	wdt_fed = emu_now;
	return 0;
}

int wdt_feed(const struct device *dev, int ch)
{
	ARG_UNUSED(dev); ARG_UNUSED(ch);
	wdt_fed = emu_now;
	return 0;
}

static void wdt_check(void)
{
	bool fed = false;
	for (int ch = 0; ch < 8; ch++) {
		if (emu_nrf_wdt.RR[ch] == WDT_RR_RR_Reload) {
			fed = true;
			emu_nrf_wdt.RR[ch] = 0;
		}
	}
	if (fed) {
		wdt_fed = emu_now;
	}
	if (wdt_on && emu_now - wdt_fed > (uint64_t)wdt_ms * EMU_MS) {
		if (wdt_cb) {
			wdt_cb(&emu_device_wdt0, 0);
		}
		emu_stop_reason = EMU_STOP_WATCHDOG;
	}
}

uint64_t emu_wdt_deadline(void)
{
	return wdt_on ? wdt_fed + (uint64_t)wdt_ms * EMU_MS + 1u : EMU_NEVER;
}

/* ------------------------------------------------------------------ UART / CDC */

int uart_line_ctrl_get(const struct device *dev, uint32_t ctrl, uint32_t *val)
{
	if (dev != &emu_device_cdc_acm_uart0) {
		return -ENOTSUP;
	}
	*val = (ctrl == UART_LINE_CTRL_DTR) ? (uint32_t)(emu_in.usb == 2 && emu_in.console_open)
					    : 0u;
	return 0;
}

int uart_line_ctrl_set(const struct device *dev, uint32_t ctrl, uint32_t val)
{
	ARG_UNUSED(dev); ARG_UNUSED(ctrl); ARG_UNUSED(val);
	return 0;
}

int uart_poll_in(const struct device *dev, unsigned char *c)
{
	ARG_UNUSED(dev); ARG_UNUSED(c);
	return -1;
}

void uart_poll_out(const struct device *dev, unsigned char c)
{
	ARG_UNUSED(dev);
	char ch = (char)c;
	emu_console_write(&ch, 1);
}

/* ------------------------------------------------------------------ USB */

static struct usbd_context *usb_ctx;
static bool usb_host_up;             /* what Wakes has been told */
static uint64_t usb_enum_at = EMU_NEVER;

int usbd_add_descriptor(struct usbd_context *c, struct usbd_desc_node *dn) { (void)c; (void)dn; return 0; }
int usbd_add_configuration(struct usbd_context *c, enum usbd_speed s, struct usbd_config_node *cd)
{ (void)c; (void)s; (void)cd; return 0; }
int usbd_register_all_classes(struct usbd_context *c, enum usbd_speed s, uint8_t cfg,
			      const char *const bl[]) { (void)c; (void)s; (void)cfg; (void)bl; return 0; }
int usbd_register_class(struct usbd_context *c, const char *n, enum usbd_speed s, uint8_t cfg)
{ (void)c; (void)n; (void)s; (void)cfg; return 0; }
int usbd_device_set_bcd_device(struct usbd_context *c, uint16_t b) { (void)c; (void)b; return 0; }
int usbd_device_set_code_triple(struct usbd_context *c, enum usbd_speed s, uint8_t a, uint8_t b, uint8_t p)
{ (void)c; (void)s; (void)a; (void)b; (void)p; return 0; }
int usbd_self_powered(struct usbd_context *c, bool st) { (void)c; (void)st; return 0; }

int usbd_msg_register_cb(struct usbd_context *c, usbd_msg_cb_t cb)
{
	c->msg_cb = cb;
	usb_ctx = c;
	return 0;
}

int usbd_init(struct usbd_context *c) { c->inited = true; return 0; }

int usbd_enable(struct usbd_context *c)
{
	c->enabled = true;
	usb_ctx = c;
	emu_usb_update();
	return 0;
}

int usbd_disable(struct usbd_context *c)
{
	c->enabled = false;
	emu_usb_update();
	return 0;
}

static void usb_msg(enum usbd_msg_type type, int status)
{
	if (usb_ctx && usb_ctx->msg_cb) {
		const struct usbd_msg m = { type, status, &emu_device_zephyr_udc0 };
		usb_ctx->msg_cb(usb_ctx, &m);
	}
}

void emu_usb_update(void)
{
	const bool want = usb_ctx && usb_ctx->enabled && emu_in.usb == 2;
	if (want && !usb_host_up && usb_enum_at == EMU_NEVER) {
		usb_enum_at = emu_now + 120u * EMU_MS;     /* a host takes ~100 ms to enumerate */
	}
	if (!want) {
		usb_enum_at = EMU_NEVER;
		if (usb_host_up) {
			usb_host_up = false;
			usb_msg(USBD_MSG_VBUS_REMOVED, 0);
#if defined(CONFIG_SP1_MIDI)
			sp1_midi_usb_port(false);
#endif
		}
	}
}

static void usb_events(void)
{
	if (usb_enum_at != EMU_NEVER && emu_now >= usb_enum_at) {
		usb_enum_at = EMU_NEVER;
		usb_host_up = true;
		usb_msg(USBD_MSG_VBUS_READY, 0);
		usb_msg(USBD_MSG_RESET, 0);
		usb_msg(USBD_MSG_CONFIGURATION, 1);
#if defined(CONFIG_SP1_MIDI)
		sp1_midi_usb_port(true);
#endif
	}
}

/* MIDI 1.0 bytes from the host -> USB-MIDI 1.0 event packets -> Wakes' receive path.
 * Only while a host has us configured: on the device there is no other way in. */
static uint8_t midi_run, midi_buf[3], midi_n, midi_need;
static uint8_t sysex[3], sysex_n, in_sysex;

static void midi_packet(uint8_t cin, uint8_t a, uint8_t b, uint8_t c)
{
#if defined(CONFIG_SP1_MIDI)
	const uint8_t p[4] = { cin, a, b, c };
	sp1_midi_usb_rx(p, 4);
#else
	(void)cin; (void)a; (void)b; (void)c;
#endif
}

void emu_midi_in(const uint8_t *bytes, size_t n)
{
	if (!usb_host_up) {
		return;
	}
	for (size_t i = 0; i < n; i++) {
		const uint8_t x = bytes[i];
		if (x >= 0xF8u) {                      /* real-time, anywhere */
			midi_packet(0x0F, x, 0, 0);
			continue;
		}
		if (x == 0xF0u) {
			in_sysex = 1; sysex[0] = x; sysex_n = 1; midi_run = 0;
			continue;
		}
		if (in_sysex) {
			if (x == 0xF7u) {
				/* the end: CIN 5, 6 or 7 for 1, 2 or 3 bytes in this last packet */
				sysex[sysex_n++] = x;
				midi_packet((uint8_t)(0x04 + sysex_n), sysex[0],
					    sysex_n > 1 ? sysex[1] : 0, sysex_n > 2 ? sysex[2] : 0);
				sysex_n = 0;
				in_sysex = 0;
			} else if (x < 0x80u) {
				sysex[sysex_n++] = x;
				if (sysex_n == 3) {          /* start or continue: CIN 4 */
					midi_packet(0x04, sysex[0], sysex[1], sysex[2]);
					sysex_n = 0;
				}
			} else {                             /* any other status aborts it */
				in_sysex = 0;
				sysex_n = 0;
				i--;                         /* reparse this byte normally */
			}
			continue;
		}
		if (x & 0x80u) {
			midi_run = x;
			midi_buf[0] = x;
			midi_n = 1;
			const uint8_t hi = x & 0xF0u;
			midi_need = (hi == 0xC0u || hi == 0xD0u) ? 2u : (x >= 0xF0u ? 1u : 3u);
			if (x >= 0xF0u) {
				midi_run = 0;
				if (x == 0xF2u) { midi_need = 3; }
				else if (x == 0xF1u || x == 0xF3u) { midi_need = 2; }
			}
		} else {
			if (midi_n == 0 && midi_run) {
				midi_buf[0] = midi_run;
				midi_n = 1;
			}
			if (midi_n == 0) {
				continue;
			}
			midi_buf[midi_n++] = x;
		}
		if (midi_n == midi_need) {
			const uint8_t st = midi_buf[0];
			uint8_t cin = (st < 0xF0u) ? (uint8_t)(st >> 4) : (midi_need == 3 ? 0x3 : 0x2);
			if (st >= 0xF0u && midi_need == 1) { cin = 0x5; }
			midi_packet(cin, midi_buf[0], midi_n > 1 ? midi_buf[1] : 0,
				    midi_n > 2 ? midi_buf[2] : 0);
			midi_n = 0;
		}
	}
}

/* ------------------------------------------------------------------ resets */

void emu_dsb(void)
{
	latch_port(&emu_nrf_p0);
	latch_port(&emu_nrf_p1);
	if (emu_nrf_power.SYSTEMOFF) {
		emu_stop(EMU_STOP_SYSTEM_OFF);
	}
}

FUNC_NORETURN void NVIC_SystemReset(void) { emu_stop(EMU_STOP_SOFT_RESET); }
FUNC_NORETURN void sys_reboot(int type) { (void)type; emu_stop(EMU_STOP_SOFT_RESET); }
FUNC_NORETURN void sys_poweroff(void) { emu_stop(EMU_STOP_SYSTEM_OFF); }

/* ------------------------------------------------------------------ the scheduler's view */

uint64_t emu_board_next_event(void)
{
	uint64_t next = EMU_NEVER;
	if (emu_i2s_running && i2s_next < next) { next = i2s_next; }
	if (usb_enum_at < next) { next = usb_enum_at; }
	const uint64_t w = emu_wdt_deadline();
	if (w < next) { next = w; }
	return next;
}

void emu_board_run_events(uint64_t now)
{
	while (emu_i2s_running && i2s_next <= now) {
		i2s_tick();
	}
	usb_events();
}

void emu_board_on_switch(void)
{
	latch_port(&emu_nrf_p0);
	latch_port(&emu_nrf_p1);
	update_inputs();
	wdt_check();
}

void emu_board_inputs_changed(void)
{
	update_inputs();
	emu_usb_update();
}

void emu_board_reset(uint32_t resetreas)
{
	memset(&emu_nrf_p0, 0, sizeof(emu_nrf_p0));
	memset(&emu_nrf_p1, 0, sizeof(emu_nrf_p1));
	memset(&emu_nrf_power, 0, sizeof(emu_nrf_power));
	memset(&emu_nrf_wdt, 0, sizeof(emu_nrf_wdt));
	memset(&emu_dwt, 0, sizeof(emu_dwt));
	emu_nrf_power.RESETREAS = resetreas;
	memset(emu_led, 0, sizeof(emu_led));
	i2s_configured = false;
	i2s_qn = 0;
	emu_i2s_running = 0;
	i2s_next = EMU_NEVER;
	wdt_on = false;
	wdt_ms = 0;
	usb_ctx = NULL;
	usb_host_up = false;
	usb_enum_at = EMU_NEVER;
	cs42_page = 0;
	tas_page = 0;
	memset(cs42_reg, 0, sizeof(cs42_reg));
	memset(tas_reg, 0, sizeof(tas_reg));
	line_n = 0;
	hook = default_hook;
	midi_n = 0; midi_run = 0; sysex_n = 0; in_sysex = 0;
	update_inputs();
}
