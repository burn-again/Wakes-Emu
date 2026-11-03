/*
 * emu_emmc.c -- stands in for firmware/src/sp1_emmc.c, the bit-banged 1-bit MMC driver.
 *
 * Files live in emu_fs.c, above the block layer, so the only blocks anything writes
 * here are the few a format touches (the MBR and the boot sectors). They are kept in a
 * small table; every other block reads back as zeros.
 */
#include "emu.h"
#include "sp1_emmc.h"

#define SECTORS   7634944u     /* a 4 GB eMMC's user area */
#define MAX_KEEP  64

static struct { uint32_t block; uint8_t data[SP1_EMMC_BLOCK]; int used; } kept[MAX_KEEP];
static bool ready_flag;
static struct sp1_emmc_stats stats;

void emu_emmc_reset(void)
{
	ready_flag = false;
	memset(&stats, 0, sizeof(stats));
}

bool sp1_emmc_init(struct sp1_emmc_ident *id)
{
	if (id) {
		memset(id, 0, sizeof(*id));
		id->ocr = 0xC0FF8080u;            /* ready, sector addressing */
		id->ocr_tries = 1;
		id->ready_ms = 1;
		static const uint8_t cid[16] = { 0x11, 0x01, 0x00, 'E', 'M', 'U', 'L', 'A',
						 'T', 0x10, 0x00, 0x00, 0x00, 0x01, 0x14, 0x01 };
		memcpy(id->cid, cid, sizeof(cid));
		id->cid_shift = 0;
	}
	ready_flag = true;
	return true;
}

bool     sp1_emmc_ready(void)   { return ready_flag; }
uint32_t sp1_emmc_sectors(void) { return SECTORS; }

bool sp1_emmc_status(uint32_t *status)
{
	if (status) {
		*status = 0x00000900u;            /* READY_FOR_DATA, state tran */
	}
	return ready_flag;
}

bool sp1_emmc_read_ext_csd(uint8_t buf[SP1_EMMC_BLOCK])
{
	if (!ready_flag) {
		return false;
	}
	memset(buf, 0, SP1_EMMC_BLOCK);
	buf[212] = (uint8_t)SECTORS;          /* SEC_COUNT, little-endian */
	buf[213] = (uint8_t)(SECTORS >> 8);
	buf[214] = (uint8_t)(SECTORS >> 16);
	buf[215] = (uint8_t)(SECTORS >> 24);
	buf[192] = 8;                         /* EXT_CSD_REV: eMMC 5.1 */
	return true;
}

bool sp1_emmc_read_block(uint32_t block, uint8_t buf[SP1_EMMC_BLOCK])
{
	if (!ready_flag || block >= SECTORS) {
		return false;
	}
	for (int i = 0; i < MAX_KEEP; i++) {
		if (kept[i].used && kept[i].block == block) {
			memcpy(buf, kept[i].data, SP1_EMMC_BLOCK);
			return true;
		}
	}
	memset(buf, 0, SP1_EMMC_BLOCK);
	return true;
}

bool sp1_emmc_write_block(uint32_t block, const uint8_t buf[SP1_EMMC_BLOCK])
{
	if (!ready_flag || block >= SECTORS) {
		return false;
	}
	int slot = -1;
	for (int i = 0; i < MAX_KEEP; i++) {
		if (kept[i].used && kept[i].block == block) { slot = i; break; }
		if (!kept[i].used && slot < 0) { slot = i; }
	}
	if (slot < 0) {
		return true;                      /* table full: accept and forget */
	}
	kept[slot].used = 1;
	kept[slot].block = block;
	memcpy(kept[slot].data, buf, SP1_EMMC_BLOCK);
	return true;
}

bool sp1_emmc_read_blocks(uint32_t block, uint8_t *buf, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		if (!sp1_emmc_read_block(block + i, buf + i * SP1_EMMC_BLOCK)) {
			return false;
		}
	}
	return true;
}

bool sp1_emmc_write_blocks(uint32_t block, const uint8_t *buf, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		if (!sp1_emmc_write_block(block + i, buf + i * SP1_EMMC_BLOCK)) {
			return false;
		}
	}
	return true;
}

void sp1_emmc_get_stats(struct sp1_emmc_stats *s) { *s = stats; }
void sp1_emmc_power_down(void) { ready_flag = false; }
void sp1_emmc_abort(void) { }
