/*
 * Copyright (C) 2009 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

ENTRY(omap3_rng_call_rom_asm)
	stmfd	sp!, {r4-r12, lr}
	stmfd	sp!, {r0-r3}
	bl	v7_flush_dcache_all
	ldmfd	sp!, {r0-r3}
	mov	r6, #0xff
	mov	r12, r0
	smc	#1
	mov	r12, r0
	bl	v7_flush_dcache_all
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0
	mov	r0, r12
	ldmfd	sp!, {r4-r12, pc}
