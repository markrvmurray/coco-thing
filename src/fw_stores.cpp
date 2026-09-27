/*
 * fw_stores.cpp
 *
 * This platform's storage for the firmware's PSRAM-backed stores: the
 * graphics shadow, the get/put buffer pool, the command trace, the flood
 * fill's span log and seed stack, and the parked windows. On the board they
 * live in PSRAM because SRAM has no room; a host has no such constraint, so
 * each is an array.
 *
 * The portable units state what storage they need through a *_base() seam,
 * and each platform says where it comes from. Every store follows the one
 * probe verdict, win_store_slots() != 0, as on the board; here it is always
 * present.
 */

#include <stdio.h>

#include "cmd_trace.h"
#include "fill_log.h"
#include "gfx_bufs.h"
#include "gfx_shadow.h"
#include "win_store.h"

#define WIN_STORE_HOST_SLOTS  16u

static uint8_t slots[WIN_STORE_HOST_SLOTS][WIN_STORE_SLOT_BYTES];

/* Word-aligned explicitly: the renderer reads the shadow as 32-bit words. */
alignas(4) static uint8_t shadow[GFX_SHADOW_BYTES];
static uint8_t pool[GFX_BUFPOOL_BYTES];
alignas(4) static uint8_t trace_ring[CMD_TRACE_BYTES];
static uint32_t spans[FILL_LOG_SPANS];
static uint32_t seeds[FILL_SEEDS];

bool
win_store_init(void)
{
	return true;
}

unsigned
win_store_slots(void)
{
	return WIN_STORE_HOST_SLOTS;
}

uint8_t *
win_store_slot(unsigned i)
{
	return i < win_store_slots() ? slots[i] : NULL;
}

void
win_store_print_status(void)
{
	printf("Windows:     %u parked screens of %u B (host)\n",
	    win_store_slots(), WIN_STORE_SLOT_BYTES);
}

uint8_t *
gfx_shadow_base(void)
{
	return shadow;
}

uint8_t *
gfx_bufpool_base(void)
{
	return pool;
}

uint8_t *
cmd_trace_base(void)
{
	return trace_ring;
}

uint32_t *
fill_log_base(void)
{
	return spans;
}

uint32_t *
fill_seeds_base(void)
{
	return seeds;
}
