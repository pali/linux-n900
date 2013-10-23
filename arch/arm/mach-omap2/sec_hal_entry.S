/*
 * linux/arch/arm/mach-omap2/sec_hal_entry.S
 *
 * Copyright (C) 2008 Nokia Corporation
 * Author: Urpo Pietikäinen <urpo.pietikainen@offcode.fi>
 *         Dmitry Kasatkin <dmitry.kasatkin@nokia.com>
 *
 * OMAP3 HS HAL secure mode entry
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/linkage.h>
#include <asm/assembler.h>

ENTRY(asm_sec_hal_entry)
	stmfd	sp!, {r4-r12, lr}
	stmfd	sp!, {r0-r3}
	bl	sec_hal_flush_helper
	ldmfd	sp!, {r0-r3}
	mov	r6, #0xff
	mov	r12, r0
	smc	#1
	mov	r11, r0
	bl	sec_hal_flush_helper
	mov	r0, #0
	mcr	p15, 0, r0, c7, c5, 0 /* flush Icache */
	mov	r0, r11
	ldmfd	sp!, {r4-r12, pc}
ENDPROC(asm_sec_hal_entry)

ENTRY(asm_sec_hal_rpc_handler)
	stmfd sp!, {r0-r3}
	bl sec_hal_rpc_handler
	ldmfd sp!, {r0-r3}
	mov r12, #0xfc
	smc #1
ENDPROC(asm_sec_hal_rpc_handler)
