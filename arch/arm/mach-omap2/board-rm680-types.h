/*
 *  arch/arm/mach-omap2/board-rm680-types.h
 *
 *  system_rev helpers for Nokia RM-680 family.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __BOARD_RM680_TYPES_H
#define __BOARD_RM680_TYPES_H

static inline int board_is_rm680(void)
{
	return (system_rev & 0x00f0) == 0x0020;
}

static inline int board_is_rm690(void)
{
	return (system_rev & 0x00f0) == 0x0030;
}



#endif /* __BOARD_RM680_TYPES_H */

