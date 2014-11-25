/*
 * This file contains the processor specific definitions of the TI OMAP34XX.
 *
 * Copyright (C) 2007 Texas Instruments.
 * Copyright (C) 2007 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __ASM_ARCH_OMAP3_H
#define __ASM_ARCH_OMAP3_H

/*
 * Please place only base defines here and put the rest in device
 * specific headers.
 */

#define L4_34XX_BASE		0x48000000
#define L4_WK_34XX_BASE		0x48300000
#define L4_PER_34XX_BASE	0x49000000
#define L4_EMU_34XX_BASE	0x54000000
#define L3_34XX_BASE		0x68000000

#define L4_WK_AM33XX_BASE	0x44C00000

#define OMAP3430_32KSYNCT_BASE	0x48320000
#define OMAP3430_CM_BASE	0x48004800
#define OMAP3430_PRM_BASE	0x48306800
#define OMAP343X_SMS_BASE	0x6C000000
#define OMAP343X_SDRC_BASE	0x6D000000
#define OMAP34XX_GPMC_BASE	0x6E000000
#define OMAP343X_SCM_BASE	0x48002000
#define OMAP343X_CTRL_BASE	OMAP343X_SCM_BASE

#define OMAP34XX_IC_BASE	0x48200000

#define OMAP3430_ISP_BASE	(L4_34XX_BASE + 0xBC000)
#define OMAP3430_ISP_MMU_BASE	(OMAP3430_ISP_BASE + 0x1400)
#define OMAP3430_ISP_BASE2	(OMAP3430_ISP_BASE + 0x1800)

#define OMAP34XX_HSUSB_OTG_BASE	(L4_34XX_BASE + 0xAB000)
#define OMAP34XX_USBTLL_BASE	(L4_34XX_BASE + 0x62000)
#define OMAP34XX_UHH_CONFIG_BASE	(L4_34XX_BASE + 0x64000)
#define OMAP34XX_OHCI_BASE	(L4_34XX_BASE + 0x64400)
#define OMAP34XX_EHCI_BASE	(L4_34XX_BASE + 0x64800)
#define OMAP34XX_SR1_BASE	0x480C9000
#define OMAP34XX_SR2_BASE	0x480CB000

#define OMAP34XX_MAILBOX_BASE		(L4_34XX_BASE + 0x94000)

/* Security */
#define OMAP34XX_SEC_BASE	(L4_34XX_BASE + 0xA0000)
#define OMAP34XX_SEC_SHA1MD5_BASE	(OMAP34XX_SEC_BASE + 0x23000)
#define OMAP34XX_SEC_AES_BASE	(OMAP34XX_SEC_BASE + 0x25000)

/* VDD1 OPPS */
#define VDD1_OPP1	0x1
#define VDD1_OPP2	0x2
#define VDD1_OPP3	0x3
#define VDD1_OPP4	0x4
#define VDD1_OPP5	0x5
#define VDD1_OPP6	0x6
#define VDD1_OPP7	0x7
#define VDD1_OPP8	0x8
#define VDD1_OPP9	0x9
#define VDD1_OPP10	0xA
#define VDD1_OPP11	0xB
#define VDD1_OPP12	0xC
#define VDD1_OPP13	0xD

/* VDD2 OPPS */
#define VDD2_OPP1	0x1
#define VDD2_OPP2	0x2
#define VDD2_OPP3	0x3

#define MIN_VDD1_OPP	VDD1_OPP1
#define MAX_VDD1_OPP	VDD1_OPP13
#define MIN_VDD2_OPP	VDD2_OPP1
#define MAX_VDD2_OPP	VDD2_OPP3

/* MPU speeds */
#define S1150M  1150000000
#define S1100M  1100000000
#define S1000M  1000000000
#define S950M   950000000
#define S900M   900000000
#define S850M   850000000
#define S805M   805000000
#define S720M   720000000
#define S600M   600000000
#define S550M   550000000
#define S500M   500000000
#define S250M   250000000
#define S125M   125000000

/* DSP speeds */
#define S520M   520000000
#define S430M   430000000
#define S400M   400000000
#define S360M   360000000
#define S180M   180000000
#define S90M    90000000

/* L3 speeds */
#define S83M    83000000
#define S166M   166000000

#endif /* __ASM_ARCH_OMAP3_H */

