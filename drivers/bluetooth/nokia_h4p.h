/*
 * This file is part of Nokia H4P bluetooth driver
 *
 * Copyright (C) 2005-2008 Nokia Corporation.
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

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci.h>

#include <linux/serial_reg.h>

#define UART_SYSC_OMAP_RESET	0x03
#define UART_SYSS_RESETDONE	0x01
#define UART_OMAP_SCR_EMPTY_THR	0x08
#define UART_OMAP_SCR_WAKEUP	0x10
#define UART_OMAP_SSR_WAKEUP	0x02
#define UART_OMAP_SSR_TXFULL	0x01

#define UART_OMAP_SYSC_IDLEMODE		0x03
#define UART_OMAP_SYSC_IDLEMASK		(3 << UART_OMAP_SYSC_IDLEMODE)

#define UART_OMAP_SYSC_FORCE_IDLE	(0 << UART_OMAP_SYSC_IDLEMODE)
#define UART_OMAP_SYSC_NO_IDLE		(1 << UART_OMAP_SYSC_IDLEMODE)
#define UART_OMAP_SYSC_SMART_IDLE	(2 << UART_OMAP_SYSC_IDLEMODE)

#define H4P_TRANSFER_MODE		1
#define H4P_SCHED_TRANSFER_MODE		2
#define H4P_ACTIVE_MODE			3

struct h4p_info {
	struct timer_list lazy_release;
	struct hci_dev *hdev;
	spinlock_t lock;

	void __iomem *uart_base;
	unsigned long uart_phys_base;
	int irq;
	struct device *dev;
	u8 chip_type;
	u8 bt_wakeup_gpio;
	u8 host_wakeup_gpio;
	u8 reset_gpio;
	u8 reset_gpio_shared;
	u8 bt_sysclk;
	u8 man_id;
	u8 ver_id;

	struct sk_buff_head fw_queue;
	struct sk_buff *alive_cmd_skb;
	struct completion init_completion;
	struct completion fw_completion;
	struct completion test_completion;
	int fw_error;
	int init_error;

	struct sk_buff_head txq;

	struct sk_buff *rx_skb;
	int rx_count;
	unsigned int rx_state;
	unsigned int garbage_bytes;

	struct sk_buff_head *fw_q;

	bool pm_enabled;
	bool tx_enabled;
	int autorts;
	bool rx_enabled;
	unsigned long pm_flags;

	int tx_clocks_en;
	int rx_clocks_en;
	spinlock_t clocks_lock;
	struct clk *uart_iclk;
	struct clk *uart_fclk;
	atomic_t clk_users;
	u16 dll;
	u16 dlh;
	u16 ier;
	u16 mdr1;
	u16 efr;

	bool init_phase;
};

struct h4p_radio_hdr {
	u8 evt;
	u8 dlen;
} __packed;

struct h4p_neg_hdr {
	u8 dlen;
} __packed;
#define H4P_NEG_HDR_SIZE 1

#define H4P_NEG_REQ	0x00
#define H4P_NEG_ACK	0x20
#define H4P_NEG_NAK	0x40

#define H4P_PROTO_PKT	0x44
#define H4P_PROTO_BYTE	0x4c

#define H4P_ID_BCM2048	0x04

struct h4p_neg_cmd {
	u8	ack;
	__le16	baud;
	u16	unused1;
	u8	proto;
	__le16	sys_clk;
	u16	unused2;
} __packed;

struct h4p_neg_evt {
	u8	ack;
	__le16	baud;
	__le16	unused1;
	u8	proto;
	__le16	sys_clk;
	u16	unused2;
	u8	man_id;
	u8	ver_id;
} __packed;

#define H4P_ALIVE_REQ	0x55
#define H4P_ALIVE_RESP	0xcc

struct h4p_alive_hdr {
	u8	dlen;
} __packed;
#define H4P_ALIVE_HDR_SIZE 1

struct h4p_alive_pkt {
	u8	mid;
	u8	unused;
} __packed;

#define MAX_BAUD_RATE		921600
#define BC4_MAX_BAUD_RATE	3692300
#define UART_CLOCK		48000000
#define BT_INIT_DIVIDER		320
#define BT_BAUDRATE_DIVIDER	384000000
#define BT_SYSCLK_DIV		1000
#define INIT_SPEED		120000

#define H4_TYPE_SIZE		1
#define H4_RADIO_HDR_SIZE	2

/* H4+ packet types */
#define H4_CMD_PKT		0x01
#define H4_ACL_PKT		0x02
#define H4_SCO_PKT		0x03
#define H4_EVT_PKT		0x04
#define H4_NEG_PKT		0x06
#define H4_ALIVE_PKT		0x07
#define H4_RADIO_PKT		0x08

/* TX states */
#define WAIT_FOR_PKT_TYPE	1
#define WAIT_FOR_HEADER		2
#define WAIT_FOR_DATA		3

int h4p_read_fw(struct h4p_info *info);

static inline void h4p_outb(struct h4p_info *info, unsigned int offset, u8 val)
{
	__raw_writeb(val, info->uart_base + (offset << 2));
}

static inline u8 h4p_inb(struct h4p_info *info, unsigned int offset)
{
	u8 val;
	val = __raw_readb(info->uart_base + (offset << 2));
	return val;
}

static inline void h4p_set_rts(struct h4p_info *info, int active)
{
	u8 b;

	b = h4p_inb(info, UART_MCR);
	if (active)
		b |= UART_MCR_RTS;
	else
		b &= ~UART_MCR_RTS;
	h4p_outb(info, UART_MCR, b);
}

int h4p_wait_for_cts(struct h4p_info *info, bool active, int timeout_ms);
void __h4p_set_auto_ctsrts(struct h4p_info *info, bool on, u8 which);
void h4p_set_auto_ctsrts(struct h4p_info *info, bool on, u8 which);
void h4p_change_speed(struct h4p_info *info, unsigned long speed);
int h4p_reset_uart(struct h4p_info *info);
void h4p_init_uart(struct h4p_info *info);
void h4p_enable_tx(struct h4p_info *info);
void h4p_store_regs(struct h4p_info *info);
void h4p_restore_regs(struct h4p_info *info);
void h4p_smart_idle(struct h4p_info *info, bool enable);
