/*
 * ssi_char.h
 *
 * Part of the SSI character device driver.
 *
 * Copyright (C) 2009 Nokia Corporation. All rights reserved.
 *
 * Contact: Andras Domokos <andras.domokos@nokia.com>
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


#ifndef SSI_CHAR_H
#define SSI_CHAR_H

#define SSI_CHAR_BASE		'S'
#define CS_IOW(num, dtype)	_IOW(SSI_CHAR_BASE, num, dtype)
#define CS_IOR(num, dtype)	_IOR(SSI_CHAR_BASE, num, dtype)
#define CS_IOWR(num, dtype)	_IOWR(SSI_CHAR_BASE, num, dtype)
#define CS_IO(num)		_IO(SSI_CHAR_BASE, num)

#define CS_SEND_BREAK		CS_IO(1)
#define CS_FLUSH_RX		CS_IO(2)
#define CS_FLUSH_TX		CS_IO(3)
#define CS_BOOTSTRAP		CS_IO(4)
#define CS_SET_WAKELINE		CS_IOW(5, unsigned int)
#define CS_GET_WAKELINE		CS_IOR(6, unsigned int)
#define CS_SET_RX		CS_IOW(7, struct ssi_rx_config)
#define CS_GET_RX		CS_IOW(8, struct ssi_rx_config)
#define CS_SET_TX		CS_IOW(9, struct ssi_tx_config)
#define CS_GET_TX		CS_IOW(10, struct ssi_tx_config)

#define SSI_MODE_SLEEP		0
#define SSI_MODE_STREAM		1
#define SSI_MODE_FRAME		2

#define SSI_ARBMODE_RR		0
#define SSI_ARBMODE_PRIO	1

#define WAKE_UP			0
#define WAKE_DOWN		1

struct ssi_tx_config {
	u32 mode;
	u32 frame_size;
	u32 channels;
	u32 divisor;
	u32 arb_mode;
};

struct ssi_rx_config {
	u32 mode;
	u32 frame_size;
	u32 channels;
	u32 timeout;
};

#endif /* SSI_CHAR_H */
