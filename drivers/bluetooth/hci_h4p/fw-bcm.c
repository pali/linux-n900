/*
 * This file is part of hci_h4p bluetooth driver
 *
 * Copyright (C) 2005-2008 Nokia Corporation.
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

#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/serial_reg.h>

#include "hci_h4p.h"

static struct sk_buff_head *fw_q;

static int inject_bdaddr(struct hci_h4p_info *info, struct sk_buff *skb)
{
	unsigned int offset;

	if (skb->len < 10) {
		dev_info(info->dev, "Valid bluetooth address not found.\n");
		return -ENODATA;
	}

	offset = 4;
	skb->data[offset + 5] = 0x00;
	skb->data[offset + 4] = 0x11;
	skb->data[offset + 3] = 0x22;
	skb->data[offset + 2] = 0x33;
	skb->data[offset + 1] = 0x44;
	skb->data[offset + 0] = 0x55;

	return 0;
}

void hci_h4p_bcm_parse_fw_event(struct hci_h4p_info *info, struct sk_buff *skb)
{
	struct sk_buff *fw_skb;
	int err;
	unsigned long flags;

	if (skb->data[5] != 0x00) {
		dev_err(info->dev, "Firmware sending command failed 0x%.2x\n",
			skb->data[5]);
		info->fw_error = -EPROTO;
	}

	kfree_skb(skb);

	fw_skb = skb_dequeue(fw_q);
	if (fw_skb == NULL || info->fw_error) {
		complete(&info->fw_completion);
		return;
	}

	if (fw_skb->data[1] == 0x01 && fw_skb->data[2] == 0xfc) {
		NBT_DBG_FW("Injecting bluetooth address\n");
		err = inject_bdaddr(info, fw_skb);
		if (err < 0) {
			kfree_skb(fw_skb);
			info->fw_error = err;
			complete(&info->fw_completion);
			return;
		}
	}

	skb_queue_tail(&info->txq, fw_skb);
	spin_lock_irqsave(&info->lock, flags);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) |
			UART_IER_THRI);
	spin_unlock_irqrestore(&info->lock, flags);
}


int hci_h4p_bcm_send_fw(struct hci_h4p_info *info,
			struct sk_buff_head *fw_queue)
{
	struct sk_buff *skb;
	unsigned long flags, time;

	info->fw_error = 0;

	NBT_DBG_FW("Sending firmware\n");

	time = jiffies;

	fw_q = fw_queue;
	skb = skb_dequeue(fw_queue);
	if (!skb)
		return -ENODATA;

	NBT_DBG_FW("Sending commands\n");

	/*
	 * Disable smart-idle as UART TX interrupts
	 * are not wake-up capable
	 */
	hci_h4p_smart_idle(info, 0);

	/* Check if this is bd_address packet */
	init_completion(&info->fw_completion);
	skb_queue_tail(&info->txq, skb);
	spin_lock_irqsave(&info->lock, flags);
	hci_h4p_outb(info, UART_IER, hci_h4p_inb(info, UART_IER) |
			UART_IER_THRI);
	spin_unlock_irqrestore(&info->lock, flags);

	if (!wait_for_completion_timeout(&info->fw_completion,
				msecs_to_jiffies(2000))) {
		dev_err(info->dev, "No reply to fw command\n");
		return -ETIMEDOUT;
	}

	if (info->fw_error) {
		dev_err(info->dev, "FW error\n");
		return -EPROTO;
	}

	NBT_DBG_FW("Firmware sent in %d msecs\n",
		   jiffies_to_msecs(jiffies-time));

	hci_h4p_set_auto_ctsrts(info, 0, UART_EFR_RTS);
	hci_h4p_set_rts(info, 0);
	hci_h4p_change_speed(info, BC4_MAX_BAUD_RATE);
	hci_h4p_set_auto_ctsrts(info, 1, UART_EFR_RTS);

	return 0;
}
