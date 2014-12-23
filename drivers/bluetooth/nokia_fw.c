/*
 * This file is part of nokia_h4p bluetooth driver
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
 */

#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/firmware.h>
#include <linux/clk.h>

#include <net/bluetooth/bluetooth.h>

#include "nokia_h4p.h"

#define FW_NAME_BCM2048		"nokia/bcmfw.bin"

/* Read fw. Return length of the command. If no more commands in
 * fw 0 is returned. In error case return value is negative.
 */
int h4p_read_fw(struct h4p_info *info)
{
	int num = 0;
	int fw_pos = 0;
	struct sk_buff *skb;
	const struct firmware *fw_entry = NULL;
	int err = -ENOENT;
	unsigned int cmd_len = 0;

	err = request_firmware(&fw_entry, FW_NAME_BCM2048, info->dev);
	if (err != 0)
		return err;

	while (1) {
		int cmd, len;

		fw_pos += cmd_len;

		if (fw_pos >= fw_entry->size)
			break;

		if (fw_pos + 2 > fw_entry->size) {
			dev_err(info->dev, "Corrupted firmware image\n");
			err = -EMSGSIZE;
			break;
		}

		cmd_len = fw_entry->data[fw_pos++];
		cmd_len += fw_entry->data[fw_pos++] << 8;
		if (cmd_len == 0)
			break;

		if (fw_pos + cmd_len > fw_entry->size) {
			dev_err(info->dev, "Corrupted firmware image\n");
			err = -EMSGSIZE;
			break;
		}

		/* Skip first two packets */
		if (++num <= 2)
			continue;

		/* Note that this is timing-critical. If sending packets takes too
		 * long, initialization will fail.
		 */
		cmd = fw_entry->data[fw_pos+1];
		cmd += fw_entry->data[fw_pos+2] << 8;
		len = fw_entry->data[fw_pos+3];

		skb = __hci_cmd_sync(info->hdev, cmd, len, fw_entry->data+fw_pos+4, 500);
		if (IS_ERR(skb)) {
			dev_err(info->dev, "...sending cmd %x len %d failed %ld\n",
				cmd, len, PTR_ERR(skb));
			err = -EIO;
			break;
		}
	}

	release_firmware(fw_entry);
	return err;
}

MODULE_FIRMWARE(FW_NAME_BCM2048);
