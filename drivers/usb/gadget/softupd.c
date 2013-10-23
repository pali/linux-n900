/*
 * softupd.c -- Nokia Software Update Gadget
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

#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/device.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include "gadget_chips.h"

/* Defines */

#define NOKIA_VERSION_NUM		0x0100

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

#include "u_phonet.h"

#include "f_raw.c"
#include "f_phonet.c"

/*-------------------------------------------------------------------------*/

#define NOKIA_VENDOR_ID			0x0421	/* Nokia */
#define NOKIA_PRODUCT_ID		0x01c8	/* Nokia Update Gadget */

/* string IDs are assigned dynamically */

#define STRING_MANUFACTURER_IDX		0
#define STRING_PRODUCT_IDX		1
#define STRING_DESCRIPTION_IDX		2
#define STRING_SERIAL_IDX		3

static char manufacturer_nokia[] = "Nokia";
static const char product_nokia[] = "N900 (PC-Suite Mode)";
static const char description_nokia[] = "Firmware Upgrade Configuration";

static struct usb_string strings_dev[] = {
	[STRING_MANUFACTURER_IDX].s = manufacturer_nokia,
	[STRING_PRODUCT_IDX].s = product_nokia,
	[STRING_DESCRIPTION_IDX].s = description_nokia,
	[STRING_SERIAL_IDX].s = "",
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
	.bLength		= USB_DT_DEVICE_SIZE,
	.bDescriptorType	= USB_DT_DEVICE,

	.bcdUSB			= __constant_cpu_to_le16(0x0200),

	.bDeviceClass		= USB_CLASS_COMM,
	.idVendor		= __constant_cpu_to_le16(NOKIA_VENDOR_ID),
	.idProduct		= __constant_cpu_to_le16(NOKIA_PRODUCT_ID),

	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
	/* .iSerialNumber = DYNAMIC */

	.bNumConfigurations =	1,
};

/*-------------------------------------------------------------------------*/

/* Module */
MODULE_DESCRIPTION("Nokia Firmware Upgrade Gadget Driver");
MODULE_AUTHOR("Felipe Balbi");
MODULE_LICENSE("GPL");

/*-------------------------------------------------------------------------*/

static int __init softupd_bind_config(struct usb_configuration *c)
{
	int status = 0;

	status = phonet_bind_config(c);
	if (status) {
		struct usb_composite_dev *cdev = c->cdev;

		dev_err(&cdev->gadget->dev, "could not bind phonet config\n");
	}

	status = raw_bind_config(c);
	if (status)
		dev_err(&c->cdev->gadget->dev, "could not bind raw config\n");

	return status;
}

static struct usb_configuration softupd_config_driver = {
	.label		= "softupd",
	.bind		= softupd_bind_config,

	.bmAttributes	= USB_CONFIG_ATT_ONE,
	.bMaxPower	= 250,

	/* .iConfiguration = DYNAMIC */
	.bConfigurationValue = 1,
};

static int __init softupd_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget	*gadget = cdev->gadget;
	int			status;
	int			gcnum;

	status = gphonet_setup(cdev->gadget);
	if (status < 0)
		goto err_phonet;

	status = graw_setup(cdev->gadget);
	if (status < 0)
		goto err_raw;

	status = usb_string_id(cdev);
	if (status < 0)
		goto err_usb;
	strings_dev[STRING_MANUFACTURER_IDX].id = status;

	device_desc.iManufacturer = status;

	status = usb_string_id(cdev);
	if (status < 0)
		goto err_usb;
	strings_dev[STRING_PRODUCT_IDX].id = status;

	device_desc.iProduct = status;

	status = usb_string_id(cdev);
	if (status < 0)
		goto err_usb;
	strings_dev[STRING_SERIAL_IDX].id = status;

	device_desc.iSerialNumber = status;

	/* config description */
	status = usb_string_id(cdev);
	if (status < 0)
		goto err_usb;
	strings_dev[STRING_DESCRIPTION_IDX].id = status;

	softupd_config_driver.iConfiguration = status;

	/* set up other descriptors */
	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0)
		device_desc.bcdDevice = cpu_to_le16(NOKIA_VERSION_NUM);
	else {
		/* this should only work with hw that supports altsettings
		 * and several endpoints, anything else, panic.
		 */
		pr_err("%s: controller '%s' not recognized\n",
				__func__, gadget->name);
		goto err_usb;
	}

	/* finaly register the configuration */
	status = usb_add_config(cdev, &softupd_config_driver);
	if (status < 0)
		goto err_usb;

	INFO(cdev, "%s\n", product_nokia);

	return 0;

err_usb:
	graw_cleanup();

err_raw:
	gphonet_cleanup();

err_phonet:
	return status;
}

static int softupd_unbind(struct usb_composite_dev *cdev)
{
	graw_cleanup();
	gphonet_cleanup();

	return 0;
}

static struct usb_composite_driver softupd_driver = {
	.name		= "g_softupd",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.bind		= softupd_bind,
	.unbind		= softupd_unbind,
};

static int __init softupd_init(void)
{
	return usb_composite_register(&softupd_driver);
}
module_init(softupd_init);

static void __exit softupd_cleanup(void)
{
	usb_composite_unregister(&softupd_driver);
}
module_exit(softupd_cleanup);

