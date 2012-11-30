/*
 * linux/arch/arm/plat-omap/common.c
 *
 * Code common to all OMAP machines.
 * The file is created by Tony Lindgren <tony@atomide.com>
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/dma-mapping.h>

#include <plat/common.h>
#include <plat/board.h>
#include <plat/vram.h>
#include <plat/dsp.h>
#include <plat/dma.h>

#include <plat/omap-secure.h>

#include <asm/setup.h>

#define NO_LENGTH_CHECK 0xffffffff

unsigned char omap_bootloader_tag[1024];
int omap_bootloader_tag_len;

struct omap_board_config_kernel *omap_board_config __initdata;
int omap_board_config_size;

#ifdef CONFIG_OMAP_BOOT_TAG

static int __init parse_tag_omap(const struct tag *tag)
{
	u32 size = tag->hdr.size - (sizeof(tag->hdr) >> 2);

        size <<= 2;
	if (size > sizeof(omap_bootloader_tag))
		return -1;

	memcpy(omap_bootloader_tag, tag->u.omap.data, size);
	omap_bootloader_tag_len = size;

        return 0;
}

__tagtable(ATAG_BOARD, parse_tag_omap);

#endif

static const void *__init get_config(u16 tag, size_t len,
		int skip, size_t *len_out)
{
	struct omap_board_config_kernel *kinfo = NULL;
	int i;

#ifdef CONFIG_OMAP_BOOT_TAG
	struct omap_board_config_entry *info = NULL;

	if (omap_bootloader_tag_len > 4)
		info = (struct omap_board_config_entry *) omap_bootloader_tag;
	while (info != NULL) {
		u8 *next;

		if (info->tag == tag) {
			if (skip == 0)
				break;
			skip--;
		}

		if ((info->len & 0x03) != 0) {
			/* We bail out to avoid an alignment fault */
			printk(KERN_ERR "OMAP peripheral config: Length (%d) not word-aligned (tag %04x)\n",
			       info->len, info->tag);
			return NULL;
		}
		next = (u8 *) info + sizeof(*info) + info->len;
		if (next >= omap_bootloader_tag + omap_bootloader_tag_len)
			info = NULL;
		else
			info = (struct omap_board_config_entry *) next;
	}
	if (info != NULL) {
		/* Check the length as a lame attempt to check for
		 * binary inconsistency. */
		if (len != NO_LENGTH_CHECK) {
			/* Word-align len */
			if (len & 0x03)
				len = (len + 3) & ~0x03;
			if (info->len != len) {
				printk(KERN_ERR "OMAP peripheral config: Length mismatch with tag %x (want %d, got %d)\n",
				       tag, len, info->len);
				return NULL;
			}
		}
		if (len_out != NULL)
			*len_out = info->len;
		return info->data;
	}
#endif

	/* Try to find the config from the board-specific structures
	 * in the kernel. */
	for (i = 0; i < omap_board_config_size; i++) {
		if (omap_board_config[i].tag == tag) {
			if (skip == 0) {
				kinfo = &omap_board_config[i];
				break;
			} else {
				skip--;
			}
		}
	}
	if (kinfo == NULL)
		return NULL;
	return kinfo->data;
}

const void *__init __omap_get_config(u16 tag, size_t len, int nr)
{
        return get_config(tag, len, nr, NULL);
}

const void *__init omap_get_var_config(u16 tag, size_t *len)
{
        return get_config(tag, NO_LENGTH_CHECK, 0, len);
}

void __init omap_reserve(void)
{
	omap_vram_reserve_sdram_memblock();
	omap_dsp_reserve_sdram_memblock();
	omap_secure_ram_reserve_memblock();
	omap_barrier_reserve_memblock();
}

void __init omap_init_consistent_dma_size(void)
{
#ifdef CONFIG_FB_OMAP_CONSISTENT_DMA_SIZE
	init_consistent_dma_size(CONFIG_FB_OMAP_CONSISTENT_DMA_SIZE << 20);
#endif
}
