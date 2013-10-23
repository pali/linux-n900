/*
 * drivers/usb/core/otg_whitelist.h
 *
 * Copyright (C) 2004 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifdef	CONFIG_USB_OTG_WHITELIST
extern int is_targeted(struct usb_device *);
#else
static inline int is_targeted(struct usb_device *d)
{
	return 0;
}
#endif

