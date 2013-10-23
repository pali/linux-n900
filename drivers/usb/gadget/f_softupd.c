/*
 * f_softupd.c -- USB Raw Access Function Driver
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
#include "softupd.h"

#include "gadget_chips.h"

struct gsoftupd {
	struct cdev			chdev;
	struct class			*class;

	struct usb_gadget		*gadget;
	struct usb_function		func;

	unsigned			major;
	dev_t				dev;
};

static struct gsoftupd *the_gsoftupd;
static struct f_softupd *the_softupd;

struct softupd_request {
	struct usb_request		*req;
	struct list_head		list;
	wait_queue_head_t		wait;
	unsigned long			len;

	unsigned			queued:1, completed:1;
	int				nr;
};

struct softupd_ep_descs {
	struct usb_endpoint_descriptor	*softupd_out;
};

struct f_softupd {
	/* pool of read requests */
	struct list_head		read_pool;
	int				nr_reqs;

	/* synchronize with userland access */
	struct mutex			mutex;

	struct usb_ep			*out;
	struct softupd_request		*allocated_req;

	struct softupd_ep_descs		fs;
	struct softupd_ep_descs		hs;

	struct gsoftupd			gsoftupd;

	unsigned			vmas;
	unsigned			connected:1;
	unsigned			can_activate:1;

	u8				intf_id;
};

static inline struct f_softupd *func_to_softupd(struct usb_function *f)
{
	return container_of(f, struct f_softupd, gsoftupd.func);
}

static u64 softupd_dmamask = DMA_BIT_MASK(64);

/*-------------------------------------------------------------------------*/

#define RAW_INTF_IDX	0

static struct usb_string softupd_string_defs[] = {
	[RAW_INTF_IDX].s	= "Device Upgrade Interface",
	{  },	/* end of list */
};

static struct usb_gadget_strings softupd_string_table = {
	.language		= 0x0409,	/* en-US */
	.strings		= softupd_string_defs,
};

static struct usb_gadget_strings *softupd_strings[] = {
	&softupd_string_table,
	NULL,
};

/*-------------------------------------------------------------------------*/

static struct usb_interface_descriptor softupd_intf __initdata = {
	.bLength		= sizeof(softupd_intf),
	.bDescriptorType	= USB_DT_INTERFACE,
	.bInterfaceNumber	= 0,

	.bAlternateSetting	= 0,
	.bNumEndpoints		= 1,
	.bInterfaceClass	= USB_CLASS_VENDOR_SPEC,
};

/* High-Speed Support */

static struct usb_endpoint_descriptor softupd_hs_ep_out_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,

	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
	.wMaxPacketSize		= __constant_cpu_to_le16(512),
};

static struct usb_descriptor_header *hs_function[] __initdata = {
	(struct usb_descriptor_header *) &softupd_intf,
	(struct usb_descriptor_header *) &softupd_hs_ep_out_desc,
	NULL,
};

/* Full-Speed Support */

static struct usb_endpoint_descriptor softupd_fs_ep_out_desc = {
	.bLength		= USB_DT_ENDPOINT_SIZE,
	.bDescriptorType	= USB_DT_ENDPOINT,

	.bEndpointAddress	= USB_DIR_OUT,
	.bmAttributes		= USB_ENDPOINT_XFER_BULK,
};

static struct usb_descriptor_header *fs_function[] __initdata = {
	(struct usb_descriptor_header *) &softupd_intf,
	(struct usb_descriptor_header *) &softupd_fs_ep_out_desc,
	NULL,
};

/*-------------------------------------------------------------------------*/

static void softupd_complete(struct usb_ep *ep, struct usb_request *req);

static struct softupd_request *softupd_alloc_request(struct f_softupd *softupd,
						     unsigned buflen)
{
	struct list_head	*pool = &softupd->read_pool;
	struct usb_request	*req;
	struct softupd_request	*softupd_req;
	void			*buf;

	softupd_req = kzalloc(sizeof(*softupd_req), GFP_KERNEL);
	if (softupd_req == NULL)
		goto fail1;

	INIT_LIST_HEAD(&softupd_req->list);
	init_waitqueue_head(&softupd_req->wait);

	req = usb_ep_alloc_request(softupd->out, GFP_KERNEL);
	if (req == NULL)
		goto fail2;

	req->length = buflen;
	req->complete = softupd_complete;
	req->context = softupd_req;

	buf = dma_alloc_coherent(&softupd->gsoftupd.gadget->dev, buflen,
				 &req->dma, GFP_KERNEL);
	if (IS_ERR(buf))
		goto fail3;
	req->buf = buf;

	softupd_req->req = req;
	softupd_req->len = buflen;

	if (softupd->nr_reqs == MAX_NR_REQUESTS)
		goto fail4;

	softupd_req->nr = softupd->nr_reqs;
	softupd->nr_reqs++;
	list_add_tail(&softupd_req->list, pool);

	return softupd_req;

fail4:
	dma_free_coherent(&softupd->gsoftupd.gadget->dev, buflen,
			  buf, req->dma);

fail3:
	usb_ep_free_request(softupd->out, req);

fail2:
	kfree(softupd_req);

fail1:
	return NULL;
}

static void softupd_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct f_softupd		*softupd = ep->driver_data;
	struct softupd_request		*softupd_req = req->context;
	struct usb_composite_dev	*cdev;
	int				status = req->status;

	cdev = softupd->gsoftupd.func.config->cdev;
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

	softupd_req->queued = 0;
	softupd_req->completed = 1;
	wake_up_interruptible(&softupd_req->wait);
}

static struct softupd_request *find_request(struct f_softupd *softupd,
					    int value)
{
	struct softupd_request *req;

	list_for_each_entry(req, &softupd->read_pool, list)
		if (req->nr == value)
			return req;

	return NULL;
}

static inline int enable_softupd(struct usb_composite_dev *cdev,
				 struct f_softupd *softupd)
{
	const struct usb_endpoint_descriptor	*out_desc;
	struct usb_ep				*ep;

	int					status = 0;

	/* choose endpoint */
	out_desc = ep_choose(cdev->gadget, &softupd_hs_ep_out_desc,
			&softupd_fs_ep_out_desc);

	/* enable it */
	ep = softupd->out;
	status = usb_ep_enable(ep, out_desc);
	if (status < 0)
		return status;
	ep->driver_data = softupd;

	DBG(cdev, "%s enabled\n", softupd->gsoftupd.func.name);

	return 0;
}

static inline void disable_softupd(struct f_softupd *softupd)
{
	struct usb_composite_dev	*cdev;
	struct usb_ep			*ep;

	int				status;

	cdev = softupd->gsoftupd.func.config->cdev;

	ep = softupd->out;
	if (ep->driver_data) {
		status = usb_ep_disable(ep);
		if (status < 0)
			DBG(cdev, "disable %s --> %d\n",
					ep->name, status);
		ep->driver_data = NULL;
	}

	VDBG(cdev, "%s disabled\n", softupd->gsoftupd.func.name);
}

static int gsoftupd_set_alt(struct usb_function *f, unsigned intf, unsigned alt)
{
	struct usb_composite_dev	*cdev = f->config->cdev;
	struct f_softupd		*softupd = func_to_softupd(f);

	/* we konw alt is zero */
	if (softupd->out->driver_data)
		disable_softupd(softupd);

	return enable_softupd(cdev, softupd);
}

static void gsoftupd_disable(struct usb_function *f)
{
	struct f_softupd	*softupd = func_to_softupd(f);

	disable_softupd(softupd);
}

static int softupd_queue_request(struct f_softupd *softupd,
				 struct softupd_queue_request *qr)
{
	struct usb_ep		*ep = softupd->out;
	struct softupd_request	*softupd_req;
	int			status = 0;

	softupd_req = find_request(softupd, qr->nr);
	if (softupd_req == NULL)
		return -ENOENT;

	if (qr->nr_bytes > softupd_req->len)
		return -EINVAL;

	/* FIXME: lock with irqsave and check if transfer already in progress,
	 * bail out if so. */

	softupd_req->req->length = qr->nr_bytes;

	softupd_req->completed = 0;
	softupd_req->queued = 1;
	status = usb_ep_queue(ep, softupd_req->req, GFP_KERNEL);
	if (status) {
		struct usb_composite_dev	*cdev;

		cdev = softupd->gsoftupd.func.config->cdev;
		ERROR(cdev, "start %s %s --> %d\n", "OUT", ep->name, status);
		softupd_req->queued = 0;
	}

	return status;
}

static int softupd_free_request(struct f_softupd *softupd, int nr)
{
	struct softupd_request *softupd_req;
	struct usb_request *req;

	softupd_req = find_request(softupd, nr);
	if (softupd_req == NULL)
		return -ENOENT;

	if (softupd->allocated_req == softupd_req)
		softupd->allocated_req = NULL;
	/* FIXME: munmap? */

	req = softupd_req->req;
	/* FIXME: spinlocking? */
	if (softupd_req->queued)
		usb_ep_dequeue(softupd->out, req);
	softupd_req->queued = 0;
	dma_free_coherent(&softupd->gsoftupd.gadget->dev, softupd_req->len,
			  req->buf, req->dma);
	usb_ep_free_request(softupd->out, req);
	list_del(&softupd_req->list);
	kfree(softupd_req);

	return 0;
}

static int softupd_get_request_status(struct f_softupd *softupd,
				  struct softupd_request_status *st)
{
	struct softupd_request	*softupd_req;

	softupd_req = find_request(softupd, st->nr);
	if (softupd_req == NULL)
		return -ENOENT;

	if (!softupd_req->queued) {
		st->status = softupd_req->req->status;
		st->nr_bytes = softupd_req->req->actual;
		softupd_req->completed = 0;
	} else {
		st->status = -EBUSY;
		st->nr_bytes = 0;
	}

	return 0;
}

static void get_completion_map(struct f_softupd *softupd,
			       unsigned int *mask_out)
{
	struct softupd_request *req;
	unsigned int mask = 0;

	list_for_each_entry(req, &softupd->read_pool, list)
		if (req->completed)
			mask |= (1 << req->nr);

	*mask_out = mask;
}

static long fsoftupd_ioctl(struct file *filp, unsigned code,
			   unsigned long value)
{
	struct f_softupd		*softupd = filp->private_data;
	struct usb_ep			*ep = softupd->out;
	unsigned int			map;
	int				status = 0;
	struct softupd_request_status	req_st;
	struct softupd_queue_request	que_req;

	if (unlikely(!ep))
		return -EINVAL;

	mutex_lock(&softupd->mutex);

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
		if (softupd->allocated_req != NULL) {
			status = -EBUSY;
			break;
		}
		if (value > MAX_REQUEST_LEN || (value % PAGE_SIZE) != 0) {
			status = -EINVAL;
			break;
		}
		softupd->allocated_req = softupd_alloc_request(softupd, value);
		if (softupd->allocated_req == NULL) {
			status = -ENOMEM;
			break;
		}
		status = softupd->allocated_req->nr;
		break;
	case RAW_QUEUE_REQUEST:
		status = copy_from_user(&que_req, (void __user *) value,
					sizeof(que_req));
		if (status)
			break;
		status = softupd_queue_request(softupd, &que_req);
		break;
	case RAW_FREE_REQUEST:
		status = softupd_free_request(softupd, value);
		break;
	case RAW_GET_COMPLETION_MAP:
		get_completion_map(softupd, &map);
		status = put_user(map, (unsigned int __user *) value);
		break;
	case RAW_GET_REQUEST_STATUS:
		status = copy_from_user(&req_st, (void __user *) value,
					sizeof(req_st));
		if (status)
			break;
		status = softupd_get_request_status(softupd, &req_st);
		if (status)
			break;
		status = copy_to_user((void __user *) value, &req_st,
				      sizeof(req_st));
	}

	mutex_unlock(&softupd->mutex);

	return status;
}

static int fsoftupd_mmap(struct file *filp, struct vm_area_struct *vma)
{
	size_t			size = vma->vm_end - vma->vm_start;
	struct f_softupd	*softupd = filp->private_data;
	struct softupd_request	*softupd_req;
	struct usb_request	*req;
	int			ret;

	mutex_lock(&softupd->mutex);
	softupd_req = softupd->allocated_req;
	if (softupd_req == NULL) {
		ret = -ENXIO;
		goto out;
	}
	req = softupd_req->req;

	if (size != softupd_req->len) {
		ret = -EINVAL;
		goto out;
	}

	vma->vm_private_data = softupd;

	ret = dma_mmap_coherent(&softupd->gsoftupd.gadget->dev, vma, req->buf,
				req->dma, softupd_req->len);
	if (ret < 0)
		goto out;

	softupd->allocated_req = NULL;

out:
	mutex_unlock(&softupd->mutex);

	return 0;
}

static int fsoftupd_open(struct inode *inode, struct file *filp)
{
	struct f_softupd	*softupd;

	softupd = the_softupd;
	filp->private_data = the_softupd;

	return 0;
}

static int fsoftupd_release(struct inode *inode, struct file *filp)
{
	struct f_softupd	*softupd = filp->private_data;

	while (!list_empty(&softupd->read_pool)) {
		struct softupd_request *req;

		req = list_first_entry(&softupd->read_pool,
				       struct softupd_request, list);
		softupd_free_request(softupd, req->nr);
	}
	softupd->nr_reqs = 0;
	filp->private_data = NULL;

	return 0;
}

static unsigned int fsoftupd_poll(struct file *filp,
				  struct poll_table_struct *pt)
{
	struct f_softupd		*softupd = filp->private_data;
	struct softupd_request		*req;
	int				ret = 0;

	mutex_lock(&softupd->mutex);
	list_for_each_entry(req, &softupd->read_pool, list) {
		poll_wait(filp, &req->wait, pt);

		if (req->completed) {
			ret = POLLIN | POLLRDNORM;
			break;
		}
	}
	mutex_unlock(&softupd->mutex);

	return ret;
}

static const struct file_operations fsoftupd_fops = {
	.owner		= THIS_MODULE,
	.open		= fsoftupd_open,
	.release	= fsoftupd_release,
	.unlocked_ioctl	= fsoftupd_ioctl,
	.mmap		= fsoftupd_mmap,
	.poll		= fsoftupd_poll,
};

/*-------------------------------------------------------------------------*/

static int __init gsoftupd_bind(struct usb_configuration *c,
				struct usb_function *f)
{
	struct usb_composite_dev	*cdev = c->cdev;
	struct f_softupd		*softupd = func_to_softupd(f);
	struct usb_ep			*ep;

	int				status;

	/* allocate instance-specific interface IDs and patch descriptors */

	status = usb_interface_id(c, f);
	if (status < 0)
		goto fail;
	softupd->intf_id = status;

	softupd_intf.bInterfaceNumber = status;

	/* allocate instance-specific endpoints */

	ep = usb_ep_autoconfig(cdev->gadget, &softupd_fs_ep_out_desc);
	if (!ep)
		goto fail;
	softupd->out = ep;
	ep->driver_data = cdev; /* claim */

	/* copy descriptors and track endpoint copies */
	f->descriptors = usb_copy_descriptors(fs_function);

	softupd->fs.softupd_out = usb_find_endpoint(fs_function,
			f->descriptors, &softupd_fs_ep_out_desc);

	/* support all relevant hardware speeds... we expect that when
	 * hardware is dual speed, all bulk-capable endpoints work at
	 * both speeds
	 */
	if (gadget_is_dualspeed(c->cdev->gadget)) {
		softupd_hs_ep_out_desc.bEndpointAddress =
			softupd_fs_ep_out_desc.bEndpointAddress;

		/* copy descriptors and track endpoint copies */
		f->hs_descriptors = usb_copy_descriptors(hs_function);

		softupd->hs.softupd_out = usb_find_endpoint(hs_function,
				f->hs_descriptors, &softupd_hs_ep_out_desc);
	}

	INIT_LIST_HEAD(&softupd->read_pool);
	mutex_init(&softupd->mutex);

	device_create(softupd->gsoftupd.class, &cdev->gadget->dev,
		      MKDEV(softupd->gsoftupd.major, 0), softupd, "%s",
		      f->name);

	cdev->gadget->dev.dma_mask = &softupd_dmamask;
	cdev->gadget->dev.coherent_dma_mask = DMA_BIT_MASK(64);
	softupd->gsoftupd.gadget = cdev->gadget;
	the_softupd = softupd;

	DBG(cdev, "softupd: %s speed OUT/%s\n",
			gadget_is_dualspeed(c->cdev->gadget) ? "dual" : "full",
			softupd->out->name);

	return 0;

fail:
	if (softupd->out)
		softupd->out->driver_data = NULL;

	ERROR(cdev, "%s/%p: can't bind, err %d\n", f->name, f, status);

	return status;
}

static void gsoftupd_unbind(struct usb_configuration *c, struct usb_function *f)
{
	struct f_softupd *softupd = func_to_softupd(f);

	if (gadget_is_dualspeed(c->cdev->gadget))
		usb_free_descriptors(f->hs_descriptors);
	usb_free_descriptors(f->descriptors);
	device_destroy(softupd->gsoftupd.class,
			MKDEV(softupd->gsoftupd.major, 0));
	kfree(softupd);
}

/**
 * gsoftupd_bind_config - add a RAW function to a configuration
 * @c: the configuration to support the RAW instance
 * Context: single threaded during gadget setup
 *
 * Returns zero on success, else negative errno.
 */
static int __init gsoftupd_bind_config(struct usb_configuration *c)
{
	struct f_softupd	*softupd;
	int			status;

	if (softupd_string_defs[RAW_INTF_IDX].id == 0) {
		status = usb_string_id(c->cdev);
		if (status < 0)
			return status;

		softupd_string_defs[RAW_INTF_IDX].id = status;
		softupd_intf.iInterface = status;
	}

	/* allocate and initialize one new instance */
	softupd = kzalloc(sizeof(*softupd), GFP_KERNEL);
	if (!softupd)
		return -ENOMEM;

	softupd->gsoftupd.func.name	= "softupd";
	softupd->gsoftupd.func.strings	= softupd_strings;
	/* descriptors are per-instance copies */
	softupd->gsoftupd.func.bind	= gsoftupd_bind;
	softupd->gsoftupd.func.unbind	= gsoftupd_unbind;
	softupd->gsoftupd.func.set_alt	= gsoftupd_set_alt;
	softupd->gsoftupd.func.disable	= gsoftupd_disable;

	status = usb_add_function(c, &softupd->gsoftupd.func);
	if (status)
		kfree(softupd);

	return status;
}

/**
 * gsoftupd_setup - initialize character driver for one rx
 * @g: gadget to associate with
 * Contex: may sleep
 *
 * Returns negative errno or zero.
 */
static int __init gsoftupd_setup(struct usb_gadget *g)
{
	struct gsoftupd	*gsoftupd;

	int		status;
	int		major;

	dev_t		dev;

	if (the_gsoftupd)
		return -EBUSY;

	gsoftupd = kzalloc(sizeof(*gsoftupd), GFP_KERNEL);
	if (!gsoftupd) {
		status = -ENOMEM;
		goto fail1;
	}

	status = alloc_chrdev_region(&dev, 0, 1, "fsoftupd");
	if (status)
		goto fail2;

	major = MAJOR(dev);

	cdev_init(&gsoftupd->chdev, &fsoftupd_fops);
	gsoftupd->chdev.owner	= THIS_MODULE;
	gsoftupd->dev		= dev;
	gsoftupd->major		= major;

	status = cdev_add(&gsoftupd->chdev, dev, 1);
	if (status)
		goto fail3;

	gsoftupd->class = class_create(THIS_MODULE, "fsoftupd");
	if (!gsoftupd->class)
		goto fail4;

	the_gsoftupd = gsoftupd;

	return 0;

fail4:
	cdev_del(&gsoftupd->chdev);

fail3:
	/* cdev_put(&gsoftupd->chdev); */
	unregister_chrdev_region(gsoftupd->dev, 1);

fail2:
	kfree(gsoftupd);

fail1:
	return status;
}

static void __exit gsoftupd_cleanup(void)
{
	struct gsoftupd	*gsoftupd = the_gsoftupd;

	if (!gsoftupd)
		return;

	class_destroy(gsoftupd->class);
	cdev_del(&gsoftupd->chdev);
	/* cdev_put(&gsoftupd->chdev); */
	unregister_chrdev_region(gsoftupd->dev, 1);
	kfree(gsoftupd);
}

