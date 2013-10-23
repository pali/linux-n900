/*
 * Copyright (C) 2009 Nokia Corporation
 * Contact: Roger Quadros <roger.quadros@nokia.com>
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

#ifndef __U_CHAR_H
#define __U_CHAR_H

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/cdev.h>

struct gchar {
	struct usb_gadget		*gadget;
	struct usb_function		func;

	struct usb_ep			*ep_out;
	struct usb_ep			*ep_in;
	struct usb_endpoint_descriptor	*ep_out_desc;
	struct usb_endpoint_descriptor	*ep_in_desc;

	void (*open)(struct gchar *);
	void (*close)(struct gchar *);
	long (*ioctl)(struct gchar *, unsigned int, unsigned long);
};

int __init gchar_setup(struct usb_gadget *g, u8 devs_num);
void gchar_cleanup(void);
int gchar_connect(struct gchar *gchar, u8 num, const char *name);
void gchar_disconnect(struct gchar *gchar);
void gchar_notify(struct gchar *gchar);
void gchar_notify_clear(struct gchar *gchar);
void gchar_clear_fifos(struct gchar *gchar);
#endif
