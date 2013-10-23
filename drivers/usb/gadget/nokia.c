/*
 * nokia.c -- Nokia Composite Gadget Driver
 *
 * Copyright (C) 2008 Nokia Corporation
 * Contact: Felipe Balbi <felipe.balbi@nokia.com>
 *
 * This gadget driver borrows from serial.c which is:
 *
 * Copyright (C) 2003 Al Borchers (alborchers@steinerpoint.com)
 * Copyright (C) 2008 by David Brownell
 * Copyright (C) 2008 by Nokia Corporation
 *
 * This software is distributed under the terms of the GNU General
 * Public License ("GPL") as published by the Free Software Foundation,
 * version 2 of that License.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/device.h>

#include "u_serial.h"
#include "u_ether.h"
#include "u_phonet.h"
#include "u_char.h"
#include "gadget_chips.h"

/* Defines */

#define NOKIA_VERSION_NUM		0x0211
#define NOKIA_LONG_NAME			"Sync Mode"

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

#define ACM_NO_IAD	/* We do not need IAD for ACM interface */

#include "f_mass_storage.c"
#include "u_serial.c"
#include "f_acm.c"
#include "f_ecm.c"
#include "f_obex.c"
#include "f_serial.c"
#include "f_phonet.c"
#include "f_mtp.c"
#include "u_ether.c"
#include "u_char.c"

/*-------------------------------------------------------------------------*/

#define NOKIA_VENDOR_ID			0x0421	/* Nokia */
#define N900_PRODUCT_ID			0x01c8	/* N900 Gadget */
#define RM696_PRODUCT_ID		0x051a	/* RM-696 */

/* string IDs are assigned dynamically */

#define STRING_MANUFACTURER_IDX		0
#define STRING_PRODUCT_IDX		1
#define STRING_DESCRIPTION_IDX		2
#define STRING_SERIALNUMBER_IDX		3

static unsigned use_mtp;
static bool connect = true;	/* Don't prevent connect by default */

module_param(use_mtp, uint, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(use_mtp, "whether to bind the MTP function driver");

module_param(connect, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(connect, "1 : connect to bus automatically (default), "
			  "0 : don't connect till requested");

static char manufacturer_nokia[] = "Nokia";
static const char product_nokia[] = NOKIA_LONG_NAME;
static const char description_nokia[] = "Sync Mode Configuration";
static char serialnumber_nokia[12];
static const char product_cdrom[] = "CD-ROM";

static struct usb_string strings_dev[] = {
	[STRING_MANUFACTURER_IDX].s = manufacturer_nokia,
	[STRING_PRODUCT_IDX].s = product_nokia,
	[STRING_DESCRIPTION_IDX].s = description_nokia,
	[STRING_SERIALNUMBER_IDX].s = serialnumber_nokia,
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

	/*
	 * Device supports Interface Association Descriptors
	 * http://www.usb.org/developers/defined_class/#BaseClassEFh
	 */
	.bDeviceClass		= USB_CLASS_MISC,	/* 0xEF */
	.bDeviceSubClass	= 0x02,
	.bDeviceProtocol	= 0x01,

	.idVendor		= __constant_cpu_to_le16(NOKIA_VENDOR_ID),
	.idProduct		= __constant_cpu_to_le16(N900_PRODUCT_ID),
	/* .iManufacturer = DYNAMIC */
	/* .iProduct = DYNAMIC */
	.bNumConfigurations =	1,
};

/*-------------------------------------------------------------------------*/

/* Module */
MODULE_DESCRIPTION("Nokia composite gadget driver");
MODULE_AUTHOR("Felipe Balbi");
MODULE_LICENSE("GPL");

/*-------------------------------------------------------------------------*/

static u8 hostaddr[ETH_ALEN];
static bool new_interface;	/* should we use new interface or not */

/*-------------------------------------------------------------------------*/
/* CDC IAD stuff */

/*
 * We need to add a CDC IAD which encapsulates all CDC interfaces.
 * The only way to do it with existing composite framework is to make
 * a dummy CDC function containing the IAD. We then call the cdc_bind_config
 * just before we bind the CDC interfaces.
 */
static struct usb_interface_assoc_descriptor
cdc_iad_desc = {
	.bLength 		= sizeof cdc_iad_desc,
	.bDescriptorType	= USB_DT_INTERFACE_ASSOCIATION,

	/* .bFirstInterface	= DYNAMIC, */
	/* .bInterfaceCount	= set later, */

/*
 * We use Still Image Class defines in IAD so Windows 7 detects
 * us even without Nokia Cable Connectivity Drivers (NCCD). The other
 * CDC interfaces nicely remain hidden to Windows user till NCCD is installed.
 */
	.bFunctionClass		= USB_CLASS_STILL_IMAGE,
	.bFunctionSubClass	= USB_SUBCLASS_PTP,
	.bFunctionProtocol	= USB_PROTOCOL_PTP,
};

/* same stuff for FS & HS */
static struct usb_descriptor_header *cdc_function[] __initdata = {
	(struct usb_descriptor_header *) &cdc_iad_desc,
	NULL,
};

static int
cdc_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	return 0;
}

static void
cdc_disable(struct usb_function *f) { }

static int
__init cdc_bind(struct usb_configuration *c, struct usb_function *f)
{
	/* we just need the next interface id and don't need to allocate
	 * a new interface because we are a dummy interface. So we don't
	 * call usb_interface_id() here.
	 */
	cdc_iad_desc.bFirstInterface = c->next_interface_id;
	cdc_iad_desc.iFunction = strings_dev[STRING_PRODUCT_IDX].id;

	/* put in the IAD */
	f->descriptors = usb_copy_descriptors(cdc_function);
	if (gadget_is_dualspeed(c->cdev->gadget))
		f->hs_descriptors = usb_copy_descriptors(cdc_function);

	return 0;
}

static void cdc_unbind(struct usb_configuration *c, struct usb_function *f)
{
	if (gadget_is_dualspeed(c->cdev->gadget))
		usb_free_descriptors(f->hs_descriptors);
	usb_free_descriptors(f->descriptors);
	kfree(f);
}

/**
 * cdc_bind_config() - Add CDC IAD to Configuration Descriptor
 * @c: usb configuration to which the IAD must be added
 * @num_intf : number of interfaces the CDC IAD should encapsulate
 *
 * Adds a dummy CDC IAD function to the Configuration. The end result is that
 * the necessary CDC IAD is added to the configuration descriptor when
 * the configuration is binded.
 *
 * Returns 0 on success and non zero on failure.
 */
static int __init cdc_bind_config(struct usb_configuration *c, int num_intf)
{
	struct usb_function *f;
	int status;

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;

	cdc_iad_desc.bInterfaceCount = num_intf;

	/* As this is a dummy function we only set bind & unbind.
	 * we can set the IAD descriptor in bind.
	 */
	f->name = description_nokia;
	f->bind = cdc_bind;
	f->unbind = cdc_unbind;

	/* we need these two to satisfy usb_add_func() */
	f->set_alt = cdc_set_alt;
	f->disable = cdc_disable;

	status = usb_add_function(c, f);
	if (status)
		kfree(f);

	return status;
}

/*-------------------------------------------------------------------------*/

/*---------- Mass storage interface for CD-ROM ----------------------------*/

static struct fsg_module_parameters mod_data = {
	.stall = 0,
	.cdrom = {1,},
	.file_count = 1,
	.luns = 1,
};

/* We only need to export the 'file' module parameter */
_FSG_MODULE_PARAM_ARRAY(/* no prefix */ , mod_data, file, charp,
	"name of backing file or device for CD-ROM emulation");


/* static unsigned long msg_registered = 0; */
static int msg_thread_exits(struct fsg_common *common)
{
	pr_debug("g_nokia: mass storage thread exited!!\n");
	return 0;
}

static struct fsg_common *msg_common;
static int __init msg_setup(struct usb_composite_dev *cdev)
{
	struct fsg_config config;
	static const struct fsg_operations ops = {
		.thread_exits = msg_thread_exits,
	};

	fsg_config_from_params(&config, &mod_data);
	config.ops = &ops;

	config.vendor_name = manufacturer_nokia;
	config.product_name = product_cdrom;

	msg_common = fsg_common_init(0, cdev, &config);
	if (IS_ERR(msg_common))
		return PTR_ERR(msg_common);

	return 0;
}

static inline void msg_cleanup(void)
{
	if (!IS_ERR(msg_common))
		fsg_common_release(&msg_common->ref);
}


/*-------------------------------------------------------------------------*/

static int __init nokia_bind_config(struct usb_configuration *c)
{
	int status = 0;

	/* Optional MTP interface was first on old interface */
	if (!new_interface && use_mtp) {
		status = mtp_bind_config(c, 0);
		if (status)
			printk(KERN_DEBUG "could not bind mtp config\n");
	}

	/* Nokia CDC Device with MTP, Phonet, obex0, obex1, ACM and ECM */

	/* CDC IAD */
	if (new_interface) {
		if (cdc_bind_config(c, use_mtp ? 11 : 10))
			printk(KERN_DEBUG "could not put CDC IAD\n");
	}

	/* MTP has to be first interface else we have problems with XP+WMP11 */
	if (new_interface && use_mtp) {
		status = mtp_bind_config(c, 0);
		if (status)
			printk(KERN_DEBUG "could not bind mtp config\n");
	}

	status = phonet_bind_config(c);
	if (status)
		printk(KERN_DEBUG "could not bind phonet config\n");

	status = obex_bind_config(c, 0);
	if (status)
		printk(KERN_DEBUG "could not bind obex config %d\n", 0);

	status = obex_bind_config(c, 1);
	if (status)
		printk(KERN_DEBUG "could not bind obex config %d\n", 0);

	status = acm_bind_config(c, 2);
	if (status)
		printk(KERN_DEBUG "could not bind acm config\n");

	status = ecm_bind_config(c, hostaddr);
	if (status)
		printk(KERN_DEBUG "could not bind ecm config\n");

	/*-------Nokia CDC function ends----------------*/

	/* CD-ROM */
	if (new_interface) {
		status = fsg_bind_config(c->cdev, c, msg_common);
		if (status)
			printk(KERN_INFO "could not bind CD-ROM interface\n");
	}

	return status;
}

static struct usb_configuration nokia_config_500ma_driver = {
	.label		= "Bus Powered",
	.bind		= nokia_bind_config,
	.bConfigurationValue = 1,
	/* .iConfiguration = DYNAMIC */
	.bmAttributes	= USB_CONFIG_ATT_ONE,
	.bMaxPower	= 250, /* 500mA */
};

static ssize_t
nokia_softconnect_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t n)
{
	int softcon;
	struct usb_gadget *gadget = container_of(dev, struct usb_gadget, dev);
	/* composite_bind sets gadget data to composite device */
	struct usb_composite_dev *cdev = get_gadget_data(gadget);

	if (sscanf(buf, "%d", &softcon) != 1
			|| (softcon != 1 && softcon != 0)) {
		dev_err(dev, "softconnect: Value must be 1 or 0\n");
		return -EINVAL;
	}

	if (softcon && !connect) {
		/* lets connect */
		connect = 1;
		usb_gadget_activate(cdev);
		dev_info(dev, "Configured to connect.\n");

	} else if (!softcon && connect) {
		/* lets disconnect */
		connect = 0;
		usb_gadget_deactivate(cdev);
		dev_info(dev, "Disconnected.\n");

	} else {
		dev_info(dev, "Already configured to%s connect. "
				"Not changing.\n", connect ? "" : " not");
	}

	return n;
}

static ssize_t
nokia_softconnect_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "%d\n", connect);
}

static DEVICE_ATTR(softconnect, 0644, nokia_softconnect_show,
		nokia_softconnect_store);

static int __init nokia_bind(struct usb_composite_dev *cdev)
{
	int			gcnum;
	struct usb_gadget	*gadget = cdev->gadget;
	int			status;
	int			i;

	/*
	 * If idProduct matches that of RM-696 then we use new behaviour
	 * i.e. IAD and CD-ROM emulation. Else, we fall back to old behaviour
	 *
	 * This is important because ENOS image uses g_nokia as built-into
	 * the kernel, and most likely they use Windows tools which won't work
	 * without a driver update.
	 *
	 */

	if (idProduct == RM696_PRODUCT_ID) {
		new_interface = true;
		pr_info("Using new descriptors & enabling CD-ROM interface\n");
	} else {
		new_interface = false;
	}

	if (new_interface) {
		device_desc.bDeviceClass	= USB_CLASS_MISC; /* 0xEF */
		device_desc.bDeviceSubClass	= 0x02;
		device_desc.bDeviceProtocol	= 0x01;
	} else {
		device_desc.bDeviceClass	= USB_CLASS_COMM;
		device_desc.bDeviceSubClass	= 0;
		device_desc.bDeviceProtocol	= 0;
	}

	if (!connect) {
		/*
		 * prevent enumeration till user space signals
		 * us via softconnect
		 */
		usb_gadget_deactivate(cdev);
		dev_info(&gadget->dev, "Enumeration prevented till "
					"user space requests to connect.\n");
	}

	status = msg_setup(cdev);
	if (status) {
		dev_err(&gadget->dev, "msg_setup failed\n");
		goto err_msg;
	}

	status = gchar_setup(cdev->gadget, 1);
	if (status < 0)
		goto err_char;

	status = gphonet_setup(cdev->gadget);
	if (status < 0)
		goto err_phonet;

	status = gserial_setup(cdev->gadget, 3);
	if (status < 0)
		goto err_serial;

	status = gether_setup(cdev->gadget, hostaddr);
	if (status < 0)
		goto err_ether;

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

	/* serial number.
	 *
	 * NOTE: a real product would change this to something
	 * meaningful, we are enconding the serial number from
	 * the NOKIA_LONG_NAME string
	 */
	for (i = 0; i < sizeof serialnumber_nokia - 2; i += 2) {
		unsigned char		c = NOKIA_LONG_NAME[i / 2];

		if (!c)
			break;
		sprintf(&serialnumber_nokia[i], "%02X", c);
	}

	status = usb_string_id(cdev);
	if (status < 0)
		goto err_usb;
	strings_dev[STRING_SERIALNUMBER_IDX].id = status;

	device_desc.iSerialNumber = status;
	/* config description */
	status = usb_string_id(cdev);
	if (status < 0)
		goto err_usb;
	strings_dev[STRING_DESCRIPTION_IDX].id = status;

	nokia_config_500ma_driver.iConfiguration = status;

	/* set up other descriptors */
	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0)
		device_desc.bcdDevice = cpu_to_le16(NOKIA_VERSION_NUM);
	else {
		/* this should only work with hw that supports altsettings
		 * and several endpoints, anything else, panic.
		 */
		pr_err("nokia_bind: controller '%s' not recognized\n",
			gadget->name);
		goto err_usb;
	}

	/* finaly register the configuration */
	status = usb_add_config(cdev, &nokia_config_500ma_driver);
	if (status < 0)
		goto err_usb;

	status = sysfs_create_file(&gadget->dev.kobj,
			&dev_attr_softconnect.attr);
	if (status)
		goto err_usb;

	dev_info(&gadget->dev, "%s\n", NOKIA_LONG_NAME);

	return 0;

err_usb:
	gether_cleanup();
err_ether:
	gserial_cleanup();
err_serial:
	gphonet_cleanup();
err_phonet:
	gchar_cleanup();
err_char:
	msg_cleanup();
err_msg:
	return status;
}

static int __exit nokia_unbind(struct usb_composite_dev *cdev)
{
	sysfs_remove_file(&cdev->gadget->dev.kobj, &dev_attr_softconnect.attr);
	msg_cleanup();
	gchar_cleanup();
	gphonet_cleanup();
	gserial_cleanup();
	gether_cleanup();

	return 0;
}

static struct usb_composite_driver nokia_driver = {
	.name		= "g_nokia",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.bind		= nokia_bind,
	.unbind		= __exit_p(nokia_unbind),
};

static int __init nokia_init(void)
{
	return usb_composite_register(&nokia_driver);
}
module_init(nokia_init);

static void __exit nokia_cleanup(void)
{
	usb_composite_unregister(&nokia_driver);
}
module_exit(nokia_cleanup);

