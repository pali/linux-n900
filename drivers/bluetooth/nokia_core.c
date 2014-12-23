/*
 * This file is part of Nokia H4P bluetooth driver
 *
 * Copyright (C) 2005-2008 Nokia Corporation.
 * Copyright (C) 2014 Pavel Machek <pavel@ucw.cz>
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
 * Thanks to all the Nokia people that helped with this driver,
 * including Ville Tervo and Roger Quadros.
 *
 * Power saving functionality was removed from this driver to make
 * merging easier.
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
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/io.h>
#include <linux/completion.h>
#include <linux/sizes.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>
#include <net/bluetooth/hci.h>

#include "nokia_h4p.h"

/* This should be used in function that cannot release clocks */
static void h4p_set_clk(struct h4p_info *info, int *clock, bool enable)
{
	unsigned long flags;

	spin_lock_irqsave(&info->clocks_lock, flags);
	if (enable && !*clock) {
		BT_DBG("Enabling %p", clock);
		clk_prepare_enable(info->uart_fclk);
		clk_prepare_enable(info->uart_iclk);
		if (atomic_read(&info->clk_users) == 0)
			h4p_restore_regs(info);
		atomic_inc(&info->clk_users);
	}

	if (!enable && *clock) {
		BT_DBG("Disabling %p", clock);
		if (atomic_dec_and_test(&info->clk_users))
			h4p_store_regs(info);
		clk_disable_unprepare(info->uart_fclk);
		clk_disable_unprepare(info->uart_iclk);
	}

	*clock = enable;
	spin_unlock_irqrestore(&info->clocks_lock, flags);
}

static void h4p_lazy_clock_release(unsigned long data)
{
	struct h4p_info *info = (struct h4p_info *)data;
	unsigned long flags;

	spin_lock_irqsave(&info->lock, flags);
	if (!info->tx_enabled)
		h4p_set_clk(info, &info->tx_clocks_en, false);
	spin_unlock_irqrestore(&info->lock, flags);
}

/* Power management functions */
void h4p_smart_idle(struct h4p_info *info, bool enable)
{
	u8 v;

	v = h4p_inb(info, UART_OMAP_SYSC);
	v &= ~(UART_OMAP_SYSC_IDLEMASK);

	if (enable)
		v |= UART_OMAP_SYSC_SMART_IDLE;
	else
		v |= UART_OMAP_SYSC_NO_IDLE;

	h4p_outb(info, UART_OMAP_SYSC, v);
}

static inline void h4p_schedule_pm(struct h4p_info *info)
{
}

static void h4p_disable_tx(struct h4p_info *info)
{
	if (!info->pm_enabled)
		return;

	/* Re-enable smart-idle */
	h4p_smart_idle(info, 1);

	gpio_set_value(info->bt_wakeup_gpio, 0);
	mod_timer(&info->lazy_release, jiffies + msecs_to_jiffies(100));
	info->tx_enabled = false;
}

static void h4p_enable_tx_nopm(struct h4p_info *info)
{
	unsigned long flags;

	spin_lock_irqsave(&info->lock, flags);
	h4p_outb(info, UART_IER,
		 h4p_inb(info, UART_IER) | UART_IER_THRI);
	spin_unlock_irqrestore(&info->lock, flags);
}

void h4p_enable_tx(struct h4p_info *info)
{
	unsigned long flags;

	if (!info->pm_enabled)
		return;

	h4p_schedule_pm(info);

	spin_lock_irqsave(&info->lock, flags);
	del_timer(&info->lazy_release);
	h4p_set_clk(info, &info->tx_clocks_en, true);
	info->tx_enabled = true;
	gpio_set_value(info->bt_wakeup_gpio, 1);
	h4p_outb(info, UART_IER,
		 h4p_inb(info, UART_IER) | UART_IER_THRI);

	/* Disable smart-idle as UART TX interrupts
	 * are not wake-up capable
	 */
	h4p_smart_idle(info, 0);

	spin_unlock_irqrestore(&info->lock, flags);
}

static void h4p_disable_rx(struct h4p_info *info)
{
	if (!info->pm_enabled)
		return;

	info->rx_enabled = false;

	if (h4p_inb(info, UART_LSR) & UART_LSR_DR)
		return;

	if (!(h4p_inb(info, UART_LSR) & UART_LSR_TEMT))
		return;

	__h4p_set_auto_ctsrts(info, 0, UART_EFR_RTS);
	info->autorts = 0;
	h4p_set_clk(info, &info->rx_clocks_en, false);
}

static void h4p_enable_rx(struct h4p_info *info)
{
	if (!info->pm_enabled)
		return;

	h4p_schedule_pm(info);

	h4p_set_clk(info, &info->rx_clocks_en, true);
	info->rx_enabled = true;

	if (!(h4p_inb(info, UART_LSR) & UART_LSR_TEMT))
		return;

	__h4p_set_auto_ctsrts(info, 1, UART_EFR_RTS);
	info->autorts = 1;
}

static void h4p_simple_send_frame(struct h4p_info *info, struct sk_buff *skb)
{
	skb_queue_tail(&info->txq, skb);
	h4p_enable_tx_nopm(info);
}

/* Negotiation functions */
static int h4p_send_alive_packet(struct h4p_info *info)
{
	struct h4p_alive_hdr *hdr;
	struct h4p_alive_pkt *pkt;
	struct sk_buff *skb;
	int len;

	BT_DBG("Sending alive packet");

	len = H4_TYPE_SIZE + sizeof(*hdr) + sizeof(*pkt);
	skb = bt_skb_alloc(len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	memset(skb->data, 0x00, len);
	*skb_put(skb, 1) = H4_ALIVE_PKT;
	hdr = (struct h4p_alive_hdr *)skb_put(skb, sizeof(*hdr));
	hdr->dlen = sizeof(*pkt);
	pkt = (struct h4p_alive_pkt *)skb_put(skb, sizeof(*pkt));
	pkt->mid = H4P_ALIVE_REQ;

	h4p_simple_send_frame(info, skb);

	BT_DBG("Alive packet sent");

	return 0;
}

static void h4p_alive_packet(struct h4p_info *info, struct sk_buff *skb)
{
	struct h4p_alive_hdr *hdr;
	struct h4p_alive_pkt *pkt;

	BT_DBG("Received alive packet");
	hdr = (struct h4p_alive_hdr *)skb->data;
	if (hdr->dlen != sizeof(*pkt)) {
		dev_err(info->dev, "Corrupted alive message\n");
		info->init_error = -EIO;
		goto finish_alive;
	}

	pkt = (struct h4p_alive_pkt *)skb_pull(skb, sizeof(*hdr));
	if (pkt->mid != H4P_ALIVE_RESP) {
		dev_err(info->dev, "Could not negotiate nokia_h4p settings\n");
		info->init_error = -EINVAL;
	}

finish_alive:
	complete(&info->init_completion);
	kfree_skb(skb);
}

static int h4p_send_negotiation(struct h4p_info *info)
{
	struct h4p_neg_cmd *neg_cmd;
	struct h4p_neg_hdr *neg_hdr;
	struct sk_buff *skb;
	int err, len;
	u16 sysclk = 38400;

	BT_DBG("Sending negotiation..");
	len = sizeof(*neg_cmd) + sizeof(*neg_hdr) + H4_TYPE_SIZE;

	skb = bt_skb_alloc(len, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	memset(skb->data, 0x00, len);
	*skb_put(skb, 1) = H4_NEG_PKT;
	neg_hdr = (struct h4p_neg_hdr *)skb_put(skb, sizeof(*neg_hdr));
	neg_cmd = (struct h4p_neg_cmd *)skb_put(skb, sizeof(*neg_cmd));

	neg_hdr->dlen = sizeof(*neg_cmd);
	neg_cmd->ack = H4P_NEG_REQ;
	neg_cmd->baud = cpu_to_le16(BT_BAUDRATE_DIVIDER/MAX_BAUD_RATE);
	neg_cmd->proto = H4P_PROTO_BYTE;
	neg_cmd->sys_clk = cpu_to_le16(sysclk);

	h4p_change_speed(info, INIT_SPEED);

	h4p_set_rts(info, 1);
	info->init_error = 0;
	init_completion(&info->init_completion);

	h4p_simple_send_frame(info, skb);

	if (!wait_for_completion_interruptible_timeout(&info->init_completion,
						       msecs_to_jiffies(1000))) {
		BT_ERR("h4p: negotiation did not return\n");
		return -ETIMEDOUT;
	}

	if (info->init_error < 0)
		return info->init_error;

	/* Change to operational settings */
	h4p_set_auto_ctsrts(info, 0, UART_EFR_RTS);
	h4p_set_rts(info, 0);
	h4p_change_speed(info, MAX_BAUD_RATE);

	err = h4p_wait_for_cts(info, true, 100);
	if (err < 0)
		return err;

	h4p_set_auto_ctsrts(info, 1, UART_EFR_RTS);
	init_completion(&info->init_completion);
	err = h4p_send_alive_packet(info);
	if (err < 0)
		return err;

	if (!wait_for_completion_interruptible_timeout(&info->init_completion,
						msecs_to_jiffies(1000)))
		return -ETIMEDOUT;

	if (info->init_error < 0)
		return info->init_error;

	BT_DBG("Negotiation successful\n");
	return 0;
}

static void h4p_negotiation_packet(struct h4p_info *info, struct sk_buff *skb)
{
	struct h4p_neg_hdr *hdr;
	struct h4p_neg_evt *evt;

	hdr = (struct h4p_neg_hdr *)skb->data;
	if (hdr->dlen != sizeof(*evt)) {
		info->init_error = -EIO;
		goto finish_neg;
	}

	evt = (struct h4p_neg_evt *)skb_pull(skb, sizeof(*hdr));

	if (evt->ack != H4P_NEG_ACK) {
		dev_err(info->dev, "Could not negotiate nokia_h4p settings\n");
		info->init_error = -EINVAL;
	}

	info->man_id = evt->man_id;
	info->ver_id = evt->ver_id;
	BT_DBG("Negotiation finished.\n");

finish_neg:
	complete(&info->init_completion);
	kfree_skb(skb);
}

/* H4 packet handling functions */
static int h4p_get_hdr_len(struct h4p_info *info, u8 pkt_type)
{
	int retval;

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
		retval = H4P_NEG_HDR_SIZE;
		break;
	case H4_ALIVE_PKT:
		retval = H4P_ALIVE_HDR_SIZE;
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

static unsigned int
h4p_get_data_len(struct h4p_info *info, struct sk_buff *skb)
{
	struct hci_acl_hdr *acl_hdr;
	struct hci_sco_hdr *sco_hdr;
	struct hci_event_hdr *evt_hdr;
	struct h4p_neg_hdr *neg_hdr;
	struct h4p_alive_hdr *alive_hdr;
	struct h4p_radio_hdr *radio_hdr;

	switch (bt_cb(skb)->pkt_type) {
	case H4_EVT_PKT:
		evt_hdr = (struct hci_event_hdr *)skb->data;
		return evt_hdr->plen;
	case H4_ACL_PKT:
		acl_hdr = (struct hci_acl_hdr *)skb->data;
		return le16_to_cpu(acl_hdr->dlen);
	case H4_SCO_PKT:
		sco_hdr = (struct hci_sco_hdr *)skb->data;
		return sco_hdr->dlen;
	case H4_RADIO_PKT:
		radio_hdr = (struct h4p_radio_hdr *)skb->data;
		return radio_hdr->dlen;
	case H4_NEG_PKT:
		neg_hdr = (struct h4p_neg_hdr *)skb->data;
		return neg_hdr->dlen;
	case H4_ALIVE_PKT:
		alive_hdr = (struct h4p_alive_hdr *)skb->data;
		return alive_hdr->dlen;
	default:
		return ~0;
	}
}

static inline void h4p_recv_frame(struct h4p_info *info, struct sk_buff *skb)
{
	if (info->init_phase) {
		switch (bt_cb(skb)->pkt_type) {
		case H4_NEG_PKT:
			h4p_negotiation_packet(info, skb);
			info->rx_state = WAIT_FOR_PKT_TYPE;
			return;
		case H4_ALIVE_PKT:
			h4p_alive_packet(info, skb);
			info->rx_state = WAIT_FOR_PKT_TYPE;
			return;
		}
	}

	hci_recv_frame(info->hdev, skb);
	BT_DBG("Frame sent to upper layer");
}

static inline void h4p_handle_byte(struct h4p_info *info, u8 byte)
{
	switch (info->rx_state) {
	case WAIT_FOR_PKT_TYPE:
		bt_cb(info->rx_skb)->pkt_type = byte;
		info->rx_count = h4p_get_hdr_len(info, byte);
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
		info->rx_count = h4p_get_data_len(info, info->rx_skb);
		if (info->rx_count > skb_tailroom(info->rx_skb)) {
			dev_err(info->dev, "frame too long\n");
			info->garbage_bytes = info->rx_count
				- skb_tailroom(info->rx_skb);
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
		/* H4+ devices should always send word aligned packets */
		if (!(info->rx_skb->len % 2))
			info->garbage_bytes++;
		h4p_recv_frame(info, info->rx_skb);
		info->rx_skb = NULL;
	}
}

static void h4p_rx_tasklet(unsigned long data)
{
	struct h4p_info *info = (struct h4p_info *)data;
	u8 byte;

	BT_DBG("rx_tasklet woke up");

	while (h4p_inb(info, UART_LSR) & UART_LSR_DR) {
		byte = h4p_inb(info, UART_RX);
		BT_DBG("[in: %02x]", byte);
		if (info->garbage_bytes) {
			info->garbage_bytes--;
			continue;
		}
		if (!info->rx_skb) {
			info->rx_skb = bt_skb_alloc(HCI_MAX_FRAME_SIZE,
						    GFP_ATOMIC | GFP_DMA);
			if (!info->rx_skb) {
				dev_err(info->dev,
					"No memory for new packet\n");
				goto finish_rx;
			}
			info->rx_state = WAIT_FOR_PKT_TYPE;
			info->rx_skb->dev = (void *)info->hdev;
		}
		info->hdev->stat.byte_rx++;
		h4p_handle_byte(info, byte);
	}

	if (!info->rx_enabled) {
		if ((h4p_inb(info, UART_LSR) & UART_LSR_TEMT) &&
		    info->autorts) {
			__h4p_set_auto_ctsrts(info, 0 , UART_EFR_RTS);
			info->autorts = 0;
		}
		/* Flush posted write to avoid spurious interrupts */
		h4p_inb(info, UART_OMAP_SCR);
		h4p_set_clk(info, &info->rx_clocks_en, false);
	}

finish_rx:
	BT_DBG("rx_ended");
}

static void h4p_tx_tasklet(unsigned long data)
{
	struct h4p_info *info = (struct h4p_info *)data;
	struct sk_buff *skb;
	unsigned int sent = 0;

	BT_DBG("tx_tasklet woke up");

	if (info->autorts != info->rx_enabled) {
		if (h4p_inb(info, UART_LSR) & UART_LSR_TEMT) {
			if (info->autorts && !info->rx_enabled) {
				__h4p_set_auto_ctsrts(info, 0,
						      UART_EFR_RTS);
				info->autorts = 0;
			}
			if (!info->autorts && info->rx_enabled) {
				__h4p_set_auto_ctsrts(info, 1,
						      UART_EFR_RTS);
				info->autorts = 1;
			}
		} else {
			h4p_outb(info, UART_OMAP_SCR,
				 h4p_inb(info, UART_OMAP_SCR)
				 | UART_OMAP_SCR_EMPTY_THR);
			goto finish_tx;
		}
	}

	skb = skb_dequeue(&info->txq);
	if (!skb) {
		/* No data in buffer */
		BT_DBG("skb ready");
		if (h4p_inb(info, UART_LSR) & UART_LSR_TEMT) {
			h4p_outb(info, UART_IER,
				 h4p_inb(info, UART_IER) & ~UART_IER_THRI);
			h4p_inb(info, UART_OMAP_SCR);
			h4p_disable_tx(info);
			return;
		}
		h4p_outb(info, UART_OMAP_SCR,
			     h4p_inb(info, UART_OMAP_SCR) |
			     UART_OMAP_SCR_EMPTY_THR);
		goto finish_tx;
	}

	/* Copy data to tx fifo */
	while (!(h4p_inb(info, UART_OMAP_SSR) & UART_OMAP_SSR_TXFULL) &&
	       (sent < skb->len)) {
		BT_DBG("%02x ", skb->data[sent]);
		h4p_outb(info, UART_TX, skb->data[sent]);
		sent++;
	}

	info->hdev->stat.byte_tx += sent;
	if (skb->len == sent) {
		kfree_skb(skb);
	} else {
		skb_pull(skb, sent);
		skb_queue_head(&info->txq, skb);
	}

	h4p_outb(info, UART_OMAP_SCR,
		 h4p_inb(info, UART_OMAP_SCR) & ~UART_OMAP_SCR_EMPTY_THR);
	h4p_outb(info, UART_IER,
		 h4p_inb(info, UART_IER) | UART_IER_THRI);

finish_tx:
	/* Flush posted write to avoid spurious interrupts */
	h4p_inb(info, UART_OMAP_SCR);

}

static irqreturn_t h4p_interrupt(int irq, void *data)
{
	struct h4p_info *info = (struct h4p_info *)data;
	u8 iir, msr;
	int ret;

	ret = IRQ_NONE;

	iir = h4p_inb(info, UART_IIR);
	if (iir & UART_IIR_NO_INT)
		return IRQ_HANDLED;

	iir &= UART_IIR_ID;

	if (iir == UART_IIR_MSI) {
		msr = h4p_inb(info, UART_MSR);
		ret = IRQ_HANDLED;
	}
	if (iir == UART_IIR_RLSI) {
		h4p_inb(info, UART_RX);
		h4p_inb(info, UART_LSR);
		ret = IRQ_HANDLED;
	}

	if (iir == UART_IIR_RDI) {
		h4p_rx_tasklet((unsigned long)data);
		ret = IRQ_HANDLED;
	}

	if (iir == UART_IIR_THRI) {
		h4p_tx_tasklet((unsigned long)data);
		ret = IRQ_HANDLED;
	}

	return ret;
}

static irqreturn_t h4p_wakeup_interrupt(int irq, void *dev_inst)
{
	struct h4p_info *info = dev_inst;
	bool should_wakeup;

	BT_DBG("[wakeup irq]");

	if (!info->hdev)
		return IRQ_HANDLED;

	should_wakeup = !!gpio_get_value(info->host_wakeup_gpio);

	if (info->init_phase) {
		if (should_wakeup == 1)
			complete_all(&info->test_completion);

		BT_DBG("wakeup irq handled");

		return IRQ_HANDLED;
	}

	BT_DBG("gpio interrupt %d", should_wakeup);

	/* Check if we have missed some interrupts */
	if (info->rx_enabled == should_wakeup)
		return IRQ_HANDLED;

	if (should_wakeup)
		h4p_enable_rx(info);
	else
		h4p_disable_rx(info);

	return IRQ_HANDLED;
}

static int h4p_reset(struct h4p_info *info)
{
	int err;

	err = h4p_reset_uart(info);
	if (err < 0) {
		dev_err(info->dev, "Uart reset failed\n");
		return err;
	}
	h4p_init_uart(info);
	h4p_set_rts(info, 0);

	gpio_set_value(info->reset_gpio, 0);
	gpio_set_value(info->bt_wakeup_gpio, 1);
	msleep(10);

	if (gpio_get_value(info->host_wakeup_gpio) == 1) {
		dev_err(info->dev, "host_wakeup_gpio not low\n");
		return -EPROTO;
	}

	init_completion(&info->test_completion);
	gpio_set_value(info->reset_gpio, 1);

	if (!wait_for_completion_interruptible_timeout(&info->test_completion,
						       msecs_to_jiffies(100))) {
		dev_err(info->dev, "wakeup test timed out\n");
		complete_all(&info->test_completion);
		return -EPROTO;
	}

	err = h4p_wait_for_cts(info, true, 100);
	if (err < 0) {
		dev_err(info->dev, "No cts from bt chip\n");
		return err;
	}

	h4p_set_rts(info, 1);

	return 0;
}

/* hci callback functions */
static int h4p_hci_flush(struct hci_dev *hdev)
{
	struct h4p_info *info = hci_get_drvdata(hdev);

	skb_queue_purge(&info->txq);

	return 0;
}

static int h4p_bt_wakeup_test(struct h4p_info *info)
{
	/*
	 * Test Sequence:
	 * Host de-asserts the BT_WAKE_UP line.
	 * Host polls the UART_CTS line, waiting for it to be de-asserted.
	 * Host asserts the BT_WAKE_UP line.
	 * Host polls the UART_CTS line, waiting for it to be asserted.
	 * Host de-asserts the BT_WAKE_UP line (allow the Bluetooth device to
	 * sleep).
	 * Host polls the UART_CTS line, waiting for it to be de-asserted.
	 */
	int err = 0;

	if (!info)
		return -EINVAL;

	/* Disable wakeup interrupts */
	disable_irq(gpio_to_irq(info->host_wakeup_gpio));

	gpio_set_value(info->bt_wakeup_gpio, 0);
	err = h4p_wait_for_cts(info, false, 100);
	if (err) {
		dev_warn(info->dev,
			 "bt_wakeup_test: fail: CTS low timed out: %d\n", err);
		goto out;
	}

	gpio_set_value(info->bt_wakeup_gpio, 1);
	err = h4p_wait_for_cts(info, true, 100);
	if (err) {
		dev_warn(info->dev,
			 "bt_wakeup_test: fail: CTS high timed out: %d\n",
			 err);
		goto out;
	}

	gpio_set_value(info->bt_wakeup_gpio, 0);
	err = h4p_wait_for_cts(info, false, 100);
	if (err) {
		dev_warn(info->dev,
			 "bt_wakeup_test: fail: CTS re-low timed out: %d\n",
			 err);
		goto out;
	}

out:
	/* Re-enable wakeup interrupts */
	enable_irq(gpio_to_irq(info->host_wakeup_gpio));

	return err;
}

static int h4p_hci_set_bdaddr(struct hci_dev *hdev, const bdaddr_t *bdaddr)
{
	struct sk_buff *skb;
	long ret;

	BT_DBG("Set bdaddr... %pMR", bdaddr);

	skb = __hci_cmd_sync(hdev, 0xfc01, 6, bdaddr, HCI_INIT_TIMEOUT);
	if (IS_ERR(skb)) {
		ret = PTR_ERR(skb);
		BT_ERR("%s: BCM: Change address command failed (%ld)",
		       hdev->name, ret);
		return ret;
	}
	kfree_skb(skb);

	return 0;
}

static void h4p_deinit(struct hci_dev *hdev)
{
	struct h4p_info *info = hci_get_drvdata(hdev);

	h4p_hci_flush(hdev);
	h4p_set_clk(info, &info->tx_clocks_en, true);
	h4p_set_clk(info, &info->rx_clocks_en, true);
	h4p_reset_uart(info);
	del_timer_sync(&info->lazy_release);
	h4p_set_clk(info, &info->tx_clocks_en, false);
	h4p_set_clk(info, &info->rx_clocks_en, false);
	gpio_set_value(info->reset_gpio, 0);
	gpio_set_value(info->bt_wakeup_gpio, 0);
	kfree_skb(info->rx_skb);
	info->rx_skb = NULL;
}

static int h4p_hci_setup(struct hci_dev *hdev)
{
	struct h4p_info *info = hci_get_drvdata(hdev);
	int err;
	unsigned long flags;

	h4p_set_clk(info, &info->tx_clocks_en, true);
	h4p_set_clk(info, &info->rx_clocks_en, true);

	h4p_set_auto_ctsrts(info, 1, UART_EFR_CTS | UART_EFR_RTS);
	info->autorts = 1;

	info->init_phase = true;
	BT_DBG("hci_setup");

	err = h4p_send_negotiation(info);
	if (err < 0)
		goto err_clean;

	/* Disable smart-idle as UART TX interrupts
	 * are not wake-up capable
	 */
	h4p_smart_idle(info, 0);

	err = h4p_read_fw(info);
	if (err < 0) {
		dev_err(info->dev, "Cannot read firmware\n");
		goto err_clean;
	}

	h4p_set_auto_ctsrts(info, 0, UART_EFR_RTS);
	h4p_set_rts(info, 0);
	h4p_change_speed(info, BC4_MAX_BAUD_RATE);
	h4p_set_auto_ctsrts(info, 1, UART_EFR_RTS);

	info->pm_enabled = true;

	err = h4p_bt_wakeup_test(info);
	if (err < 0) {
		dev_err(info->dev, "BT wakeup test failed.\n");
		goto err_clean;
	}

	spin_lock_irqsave(&info->lock, flags);
	info->rx_enabled = !!gpio_get_value(info->host_wakeup_gpio);
	h4p_set_clk(info, &info->rx_clocks_en, info->rx_enabled);
	spin_unlock_irqrestore(&info->lock, flags);

	h4p_set_clk(info, &info->tx_clocks_en, 0);

	info->init_phase = false;
	return 0;

err_clean:
	BT_ERR("hci_setup: something failed, should do the clean up");
	h4p_hci_flush(hdev);
	h4p_deinit(hdev);
	return err;
}

static int h4p_boot(struct hci_dev *hdev)
{
	struct h4p_info *info = hci_get_drvdata(hdev);
	int err;

	info->rx_enabled = 1;
	info->rx_state = WAIT_FOR_PKT_TYPE;
	info->rx_count = 0;
	info->garbage_bytes = 0;
	info->rx_skb = NULL;
	info->pm_enabled = false;
	init_completion(&info->fw_completion);
	h4p_set_clk(info, &info->tx_clocks_en, 1);
	h4p_set_clk(info, &info->rx_clocks_en, 1);

	err = h4p_reset(info);
	return err;
}

static int h4p_hci_open(struct hci_dev *hdev)
{
	set_bit(HCI_RUNNING, &hdev->flags);
	return 0;
}

static int h4p_hci_close(struct hci_dev *hdev)
{
	clear_bit(HCI_RUNNING, &hdev->flags);
	return 0;
}

static int h4p_hci_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct h4p_info *info = hci_get_drvdata(hdev);
	int err = 0;

	BT_DBG("hci_send_frame: dev %p, skb %p", hdev, skb);

	if (!test_bit(HCI_RUNNING, &hdev->flags))
		return -EBUSY;

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
	*skb_push(skb, 1) = bt_cb(skb)->pkt_type;
	/* We should always send word aligned data to h4+ devices */
	if (skb->len % 2) {
		if (skb_pad(skb, 1))
			return -ENOMEM;
		*skb_put(skb, 1) = 0x00;
	}
	if (err)
		return err;

	skb_queue_tail(&info->txq, skb);
	if (!info->init_phase)
		h4p_enable_tx(info);
	else
		h4p_enable_tx_nopm(info);

	return 0;
}

static int h4p_probe_dt(struct platform_device *pdev, struct h4p_info *info)
{
	struct device_node *node;
	struct device_node *uart = pdev->dev.of_node;
	u32 val;
	struct resource *mem;

	node = of_get_child_by_name(uart, "device");

	if (!node)
		return -ENODATA;

	info->chip_type = 3;	/* Bcm2048 */

	if (of_property_read_u32(node, "bt-sysclk", &val))
		return -EINVAL;
	info->bt_sysclk = val;

	info->reset_gpio       = of_get_named_gpio(node, "reset-gpios", 0);
	info->host_wakeup_gpio = of_get_named_gpio(node, "host-wakeup-gpios", 0);
	info->bt_wakeup_gpio   = of_get_named_gpio(node, "bluetooth-wakeup-gpios", 0);

	if (!uart) {
		dev_err(&pdev->dev, "UART link not provided\n");
		return -EINVAL;
	}

	info->irq = irq_of_parse_and_map(uart, 0);

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->uart_base = devm_ioremap_resource(&pdev->dev, mem);

	info->uart_iclk = of_clk_get_by_name(uart, "ick");
	info->uart_fclk = of_clk_get_by_name(uart, "fck");

	BT_DBG("DT: have neccessary data");
	return 0;
}

static int h4p_probe(struct platform_device *pdev)
{
	struct hci_dev *hdev;
	struct h4p_info *info;
	int err;

	dev_info(&pdev->dev, "Registering HCI H4P device\n");
	info = devm_kzalloc(&pdev->dev, sizeof(struct h4p_info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->dev = &pdev->dev;
	info->tx_enabled = true;
	info->rx_enabled = true;
	spin_lock_init(&info->lock);
	spin_lock_init(&info->clocks_lock);
	skb_queue_head_init(&info->txq);

	err = h4p_probe_dt(pdev, info);
	if (err) {
		dev_err(&pdev->dev, "Could not get Bluetooth config data\n");
		return -ENODATA;
	}

	BT_DBG("base/irq gpio: %p/%d",
	       info->uart_base, info->irq);
	BT_DBG("RESET/BTWU/HOSTWU gpio: %d/%d/%d",
	       info->reset_gpio, info->bt_wakeup_gpio, info->host_wakeup_gpio);
	BT_DBG("chip type, sysclk: %d/%d", info->chip_type, info->bt_sysclk);
	BT_DBG("clock i/f: %p/%p", info->uart_iclk, info->uart_fclk);

	init_completion(&info->test_completion);
	complete_all(&info->test_completion);

	err = devm_gpio_request_one(&pdev->dev, info->reset_gpio,
				    GPIOF_OUT_INIT_LOW, "bt_reset");
	if (err < 0) {
		dev_err(&pdev->dev, "Cannot get GPIO line %d\n",
			info->reset_gpio);
		return err;
	}

	err = devm_gpio_request_one(&pdev->dev, info->bt_wakeup_gpio,
				    GPIOF_OUT_INIT_LOW, "bt_wakeup");
	if (err < 0) {
		dev_err(info->dev, "Cannot get GPIO line 0x%d",
			info->bt_wakeup_gpio);
		return err;
	}

	err = devm_gpio_request_one(&pdev->dev, info->host_wakeup_gpio,
				    GPIOF_DIR_IN, "host_wakeup");
	if (err < 0) {
		dev_err(info->dev, "Cannot get GPIO line %d",
		       info->host_wakeup_gpio);
		return err;
	}

	err = devm_request_irq(&pdev->dev, info->irq, h4p_interrupt,
				IRQF_DISABLED, "nokia_h4p", info);
	if (err < 0) {
		dev_err(info->dev, "nokia_h4p: unable to get IRQ %d\n",
			info->irq);
		return err;
	}

	err = devm_request_irq(&pdev->dev, gpio_to_irq(info->host_wakeup_gpio),
			       h4p_wakeup_interrupt,  IRQF_TRIGGER_FALLING |
			       IRQF_TRIGGER_RISING | IRQF_DISABLED,
			       "h4p_wkup", info);
	if (err < 0) {
		dev_err(info->dev, "nokia_h4p: unable to get wakeup IRQ %d\n",
			gpio_to_irq(info->host_wakeup_gpio));
		return err;
	}

	err = irq_set_irq_wake(gpio_to_irq(info->host_wakeup_gpio), 1);
	if (err < 0) {
		dev_err(info->dev, "nokia_h4p: unable to set wakeup for IRQ %d\n",
			gpio_to_irq(info->host_wakeup_gpio));
		return err;
	}

	init_timer_deferrable(&info->lazy_release);
	info->lazy_release.function = h4p_lazy_clock_release;
	info->lazy_release.data = (unsigned long)info;
	h4p_set_clk(info, &info->tx_clocks_en, 1);

	err = h4p_reset_uart(info);
	if (err < 0)
		return err;

	gpio_set_value(info->reset_gpio, 0);
	h4p_set_clk(info, &info->tx_clocks_en, 0);

	platform_set_drvdata(pdev, info);

	/* Initialize and register HCI device */

	hdev = hci_alloc_dev();
	if (!hdev) {
		dev_err(info->dev, "Can't allocate memory for device\n");
		return -ENOMEM;
	}
	info->hdev = hdev;

	hdev->bus = HCI_UART;
	hci_set_drvdata(hdev, info);

	hdev->open = h4p_hci_open;
	hdev->setup = h4p_hci_setup;
	hdev->close = h4p_hci_close;
	hdev->flush = h4p_hci_flush;
	hdev->send = h4p_hci_send_frame;
	hdev->set_bdaddr = h4p_hci_set_bdaddr;

	set_bit(HCI_QUIRK_INVALID_BDADDR, &hdev->quirks);
	SET_HCIDEV_DEV(hdev, info->dev);

	if (hci_register_dev(hdev) < 0)
		goto err;
	return h4p_boot(hdev);

err:
	dev_err(info->dev, "hci_register failed %s.\n", hdev->name);
	hci_free_dev(info->hdev);
	return -ENODEV;
}

static int h4p_remove(struct platform_device *pdev)
{
	struct h4p_info *info = platform_get_drvdata(pdev);

	h4p_hci_close(info->hdev);
	h4p_deinit(info->hdev);
	hci_unregister_dev(info->hdev);
	hci_free_dev(info->hdev);

	return 0;
}

static const struct of_device_id h4p_of_match[] = {
	{ .compatible = "brcm,uart,bcm2048" },
	{},
};
MODULE_DEVICE_TABLE(of, h4p_of_match);

static struct platform_driver h4p_driver = {
	.probe		= h4p_probe,
	.remove		= h4p_remove,
	.driver		= {
		.name	= "nokia_h4p",
		.owner  = THIS_MODULE,
		.of_match_table = of_match_ptr(h4p_of_match),
	},
};

module_platform_driver(h4p_driver);

MODULE_ALIAS("platform:nokia_h4p");
MODULE_DESCRIPTION("Bluetooth H4 driver with nokia extensions");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ville Tervo");
