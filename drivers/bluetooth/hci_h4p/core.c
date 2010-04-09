/*
 * This file is part of hci_h4p bluetooth driver
 *
 * Copyright (C) 2005-2010 Nokia Corporation.
 *
 * Contact: Ville Tervo <ville.tervo@nokia.com>
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

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/serial_reg.h>
#include <linux/skbuff.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/timer.h>
#include <linux/bluetooth/hci_h4p.h>

#include <mach/hardware.h>
#include <mach/irqs.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci.h>

#include "hci_h4p.h"

static void hci_h4p_set_clk(struct hci_h4p_info *info, int *clock, int enable)
{
	unsigned long flags;

	spin_lock_irqsave(&info->clocks_lock, flags);
	if (enable && !*clock) {
		NBT_DBG_POWER("Enabling %p\n", clock);
		clk_enable(info->uart_fclk);
		clk_enable(info->uart_iclk);
		if (atomic_read(&info->clk_users) == 0)
			hci_h4p_restore_regs(info);
		atomic_inc(&info->clk_users);
	}

	if (!enable && *clock) {
		NBT_DBG_POWER("Disabling %p\n", clock);
		if (atomic_dec_and_test(&info->clk_users))
			hci_h4p_store_regs(info);
		clk_disable(info->uart_fclk);
		clk_disable(info->uart_iclk);
	}

	*clock = enable;
	spin_unlock_irqrestore(&info->clocks_lock, flags);
}

/* Power management functions */
void hci_h4p_smart_idle(struct hci_h4p_info *info, bool enable)
{
	u8 v;

	return;

	v = hci_h4p_inb(info, UART_OMAP_SYSC);
	v &= ~(UART_OMAP_SYSC_IDLEMASK);

	if (enable)
		v |= UART_OMAP_SYSC_SMART_IDLE;
	else
		v |= UART_OMAP_SYSC_NO_IDLE;

	hci_h4p_outb(info, UART_OMAP_SYSC, v);
}

static void hci_h4p_disable_tx(struct hci_h4p_info *info)
{
	NBT_DBG_POWER("\n");

	if (!info->pm_enabled)
		return;

	hci_h4p_smart_idle(info, 1);

	info->bt_wakeup(0);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 0);
	info->tx_enabled = 0;
}

void hci_h4p_enable_tx(struct hci_h4p_info *info)
{
	NBT_DBG_POWER("\n");

	if (!info->pm_enabled)
		return;

	hci_h4p_set_clk(info, &info->tx_clocks_en, 1);
	info->tx_enabled = 1;
	hci_h4p_smart_idle(info, 0);
	info->bt_wakeup(1);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) |
		     UART_IER_THRI);
}

static void hci_h4p_disable_rx(struct hci_h4p_info *info)
{
	if (!info->pm_enabled)
		return;

	info->rx_enabled = 0;

	if (hci_h4p_inb(info, UART_LSR) & UART_LSR_DR) {
		NBT_DBG("data ready postpone autorts");
		return;
	}

	if (!(hci_h4p_inb(info, UART_LSR) & UART_LSR_TEMT)) {
		NBT_DBG("trasmitter not empty postpone autorts");
		return;
	}

	hci_h4p_set_rts(info, info->rx_enabled);
	__hci_h4p_set_auto_ctsrts(info, 0, UART_EFR_RTS);
	info->autorts = 0;
	hci_h4p_set_clk(info, &info->rx_clocks_en, 0);
}

static void hci_h4p_enable_rx(struct hci_h4p_info *info)
{
	if (!info->pm_enabled)
		return;

	hci_h4p_set_clk(info, &info->rx_clocks_en, 1);
	info->rx_enabled = 1;

	hci_h4p_set_rts(info, 1);

	if (!(hci_h4p_inb(info, UART_LSR) & UART_LSR_TEMT)) {
		NBT_DBG("trasmitter not empty postpone autorts");
		return;
	}

	if (hci_h4p_inb(info, UART_LSR) & UART_LSR_DR) {
		NBT_DBG("data ready postpone autorts");
		return;
	}

	__hci_h4p_set_auto_ctsrts(info, 1, UART_EFR_RTS);
	info->autorts = 1;
}

/* Negotiation functions */
int hci_h4p_send_alive_packet(struct hci_h4p_info *info)
{
	struct hci_h4p_alive_hdr *alive_hdr;
	struct hci_h4p_alive_msg *alive_cmd;
	struct sk_buff *skb;
	unsigned long flags;

	NBT_DBG("Sending alive packet\n");

	skb = bt_skb_alloc(HCI_H4P_ALIVE_HDR_SIZE + HCI_H4P_ALIVE_MSG_SIZE, GFP_ATOMIC);
	if (!skb)
		return -ENOMEM;

	alive_hdr = (void *) skb_put(skb, HCI_H4P_ALIVE_HDR_SIZE);
	alive_hdr->dlen = HCI_H4P_ALIVE_MSG_SIZE;
	alive_cmd = (void *) skb_put(skb, HCI_H4P_ALIVE_MSG_SIZE);
	alive_cmd->message_id = HCI_H4P_ALIVE_IND_REQ;
	alive_cmd->unused = 0x00;
	*skb_push(skb, 1) = H4_ALIVE_PKT;

	skb_queue_tail(&info->txq, skb);
	spin_lock_irqsave(&info->lock, flags);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) |
		     UART_IER_THRI);
	spin_unlock_irqrestore(&info->lock, flags);

	NBT_DBG("Alive packet sent\n");

	return 0;
}

static void hci_h4p_alive_packet(struct hci_h4p_info *info, struct sk_buff *skb)
{
	struct hci_h4p_alive_hdr *alive_hdr = (void *) skb->data;
	struct hci_h4p_alive_msg *alive_evt;

	if (alive_hdr->dlen > skb->len) {
		info->init_error = -EPROTO;
		complete(&info->init_completion);
		return;
	}

	alive_evt = (void *) skb_pull(skb, HCI_H4P_ALIVE_HDR_SIZE);

	NBT_DBG("Received alive packet\n");
	if (alive_evt->message_id != HCI_H4P_ALIVE_IND_RESP) {
		dev_err(info->dev, "Could not negotiate hci_h4p settings\n");
		info->init_error = -EINVAL;
	}

	complete(&info->init_completion);
	kfree_skb(skb);
}

static int hci_h4p_send_negotiation(struct hci_h4p_info *info)
{
	struct hci_h4p_init_cmd *init_cmd;
	struct hci_h4p_init_hdr *init_hdr;
	struct sk_buff *skb;
	unsigned long flags;
	int err;

	NBT_DBG("Sending negotiation..\n");

	skb = bt_skb_alloc(HCI_H4P_INIT_HDR_SIZE + HCI_H4P_INIT_CMD_SIZE, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	init_hdr = (void *)skb_put(skb, HCI_H4P_INIT_HDR_SIZE);
	init_hdr->dlen = HCI_H4P_INIT_CMD_SIZE;
	init_cmd = (void *)skb_put(skb, HCI_H4P_INIT_CMD_SIZE);
	init_cmd->ack = 0x00;
	init_cmd->baudrate = cpu_to_le16(0x01a1);
	init_cmd->unused = cpu_to_le16(0x0000);
	init_cmd->mode = HCI_H4P_MODE;
	init_cmd->sys_clk = cpu_to_le16(0x9600);
	init_cmd->unused2 = cpu_to_le16(0x0000);
	*skb_push(skb, 1) = H4_NEG_PKT;

	hci_h4p_change_speed(info, INIT_SPEED);

	hci_h4p_set_rts(info, 1);
	info->init_error = 0;
	init_completion(&info->init_completion);
	skb_queue_tail(&info->txq, skb);
	spin_lock_irqsave(&info->lock, flags);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) |
			UART_IER_THRI);
	spin_unlock_irqrestore(&info->lock, flags);

	if (!wait_for_completion_interruptible_timeout(&info->init_completion,
				msecs_to_jiffies(1000)))
		return -ETIMEDOUT;

	if (info->init_error < 0)
		return info->init_error;

	/* Change to operational settings */
	hci_h4p_set_auto_ctsrts(info, 0, UART_EFR_RTS);
	hci_h4p_set_rts(info, 0);
	hci_h4p_change_speed(info, MAX_BAUD_RATE);

	err = hci_h4p_wait_for_cts(info, 1, 100);
	if (err < 0)
		return err;

	hci_h4p_set_auto_ctsrts(info, 1, UART_EFR_RTS);
	init_completion(&info->init_completion);
	err = hci_h4p_send_alive_packet(info);

	if (err < 0)
		return err;

	if (!wait_for_completion_interruptible_timeout(&info->init_completion,
				msecs_to_jiffies(1000)))
		return -ETIMEDOUT;

	if (info->init_error < 0)
		return info->init_error;

	NBT_DBG("Negotiation succesful\n");
	return 0;
}

static void hci_h4p_negotiation_packet(struct hci_h4p_info *info,
				       struct sk_buff *skb)
{
	struct hci_h4p_init_hdr *init_hdr = (void *) skb->data;
	struct hci_h4p_init_evt *init_evt;

	if (init_hdr->dlen > skb->len) {
		kfree_skb(skb);
		info->init_error = -EPROTO;
		complete(&info->init_completion);
		return;
	}

	init_evt = (void *)skb_pull(skb, HCI_H4P_INIT_HDR_SIZE);

	if (init_evt->ack != HCI_H4P_ACK) {
		dev_err(info->dev, "Could not negotiate hci_h4p settings\n");
		info->init_error = -EINVAL;
	}

	info->man_id = init_evt->man_id;
	info->ver_id = init_evt->ver_id;

	complete(&info->init_completion);
	kfree_skb(skb);
}

/* H4 packet handling functions */
static int hci_h4p_get_hdr_len(struct hci_h4p_info *info, u8 pkt_type)
{
	long retval;

	switch (pkt_type) {
	case H4_EVT_PKT:
		retval = HCI_EVENT_HDR_SIZE;
		break;
	case H4_ACL_PKT:
		retval = HCI_ACL_HDR_SIZE;
		break;
	case H4_SCO_PKT:
		retval = HCI_SCO_HDR_SIZE;
		break;
	case H4_NEG_PKT:
		retval = HCI_H4P_INIT_HDR_SIZE;
		break;
	case H4_ALIVE_PKT:
		retval = HCI_H4P_ALIVE_HDR_SIZE;
		break;
	case H4_RADIO_PKT:
		retval = H4_RADIO_HDR_SIZE;
		break;
	default:
		dev_err(info->dev, "Unknown H4 packet type 0x%.2x\n", pkt_type);
		retval = -1;
		break;
	}

	return retval;
}

static unsigned int hci_h4p_get_data_len(struct hci_h4p_info *info,
					 struct sk_buff *skb)
{
	long retval = -1;
	struct hci_event_hdr *evt_hdr;
	struct hci_acl_hdr *acl_hdr;
	struct hci_sco_hdr *sco_hdr;
	struct hci_h4p_radio_hdr *radio_hdr;
	struct hci_h4p_init_hdr *init_hdr;
	struct hci_h4p_alive_hdr *alive_hdr;

	switch (bt_cb(skb)->pkt_type) {
	case H4_EVT_PKT:
		evt_hdr = (struct hci_event_hdr *)skb->data;
		retval = evt_hdr->plen;
		break;
	case H4_ACL_PKT:
		acl_hdr = (struct hci_acl_hdr *)skb->data;
		retval = le16_to_cpu(acl_hdr->dlen);
		break;
	case H4_SCO_PKT:
		sco_hdr = (struct hci_sco_hdr *)skb->data;
		retval = sco_hdr->dlen;
		break;
	case H4_RADIO_PKT:
		radio_hdr = (struct hci_h4p_radio_hdr *)skb->data;
		retval = radio_hdr->dlen;
		break;
	case H4_NEG_PKT:
		init_hdr = (struct hci_h4p_init_hdr *)skb->data;
		retval = init_hdr->dlen;
		break;
	case H4_ALIVE_PKT:
		alive_hdr = (struct hci_h4p_alive_hdr *)skb->data;
		retval = alive_hdr->dlen;
		break;
	}

	return retval;
}

static inline void hci_h4p_recv_frame(struct hci_h4p_info *info,
				      struct sk_buff *skb)
{

	if (unlikely(!test_bit(HCI_RUNNING, &info->hdev->flags))) {
		NBT_DBG("fw_event\n");
		if (bt_cb(info->rx_skb)->pkt_type == H4_NEG_PKT) {
			hci_h4p_negotiation_packet(info, info->rx_skb);
			return;
		}
		if (bt_cb(info->rx_skb)->pkt_type == H4_ALIVE_PKT) {
			hci_h4p_alive_packet(info, info->rx_skb);
			return;
		}
		hci_h4p_parse_fw_event(info, skb);
	} else {
		hci_recv_frame(skb);
		NBT_DBG("Frame sent to upper layer\n");
	}
}

static inline void hci_h4p_handle_byte(struct hci_h4p_info *info, u8 byte)
{
	switch (info->rx_state) {
	case WAIT_FOR_PKT_TYPE:
		bt_cb(info->rx_skb)->pkt_type = byte;
		info->rx_count = hci_h4p_get_hdr_len(info, byte);
		if (info->rx_count < 0) {
			info->hdev->stat.err_rx++;
			kfree_skb(info->rx_skb);
			info->rx_skb = NULL;
		} else {
			info->rx_state = WAIT_FOR_HEADER;
		}
		break;
	case WAIT_FOR_HEADER:
		info->rx_count--;
		*skb_put(info->rx_skb, 1) = byte;
		if (info->rx_count != 0)
			break;

		info->rx_count = hci_h4p_get_data_len(info,
				info->rx_skb);
		if (info->rx_count > skb_tailroom(info->rx_skb)) {
			dev_err(info->dev, "Too long frame.\n");
			info->garbage_bytes = info->rx_count -
				skb_tailroom(info->rx_skb);
			kfree_skb(info->rx_skb);
			info->rx_skb = NULL;
			break;
		}
		info->rx_state = WAIT_FOR_DATA;
		break;
	case WAIT_FOR_DATA:
		info->rx_count--;
		*skb_put(info->rx_skb, 1) = byte;
		break;
	default:
		WARN_ON(1);
		break;
	}

	if (info->rx_count == 0) {
		/* H4+ devices should allways send word aligned
		 * packets */
		if (!(info->rx_skb->len % 2))
			info->garbage_bytes++;
		hci_h4p_recv_frame(info, info->rx_skb);
		info->rx_skb = NULL;
	}
}

static void hci_h4p_rx(unsigned long data)
{
	u8 byte;
	struct hci_h4p_info *info = (struct hci_h4p_info *)data;

	NBT_DBG("rx woke up\n");

	while (hci_h4p_inb(info, UART_LSR) & UART_LSR_DR) {
		byte = hci_h4p_inb(info, UART_RX);
		if (info->garbage_bytes) {
			info->garbage_bytes--;
			continue;
		}
		if (info->rx_skb == NULL) {
			info->rx_skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE,
						    GFP_ATOMIC);
			if (!info->rx_skb) {
				dev_err(info->dev,
					"No memory for new packet\n");
				return;
			}
			info->rx_state = WAIT_FOR_PKT_TYPE;
			info->rx_skb->dev = (void *)info->hdev;
		}
		info->hdev->stat.byte_rx++;
		NBT_DBG_TRANSFER_NF("0x%.2x  ", byte);
		hci_h4p_handle_byte(info, byte);
	}

	if (info->rx_enabled == info->autorts)
		return;

	if (!(hci_h4p_inb(info, UART_LSR) & UART_LSR_TEMT))
		return;

	if (hci_h4p_inb(info, UART_LSR) & UART_LSR_DR)
		return;

	hci_h4p_set_rts(info, info->rx_enabled);
	__hci_h4p_set_auto_ctsrts(info, info->rx_enabled, UART_EFR_RTS);
	info->autorts = info->rx_enabled;

	/* Flush posted write to avoid spurious interrupts */
	hci_h4p_inb(info, UART_OMAP_SCR);
	hci_h4p_set_clk(info, &info->rx_clocks_en, 0);
}

static void hci_h4p_tx(unsigned long data)
{
	unsigned int sent = 0;
	struct sk_buff *skb;
	struct hci_h4p_info *info = (struct hci_h4p_info *)data;

	NBT_DBG("tx woke up\n");
	NBT_DBG_TRANSFER("data ");

	if (info->autorts != info->rx_enabled) {
		NBT_DBG("rts unbalanced.. autorts %d rx_enabled %d", info->autorts, info->rx_enabled);
		if (hci_h4p_inb(info, UART_LSR) & UART_LSR_TEMT &&
		    !(hci_h4p_inb(info, UART_LSR) & UART_LSR_DR)) {
			__hci_h4p_set_auto_ctsrts(info, info->rx_enabled,
							  UART_EFR_RTS);
			info->autorts = info->rx_enabled;
			hci_h4p_set_rts(info, info->rx_enabled);
			hci_h4p_set_clk(info, &info->rx_clocks_en,
					info->rx_enabled);
			NBT_DBG("transmitter empty. setinng into balance\n");
		} else {
			hci_h4p_outb(info, UART_OMAP_SCR,
				     hci_h4p_inb(info, UART_OMAP_SCR) |
				     UART_OMAP_SCR_EMPTY_THR);
			NBT_DBG("transmitter/receiver was not empty waiting for next irq\n");
			hci_h4p_set_rts(info, 1);
			goto finish_tx;
		}
	}

	skb = skb_dequeue(&info->txq);
	if (!skb) {
		/* No data in buffer */
		NBT_DBG("skb ready\n");
		if (hci_h4p_inb(info, UART_LSR) & UART_LSR_TEMT) {
			hci_h4p_outb(info, UART_IER,
				     hci_h4p_inb(info, UART_IER) &
				     ~UART_IER_THRI);
			hci_h4p_inb(info, UART_OMAP_SCR);
			hci_h4p_disable_tx(info);
			NBT_DBG("transmitter was empty. cleaning up\n");
			return;
		}
		hci_h4p_outb(info, UART_OMAP_SCR,
				hci_h4p_inb(info, UART_OMAP_SCR) |
				UART_OMAP_SCR_EMPTY_THR);
		NBT_DBG("transmitter was not empty waiting for next irq\n");
		goto finish_tx;
	}

	/* Copy data to tx fifo */
	while (!(hci_h4p_inb(info, UART_OMAP_SSR) & UART_OMAP_SSR_TXFULL) &&
	       (sent < skb->len)) {
		NBT_DBG_TRANSFER_NF("0x%.2x ", skb->data[sent]);
		hci_h4p_outb(info, UART_TX, skb->data[sent]);
		sent++;
	}

	info->hdev->stat.byte_tx += sent;
	if (skb->len == sent) {
		kfree_skb(skb);
	} else {
		skb_pull(skb, sent);
		skb_queue_head(&info->txq, skb);
	}

	hci_h4p_outb(info, UART_OMAP_SCR, hci_h4p_inb(info, UART_OMAP_SCR) &
						     ~UART_OMAP_SCR_EMPTY_THR);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) |
						 UART_IER_THRI);

finish_tx:
	/* Flush posted write to avoid spurious interrupts */
	hci_h4p_inb(info, UART_OMAP_SCR);

}

static irqreturn_t hci_h4p_interrupt(int irq, void *data)
{
	struct hci_h4p_info *info = (struct hci_h4p_info *)data;
	u8 iir, msr;
	int ret;

	ret = IRQ_NONE;

	iir = hci_h4p_inb(info, UART_IIR);
	if (iir & UART_IIR_NO_INT)
		return IRQ_HANDLED;

	NBT_DBG("In interrupt handler iir 0x%.2x\n", iir);

	iir &= UART_IIR_ID;

	if (iir == UART_IIR_MSI) {
		msr = hci_h4p_inb(info, UART_MSR);
		ret = IRQ_HANDLED;
	}
	if (iir == UART_IIR_RLSI) {
		hci_h4p_inb(info, UART_RX);
		hci_h4p_inb(info, UART_LSR);
		ret = IRQ_HANDLED;
	}

	if (iir == UART_IIR_RDI) {
		hci_h4p_rx((unsigned long)data);
		ret = IRQ_HANDLED;
	}

	if (iir == UART_IIR_THRI) {
		hci_h4p_tx((unsigned long)data);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static irqreturn_t hci_h4p_wakeup_interrupt(int irq, void *dev_inst)
{
	struct hci_h4p_info *info = dev_inst;
	int should_wakeup;
	struct hci_dev *hdev;

	if (!info->hdev)
		return IRQ_HANDLED;

	hdev = info->hdev;

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return IRQ_HANDLED;

	should_wakeup = info->host_wakeup();
	NBT_DBG_POWER("gpio interrupt %d\n", should_wakeup);

	/* Check if wee have missed some interrupts */
	if (info->rx_enabled == should_wakeup)
		return IRQ_HANDLED;

	if (should_wakeup)
		hci_h4p_enable_rx(info);
	else
		hci_h4p_disable_rx(info);

	return IRQ_HANDLED;
}

static int hci_h4p_reset(struct hci_h4p_info *info)
{
	int err;

	err = hci_h4p_reset_uart(info);
	if (err < 0) {
		dev_err(info->dev, "Uart reset failed\n");
		return err;
	}
	hci_h4p_init_uart(info);
	hci_h4p_set_rts(info, 0);

	info->reset(0);
	info->bt_wakeup(1);
	msleep(10);
	info->reset(1);

	err = hci_h4p_wait_for_cts(info, 1, 100);
	if (err < 0) {
		dev_err(info->dev, "No cts from bt chip\n");
		return err;
	}

	hci_h4p_set_rts(info, 1);

	return 0;
}

/* hci callback functions */
static int hci_h4p_hci_flush(struct hci_dev *hdev)
{
	struct hci_h4p_info *info;
	info = hdev->driver_data;

	skb_queue_purge(&info->txq);

	return 0;
}

static int hci_h4p_hci_open(struct hci_dev *hdev)
{
	struct hci_h4p_info *info;
	int err;
	struct sk_buff_head fw_queue;
	unsigned long flags;

	info = hdev->driver_data;

	if (test_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	info->rx_enabled = 1;
	info->rx_state = WAIT_FOR_PKT_TYPE;
	info->rx_count = 0;
	info->garbage_bytes = 0;
	info->rx_skb = NULL;
	info->pm_enabled = 0;
	init_completion(&info->fw_completion);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 1);
	hci_h4p_set_clk(info, &info->rx_clocks_en, 1);
	skb_queue_head_init(&fw_queue);

	err = hci_h4p_reset(info);
	if (err < 0)
		goto err_clean;

	hci_h4p_set_auto_ctsrts(info, 1, UART_EFR_CTS | UART_EFR_RTS);
	info->autorts = 1;
	err = hci_h4p_send_negotiation(info);
	if (err < 0)
		goto err_clean;

	skb_queue_head_init(&fw_queue);
	err = hci_h4p_read_fw(info, &fw_queue);
	if (err < 0) {
		dev_err(info->dev, "Cannot read firmware\n");
		return err;
	}

	/* FW image contains also unneeded negoation and alive msgs */
	skb_dequeue(&fw_queue);
	skb_dequeue(&fw_queue);

	err = hci_h4p_send_fw(info, &fw_queue);
	if (err < 0) {
		dev_err(info->dev, "Sending firmware failed.\n");
		goto err_clean;
	}

	info->pm_enabled = 1;

	spin_lock_irqsave(&info->lock, flags);
	info->rx_enabled = info->host_wakeup();
	hci_h4p_set_clk(info, &info->rx_clocks_en, info->rx_enabled);
	spin_unlock_irqrestore(&info->lock, flags);

	hci_h4p_set_clk(info, &info->tx_clocks_en, 0);

	set_bit(HCI_RUNNING, &hdev->flags);

	NBT_DBG("hci up and running\n");
	return 0;

err_clean:
	hci_h4p_hci_flush(hdev);
	hci_h4p_reset_uart(info);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 0);
	hci_h4p_set_clk(info, &info->rx_clocks_en, 0);
	info->reset(0);
	info->bt_wakeup(0);
	skb_queue_purge(&fw_queue);
	kfree_skb(info->rx_skb);

	return err;
}

static int hci_h4p_hci_close(struct hci_dev *hdev)
{
	struct hci_h4p_info *info = hdev->driver_data;

	if (!test_and_clear_bit(HCI_RUNNING, &hdev->flags))
		return 0;

	hci_h4p_hci_flush(hdev);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 1);
	hci_h4p_set_clk(info, &info->rx_clocks_en, 1);
	hci_h4p_reset_uart(info);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 0);
	hci_h4p_set_clk(info, &info->rx_clocks_en, 0);
	info->reset(0);
	info->bt_wakeup(0);
	kfree_skb(info->rx_skb);

	return 0;
}

static void hci_h4p_hci_destruct(struct hci_dev *hdev)
{
}

static int hci_h4p_hci_send_frame(struct sk_buff *skb)
{
	struct hci_h4p_info *info;
	struct hci_dev *hdev = (struct hci_dev *)skb->dev;
	int err = 0;
	unsigned long flags;

	if (!hdev) {
		printk(KERN_WARNING "hci_h4p: Frame for unknown device\n");
		return -ENODEV;
	}

	NBT_DBG("dev %p, skb %p\n", hdev, skb);

	info = hdev->driver_data;

	if (!test_bit(HCI_RUNNING, &hdev->flags)) {
		dev_warn(info->dev, "Frame for non-running device\n");
		return -EIO;
	}

	switch (bt_cb(skb)->pkt_type) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;
	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;
	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;
	}

	/* Push frame type to skb */
	*skb_push(skb, 1) = (bt_cb(skb)->pkt_type);
	/* We should allways send word aligned data to h4+ devices */
	if (skb->len % 2) {
		err = skb_pad(skb, 1);
		if (!err)
			*skb_put(skb, 1) = 0x00;
	}
	if (err)
		return err;

	spin_lock_irqsave(&info->lock, flags);
	skb_queue_tail(&info->txq, skb);
	hci_h4p_enable_tx(info);
	spin_unlock_irqrestore(&info->lock, flags);

	return 0;
}

static int hci_h4p_hci_ioctl(struct hci_dev *hdev, unsigned int cmd,
			     unsigned long arg)
{
	return -ENOIOCTLCMD;
}

static int hci_h4p_register_hdev(struct hci_h4p_info *info)
{
	struct hci_dev *hdev;

	/* Initialize and register HCI device */

	hdev = hci_alloc_dev();
	if (!hdev) {
		dev_err(info->dev, "Can't allocate memory for device\n");
		return -ENOMEM;
	}
	info->hdev = hdev;

	hdev->bus = HCI_UART;
	hdev->driver_data = info;

	hdev->open = hci_h4p_hci_open;
	hdev->close = hci_h4p_hci_close;
	hdev->flush = hci_h4p_hci_flush;
	hdev->send = hci_h4p_hci_send_frame;
	hdev->destruct = hci_h4p_hci_destruct;
	hdev->ioctl = hci_h4p_hci_ioctl;
	set_bit(HCI_QUIRK_NO_RESET, &hdev->quirks);

	hdev->owner = THIS_MODULE;

	if (hci_register_dev(hdev) < 0) {
		dev_err(info->dev, "hci_register failed %s.\n", hdev->name);
		return -ENODEV;
	}

	return 0;
}

static int hci_h4p_probe(struct platform_device *pdev)
{
	struct hci_h4p_platform_data *bt_plat_data;
	struct hci_h4p_info *info;
	int err;

	dev_info(&pdev->dev, "Registering HCI H4P device\n");
	info = kzalloc(sizeof(struct hci_h4p_info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->pm_enabled = 0;
	info->tx_enabled = 1;
	info->rx_enabled = 1;
	info->garbage_bytes = 0;
	info->tx_clocks_en = 0;
	info->rx_clocks_en = 0;
	spin_lock_init(&info->lock);
	spin_lock_init(&info->clocks_lock);
	skb_queue_head_init(&info->txq);

	if (pdev->dev.platform_data == NULL) {
		dev_err(&pdev->dev, "Could not get Bluetooth config data\n");
		kfree(info);
		return -ENODATA;
	}

	bt_plat_data = pdev->dev.platform_data;
	info->chip_type = 3;
	info->bt_wakeup = bt_plat_data->bt_wu;
	info->host_wakeup = bt_plat_data->host_wu;
	info->reset = bt_plat_data->reset;
	info->uart_base = bt_plat_data->uart_base;
	info->host_wakeup_gpio = bt_plat_data->host_wu_gpio;

	NBT_DBG("RESET gpio: %p\n", info->reset);
	NBT_DBG("BTWU gpio: %p\n", info->bt_wakeup);
	NBT_DBG("HOSTWU gpio: %p\n", info->host_wakeup);

	info->irq = bt_plat_data->uart_irq;
	err = request_irq(info->irq, hci_h4p_interrupt, IRQF_DISABLED | IRQF_SHARED,
			"hci_h4p", info);
	if (err < 0) {
		dev_err(info->dev, "hci_h4p: unable to get IRQ %d\n", info->irq);
		goto cleanup;
	}

	err = request_irq(gpio_to_irq(info->host_wakeup_gpio),
			  hci_h4p_wakeup_interrupt,  IRQF_TRIGGER_FALLING |
			  IRQF_TRIGGER_RISING | IRQF_DISABLED,
			  "hci_h4p_wkup", info);
	if (err < 0) {
		dev_err(info->dev, "hci_h4p: unable to get wakeup IRQ %d\n",
			  gpio_to_irq(info->host_wakeup_gpio));
		free_irq(info->irq, info);
		goto cleanup;
	}

	err = set_irq_wake(gpio_to_irq(info->host_wakeup_gpio), 1);
	if (err < 0) {
		dev_err(info->dev, "hci_h4p: unable to set wakeup for IRQ %d\n",
				gpio_to_irq(info->host_wakeup_gpio));
		free_irq(info->irq, info);
		free_irq(gpio_to_irq(info->host_wakeup_gpio), info);
		goto cleanup;
	}

	hci_h4p_set_clk(info, &info->tx_clocks_en, 1);
	err = hci_h4p_reset_uart(info);
	if (err < 0)
		goto cleanup_irq;
	hci_h4p_init_uart(info);
	hci_h4p_set_rts(info, 0);
	err = hci_h4p_reset(info);
	hci_h4p_reset_uart(info);
	if (err < 0)
		goto cleanup_irq;
	info->reset(0);
	hci_h4p_set_clk(info, &info->tx_clocks_en, 0);

	platform_set_drvdata(pdev, info);

	if (hci_h4p_register_hdev(info) < 0) {
		dev_err(info->dev, "failed to register hci_h4p hci device\n");
		goto cleanup_irq;
	}

	return 0;

cleanup_irq:
	free_irq(info->irq, (void *)info);
	free_irq(gpio_to_irq(info->host_wakeup_gpio), info);
cleanup:
	info->reset(0);
	kfree(info);
	return err;

}

static int hci_h4p_remove(struct platform_device *pdev)
{
	struct hci_h4p_info *info;

	info = platform_get_drvdata(pdev);

	hci_h4p_hci_close(info->hdev);
	free_irq(gpio_to_irq(info->host_wakeup_gpio), info);
	hci_unregister_dev(info->hdev);
	hci_free_dev(info->hdev);
	free_irq(info->irq, (void *) info);
	kfree(info);

	return 0;
}

static struct platform_driver hci_h4p_driver = {
	.probe		= hci_h4p_probe,
	.remove		= hci_h4p_remove,
	.driver		= {
		.name	= "hci_h4p",
	},
};

static int __init hci_h4p_init(void)
{
	int err = 0;

	/* Register the driver with LDM */
	err = platform_driver_register(&hci_h4p_driver);
	if (err < 0)
		printk(KERN_WARNING "failed to register hci_h4p driver\n");

	return err;
}

static void __exit hci_h4p_exit(void)
{
	platform_driver_unregister(&hci_h4p_driver);
}

module_init(hci_h4p_init);
module_exit(hci_h4p_exit);

MODULE_DESCRIPTION("h4 driver with nokia extensions");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ville Tervo");
