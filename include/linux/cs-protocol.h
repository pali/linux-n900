/*
 *  cs-protocol.h
 *
 * Part of the CMT speech driver. Protocol definitions.
 *
 * Copyright (C) 2008,2009 Nokia Corporation. All rights reserved.
 *
 * Contact: Kai Vehmanen <kai.vehmanen@nokia.com>
 * Original author: Peter Ujfalusi <peter.ujfalusi@nokia.com>
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


#ifndef _CS_PROTOCOL_H
#define _CS_PROTOCOL_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* chardev parameters
 * ***********************************/

#define CS_DEV_FILE_NAME                "/dev/cmt_speech"

/* APE kernel <-> user space messages
 * ***********************************/

#define CS_CMD_SHIFT			28
#define CS_DOMAIN_SHIFT			24

#define CS_CMD_MASK			(0xff000000)
#define CS_PARAM_MASK			(0x00ffffff)

#define CS_CDSP_RESET_DONE		(0x1<<CS_CMD_SHIFT|0x0<<CS_DOMAIN_SHIFT)
#define CS_RX_DATA_RECEIVED 	        (0x2<<CS_CMD_SHIFT|0x0<<CS_DOMAIN_SHIFT)
#define CS_TX_DATA_READY		(0x3<<CS_CMD_SHIFT|0x0<<CS_DOMAIN_SHIFT)
#define CS_TX_DATA_SENT			(0x4<<CS_CMD_SHIFT|0x0<<CS_DOMAIN_SHIFT)

/* ioctl interface
 * ***************/

/* parameters to CS_CONFIG_BUGS ioctl */
#define CS_FEAT_TSTAMP_RX_CTRL          (1 << 0)

/* parameters to CS_GET_STATE ioctl */
#define CS_STATE_CLOSED		        0
#define CS_STATE_OPENED		        1 /* resource allocated */
#define CS_STATE_CONFIGURED	        2 /* data path active*/

#define CS_MAX_BUFFERS                  16 /* maximum number of TX/RX buffers */

/*
 * Parameters for setting up the data buffers
 */
struct cs_buffer_config {
	__u32 rx_bufs;  /* number of RX buffer slots */
	__u32 tx_bufs;  /* number of TX buffer slots */
	__u32 buf_size; /* bytes */
	__u32 flags;    /* see CS_FEAT_* */
	__u32 reserved[4];
};

/*
 * Struct describing the layout and contents of
 * the driver mmap area. This information is meant
 * as read-only information for the application.
 */
struct cs_mmap_config_block {
	__u16 version;   /* 8bit major, 8bit minor */
	__u16 reserved1;
	__u32 buf_size;  /* 0=disabled, otherwise the transfer size */
	__u32 rx_bufs;   /* # of RX buffers */
	__u32 tx_bufs;   /* # of TX buffers */
	__u32 reserved2;
	/* array of offsets within the mmap area for
	 * each RX and TX buffer */
	__u32 rx_offsets[CS_MAX_BUFFERS];
	__u32 tx_offsets[CS_MAX_BUFFERS];
	__u32 reserved3[4];
	/* if enabled with CS_FEAT_TSTAMP_RX_CTRL, monotonic
	 * timestamp taken when the last control command was
	 * received */
	struct timespec tstamp_rx_ctrl;
};

#define CS_IO_MAGIC           'C'

#define CS_IOW(num, dtype)    _IOW(CS_IO_MAGIC,  num, dtype)
#define CS_IOR(num, dtype)    _IOR(CS_IO_MAGIC,  num, dtype)
#define CS_IOWR(num, dtype)   _IOWR(CS_IO_MAGIC, num, dtype)
#define CS_IO(num)            _IO(CS_IO_MAGIC,   num)

#define CS_GET_STATE	      CS_IOR(21, unsigned int)
#define CS_CONFIG	      CS_IOW(22, struct cs_config)
#define CS_SET_WAKELINE	      CS_IOW(23, unsigned int)
#define CS_CONFIG_BUFS        CS_IOW(31, struct cs_buffer_config)

/* ioctl arguments (deprecated commands)
 * *************************************/

/** Deprecated V0.1.x interface (CS_CONFIG ioctl) */
struct cs_config {
	unsigned long tstamp_offset; /* valid, if:
					if (~timestamp_offset){} is true*/

	unsigned long dl_data_start_offset; /* offset from mmap base */
	unsigned int  dl_slots; /* number of dl slots */

	unsigned long ul_data_start_offset; /* offset from mmap base */
	unsigned int  ul_slots; /* number of ul slots */

	unsigned int  slot_size; /* bytes */
};

#endif /* _CS_PROTOCOL_H */
