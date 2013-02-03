/*
 * RX51 secure APIs file.
 *
 * Copyright (C) 2012 Ivaylo Dimitrov <freemangordon@abv.bg>
 *
 *
 * This program is free software,you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/linkage.h>

/**
 * u32 rx51_ppa_smc(u32 id, u32 flag, u32 pargs)
 * Low level common routine for secure HAL and PPA APIs.
 * @id: Secure Service ID
 * @flag: Flag to indicate the criticality of operation
 * @pargs: Physical address of parameter list starting
 *          with number of parametrs
 */
ENTRY(rx51_ppa_smc)
	.arch_extension sec
	stmfd	sp!, {r4-r12, lr}
	mov	r12, r0		@ Copy the secure service ID
	mov	r3, r2		@ Copy the pointer to va_list in R3
	mov	r2, r1		@ Copy the flags in R2
	mov	r1, #0x0	@ Process ID - 0
	mov	r6, #0xff	@ Indicate new Task call
	dsb
	dmb
	smc	#1		@ call PPA service
	ldmfd	sp!, {r4-r12, pc}
ENDPROC(rx51_ppa_smc)
