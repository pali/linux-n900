/*
 * f_raw.c -- USB Raw Access Function Driver
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

/* #define VERBOSE_DEBUG */

#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/wait.h>
#include <linux/list.h>
#include <linux/cdev.h>
#include <linux/poll.h>
#include <linux/dma-mapping.h>
#include <linux/uaccess.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/raw.h>

#include "gadget_chips.h"

struct graw {
	struct cdev			chdev;

	struct usb_gadget		*gadget;
	struct usb_function		func;

	unsigned			major;
	dev_t				dev;
};

static struct graw *the_graw;
static struct f_raw *the_raw;

struct raw_request {
	struct usb_request		*req;
	struct list_head		list;
	wait_queue_head_t		wait;
	unsigned long			len;

	unsigned			queued:1, completed:1;
	int				nr;
};

struct raw_ep_descs {
	struct usb_endpoint_descriptor	*raw_out;
};

struct f_raw {
	/* pool of read requests */
	struct list_head		read_pool;
	int				nr_reqs;

	/* synchronize with userland access */
	struct mutex			mutex;

	struct usb_ep			*out;
	struct raw_request		*allocated_req;
	struct class			*class;

	struct raw_ep_descs		fs;
	struct raw_ep_descs		hs;

	struct graw			graw;

	unsigned			vmas;
	unsigned			connected:1;
	unsigned			can_activate:1;

	u8				intf_id;
};

static inline struct f_raw *func_to_raw(struct usb_function *f)
{
	return container_of(f, struct f_raw, graw.func);
}

static u64 raw_dmamask = DMA_BIT_MASK(64);

/*-------------------------------------------------------------------------*/

#define RAW_INTF_IDX	1

static struct usb_string raw_string_defs[] = {
	[RAW_INTF_IDX].s	= "Device Upgrade Interface",
	{  },	/* end of list */
};

static struct usb_gadget_strings raw_string_table = {
	.language		= 0x0409,	/* en-US */
	.strings		= raw_string_defs,
};

static struct usb_gadget_strings *raw_strings[] = {
	&raw_string_table,
	NULL,
};

/*-------------------------------------------------------------------------*/

static struct usb_interface_descriptor raw_intf __initdata = {
	.bLength		= sizeof(raw_intf),
	.bDescriptorType	= USB_DT_INTERFACE,
	.bInterfaceNumber	= 0,

	.bAlternateSetting	= 0,
	.bNumEndpoints		= 1,
	.bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
};

/* High-Speed Support */

static struct usb_endpoint_descriptor raw_hs_ep_out_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,

	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize		= __constant_cpu_to_le16(512),
};

static struct usb_descriptor_header *hs_function[] __initdata = {
	(struct usb_descriptor_header *) &raw_intf,
	(struct usb_descriptor_header *) &raw_hs_ep_out_desc,
	NULL,
};

/* Full-Speed Support */

static struct usb_endpoint_descriptor raw_fs_ep_out_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,

	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_function[] __initdata = {
	(struct usb_descriptor_header *) &raw_intf,
	(struct usb_descriptor_header *) &raw_fs_ep_out_desc,
	NULL,
};

/*-------------------------------------------------------------------------*/

static void raw_complete(struct usb_ep *ep, struct usb_request *req);

static struct raw_request *raw_alloc_request(struct f_raw *raw, unsigned buflen)
{
	struct list_head	*pool = &raw->read_pool;
	struct usb_request	*req;
	struct raw_request	*raw_req;
	void			*buf;

	raw_req = kzalloc(sizeof(*raw_req), GFP_KERNEL);
	if (raw_req == NULL)
		goto fail1;

	INIT_LIST_HEAD(&raw_req->list);

	req = usb_ep_alloc_request(raw->out, GFP_KERNEL);
	if (req == NULL)
		goto fail2;

	req->length = buflen;
	req->complete = raw_complete;
	req->context = raw_req;

	buf = dma_alloc_coherent(&raw->graw.gadget->dev, buflen,
				 &req->dma, GFP_KERNEL);
	if (IS_ERR(buf))
		goto fail3;
	req->buf = buf;

	raw_req->req = req;
	raw_req->len = buflen;

	if (raw->nr_reqs == MAX_NR_REQUESTS)
		goto fail4;

	raw_req->nr = raw->nr_reqs;
	raw->nr_reqs++;
	list_add_tail(&raw_req->list, pool);

	return raw_req;

fail4:
	dma_free_coherent(&raw->graw.gadget->dev, buflen,
			  buf, req->dma);

fail3:
	usb_ep_free_request(raw->out, req);

fail2:
	kfree(raw_req);

fail1:
	return NULL;
}

static void raw_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_raw			*raw = ep->driver_data;
	struct raw_request		*raw_req = req->context;
	struct usb_composite_dev	*cdev = raw->graw.func.config->cdev;
	int				status = req->status;

	switch (status) {
	case 0:				/* normal completion */
		break;
	case -ECONNABORTED:		/* hardware forced ep reset */
	case -ECONNRESET:		/* request dequeued */
	case -ESHUTDOWN:		/* disconnected from host */
		VDBG(cdev, "%s gone (%d), %d/%d\n", ep->name, status,
				req->actual, req->length);
		return;
	case -EOVERFLOW:		/* not big enough buffer */
	default:
		DBG(cdev, "%s complete --> %d, %d/%d\n", ep->name,
				status, req->actual, req->length);
	case -EREMOTEIO:		/* short read */
		break;
	}

	raw_req->queued = 0;
	raw_req->completed = 1;
	wake_up_interruptible(&raw_req->wait);
}

static struct raw_request *find_request(struct f_raw *raw, int value)
{
	struct raw_request *req;

	list_for_each_entry(req, &raw->read_pool, list)
		if (req->nr == value)
			return req;

	return NULL;
}

static inline int enable_raw(struct usb_composite_dev *cdev, struct f_raw *raw)
{
	const struct usb_endpoint_descriptor	*out_desc;
	struct usb_ep				*ep;

	int					status = 0;

	/* choose endpoint */
	out_desc = ep_choose(cdev->gadget, &raw_hs_ep_out_desc,
			&raw_fs_ep_out_desc);

	/* enable it */
	ep = raw->out;
	status = usb_ep_enable(ep, out_desc);
	if (status < 0)
		return status;
	ep->driver_data = raw;

	DBG(cdev, "%s enabled\n", raw->graw.func.name);

	return 0;
}

static inline void disable_raw(struct f_raw *raw)
{
	struct usb_composite_dev	*cdev;
	struct usb_ep			*ep;

	int				status;

	cdev = raw->graw.func.config->cdev;

	ep = raw->out;
	if (ep->driver_data) {
		status = usb_ep_disable(ep);
		if (status < 0)
			DBG(cdev, "disable %s --> %d\n",
					ep->name, status);
		ep->driver_data = NULL;
	}

	VDBG(cdev, "%s disabled\n", raw->graw.func.name);
}

static int raw_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct usb_composite_dev	*cdev = f->config->cdev;
	struct f_raw			*raw = func_to_raw(f);

	/* we konw alt is zero */
	if (raw->out->driver_data)
		disable_raw(raw);

	return enable_raw(cdev, raw);
}

static void raw_disable(struct usb_function *f)
{
	struct f_raw	*raw = func_to_raw(f);

	disable_raw(raw);
}

static int raw_queue_request(struct f_raw *raw, struct raw_queue_request *qr)
{
	struct usb_ep		*ep = raw->out;
	struct raw_request	*raw_req;
	int			status = 0;

	raw_req = find_request(raw, qr->nr);
	if (raw_req == NULL)
		return -ENOENT;

	if (qr->nr_bytes > raw_req->len)
		return -EINVAL;

	/* FIXME: lock with irqsave and check if transfer already in progress,
	 * bail out if so. */

	raw_req->req->length = qr->nr_bytes;

	init_waitqueue_head(&raw_req->wait);
	raw_req->completed = 0;
	raw_req->queued = 1;
	status = usb_ep_queue(ep, raw_req->req, GFP_KERNEL);
	if (status) {
		struct usb_composite_dev	*cdev;

		cdev = raw->graw.func.config->cdev;
		ERROR(cdev, "start %s %s --> %d\n", "OUT", ep->name, status);
		raw_req->queued = 0;
	}

	return status;
}

static int raw_free_request(struct f_raw *raw, int nr)
{
	struct raw_request *raw_req;
	struct usb_request *req;

	raw_req = find_request(raw, nr);
	if (raw_req == NULL)
		return -ENOENT;

	if (raw->allocated_req == raw_req)
		raw->allocated_req = NULL;
	/* FIXME: munmap? */

	req = raw_req->req;
	/* FIXME: spinlocking? */
	if (raw_req->queued)
		usb_ep_dequeue(raw->out, req);
	raw_req->queued = 0;
	dma_free_coherent(&raw->graw.gadget->dev, raw_req->len, req->buf,
			req->dma);
	usb_ep_free_request(raw->out, req);
	list_del(&raw_req->list);
	kfree(raw_req);

	return 0;
}

static int raw_get_request_status(struct f_raw *raw,
				  struct raw_request_status *st)
{
	struct raw_request	*raw_req;

	raw_req = find_request(raw, st->nr);
	if (raw_req == NULL)
		return -ENOENT;

	if (!raw_req->queued) {
		st->status = raw_req->req->status;
		st->nr_bytes = raw_req->req->actual;
		raw_req->completed = 0;
	} else {
		st->status = -EBUSY;
		st->nr_bytes = 0;
	}

	return 0;
}

static void get_completion_map(struct f_raw *raw, unsigned int *mask_out)
{
	struct raw_request *req;
	unsigned int mask = 0;

	list_for_each_entry(req, &raw->read_pool, list)
		if (req->completed)
			mask |= (1 << req->nr);

	*mask_out = mask;
}

static long fraw_ioctl(struct file *filp, unsigned code, unsigned long value)
{
	struct f_raw			*raw = filp->private_data;
	struct usb_ep			*ep = raw->out;
	unsigned int			map;
	int				status = 0;
	struct raw_request_status	req_st;
	struct raw_queue_request	que_req;

	if (unlikely(!ep))
		return -EINVAL;

	mutex_lock(&raw->mutex);

	switch (code) {
	case RAW_FIFO_STATUS:
		status = usb_ep_fifo_status(ep);
		break;
	case RAW_FIFO_FLUSH:
		usb_ep_fifo_flush(ep);
		break;
	case RAW_CLEAR_HALT:
		status = usb_ep_clear_halt(ep);
		break;
	case RAW_ALLOC_REQUEST:
		if (raw->allocated_req != NULL) {
			status = -EBUSY;
			break;
		}
		if (value > MAX_REQUEST_LEN || (value % PAGE_SIZE) != 0) {
			status = -EINVAL;
			break;
		}
		raw->allocated_req = raw_alloc_request(raw, value);
		if (raw->allocated_req == NULL) {
			status = -ENOMEM;
			break;
		}
		status = raw->allocated_req->nr;
		break;
	case RAW_QUEUE_REQUEST:
		status = copy_from_user(&que_req, (void __user *) value,
					sizeof(que_req));
		if (status)
			break;
		status = raw_queue_request(raw, &que_req);
		break;
	case RAW_FREE_REQUEST:
		status = raw_free_request(raw, value);
		break;
	case RAW_GET_COMPLETION_MAP:
		get_completion_map(raw, &map);
		status = put_user(map, (unsigned int __user *) value);
		break;
	case RAW_GET_REQUEST_STATUS:
		status = copy_from_user(&req_st, (void __user *) value,
					sizeof(req_st));
		if (status)
			break;
		status = raw_get_request_status(raw, &req_st);
		if (status)
			break;
		status = copy_to_user((void __user *) value, &req_st,
				      sizeof(req_st));
	}

	mutex_unlock(&raw->mutex);

	return status;
}

static int fraw_mmap(struct file *filp, struct vm_area_struct *vma)
{
	size_t			size = vma->vm_end - vma->vm_start;
	struct f_raw		*raw = filp->private_data;
	struct raw_request	*raw_req;
	struct usb_request	*req;
	int			ret;

	mutex_lock(&raw->mutex);
	raw_req = raw->allocated_req;
	if (raw_req == NULL) {
		ret = -ENXIO;
		goto out;
	}
	req = raw_req->req;

	if (size != raw_req->len) {
		ret = -EINVAL;
		goto out;
	}

	vma->vm_private_data = raw;

	ret = dma_mmap_coherent(&raw->graw.gadget->dev, vma, req->buf,
				req->dma, raw_req->len);
	if (ret < 0)
		goto out;

	raw->allocated_req = NULL;

out:
	mutex_unlock(&raw->mutex);

	return 0;
}

static int fraw_open(struct inode *inode, struct file *filp)
{
	struct f_raw			*raw;

	raw = the_raw;
	filp->private_data = the_raw;

	return 0;
}

static int fraw_release(struct inode *inode, struct file *filp)
{
	struct f_raw			*raw = filp->private_data;

	while (!list_empty(&raw->read_pool)) {
		struct raw_request *req;

		req = list_first_entry(&raw->read_pool, struct raw_request,
				       list);
		raw_free_request(raw, req->nr);
	}
	raw->nr_reqs = 0;
	filp->private_data = NULL;

	return 0;
}

static unsigned int fraw_poll(struct file *filp, struct poll_table_struct *pt)
{
	struct f_raw			*raw = filp->private_data;
	struct raw_request		*req;
	int				ret = 0;

	mutex_lock(&raw->mutex);
	list_for_each_entry(req, &raw->read_pool, list) {
		poll_wait(filp, &req->wait, pt);

		if (req->completed) {
			ret = POLLIN | POLLRDNORM;
			break;
		}
	}
	mutex_unlock(&raw->mutex);

	return ret;
}

static struct file_operations fraw_fops = {
	.owner		= THIS_MODULE,
	.open		= fraw_open,
	.release	= fraw_release,
	.unlocked_ioctl	= fraw_ioctl,
	.mmap		= fraw_mmap,
	.poll		= fraw_poll,
};

/*-------------------------------------------------------------------------*/

static int __init raw_bind(struct usb_configuration *c, struct usb_function *f)
{
	struct usb_composite_dev	*cdev = c->cdev;
	struct f_raw			*raw = func_to_raw(f);
	struct usb_ep			*ep;

	int				status;

	/* allocate instance-specific interface IDs and patch descriptors */

	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	raw->intf_id = status;

	raw_intf.bInterfaceNumber = status;

	/* allocate instance-specific endpoints */

	ep = usb_ep_autoconfig(cdev->gadget, &raw_fs_ep_out_desc);
	if (!ep)
		goto fail;
	raw->out = ep;
	ep->driver_data = cdev; /* claim */

	/* copy descriptors and track endpoint copies */
	f->descriptors = usb_copy_descriptors(fs_function);

	raw->fs.raw_out = usb_find_endpoint(fs_function,
			f->descriptors, &raw_fs_ep_out_desc);

	/* support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */
	if (gadget_is_dualspeed(c->cdev->gadget)) {
		raw_hs_ep_out_desc.bEndpointAddress =
			raw_fs_ep_out_desc.bEndpointAddress;

		/* copy descriptors and track endpoint copies */
		f->hs_descriptors = usb_copy_descriptors(hs_function);

		raw->hs.raw_out = usb_find_endpoint(hs_function,
				f->hs_descriptors, &raw_hs_ep_out_desc);
	}

	INIT_LIST_HEAD(&raw->read_pool);
	mutex_init(&raw->mutex);

	/* create device nodes */
	raw->class = class_create(THIS_MODULE, "fraw");
	device_create(raw->class, &cdev->gadget->dev,
			MKDEV(raw->graw.major, 0), raw, "%s", f->name);

	cdev->gadget->dev.dma_mask = &raw_dmamask;
	cdev->gadget->dev.coherent_dma_mask = DMA_64BIT_MASK;
	raw->graw.gadget = cdev->gadget;
	the_raw = raw;

	DBG(cdev, "raw: %s speed OUT/%s\n",
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			raw->out->name);

	return 0;

fail:
	if (raw->class)
		class_destroy(raw->class);

	if (raw->out)
		raw->out->driver_data = NULL;

	ERROR(cdev, "%s/%p: can't bind, err %d\n", f->name, f, status);

	return status;
}

static void raw_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_raw *raw = func_to_raw(f);

	if (gadget_is_dualspeed(c->cdev->gadget))
		usb_free_descriptors(f->hs_descriptors);
	usb_free_descriptors(f->descriptors);
	device_destroy(raw->class, MKDEV(raw->graw.major, 0));
	class_destroy(raw->class);
	kfree(raw);
}

/**
 * raw_bind_config - add a RAW function to a configuration
 * @c: the configuration to support the RAW instance
 * Context: single threaded during gadget setup
 *
 * Returns zero on success, else negative errno.
 */
static int __init raw_bind_config(struct usb_configuration *c)
{
	struct f_raw	*raw;
	int		status;

	if (raw_string_defs[RAW_INTF_IDX].id == 0) {
		status = usb_string_id(c->cdev);
		if (status < 0)
			return status;

		raw_string_defs[RAW_INTF_IDX].id = status;
		raw_intf.iInterface = status;
	}

	/* allocate and initialize one new instance */
	raw = kzalloc(sizeof(*raw), GFP_KERNEL);
	if (!raw)
		return -ENOMEM;

	raw->graw.func.name	= "raw";
	raw->graw.func.strings	= raw_strings;
	/* descriptors are per-instance copies */
	raw->graw.func.bind	= raw_bind;
	raw->graw.func.unbind	= raw_unbind;
	raw->graw.func.set_alt	= raw_set_alt;
	raw->graw.func.disable	= raw_disable;

	status = usb_add_function(c, &raw->graw.func);
	if (status)
		kfree(raw);

	return status;
}

/**
 * graw_setup - initialize character driver for one rx
 * @g: gadget to associate with
 * Contex: may sleep
 *
 * Returns negative errno or zero.
 */
static int __init graw_setup(struct usb_gadget *g)
{
	struct graw	*graw;

	int		status;
	int		major;

	dev_t		dev;

	if (the_graw)
		return -EBUSY;

	graw = kzalloc(sizeof(*graw), GFP_KERNEL);
	if (!graw) {
		status = -ENOMEM;
		goto fail1;
	}

	status = alloc_chrdev_region(&dev, 0, 1, "fraw");
	if (status)
		goto fail2;

	major = MAJOR(dev);

	cdev_init(&graw->chdev, &fraw_fops);
	graw->chdev.owner	= THIS_MODULE;
	graw->dev		= dev;
	graw->major		= major;

	status = cdev_add(&graw->chdev, dev, 1);
	if (status)
		goto fail3;

	the_graw = graw;

	return 0;

fail3:
	/* cdev_put(&graw->cdev); */
	unregister_chrdev_region(graw->dev, 1);

fail2:
	kfree(graw);

fail1:
	return status;
}

static void __exit graw_cleanup(void)
{
	struct graw	*graw = the_graw;

	if (!graw)
		return;

	cdev_del(&graw->chdev);
	/* cdev_put(&graw->chdev); */
	unregister_chrdev_region(graw->dev, 1);
	kfree(graw);
}

