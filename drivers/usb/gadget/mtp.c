/*
 * mtp.c -- Media Transfer Protocol USB Gadget Driver
 *
 * Copyright (C) 2009 Nokia Corporation
 * Contact: Yauheni Kaliuta <yauheni.kaliuta@nokia.com>
 *
 * This gadget driver borrows from serial.c which is:
 *
 * Copyright (C) 2003 Al Borchers (alborchers@steinerpoint.com)
 * Copyright (C) 2008 by David Brownell
 * Copyright (C) 2008 by Nokia Corporation
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

#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/device.h>

#include <linux/usb/composite.h>
#include "u_char.h"
#include "gadget_chips.h"

/*-------------------------------------------------------------------------*/

/*
 * Kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */
#include "composite.c"
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"

#include "f_mtp.c"
#include "u_char.c"

/*-------------------------------------------------------------------------*/

#define MTP_VERSION_NUM			0x1000
#define MTP_VERSION_STR			"1.0"

#define MTP_VENDOR_ID			0x0101
#define MTP_PRODUCT_ID			0x0202

#define MTP_DRIVER_DESC			"MTP Gadget"

/* string IDs are assigned dynamically */

#define STRING_MANUFACTURER_IDX		0
#define STRING_PRODUCT_IDX		1
#define STRING_DESCRIPTION_IDX		2

static char manufacturer[50];

static struct usb_string strings_dev[] = {
	[STRING_MANUFACTURER_IDX].s = manufacturer,
	[STRING_PRODUCT_IDX].s = MTP_DRIVER_DESC,
	[STRING_DESCRIPTION_IDX].s = MTP_DRIVER_DESC,
	{  } /* end of list */
};

static struct usb_gadget_strings stringtab_dev = {
	.language	= 0x0409,	/* en-us */
	.strings	= strings_dev,
};

static struct usb_gadget_strings *dev_strings[] = {
	&stringtab_dev,
	NULL,
};

static struct usb_device_descriptor device_desc = {
	.bLength =		USB_DT_DEVICE_SIZE,
	.bDescriptorType =	USB_DT_DEVICE,
	.bcdUSB =		cpu_to_le16(0x0200),
	.bDeviceClass =		USB_CLASS_PER_INTERFACE,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	/* .bMaxPacketSize0 = f(hardware) */
	.idVendor =		cpu_to_le16(MTP_VENDOR_ID),
	.idProduct =		cpu_to_le16(MTP_PRODUCT_ID),
	/* .bcdDevice = f(hardware) */
	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
	.bNumConfigurations =	1,
};

static struct usb_otg_descriptor otg_descriptor = {
	.bLength =		sizeof otg_descriptor,
	.bDescriptorType =	USB_DT_OTG,

	/* REVISIT SRP-only hardware is possible, although
	 * it would not be called "OTG" ...
	 */
	.bmAttributes =		USB_OTG_SRP | USB_OTG_HNP,
};

static const struct usb_descriptor_header *otg_desc[] = {
	(struct usb_descriptor_header *) &otg_descriptor,
	NULL,
};

/*-------------------------------------------------------------------------*/

/* Module */
MODULE_DESCRIPTION(MTP_DRIVER_DESC);
MODULE_AUTHOR("Yauheni Kaliuta");
MODULE_LICENSE("GPL");

/*-------------------------------------------------------------------------*/

static int __init g_mtp_bind_config(struct usb_configuration *c)
{
	int status;

	status = mtp_bind_config(c, 0);
	if (status)
		printk(KERN_DEBUG "could not bind mtp config\n");

	return status;
}

static struct usb_configuration mtp_config_driver = {
	/* .label = f(use_acm) */
	.bind		= g_mtp_bind_config,
	.bConfigurationValue = 1,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes	= USB_CONFIG_ATT_SELFPOWER,
};

static int __init g_mtp_bind(struct usb_composite_dev *cdev)
{
	int			gcnum;
	struct usb_gadget	*gadget = cdev->gadget;
	int			status;

	status = gchar_setup(cdev->gadget, 1);
	if (status < 0)
		return status;

	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */

	/* device description: manufacturer, product */
	snprintf(manufacturer, sizeof manufacturer, "%s %s with %s",
		init_utsname()->sysname, init_utsname()->release,
		gadget->name);
	status = usb_string_id(cdev);
	if (status < 0)
		goto fail;
	strings_dev[STRING_MANUFACTURER_IDX].id = status;

	device_desc.iManufacturer = status;

	status = usb_string_id(cdev);
	if (status < 0)
		goto fail;
	strings_dev[STRING_PRODUCT_IDX].id = status;

	device_desc.iProduct = status;

	/* config description */
	status = usb_string_id(cdev);
	if (status < 0)
		goto fail;
	strings_dev[STRING_DESCRIPTION_IDX].id = status;

	mtp_config_driver.iConfiguration = status;

	/* set up other descriptors */
	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0)
		device_desc.bcdDevice = cpu_to_le16(MTP_VERSION_NUM | gcnum);
	else {
		/* this is so simple (for now, no altsettings) that it
		 * SHOULD NOT have problems with bulk-capable hardware.
		 * so warn about unrecognized controllers -- don't panic.
		 *
		 * things like configuration and altsetting numbering
		 * can need hardware-specific attention though.
		 */
		WARNING(cdev, "g_mtp_bind: controller '%s' not recognized\n",
			gadget->name);
		device_desc.bcdDevice =
			cpu_to_le16(MTP_VERSION_NUM | 0x0099);
	}

	if (gadget_is_otg(cdev->gadget)) {
		mtp_config_driver.descriptors = otg_desc;
		mtp_config_driver.bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}

	/* register our configuration */
	status = usb_add_config(cdev, &mtp_config_driver);
	if (status < 0)
		goto fail;

	DBG(cdev, "%s, version: " MTP_VERSION_STR "\n",
		 MTP_DRIVER_DESC);

	return 0;

fail:
	gchar_cleanup();
	return status;
}

static int __exit g_mtp_unbind(struct usb_composite_dev *cdev)
{
	gchar_cleanup();
	return 0;
}

static struct usb_composite_driver gmtp_driver = {
	.name		= "g_mtp",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.bind		= g_mtp_bind,
	.unbind		= __exit_p(g_mtp_unbind),
};

static int __init init(void)
{
	return usb_composite_register(&gmtp_driver);
}
module_init(init);

static void __exit cleanup(void)
{
	usb_composite_unregister(&gmtp_driver);
}
module_exit(cleanup);
