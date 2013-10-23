/*
 * This file is part of wl12xx
 *
 * Copyright (c) 1998-2007 Texas Instruments Incorporated
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

#ifndef __WL12XX_H__
#define __WL12XX_H__

#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/bitops.h>
#include <net/mac80211.h>
#include <mach/board.h>
#include <mach/board-nokia.h>

#define DRIVER_NAME "wl12xx"
#define DRIVER_PREFIX DRIVER_NAME ": "

enum {
	DEBUG_NONE	= 0,
	DEBUG_IRQ	= BIT(0),
	DEBUG_SPI	= BIT(1),
	DEBUG_BOOT	= BIT(2),
	DEBUG_MAILBOX	= BIT(3),
	DEBUG_NETLINK	= BIT(4),
	DEBUG_EVENT	= BIT(5),
	DEBUG_TX	= BIT(6),
	DEBUG_RX	= BIT(7),
	DEBUG_SCAN	= BIT(8),
	DEBUG_CRYPT	= BIT(9),
	DEBUG_PSM	= BIT(10),
	DEBUG_MAC80211	= BIT(11),
	DEBUG_CMD	= BIT(12),
	DEBUG_ACX	= BIT(13),
	DEBUG_ALL	= ~0,
};

#define DEBUG_LEVEL (DEBUG_NONE)

#define DEBUG_DUMP_LIMIT 1024

#define wl12xx_error(fmt, arg...) \
	WARN(1, DRIVER_PREFIX "ERROR " fmt "\n", ##arg)

#define wl12xx_warning(fmt, arg...) \
	printk(KERN_WARNING DRIVER_PREFIX "WARNING " fmt "\n", ##arg)

#define wl12xx_notice(fmt, arg...) \
	printk(KERN_INFO DRIVER_PREFIX fmt "\n", ##arg)

#define wl12xx_info(fmt, arg...) \
	printk(KERN_DEBUG DRIVER_PREFIX fmt "\n", ##arg)

#define wl12xx_debug(level, fmt, arg...) \
	do { \
		if (level & DEBUG_LEVEL) \
			printk(KERN_DEBUG DRIVER_PREFIX fmt "\n", ##arg); \
	} while (0)

#define wl12xx_dump(level, prefix, buf, len)	\
	do { \
		if (level & DEBUG_LEVEL) \
			print_hex_dump(KERN_INFO, DRIVER_PREFIX prefix, \
				       DUMP_PREFIX_OFFSET, 16, 1,	\
				       buf,				\
				       min_t(size_t, len, DEBUG_DUMP_LIMIT), \
				       0);				\
	} while (0)

#define wl12xx_dump_ascii(level, prefix, buf, len)	\
	do { \
		if (level & DEBUG_LEVEL) \
			print_hex_dump(KERN_INFO, DRIVER_PREFIX prefix, \
				       DUMP_PREFIX_OFFSET, 16, 1,	\
				       buf,				\
				       min_t(size_t, len, DEBUG_DUMP_LIMIT), \
				       true);				\
	} while (0)

#define WL12XX_DEFAULT_RX_CONFIG (CFG_UNI_FILTER_EN |	\
				  CFG_BSSID_FILTER_EN)

#define WL12XX_DEFAULT_RX_FILTER (CFG_RX_PRSP_EN |  \
				  CFG_RX_MGMT_EN |  \
				  CFG_RX_DATA_EN |  \
				  CFG_RX_CTL_EN |   \
				  CFG_RX_BCN_EN |   \
				  CFG_RX_AUTH_EN |  \
				  CFG_RX_ASSOC_EN)


struct boot_attr {
	u32 radio_type;
	u8 mac_clock;
	u8 arm_clock;
	int firmware_debug;
	u32 minor;
	u32 major;
	u32 bugfix;
};

enum wl12xx_state {
	WL12XX_STATE_OFF,
	WL12XX_STATE_ON,
	WL12XX_STATE_PLT,
};

enum wl12xx_partition_type {
	PART_DOWN,
	PART_WORK,
	PART_DRPW,

	PART_TABLE_LEN
};

struct wl12xx_partition {
	u32 size;
	u32 start;
};

struct wl12xx_partition_set {
	struct wl12xx_partition mem;
	struct wl12xx_partition reg;
};

struct wl12xx;

/* FIXME: I'm not sure about this structure name */
struct wl12xx_chip {
	u32 id;

	const char *fw_filename;
	const char *nvs_filename;

	char fw_ver[21];

	unsigned int power_on_sleep;
	int intr_cmd_complete;
	int intr_init_complete;

	int (*op_upload_fw)(struct wl12xx *wl);
	int (*op_upload_nvs)(struct wl12xx *wl);
	int (*op_boot)(struct wl12xx *wl);
	void (*op_set_ecpu_ctrl)(struct wl12xx *wl, u32 flag);
	void (*op_target_enable_interrupts)(struct wl12xx *wl);
	int (*op_hw_init)(struct wl12xx *wl);
	int (*op_plt_init)(struct wl12xx *wl);

	struct wl12xx_partition_set *p_table;
	enum wl12xx_acx_int_reg *acx_reg_table;
};

struct wl12xx {
	struct ieee80211_hw *hw;
	bool mac80211_registered;

	struct spi_device *spi;

	void (*set_power)(bool enable);
	int irq;

	enum wl12xx_state state;
	struct mutex mutex;

	int physical_mem_addr;
	int physical_reg_addr;
	int virtual_mem_addr;
	int virtual_reg_addr;

	struct wl12xx_chip chip;

	int cmd_box_addr;
	int event_box_addr;
	struct boot_attr boot_attr;

	u8 *fw;
	size_t fw_len;
	u8 *nvs;
	size_t nvs_len;

	u8 bssid[ETH_ALEN];
	u8 mac_addr[ETH_ALEN];
	u8 bss_type;
	u8 ssid[IW_ESSID_MAX_SIZE + 1];
	u8 ssid_len;
	u8 listen_int;
	int channel;

	void *target_mem_map;
	struct acx_data_path_params_resp *data_path;

	/* Number of TX packets transferred to the FW, modulo 16 */
	u32 data_in_count;

	/* Frames scheduled for transmission, not handled yet */
	struct sk_buff_head tx_queue;
	bool tx_queue_stopped;

	struct work_struct tx_work;
	struct work_struct filter_work;

	/* Pending TX frames */
	struct sk_buff *tx_frames[16];

	/*
	 * Index pointing to the next TX complete entry
	 * in the cyclic XT complete array we get from
	 * the FW.
	 */
	u32 next_tx_complete;

	/* FW Rx counter */
	u32 rx_counter;

	/* Rx frames handled */
	u32 rx_handled;

	/* Current double buffer */
	u32 rx_current_buffer;
	u32 rx_last_id;

	/* The target interrupt mask */
	u32 intr_mask;
	struct work_struct irq_work;

	/* The mbox event mask */
	u32 event_mask;

	/* Mailbox pointers */
	u32 mbox_ptr[2];

	/* Are we currently scanning */
	bool scanning;

	/* Our association ID */
	u16 aid;

	/* Default key (for WEP) */
	u32 default_key;

	unsigned int tx_mgmt_frm_rate;
	unsigned int tx_mgmt_frm_mod;

	unsigned int rx_config;
	unsigned int rx_filter;

	/* is firmware in elp mode */
	bool elp;

	/* we can be in psm, but not in elp, we have to differentiate */
	bool psm;

	/* PSM mode requested */
	bool psm_requested;

	/* in dBm */
	int power_level;
};

int wl12xx_plt_start(struct wl12xx *wl);
int wl12xx_plt_stop(struct wl12xx *wl);

#define DEFAULT_HW_GEN_MODULATION_TYPE    CCK_LONG /* Long Preamble */
#define DEFAULT_HW_GEN_TX_RATE          RATE_2MBPS
#define JOIN_TIMEOUT 5000 /* 5000 milliseconds to join */

#define WL12XX_DEFAULT_POWER_LEVEL 20

#define WL12XX_TX_QUEUE_MAX_LENGTH 20

/* Different chips need different sleep times after power on.  WL1271 needs
 * 200ms, WL1251 needs only 10ms.  By default we use 200ms, but as soon as we
 * know the chip ID, we change the sleep value in the wl12xx chip structure,
 * so in subsequent power ons, we don't waste more time then needed.  */
#define WL12XX_DEFAULT_POWER_ON_SLEEP 200

#define CHIP_ID_1251_PG10	           (0x7010101)
#define CHIP_ID_1251_PG11	           (0x7020101)
#define CHIP_ID_1251_PG12	           (0x7030101)
#define CHIP_ID_1271_PG10	           (0x4030101)

#endif
