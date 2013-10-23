/*
 * mach/ssi.h
 *
 * Hardware definitions for SSI.
 *
 * Copyright (C) 2007-2008 Nokia Corporation. All rights reserved.
 *
 * Contact: Carlos Chinea <carlos.chinea@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __SSI_REGS_H__
#define __SSI_REGS_H__

#define SSI_PORT_OFFSET			0x1000

/*
 * GDD base addr : 0x48059000
 */
#define SSI_GDD_OFFSET			0x1000
#define SSI_GDD_BASE			SSI_GDD_OFFSET	/* 0x9000 */

/*
 * SST base addr:
 *	port 1: 0x4805a000
 *	port 2: 0x4805b000
 */
#define SSI_SST_OFFSET			0x2000
#define SSI_SST_BASE(port)		(SSI_SST_OFFSET + (((port) - 1) *\
							(SSI_PORT_OFFSET)))
/*
 * SSR base addr:
 *	port 1: 0x4805a800
 *	port 2: 0x4805b800
 */
#define SSI_SSR_OFFSET			0x2800
#define SSI_SSR_BASE(port)		(SSI_SSR_OFFSET + (((port) - 1) *\
							(SSI_PORT_OFFSET)))
/*
 * SSI SYS registers
 */
#define SSI_SYS_REVISION_REG		0x0000
#	define SSI_REV_MASK		0x000000ff
#	define SSI_REV_MAJOR		0xf0
#	define SSI_REV_MINOR		0x0f

#define SSI_SYS_SYSCONFIG_REG		0x0010
#	define SSI_AUTOIDLE		(1 << 0)
#	define SSI_SOFTRESET		(1 << 1)
#	define SSI_SIDLEMODE_FORCE	0
#	define SSI_SIDLEMODE_NO		(1 << 3)
#	define SSI_SIDLEMODE_SMART	(1 << 4)
#	define SSI_SIDLEMODE_MASK	0x00000018
#	define SSI_MIDLEMODE_FORCE	0
#	define SSI_MIDLEMODE_NO		(1 << 12)
#	define SSI_MIDLEMODE_SMART	(1 << 13)
#	define SSI_MIDLEMODE_MASK	0x00003000

#define SSI_SYS_SYSSTATUS_REG		0x0014
#	define SSI_RESETDONE		1

#define SSI_SYS_MPU_STATUS_BASE		0x0808
#define SSI_SYS_MPU_STATUS_PORT_OFFSET	0x10
#define SSI_SYS_MPU_STATUS_IRQ_OFFSET	2

#define SSI_SYS_MPU_STATUS_REG(port, irq) \
			(SSI_SYS_MPU_STATUS_BASE +\
			((((port) - 1) * SSI_SYS_MPU_STATUS_PORT_OFFSET) +\
			((irq) * SSI_SYS_MPU_STATUS_IRQ_OFFSET)))

#define SSI_SYS_MPU_ENABLE_BASE		0X080c
#define SSI_SYS_MPU_ENABLE_PORT_OFFSET	0x10
#define SSI_SYS_MPU_ENABLE_IRQ_OFFSET	8

#define SSI_SYS_MPU_ENABLE_REG(port, irq) \
			(SSI_SYS_MPU_ENABLE_BASE +\
			((((port) - 1) * SSI_SYS_MPU_ENABLE_PORT_OFFSET) +\
			((irq) * SSI_SYS_MPU_ENABLE_IRQ_OFFSET)))

#	define SSI_SST_DATAACCEPT(channel)	(1 << (channel))
#	define SSI_SSR_DATAAVAILABLE(channel)	(1 << ((channel) + 8))
#	define SSI_SSR_DATAOVERRUN(channel)	(1 << ((channel) + 16))
#	define SSI_ERROROCCURED			(1 << 24)
#	define SSI_BREAKDETECTED		(1 << 25)

#define SSI_SYS_GDD_MPU_IRQ_STATUS_REG	0x0800
#define SSI_SYS_GDD_MPU_IRQ_ENABLE_REG	0x0804
#	define SSI_GDD_LCH(channel)	(1 << (channel))

#define SSI_SYS_WAKE_OFFSET		0x10
#define SSI_SYS_WAKE_BASE		0x0c00
#define SSI_SYS_WAKE_REG(port)		(SSI_SYS_WAKE_BASE +\
					(((port) - 1) * SSI_SYS_WAKE_OFFSET))
#define SSI_SYS_CLEAR_WAKE_BASE		0x0c04
#define SSI_SYS_CLEAR_WAKE_REG(port)	(SSI_SYS_CLEAR_WAKE_BASE +\
					(((port) - 1) * SSI_SYS_WAKE_OFFSET))
#define SSI_SYS_SET_WAKE_BASE		0x0c08
#define SSI_SYS_SET_WAKE_REG(port)	(SSI_SYS_SET_WAKE_BASE +\
					(((port) - 1) * SSI_SYS_WAKE_OFFSET))
#	define SSI_WAKE(channel)	(1 << (channel))
#	define SSI_WAKE_MASK		0xff

/*
 * SSI SST registers
 */
#define SSI_SST_ID_REG(port)			(SSI_SST_BASE(port) + 0x0000)
#define SSI_SST_MODE_REG(port)			(SSI_SST_BASE(port) + 0x0004)
#	define SSI_MODE_VAL_MASK		3
#	define SSI_MODE_SLEEP			0
#	define SSI_MODE_STREAM			1
#	define SSI_MODE_FRAME			2
#	define SSI_MODE_MULTIPOINTS		3
#define SSI_SST_FRAMESIZE_REG(port)		(SSI_SST_BASE(port) + 0x0008)
#	define SSI_FRAMESIZE_DEFAULT		31
#define SSI_SST_TXSTATE_REG(port)		(SSI_SST_BASE(port) + 0X000c)
#	define	TXSTATE_IDLE			0
#define SSI_SST_BUFSTATE_REG(port)		(SSI_SST_BASE(port) + 0x0010)
#	define	NOTFULL(channel)		(1 << (channel))
#define SSI_SST_DIVISOR_REG(port)		(SSI_SST_BASE(port) + 0x0018)
#	define SSI_DIVISOR_DEFAULT		1

#define SSI_SST_BREAK_REG(port)			(SSI_SST_BASE(port) + 0x0020)
#define SSI_SST_CHANNELS_REG(port)		(SSI_SST_BASE(port) + 0x0024)
#	define SSI_CHANNELS_DEFAULT		4

#define SSI_SST_ARBMODE_REG(port)		(SSI_SST_BASE(port) + 0x0028)
#	define SSI_ARBMODE_ROUNDROBIN		0
#	define SSI_ARBMODE_PRIORITY		1

#define SSI_SST_BUFFER_BASE(port)		(SSI_SST_BASE(port) + 0x0080)
#define SSI_SST_BUFFER_CH_REG(port, channel)	(SSI_SST_BUFFER_BASE(port) +\
						((channel) * 4))

#define SSI_SST_SWAPBUF_BASE(port)		(SSI_SST_BASE(port) + 0X00c0)
#define SSI_SST_SWAPBUF_CH_REG(port, channel)	(SSI_SST_SWAPBUF_BASE(port) +\
						((channel) * 4))
/*
 * SSI SSR registers
 */
#define SSI_SSR_ID_REG(port)			(SSI_SSR_BASE(port) + 0x0000)
#define SSI_SSR_MODE_REG(port)			(SSI_SSR_BASE(port) + 0x0004)
#define SSI_SSR_FRAMESIZE_REG(port)		(SSI_SSR_BASE(port) + 0x0008)
#define SSI_SSR_RXSTATE_REG(port)		(SSI_SSR_BASE(port) + 0x000c)
#define SSI_SSR_BUFSTATE_REG(port)		(SSI_SSR_BASE(port) + 0x0010)
#	define NOTEMPTY(channel)		(1 << (channel))
#define SSI_SSR_BREAK_REG(port)			(SSI_SSR_BASE(port) + 0x001c)
#define SSI_SSR_ERROR_REG(port)			(SSI_SSR_BASE(port) + 0x0020)
#define SSI_SSR_ERRORACK_REG(port)		(SSI_SSR_BASE(port) + 0x0024)
#define SSI_SSR_OVERRUN_REG(port)		(SSI_SSR_BASE(port) + 0x002c)
#define SSI_SSR_OVERRUNACK_REG(port)		(SSI_SSR_BASE(port) + 0x0030)
#define SSI_SSR_TIMEOUT_REG(port)		(SSI_SSR_BASE(port) + 0x0034)
#	define SSI_TIMEOUT_DEFAULT		0
#define SSI_SSR_CHANNELS_REG(port)		(SSI_SSR_BASE(port) + 0x0028)

#define SSI_SSR_BUFFER_BASE(port)		(SSI_SSR_BASE(port) + 0x0080)
#define SSI_SSR_BUFFER_CH_REG(port, channel)	(SSI_SSR_BUFFER_BASE(port) +\
						((channel) * 4))

#define SSI_SSR_SWAPBUF_BASE(port)		(SSI_SSR_BASE(port) + 0x00c0)
#define SSI_SSR_SWAPBUF_CH_REG(port, channel)	(SSI_SSR_SWAPBUF_BASE +\
						((channel) * 4))
/*
 * SSI GDD registers
 */
#define SSI_GDD_HW_ID_REG		(SSI_GDD_BASE + 0x0000)
#define SSI_GDD_PPORT_ID_REG		(SSI_GDD_BASE + 0x0010)
#define SSI_GDD_MPORT_ID_REG		(SSI_GDD_BASE + 0x0014)

#define SSI_GDD_PPORT_SR_REG		(SSI_GDD_BASE + 0x0020)
#	define SSI_PPORT_ACTIVE_LCH_NUMBER_MASK	0Xff

#define SSI_GDD_MPORT_SR_REG		(SSI_GDD_BASE + 0x0024)
#	define SSI_MPORT_ACTIVE_LCH_NUMBER_MASK	0xff

#define SSI_GDD_TEST_REG		(SSI_GDD_BASE + 0x0040)
#	define SSI_TEST			1

#define SSI_GDD_GCR_REG			(SSI_GDD_BASE + 0x0100)
#	define	SSI_CLK_AUTOGATING_ON	(1 << 3)
#	define	SSI_FREE		(1 << 2)
#	define	SSI_SWITCH_OFF		(1 << 0)

#define SSI_GDD_GRST_REG		(SSI_GDD_BASE + 0x0200)
#	define SSI_SWRESET		1

#define SSI_GDD_CSDP_BASE		(SSI_GDD_BASE + 0x0800)
#define SSI_GDD_CSDP_OFFSET		0x40
#define SSI_GDD_CSDP_REG(channel)	(SSI_GDD_CSDP_BASE +\
					((channel) * SSI_GDD_CSDP_OFFSET))
#	define SSI_DST_BURST_EN_MASK	0xC000
#	define SSI_DST_SINGLE_ACCESS0	0
#	define SSI_DST_SINGLE_ACCESS	(1 << 14)
#	define SSI_DST_BURST_4X32_BIT	(2 << 14)
#	define SSI_DST_BURST_8x32_BIT	(3 << 14)

#	define SSI_DST_MASK		0x1e00
#	define SSI_DST_MEMORY_PORT	(8 << 9)
#	define SSI_DST_PERIPHERAL_PORT	(9 << 9)

#	define SSI_SRC_BURST_EN_MASK	0x0180
#	define SSI_SRC_SINGLE_ACCESS0	0
#	define SSI_SRC_SINGLE_ACCESS	(1 << 7)
#	define SSI_SRC_BURST_4x32_BIT	(2 << 7)
#	define SSI_SRC_BURST_8x32_BIT	(3 << 7)

#	define SSI_SRC_MASK		0x003c
#	define SSI_SRC_MEMORY_PORT	(8 << 2)
#	define SSI_SRC_PERIPHERAL_PORT	(9 << 2)

#	define SSI_DATA_TYPE_MASK	3
#	define SSI_DATA_TYPE_S32	2

#define SSI_GDD_CCR_BASE		(SSI_GDD_BASE + 0x0802)
#define SSI_GDD_CCR_OFFSET		0x40
#define SSI_GDD_CCR_REG(channel)	(SSI_GDD_CCR_BASE +\
					((channel) * SSI_GDD_CCR_OFFSET))
#	define SSI_DST_AMODE_MASK	(3 << 14)
#	define SSI_DST_AMODE_CONST	0
#	define SSI_DST_AMODE_POSTINC	(1 << 12)

#	define SSI_SRC_AMODE_MASK	(3 << 12)
#	define SSI_SRC_AMODE_CONST	0
#	define SSI_SRC_AMODE_POSTINC	(1 << 12)

#	define SSI_CCR_ENABLE		(1 << 7)

#	define SSI_CCR_SYNC_MASK	0X001f

#define SSI_GDD_CICR_BASE		(SSI_GDD_BASE + 0x0804)
#define SSI_GDD_CICR_OFFSET		0x40
#define SSI_GDD_CICR_REG(channel)	(SSI_GDD_CICR_BASE +\
					((channel) * SSI_GDD_CICR_OFFSET))
#	define SSI_BLOCK_IE		(1 << 5)
#	define SSI_HALF_IE		(1 << 2)
#	define SSI_TOUT_IE		(1 << 0)

#define SSI_GDD_CSR_BASE		(SSI_GDD_BASE + 0x0806)
#define SSI_GDD_CSR_OFFSET		0x40
#define SSI_GDD_CSR_REG(channel)	(SSI_GDD_CSR_BASE +\
					((channel) * SSI_GDD_CSR_OFFSET))
#	define SSI_CSR_SYNC		(1 << 6)
#	define SSI_CSR_BLOCK		(1 << 5)
#	define SSI_CSR_HALF		(1 << 2)
#	define SSI_CSR_TOUR		(1 << 0)

#define SSI_GDD_CSSA_BASE		(SSI_GDD_BASE + 0x0808)
#define SSI_GDD_CSSA_OFFSET		0x40
#define SSI_GDD_CSSA_REG(channel)	(SSI_GDD_CSSA_BASE +\
					((channel) * SSI_GDD_CSSA_OFFSET))

#define SSI_GDD_CDSA_BASE		(SSI_GDD_BASE + 0x080c)
#define SSI_GDD_CDSA_OFFSET		0x40
#define SSI_GDD_CDSA_REG(channel)	(SSI_GDD_CDSA_BASE +\
					((channel) * SSI_GDD_CDSA_OFFSET))

#define SSI_GDD_CEN_BASE		(SSI_GDD_BASE + 0x0810)
#define SSI_GDD_CEN_OFFSET		0x40
#define SSI_GDD_CEN_REG(channel)	(SSI_GDD_CEN_BASE +\
					((channel) * SSI_GDD_CEN_OFFSET))

#define SSI_GDD_CSAC_BASE		(SSI_GDD_BASE + 0x0818)
#define SSI_GDD_CSAC_OFFSET		0x40
#define SSI_GDD_CSAC_REG(channel)	(SSI_GDD_CSAC_BASE +\
					((channel) * SSI_GDD_CSAC_OFFSET))

#define SSI_GDD_CDAC_BASE		(SSI_GDD_BASE + 0x081a)
#define SSI_GDD_CDAC_OFFSET		0x40
#define SSI_GDD_CDAC_REG(channel)	(SSI_GDD_CDAC_BASE +\
					((channel) * SSI_GDD_CDAC_OFFSET))

#define SSI_GDD_CLNK_CTRL_BASE		(SSI_GDD_BASE + 0x0828)
#define SSI_GDD_CLNK_CTRL_OFFSET	0x40
#define SSI_GDD_CLNK_CTRL_REG(channel)	(SSI_GDD_CLNK_CTRL_BASE +\
					((channel) * SSI_GDD_CLNK_CTRL_OFFSET))
#	define SSI_ENABLE_LNK		(1 << 15)
#	define SSI_STOP_LNK		(1 << 14)
#	define NEXT_CH_ID_MASK		0xf

/**
 *	struct omap_ssi_config - SSI board configuration
 *	@num_ports: Number of ports in use
 *	@cawake_line: Array of cawake gpio lines
 */
struct omap_ssi_board_config {
	unsigned int num_ports;
	int cawake_gpio[2];
};

extern int omap_ssi_config(struct omap_ssi_board_config *ssi_config);
#endif
