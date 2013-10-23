/*
 * ptp.h -- Picture Transfer Protocol definitions
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef __LINUX_USB_PTP_H
#define __LINUX_USB_PTP_H

#include <linux/types.h>

/* device or driver specific */
#define PTP_HS_DATA_PKT_SIZE	512
#define PTP_HS_EVENT_PKT_SIZE	64

#define PTP_FS_DATA_PKT_SIZE	64
#define PTP_FS_EVENT_PKT_SIZE	64

#define PTP_PACKET_LENGTH	16
#define PTP_MAX_STATUS_SIZE	64
#define PTP_MAX_CONTROL_SIZE	64

/* PTP USB Class codes */
#define USB_SUBCLASS_PTP	1
#define USB_PROTOCOL_PTP	1

/* PTP Response Codes */

#define PTP_RC_OK			0x2001
#define PTP_RC_DEVICE_BUSY		0x2019
#define PTP_RC_TRANSACTION_CANCELLED	0x201F

/**
 * PTP class specific requests
 */
#define PTP_REQ_CANCEL				0x64
#define PTP_REQ_GET_EXTENDED_EVENT_DATA		0x65
#define PTP_REQ_DEVICE_RESET			0x66
#define PTP_REQ_GET_DEVICE_STATUS		0x67

struct ptp_device_status_data {
	__le16 wLength;
	__le16 Code;
	__le32 Parameter1;
	__le32 Parameter2;
} __attribute__ ((packed));

struct ptp_cancel_data {
	__le16 CancellationCode;
	__le32 TransactionID;
} __attribute__ ((packed));


/* MTP IOCTLs */

#define MTP_IOCTL_BASE		0xF9
#define MTP_IO(nr)		_IO(MTP_IOCTL_BASE, nr)
#define MTP_IOR(nr, type)	_IOR(MTP_IOCTL_BASE, nr, type)
#define MTP_IOW(nr, type)	_IOW(MTP_IOCTL_BASE, nr, type)
#define MTP_IOWR(nr, type)	_IOWR(MTP_IOCTL_BASE, nr, type)


/* MTP_IOCTL_WRITE_ON_INTERRUPT_EP
 *
 * Write at max 64 bytes to MTP interrupt i.e. event endpoint
 */
#define MTP_IOCTL_WRITE_ON_INTERRUPT_EP		MTP_IOW(0, __u8[64])

/* Not yet Implemented
 *
 * #define MTP_IOCTL_DEVICE_STATUS		MTP_IOW(1, char *)
 * #define MTP_IOCTL_CANCEL_TXN			MTP_IOW(2, char *)
 *
 */

/* MTP_IOCTL_GET_MAX_DATAPKT_SIZE
 *
 * Return the max packet size of Data endpoint
 */
#define MTP_IOCTL_GET_MAX_DATAPKT_SIZE		MTP_IOR(3, __u32)

/* MTP_IOCTL_GET_MAX_EVENTPKT_SIZE
 *
 * Return the max packet size of Event endpoing
 */
#define MTP_IOCTL_GET_MAX_EVENTPKT_SIZE		MTP_IOR(4, __u32)

/* MTP_IOCTL_SET_DEVICE_STATUS
 *
 * Update drivers device status cache
 */
#define MTP_IOCTL_SET_DEVICE_STATUS	MTP_IOW(5, __u8[PTP_MAX_STATUS_SIZE])

/* MTP_IOCTL_GET_CONTROL_REQ
 *
 * Read the Class specific Control requests received on control endpoint
 */
#define MTP_IOCTL_GET_CONTROL_REQ	MTP_IOR(6, __u8[PTP_MAX_CONTROL_SIZE])

/* MTP_IOCTL_RESET_BUFFERS
 *
 * Clears the read and write buffers
 */
#define MTP_IOCTL_RESET_BUFFERS		MTP_IO(7)

#endif /* __LINUX_USB_PTP_H */
