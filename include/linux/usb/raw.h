/*
 * raw.h -- USB Raw Access Header
 *
 * Copyright (C) 2009 Nokia Corporation
 * Contact: Felipe Balbi <felipe.balbi@nokia.com>
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

#ifndef __LINUX_USB_RAW_H
#define __LINUX_USB_RAW_H

#define MAX_REQUEST_LEN		(128 * 1024)
#define MAX_NR_REQUESTS		31

#define RAW_FIFO_STATUS		0x01
#define RAW_FIFO_FLUSH		0x02
#define RAW_CLEAR_HALT		0x03
#define RAW_ALLOC_REQUEST	0x10
#define RAW_QUEUE_REQUEST	0x11
#define RAW_FREE_REQUEST	0x12
#define RAW_GET_COMPLETION_MAP	0x13
#define RAW_GET_REQUEST_STATUS	0x14
#define RAW_STOP_REQUEST	0x15

struct raw_request_status {
	int			nr;
	int			status;
	unsigned int		nr_bytes;
};

struct raw_queue_request {
	int			nr;
	unsigned int		nr_bytes;
};

#endif /* __LINUX_USB_RAW_H */

