/*
 * This file is part of wl12xx
 *
 * Copyright (C) 2008-2009 Nokia Corporation
 *
 * Contact: Kalle Valo <kalle.valo@nokia.com>
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
 *
 */

#ifndef __WL1271_H__
#define __WL1271_H__

#include <linux/bitops.h>

#include "wl12xx.h"
#include "acx.h"

#define WL1271_FW_NAME "wl1271-fw.bin"
#define WL1271_NVS_NAME "wl1271-nvs.bin"

#define WL1271_POWER_ON_SLEEP 200 /* in miliseconds */

#define REGISTERS_BASE 0x00300000
#define DRPW_BASE      0x00310000

void wl1271_setup(struct wl12xx *wl);

struct wl1271_acx_config_memory {
	struct acx_header header;

	u8 rx_mem_block_num;
	u8 tx_min_mem_block_num;
	u8 num_stations;
	u8 num_ssid_profiles;
	u32 total_tx_descriptors;
};

struct wl1271_tx_result_pointers {
	u32 *control_block; /* Points to an array with two u32 entries:
			       [0] result entries written by the FW
			       [1] result entries read by the host */

	void *tx_results_queue_start; /* points t first descriptor in TRQ */
} __attribute__ ((packed));

struct wl1271_acx_mem_map {
	struct acx_header header;

	void *code_start;
	void *code_end;

	void *wep_defkey_start;
	void *wep_defkey_end;

	void *sta_table_start;
	void *sta_table_end;

	void *packet_template_start;
	void *packet_template_end;

	struct wl1271_tx_result_pointers tx_result;

	void *queue_memory_start;
	void *queue_memory_end;

	void *packet_memory_pool_start;
	void *packet_memory_pool_end;

	void *debug_buffer1_start;
	void *debug_buffer1_end;

	void *debug_buffer2_start;
	void *debug_buffer2_end;

	/* Number of blocks FW allocated for TX packets */
	u32 num_tx_mem_blocks;

	/* Number of blocks FW allocated for RX packets */
	u32 num_rx_mem_blocks;

	/* the following 4 fields are valid in SLAVE mode only */
	u8 *tx_cbuf;
	u8 *rx_cbuf;
	void *rx_ctrl;
	void *tx_ctrl;
} __attribute__ ((packed));

struct wl1271_tx_config_opt {
	struct acx_header header;

	u16 threshold;
	u16 timeout;
};

enum wl1271_rx_queue_type {
	RX_QUEUE_TYPE_RX_LOW_PRIORITY,    /* All except the high priority */
	RX_QUEUE_TYPE_RX_HIGH_PRIORITY,   /* Management and voice packets */
	RX_QUEUE_TYPE_NUM,
	RX_QUEUE_TYPE_MAX = USHORT_MAX
};

struct wl1271_rx_config_opt {
	struct acx_header header;

	u16 mblk_threshold;
	u16 threshold;
	u16 timeout;
	enum wl1271_rx_queue_type queue_type;
	u8 reserved;
};

/* WL1271-specific registers and other defines */
#define CMD_MBOX_ADDRESS     0x407B4
#define REF_CLOCK            2
#define PLL_PARAMETERS       (REGISTERS_BASE + 0x6040)
#define WU_COUNTER_PAUSE     (REGISTERS_BASE + 0x6008)
#define WELP_ARM_COMMAND     (REGISTERS_BASE + 0x6100)
#define WU_COUNTER_PAUSE_VAL 0x3FF
#define WELP_ARM_COMMAND_VAL 0x4
#define DRPW_SCRATCH_START   (DRPW_BASE + 0x002C)

#define ACX_TX_CONFIG_OPT    0x24
#define ACX_RX_CONFIG_OPT    0x4E

#define ACX_RX_MEM_BLOCKS     64
#define ACX_TX_MIN_MEM_BLOCKS 64
#define ACX_TX_DESCRIPTORS    32
#define ACX_NUM_SSID_PROFILES 1

/*
 * Tx and Rx interrupts pacing (threshold in packets, timeouts in milliseconds)
 */
#define WL1271_TX_CMPLT_THRESHOLD_DEF 0       /* no pacing, send interrupt on
					       * every event */
#define WL1271_TX_CMPLT_THRESHOLD_MIN 0
#define WL1271_TX_CMPLT_THRESHOLD_MAX 15

#define WL1271_TX_CMPLT_TIMEOUT_DEF   5
#define WL1271_TX_CMPLT_TIMEOUT_MIN   1
#define WL1271_TX_CMPLT_TIMEOUT_MAX   100

#define WL1271_RX_INTR_THRESHOLD_DEF  0       /* no pacing, send interrupt on
					       * every event */
#define WL1271_RX_INTR_THRESHOLD_MIN  0
#define WL1271_RX_INTR_THRESHOLD_MAX  15

#define WL1271_RX_INTR_TIMEOUT_DEF    5
#define WL1271_RX_INTR_TIMEOUT_MIN    1
#define WL1271_RX_INTR_TIMEOUT_MAX    100

/*************************************************************************

    Host Interrupt Register (WiLink -> Host)

**************************************************************************/
/* HW Initiated interrupt Watchdog timer expiration */
#define WL1271_ACX_INTR_WATCHDOG           BIT(0)
/* Init sequence is done (masked interrupt, detection through polling only ) */
#define WL1271_ACX_INTR_INIT_COMPLETE      BIT(1)
/* Event was entered to Event MBOX #A*/
#define WL1271_ACX_INTR_EVENT_A            BIT(2)
/* Event was entered to Event MBOX #B*/
#define WL1271_ACX_INTR_EVENT_B            BIT(3)
/* Command processing completion*/
#define WL1271_ACX_INTR_CMD_COMPLETE       BIT(4)
/* Signaling the host on HW wakeup */
#define WL1271_ACX_INTR_HW_AVAILABLE       BIT(5)
/* The MISC bit is used for aggregation of RX, TxComplete and TX rate update */
#define WL1271_ACX_INTR_DATA               BIT(6)
/* Trace meassge on MBOX #A */
#define WL1271_ACX_INTR_TRACE_A            BIT(7)
/* Trace meassge on MBOX #B */
#define WL1271_ACX_INTR_TRACE_B            BIT(8)

#define WL1271_ACX_INTR_ALL                0xFFFFFFFF


#endif
