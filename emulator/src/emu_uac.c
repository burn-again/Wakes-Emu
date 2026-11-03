/*
 * emu_uac.c -- stands in for firmware/src/sp1_uac.c, USB Audio Class 1.0 out.
 *
 * The emulator's audio already comes from the I2S stream (what the speaker and the
 * headphones get, which is also what Wakes sends to USB), so no host ever opens this
 * stream here. Every call is the "no host has it open" path.
 */
#include "emu.h"
#include "sp1_uac.h"

void sp1_uac_claim(uint32_t frames) { (void)frames; }
void sp1_uac_push(const int16_t *lr, uint32_t frames) { (void)lr; (void)frames; }
void sp1_uac_bus_event(int type) { (void)type; }
bool sp1_uac_open(void) { return false; }
bool sp1_uac_live(void) { return false; }

void sp1_uac_get_stats(struct sp1_uac_stats *st)
{
	memset(st, 0, sizeof(*st));
}
