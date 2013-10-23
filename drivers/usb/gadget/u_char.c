/*
 * u_char.c - USB character device glue
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Roger Quadros <roger.quadros@nokia.com>
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

#include <linux/poll.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/interrupt.h>
#include <linux/kfifo.h>
#include "u_char.h"

/* Max simultaneous gchar devices. Increase if you need more */
#define GC_MAX_DEVICES		4

/* Number of USB requests that can be queued at a time */
#define GC_QUEUE_SIZE		4

/* size in bytes of RX and TX FIFOs */
#define GC_BUF_SIZE		65536

static unsigned max_devs = GC_MAX_DEVICES;
module_param(max_devs, uint, 0);
MODULE_PARM_DESC(max_devs, "Max number of devices to create. Default 4");

static unsigned queue_size = GC_QUEUE_SIZE;
module_param(queue_size, uint, 0);
MODULE_PARM_DESC(queue_size, "Number of USB requests to queue at a time. Default 4");

static unsigned buflen = GC_BUF_SIZE;
module_param(buflen, uint, 0);
MODULE_PARM_DESC(buflen, "kfifo buffer size. Default 65536");

enum gc_buf_state {
	BUF_EMPTY = 0,
	BUF_FULL,
	BUF_BUSY,
};

struct gc_buf {
	struct usb_request	*r;
	bool 			busy;
	struct gc_buf		*next;
	enum gc_buf_state	state;
};

/*----------------USB glue----------------------------------*/
/*
 * gc_alloc_req
 *
 * Allocate a usb_request and its buffer.  Returns a pointer to the
 * usb_request or NULL if there is an error.
 */
struct usb_request *
gc_alloc_req(struct usb_ep *ep, unsigned len, gfp_t kmalloc_flags)
{
	struct usb_request *req;

	req = usb_ep_alloc_request(ep, kmalloc_flags);

	if (req != NULL) {
		req->length = len;
		req->buf = kmalloc(len, kmalloc_flags);
		if (req->buf == NULL) {
			usb_ep_free_request(ep, req);
			return NULL;
		}
	}

	return req;
}

/*
 * gc_free_req
 *
 * Free a usb_request and its buffer.
 */
void gc_free_req(struct usb_ep *ep, struct usb_request *req)
{
	kfree(req->buf);
	usb_ep_free_request(ep, req);
}

static int gc_alloc_requests(struct usb_ep *ep, struct gc_buf *queue,
		void (*fn)(struct usb_ep *, struct usb_request *))
{
	int                     i;
	struct usb_request      *req;

	/* Pre-allocate up to queue_size transfers, but if we can't
	 * do quite that many this time, don't fail ... we just won't
	 * be as speedy as we might otherwise be.
	 */
	for (i = 0; i < GC_QUEUE_SIZE; i++) {
		req = gc_alloc_req(ep, PAGE_SIZE, GFP_ATOMIC);
		if (!req)
			break;
		req->complete = fn;
		queue[i].r = req;
		queue[i].busy = 0;
		queue[i].state = BUF_EMPTY;
		queue[i].next = &queue[i+1];
	}
	if (i == 0)
		return -ENOMEM;
	queue[--i].next = &queue[0];
	return 0;
}

static void gc_free_requests(struct usb_ep *ep, struct gc_buf *queue)
{
	struct usb_request      *req;
	int i;

	for (i = 0; i < GC_QUEUE_SIZE; i++) {
		req = queue[i].r;
		if (!req)
			break;
		gc_free_req(ep, req);
		queue[i].r = NULL;
	}
}

/*----------------------------------------------------------------*/

struct gc_req {
	struct usb_request	*r;
	ssize_t			l;
	wait_queue_head_t	req_wait;
};


struct gc_dev {
	struct gchar		*gchar;
	struct device		*dev;		/* Driver model state */
	spinlock_t		lock;		/* serialize access */
	wait_queue_head_t	close_wait;	/* wait for device close */
	int			index;		/* device index */
	struct task_struct	*session;	/* current device user */
	unsigned		need_reopen:1;	/* wait for reopen */
	unsigned		abort_write:1;	/* must abort the pending write */

	wait_queue_head_t	event_wait;	/* wait for events */
	bool			event;		/* event/s available */

	spinlock_t		rx_lock;	/* guard rx stuff */
	struct kfifo		rx_fifo;
	void			*rx_fifo_buf;
	struct tasklet_struct	rx_task;
	wait_queue_head_t	rx_wait;	/* wait for data in RX buf */
	unsigned int		rx_queued;	/* no. of queued requests */
	struct gc_buf		rx_queue[GC_QUEUE_SIZE];
	struct gc_buf		*rx_next;
	bool			rx_cancel;

	spinlock_t		tx_lock;	/* guard tx stuff */
	struct kfifo		tx_fifo;
	void			*tx_fifo_buf;
	wait_queue_head_t	tx_wait;	/* wait for space in TX buf */
	unsigned int		tx_flush:1;	/* flush TX buf */
	wait_queue_head_t	tx_flush_wait;
	int			tx_last_size;	/*last tx packet's size*/
	struct tasklet_struct	tx_task;
	struct tasklet_struct	tx_abort_task;
	struct gc_buf		tx_queue[GC_QUEUE_SIZE];
	unsigned int		tx_queued;
	struct gc_buf		*tx_next;
	bool			tx_cancel;
};

struct gc_data {
	struct gc_dev			*gcdevs;
	u8				nr_devs;
	struct class			*class;
	dev_t				dev;
	struct cdev			chdev;
	struct usb_gadget		*gadget;
};

static struct gc_data gcdata;

static void gc_rx_complete(struct usb_ep *ep, struct usb_request *req);
static void gc_tx_complete(struct usb_ep *ep, struct usb_request *req);
static int gc_do_rx(struct gc_dev *gc);


/*----------some more USB glue---------------------------*/

/* OUT complete, we have new data to read */
static void gc_rx_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct gc_dev	*gc = ep->driver_data;
	unsigned long	flags;
	int		i;
	struct gc_buf	*queue = req->context;

	spin_lock_irqsave(&gc->rx_lock, flags);

	/* put received data into RX ring buffer */
	/* we assume enough space is there in RX buffer for this request
	 * the checking should be done in gc_do_rx() before this request
	 * was queued */
	switch (req->status) {
	case 0:
		/* normal completion */
		i = kfifo_in(&gc->rx_fifo, req->buf, req->actual);
		if (i != req->actual) {
			WARN(1, KERN_ERR "%s: PUT(%d) != actual(%d) data "
				"loss possible. rx_queued = %d\n", __func__, i,
						req->actual, gc->rx_queued);
		}
		dev_vdbg(gc->dev,
			"%s: rx len=%d, 0x%02x 0x%02x 0x%02x ...\n", __func__,
				req->actual, *((u8 *)req->buf),
				*((u8 *)req->buf+1), *((u8 *)req->buf+2));

		/* wake up rx_wait */
		wake_up_interruptible(&gc->rx_wait);
		break;
	case -ESHUTDOWN:
		/* disconnect */
		dev_dbg(gc->dev, "%s: %s shutdown\n", __func__, ep->name);
		break;
	default:
		/* presumably a transient fault */
		dev_dbg(gc->dev, "%s: unexpected %s status %d\n",
				__func__, ep->name, req->status);
		break;
	}

	gc->rx_queued--;
	queue->busy = false;
	queue->state = BUF_EMPTY;
	spin_unlock_irqrestore(&gc->rx_lock, flags);
	if (!gc->rx_cancel)
		tasklet_schedule(&gc->rx_task);
}

static int gc_do_tx(struct gc_dev *gc);
/* IN complete, i.e. USB write complete. we can free buffer */
static void gc_tx_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct gc_dev	*gc = ep->driver_data;
	unsigned long	flags;
	struct gc_buf	*queue = req->context;

	spin_lock_irqsave(&gc->tx_lock, flags);
	queue->busy = false;
	queue->state = BUF_EMPTY;
	spin_unlock_irqrestore(&gc->tx_lock, flags);

	switch (req->status) {
	case 0:
		/* normal completion, queue next request */
		if (!gc->tx_cancel)
			tasklet_schedule(&gc->tx_task);
		break;
	case -ESHUTDOWN:
		/* disconnect */
		dev_warn(gc->dev, "%s: %s shutdown\n", __func__, ep->name);
		break;
	default:
		/* presumably a transient fault */
		dev_warn(gc->dev, "%s: unexpected %s status %d\n",
				__func__, ep->name, req->status);
		break;
	}
}


/* Read the TX buffer and send to USB */
/* gc->tx_lock must be held */
static int gc_do_tx(struct gc_dev *gc)
{
	struct gc_buf		*queue	= gc->tx_next;
	struct usb_ep		*in;
	int			status;

	if (!gc->gchar || !gc->gchar->ep_in)
		return -ENODEV;

	in = gc->gchar->ep_in;

	while (queue->state == BUF_EMPTY) {
		struct	usb_request	*req;
		unsigned int		len;

		req = queue->r;

		len = kfifo_len(&gc->tx_fifo);
		if (!len && !gc->tx_flush)
			/* TX buf empty */
			break;

		req->zero = 0;
		if (len > PAGE_SIZE) {
			len = PAGE_SIZE;
			gc->tx_last_size = 0;	/* not the last packet */
		} else {
			/* this is last packet in TX buf. send ZLP/SLP
			 * if user has requested so
			 */
			req->zero = gc->tx_flush;
			gc->tx_last_size = len;
		}

		len = kfifo_out(&gc->tx_fifo, req->buf, len);
		req->length = len;

		queue->state = BUF_FULL;
		queue->busy = true;
		queue->r->context = queue;

		if (req->zero) {
			gc->tx_flush = 0;
			wake_up_interruptible(&gc->tx_flush_wait);
		}

		dev_vdbg(gc->dev,
			"%s: tx len=%d, 0x%02x 0x%02x 0x%02x ...\n", __func__,
				len, *((u8 *)req->buf),
				*((u8 *)req->buf+1), *((u8 *)req->buf+2));
		/* Drop lock while we call out of driver; completions
		 * could be issued while we do so.  Disconnection may
		 * happen too; maybe immediately before we queue this!
		 *
		 * NOTE that we may keep sending data for a while after
		 * the file is closed.
		 */
		spin_unlock(&gc->tx_lock);
		status = usb_ep_queue(in, req, GFP_ATOMIC);
		spin_lock(&gc->tx_lock);

		if (status) {
			dev_err(gc->dev, "%s: %s %s err %d\n",
					__func__, "queue", in->name, status);
			queue->state = BUF_EMPTY;
			queue->busy = false;
			break;	 /* FIXME: re-try? */
		}

		gc->tx_next = queue->next;
		queue = gc->tx_next;

		/* abort immediately after disconnect */
		if (!gc->gchar) {
			dev_dbg(gc->dev,
				"%s: disconnected so aborting\n", __func__);
			break;
		}

		/* wake up tx_wait */
		wake_up_interruptible(&gc->tx_wait);
	}
	return 0;
}

static void gc_tx_task(unsigned long _gc)
{
	struct gc_dev	*gc = (void *)_gc;

	spin_lock_irq(&gc->tx_lock);
	if (gc->gchar && gc->gchar->ep_in)
		gc_do_tx(gc);
	spin_unlock_irq(&gc->tx_lock);
}

static void gc_tx_abort_task(unsigned long _gc)
{
	struct gc_dev	*gc = (void *)_gc;
	struct gc_buf	*queue	= gc->tx_next;
	struct usb_ep	*ep;
	int		i;

	if (!gc->gchar || !gc->gchar->ep_in)
		return;

	ep = gc->gchar->ep_in;

	spin_lock_irq(&gc->tx_lock);

	if (!gc->abort_write) {
		spin_unlock_irq(&gc->tx_lock);
		return;
	}
	
	gc->tx_cancel = true;
	for (i = 0 ; i < GC_QUEUE_SIZE ; i++) {
		queue = &gc->tx_queue[i];
		if (queue->busy) {
			spin_unlock_irq(&gc->tx_lock);
			usb_ep_dequeue(ep, queue->r);
			spin_lock_irq(&gc->tx_lock);
		}
	}
	usb_ep_fifo_flush(ep);
	kfifo_reset(&gc->tx_fifo);
	gc->tx_flush = 0;
	gc->tx_last_size = 0;
	gc->tx_cancel = false;
	gc->tx_next = &gc->tx_queue[0];

	wake_up_interruptible(&gc->tx_flush_wait);
	wake_up_interruptible(&gc->tx_wait);

	spin_unlock_irq(&gc->tx_lock);
}

/* Tasklet:  Queue USB read requests whenever RX buffer available
 *	Must be called with gc->rx_lock held
 */
static int gc_do_rx(struct gc_dev *gc)
{
	/* Queue the request only if required space is there in RX buffer */
	struct gc_buf		*queue	= gc->rx_next;
	struct usb_ep		*out;
	int			started = 0;

	if (!gc->gchar || !gc->gchar->ep_out)
		return -EINVAL;

	out = gc->gchar->ep_out;

	while (queue->state == BUF_EMPTY) {
		struct usb_request      *req;
		int                     status;

		req = queue->r;
		req->length = PAGE_SIZE;

		/* check if space is available in RX buf for this request */
		if (kfifo_avail(&gc->rx_fifo) <
				(gc->rx_queued + 2)*req->length) {
			/* insufficient space */
			break;
		}
		gc->rx_queued++;
		queue->state = BUF_FULL;
		queue->busy = true;
		queue->r->context = queue;

		/* drop lock while we call out; the controller driver
		 * may need to call us back (e.g. for disconnect)
		 */
		spin_unlock(&gc->rx_lock);
		status = usb_ep_queue(out, req, GFP_ATOMIC);
		spin_lock(&gc->rx_lock);

		if (status) {
			dev_warn(gc->dev, "%s: %s %s err %d\n",
					__func__, "queue", out->name, status);
			queue->state = BUF_EMPTY;
			queue->busy = false;
			gc->rx_queued--;
			break; /* FIXME: re-try? */
		}

		started++;
		gc->rx_next = queue->next;
		queue = gc->rx_next;

		/* abort immediately after disconnect */
		if (!gc->gchar) {
			dev_dbg(gc->dev, "%s: disconnected so aborting\n",
					__func__);
			break;
		}
	}
	return started;
}


static void gc_rx_task(unsigned long _gc)
{
	struct gc_dev	*gc = (void *)_gc;

	spin_lock_irq(&gc->rx_lock);
	if (gc->gchar && gc->gchar->ep_out)
		gc_do_rx(gc);
	spin_unlock_irq(&gc->rx_lock);
}

/*----------FILE Operations-------------------------------*/

static int gc_open(struct inode *inode, struct file *filp)
{
	unsigned	minor = iminor(inode);
	struct gc_dev	*gc;
	int		index;

	index = minor - MINOR(gcdata.dev);
	if (index >= gcdata.nr_devs)
		return -ENODEV;

	if (!gcdata.gcdevs)
		return -ENODEV;

	gc = &gcdata.gcdevs[index];
	filp->private_data = gc;

	/* prevent multiple opens for now */
	if (gc->session)
		return -EBUSY;
	spin_lock_irq(&gc->lock);
	if (gc->session) {
		spin_unlock_irq(&gc->lock);
		return -EBUSY;
	}
	gc->session = current;
	gc->need_reopen = false;
	spin_unlock_irq(&gc->lock);
	gc->index = index;

	if (gc->gchar && gc->gchar->open)
		gc->gchar->open(gc->gchar);

	/* if connected, start receiving */
	if (gc->gchar)
		tasklet_schedule(&gc->rx_task);

	dev_dbg(gc->dev, "%s: gc%d opened\n", __func__, gc->index);
	return 0;
}

static int gc_release(struct inode *inode, struct file *filp)
{
	struct gc_dev			*gc = filp->private_data;

	filp->private_data = NULL;

	dev_dbg(gc->dev, "%s: releasing gc%d\n", __func__, gc->index);

	if (!gc->session)
		goto gc_release_exit;

	if (gc->gchar && gc->gchar->close)
		gc->gchar->close(gc->gchar);

	spin_lock_irq(&gc->lock);
	gc->session = NULL;
	spin_unlock_irq(&gc->lock);

	wake_up_interruptible(&gc->close_wait);

gc_release_exit:
	dev_dbg(gc->dev, "%s: gc%d released!!\n", __func__, gc->index);
	return 0;
}

static int gc_can_read(struct gc_dev *gc)
{
	int ret;

	spin_lock_irq(&gc->rx_lock);
	ret = kfifo_len(&gc->rx_fifo) ? 1 : 0;
	spin_unlock_irq(&gc->rx_lock);

	return ret;
}

static ssize_t gc_read(struct file *filp, char __user *buff,
				size_t len, loff_t *o)
{
	struct	gc_dev	*gc = filp->private_data;
	unsigned int	read = 0;
	int		ret;

	if (gc->need_reopen)
		return -EIO;

	if (!gc->gchar || !gc->gchar->ep_out) {
		/* not yet connected or reading not possible*/
		return -EINVAL;
	}

	if (len) {
		read = kfifo_len(&gc->rx_fifo);
		if (!read) {
			/* if NONBLOCK then return immediately */
			if (filp->f_flags & O_NONBLOCK)
				return -EAGAIN;

			/* sleep till we have some data */
			ret = wait_event_interruptible(gc->rx_wait,
							gc_can_read(gc));
			if (gc->need_reopen)
				ret = -EIO;
			else if (!gc->gchar)
				ret = -EINVAL;
			if (ret < 0)
				return ret;
		}
		ret = kfifo_to_user(&gc->rx_fifo, buff, len, &read);
		if (ret < 0)
			dev_warn(gc->dev, "%s fault %d\n", __func__, ret);
	}

	if (read > 0) {
		spin_lock_irq(&gc->rx_lock);
		gc_do_rx(gc);
		spin_unlock_irq(&gc->rx_lock);
	}

	dev_vdbg(gc->dev, "%s done %d/%d\n", __func__, read, len);
	return read;
}

static int gc_can_write(struct gc_dev *gc)
{
	int ret;

	spin_lock_irq(&gc->tx_lock);
	ret = !kfifo_is_full(&gc->tx_fifo);
	spin_unlock_irq(&gc->tx_lock);

	return ret;
}

static ssize_t gc_write(struct file *filp, const char __user *buff,
						size_t len, loff_t *o)
{
	struct	gc_dev	*gc = filp->private_data;
	unsigned int	wrote = 0;
	int		ret;

	if (gc->need_reopen || gc->abort_write)
		return -EIO;

	if (!gc->gchar || !gc->gchar->ep_in) {
		/* not yet connected or writing not possible */
		return -EINVAL;
	}

	if (len) {
		if (kfifo_is_full(&gc->tx_fifo)) {
			if (filp->f_flags & O_NONBLOCK)
				return -EAGAIN;

			/* sleep till we have some space to write into */
			ret = wait_event_interruptible(gc->tx_wait,
						gc_can_write(gc));
			if (gc->need_reopen || gc->abort_write)
				ret = -EIO;
			else if (!gc->gchar)
				ret = -EINVAL;
			if (ret < 0)
				return ret;
		}
		ret = kfifo_from_user(&gc->tx_fifo, buff, len, &wrote);
		if (ret < 0)
			dev_warn(gc->dev, "%s fault %d\n", __func__, ret);
	}

	if (wrote > 0) {
		spin_lock_irq(&gc->tx_lock);
		gc_do_tx(gc);
		spin_unlock_irq(&gc->tx_lock);
		if (gc->abort_write)
			return -EIO;
	}

	dev_vdbg(gc->dev, "%s done %d/%d\n", __func__, wrote, len);
	return wrote;
}

static long gc_ioctl(struct file *filp, unsigned code, unsigned long value)
{
	struct gc_dev			*gc = filp->private_data;
	int status = -EINVAL;

	if (gc->need_reopen)
		status = -EIO;
	else if (gc->gchar && gc->gchar->ioctl)
		status = gc->gchar->ioctl(gc->gchar, code, value);

	dev_dbg(gc->dev, "%s done\n", __func__);
	return status;
}

static unsigned int gc_poll(struct file *filp, struct poll_table_struct *pt)
{
	struct gc_dev			*gc = filp->private_data;
	int				ret = 0;
	int				rx = 0, tx = 0, ev = 0;

	if (gc->need_reopen) {
		ret = POLLIN | POLLOUT | POLLERR | POLLHUP | POLLRDNORM
		      | POLLRDBAND | POLLWRNORM | POLLWRBAND | POLLRDHUP;
		goto poll_exit;
	}

	/* generic poll implementation */
	poll_wait(filp, &gc->rx_wait, pt);
	poll_wait(filp, &gc->tx_wait, pt);
	poll_wait(filp, &gc->event_wait, pt);

	if (!gc->gchar) {
		/* not yet connected */
		goto poll_exit;
	}

	/* check if data is available to read */
	if (gc->gchar->ep_out) {
		rx = kfifo_len(&gc->rx_fifo);
		if (rx)
			ret |= POLLIN | POLLRDNORM;
	}

	/* check if space is available to write */
	if (gc->gchar->ep_in) {
		tx = kfifo_avail(&gc->tx_fifo);
		if (tx)
			ret |= POLLOUT | POLLWRNORM;
	}

	/* check if event/s available */
	ev = gc->event;
	if (ev)
		ret |= POLLPRI;

	/* hangup has occurred */
	if (gc->need_reopen)
		ret |= POLLHUP | POLLRDHUP;

	dev_dbg(gc->dev, "%s: rx avl %d, tx space %d, event %d\n",
				__func__, rx, tx, ev);
poll_exit:

	return ret;
}

int gc_fsync(struct file *filp, struct dentry *dentry, int datasync)
{
	int		ret = 0;
	struct gc_dev	*gc = filp->private_data;

	if (gc->need_reopen)
		return -EIO;

	if (!gc->gchar || !gc->gchar->ep_in) {
		/* not yet connected or writing not possible */
		return -EINVAL;
	}

	/* flush the TX buffer and send ZLP/SLP
	 * we will wait till TX buffer is empty
	 */
	spin_lock_irq(&gc->tx_lock);

	if (gc->tx_flush) {
		dev_err(gc->dev, "%s tx_flush already requested\n", __func__);
		spin_unlock_irq(&gc->tx_lock);
		return -EINVAL;
	}

	if (!kfifo_len(&gc->tx_fifo)) {
		if (gc->tx_last_size == gc->gchar->ep_in->maxpacket)
			gc->tx_flush = 1;
	} else
		gc->tx_flush = 1;

	if (gc->tx_flush) {
		gc_do_tx(gc);

		spin_unlock_irq(&gc->tx_lock);

		ret = wait_event_interruptible(gc->tx_flush_wait,
					!gc->tx_flush);
		if (!gc->gchar)
			ret = -EINVAL;
		else if (gc->need_reopen || gc->abort_write)
			ret = -EIO;
		if (ret < 0)
			return ret;
	} else
		spin_unlock_irq(&gc->tx_lock);

	dev_dbg(gc->dev, "%s complete\n", __func__);

	return ret;
}

static const struct file_operations gc_fops = {
	.owner		= THIS_MODULE,
	.open		= gc_open,
	.poll		= gc_poll,
	.unlocked_ioctl	= gc_ioctl,
	.release	= gc_release,
	.read		= gc_read,
	.write		= gc_write,
	.fsync		= gc_fsync,
};

/*------------USB Gadget Driver Interface----------------------------*/

/**
 * gchar_setup - initialize the character driver for one or more devices
 * @g: gadget to associate with these devices
 * @devs_num: number of character devices to support
 * Context: may sleep
 *
 * This driver needs to know how many char. devices it should manage.
 * Use this call to set up the devices that will be exported through USB.
 * Later, connect them to functions based on what configuration is activated
 * by the USB host, and disconnect them as appropriate.
 *
 * Returns negative errno or zero.
 */
int __init gchar_setup(struct usb_gadget *g, u8 devs_num)
{
	int		status;
	int		i = 0;

	if (gcdata.nr_devs)
		return -EBUSY;

	if (devs_num == 0 || devs_num > max_devs)
		return -EINVAL;

	gcdata.gcdevs = kzalloc(sizeof(struct gc_dev) * devs_num, GFP_KERNEL);
	if (!gcdata.gcdevs)
		return -ENOMEM;

	/* created char dev */
	status = alloc_chrdev_region(&gcdata.dev, 0, devs_num, "gchar");
	if (status)
		goto fail1;

	cdev_init(&gcdata.chdev, &gc_fops);

	gcdata.chdev.owner = THIS_MODULE;
	gcdata.nr_devs	= devs_num;

	status = cdev_add(&gcdata.chdev, gcdata.dev, devs_num);
	if (status)
		goto fail2;

	/* register with sysfs */
	gcdata.class = class_create(THIS_MODULE, "gchar");
	if (IS_ERR(gcdata.class)) {
		pr_err("%s: could not create class gchar\n", __func__);
		status = PTR_ERR(gcdata.class);
		goto fail3;
	}

	for (i = 0; i < devs_num; i++) {
		struct gc_dev	*gc;

		gc = &gcdata.gcdevs[i];
		spin_lock_init(&gc->lock);
		spin_lock_init(&gc->rx_lock);
		spin_lock_init(&gc->tx_lock);
		init_waitqueue_head(&gc->rx_wait);
		init_waitqueue_head(&gc->tx_wait);
		init_waitqueue_head(&gc->event_wait);
		init_waitqueue_head(&gc->tx_flush_wait);
		init_waitqueue_head(&gc->close_wait);

		tasklet_init(&gc->rx_task, gc_rx_task, (unsigned long) gc);
		tasklet_init(&gc->tx_task, gc_tx_task, (unsigned long) gc);
		tasklet_init(&gc->tx_abort_task, gc_tx_abort_task,
			     (unsigned long) gc);
		gc->dev = device_create(gcdata.class, NULL,
			MKDEV(MAJOR(gcdata.dev), MINOR(gcdata.dev) + i),
			NULL, "gc%d", i);
		if (IS_ERR(gc->dev)) {
			pr_err("%s: device_create() failed for device %d\n",
					__func__, i);
			goto fail4;
		}
		/* Allocate FIFO buffers */
		gc->tx_fifo_buf = vmalloc(buflen);
		if (!gc->tx_fifo_buf)
			goto fail5;
		kfifo_init(&gc->tx_fifo,
				gc->tx_fifo_buf, buflen);

		gc->rx_fifo_buf = vmalloc(buflen);
		if (!gc->rx_fifo_buf) {
			vfree(gc->tx_fifo_buf);
			goto fail5;
		}
		kfifo_init(&gc->rx_fifo,
				gc->rx_fifo_buf, buflen);

	}

	gcdata.gadget = g;

	return 0;
fail5:
	device_destroy(gcdata.class,
			MKDEV(MAJOR(gcdata.dev), MINOR(gcdata.dev) + i));
fail4:
	for (i-- ; i >= 0; i--) {
		struct gc_dev	*gc = &gcdata.gcdevs[i];
		device_destroy(gcdata.class,
			MKDEV(MAJOR(gcdata.dev), MINOR(gcdata.dev) + i));
		vfree(gc->tx_fifo_buf);
		vfree(gc->rx_fifo_buf);
	}
	class_destroy(gcdata.class);
fail3:
	cdev_del(&gcdata.chdev);
fail2:
	unregister_chrdev_region(gcdata.dev, gcdata.nr_devs);
fail1:
	kfree(gcdata.gcdevs);
	gcdata.gcdevs = NULL;
	gcdata.nr_devs = 0;

	return status;
}

static int gc_closed(struct gc_dev *gc)
{
	int ret;

	spin_lock_irq(&gc->lock);
	ret = !gc->session;
	spin_unlock_irq(&gc->lock);
	return ret;
}

/**
 * gchar_cleanup - remove the USB to character devicer and devices
 * Context: may sleep
 *
 * This is called to free all resources allocated by @gchar_setup().
 * It may need to wait until some open /dev/ files have been closed.
 */
void gchar_cleanup(void)
{
	int i;

	if (!gcdata.gcdevs)
		return;

	for (i = 0; i < gcdata.nr_devs; i++) {
		struct gc_dev *gc = &gcdata.gcdevs[i];

		tasklet_kill(&gc->rx_task);
		tasklet_kill(&gc->tx_task);
		tasklet_kill(&gc->tx_abort_task);
		device_destroy(gcdata.class, MKDEV(MAJOR(gcdata.dev),
				MINOR(gcdata.dev) + i));
		/* wait till open files are closed */
		wait_event(gc->close_wait, gc_closed(gc));
		vfree(gc->tx_fifo_buf);
		vfree(gc->rx_fifo_buf);
	}

	cdev_del(&gcdata.chdev);
	class_destroy(gcdata.class);

	/* cdev_put(&gchar>chdev); */
	unregister_chrdev_region(gcdata.dev, gcdata.nr_devs);

	kfree(gcdata.gcdevs);
	gcdata.gcdevs = NULL;
	gcdata.nr_devs = 0;
}

/**
 * gchar_connect - notify the driver that USB link is active
 * @gchar: the function, setup with endpoints and descriptors
 * @num: the device number that is active
 * @name: name of the function
 * Context: any (usually from irq)
 *
 * This is called to activate the endpoints and let the driver know
 * that USB link is active.
 *
 * Caller needs to have set up the endpoints and USB function in @gchar
 * before calling this, as well as the appropriate (speed-specific)
 * endpoint descriptors, and also have set up the char driver by calling
 * @gchar_setup().
 *
 * Returns negative error or zeroa
 * On success, ep->driver_data will be overwritten
 */
int gchar_connect(struct gchar *gchar, u8 num, const char *name)
{
	int		status = 0;
	struct gc_dev	*gc;

	if (num >= gcdata.nr_devs) {
		pr_err("%s: invalid device number\n", __func__);
		return -EINVAL;
	}

	gc = &gcdata.gcdevs[num];

	dev_dbg(gc->dev, "%s %s %d\n", __func__, name, num);

	if (!gchar->ep_out && !gchar->ep_in) {
		dev_err(gc->dev, "%s: Neither IN nor OUT endpoint available\n",
								__func__);
		return -EINVAL;
	}

	if (gchar->ep_out) {
		status = usb_ep_enable(gchar->ep_out, gchar->ep_out_desc);
		if (status < 0)
			return status;

		gchar->ep_out->driver_data = gc;
	}

	if (gchar->ep_in) {
		status = usb_ep_enable(gchar->ep_in, gchar->ep_in_desc);
		if (status < 0)
			goto fail1;

		gchar->ep_in->driver_data = gc;
	}

	kfifo_reset(&gc->tx_fifo);
	kfifo_reset(&gc->rx_fifo);
	gc->rx_queued = 0;
	gc->tx_flush = 0;
	gc->tx_last_size = 0;
	gc->event = 0;

	if (gchar->ep_out) {
		status = gc_alloc_requests(gchar->ep_out, gc->rx_queue,
						&gc_rx_complete);
		if (status)
			goto fail2;
		gc->tx_next = gc->tx_queue;
	}

	if (gchar->ep_in) {
		status = gc_alloc_requests(gchar->ep_in, gc->tx_queue,
						&gc_tx_complete);
		if (status)
			goto fail3;
		gc->rx_next = gc->rx_queue;
	}

	/* connect gchar */
	gc->gchar = gchar;

	/* if userspace has opened the device, enable function */
	if (gc->session && gc->gchar->open)
		gc->gchar->open(gc->gchar);

	/* if device is opened by user space then start RX */
	if (gc->session && !gc->need_reopen)
		tasklet_schedule(&gc->rx_task);

	dev_dbg(gc->dev, "%s complete\n", __func__);
	return 0;

fail3:
	if (gchar->ep_out)
		gc_free_requests(gchar->ep_out, gc->rx_queue);

fail2:
	if (gchar->ep_in) {
		gchar->ep_in->driver_data = NULL;
		usb_ep_disable(gchar->ep_in);
	}
fail1:
	if (gchar->ep_out) {
		gchar->ep_out->driver_data = NULL;
		usb_ep_disable(gchar->ep_out);
	}

	return status;
}

/**
 * gchar_disconnect - notify the driver that USB link is inactive
 * @gchar: the function, on which, gchar_connect() was called
 * Context: any (usually from irq)
 *
 * this is called to deactivate the endpoints (related to @gchar)
 * and let the driver know that the USB link is inactive
 */
void gchar_disconnect(struct gchar *gchar)
{
	struct gc_dev *gc;

	if (!gchar->ep_out && !gchar->ep_in)
		return;

	if (gchar->ep_out)
		gc = gchar->ep_out->driver_data;
	else
		gc = gchar->ep_in->driver_data;

	if (!gc) {
		pr_err("%s Invalid gc_dev\n", __func__);
		return;
	}

	spin_lock(&gc->lock);

	gc->gchar = NULL;

	/* Hangup */
	if (gc->session) {
		gc->need_reopen = true;
		send_sig(SIGHUP, gc->session, 0);
		send_sig(SIGCONT, gc->session, 0);
	}

	/* Wakeup sleepers */
	wake_up_interruptible(&gc->rx_wait);
	wake_up_interruptible(&gc->tx_wait);
	wake_up_interruptible(&gc->event_wait);
	wake_up_interruptible(&gc->tx_flush_wait);

	if (gchar->ep_out) {
		usb_ep_disable(gchar->ep_out);
		gc_free_requests(gchar->ep_out, gc->rx_queue);
		gchar->ep_out->driver_data = NULL;
	}

	if (gchar->ep_in) {
		usb_ep_disable(gchar->ep_in);
		gc_free_requests(gchar->ep_in, gc->tx_queue);
		gchar->ep_in->driver_data = NULL;
	}

	spin_unlock(&gc->lock);
}

/**
 * gchar_notify(struct gchar *gchar);
 * @gchar: the function on which we need to notify
 * Context: any (usually form irq)
 *
 * This is called to send a POLLPRI notification to user
 * space to indicate availability of Hight Priority Data
 * The actual data can be fetched by user space via an IOCTL
 */
void gchar_notify(struct gchar *gchar)
{
	struct gc_dev *gc;
	unsigned long flags;

	if (!gchar->ep_out || !gchar->ep_in)
		/* not connected */
		return;

	if (gchar->ep_out)
		gc = gchar->ep_out->driver_data;
	else
		gc = gchar->ep_in->driver_data;


	spin_lock_irqsave(&gc->lock, flags);
	if (!gc->session) {
		/* not opened */
		spin_unlock_irqrestore(&gc->lock, flags);
		return;
	}

	gc->abort_write = true;
	tasklet_schedule(&gc->tx_abort_task);
	gc->event = true;
	wake_up_interruptible(&gc->event_wait);
	spin_unlock_irqrestore(&gc->lock, flags);
}

/**
 * gchar_notify_clear(struct gchar *gchar);
 * @gchar: the function on which we need to clear notify
 * Context: any (usually form irq)
 *
 * This is called to send clear a POLLPRI notification to user space
 */
void gchar_notify_clear(struct gchar *gchar)
{
	unsigned long flags;

	struct gc_dev *gc;
	if (!gchar->ep_out || !gchar->ep_in)
		/*not connected */
		return;

	if (gchar->ep_out)
		gc = gchar->ep_out->driver_data;
	else
		gc = gchar->ep_in->driver_data;


	spin_lock_irqsave(&gc->lock, flags);
	if (!gc->session) {
		/* not opened */
		spin_unlock_irqrestore(&gc->lock, flags);
		return;
	}

	gc->event = false;
	gc->abort_write = false;
	wake_up_interruptible(&gc->event_wait);
	spin_unlock_irqrestore(&gc->lock, flags);
}

/**
 * gchar_clear_fifos(struct gchar *gchar)
 *
 * @gchar: the function who's fifos we need to clear
 * Contect: Process. should not be called from IRQ.
 *
 * Clears the TX and RX FIFOs
 */
void gchar_clear_fifos(struct gchar *gchar)
{
	struct gc_dev *gc;
	struct usb_ep *ep;
	struct gc_buf *queue;
	int status;
	int i;

	if (!gchar->ep_out || !gchar->ep_in)
		/* not connected */
		return;

	if (gchar->ep_out)
		gc = gchar->ep_out->driver_data;
	else
		gc = gchar->ep_in->driver_data;

	if (!gc->session)
		return;

	if (gchar->ep_in) {
		/* RESET TX buffer state and pointers */
		ep = gchar->ep_in;
		gc->tx_cancel = true;
		tasklet_kill(&gc->tx_task);
		spin_lock_irq(&gc->tx_lock);

		for (i = 0 ; i < GC_QUEUE_SIZE ; i++) {
			queue = &gc->tx_queue[i];
			if (queue->busy) {
				spin_unlock_irq(&gc->tx_lock);
				status = usb_ep_dequeue(ep, queue->r);
				spin_lock_irq(&gc->tx_lock);
			}
		}
		usb_ep_fifo_flush(ep);
		kfifo_reset(&gc->tx_fifo);
		gc->tx_flush = 0;
		gc->tx_last_size = 0;
		gc->tx_cancel = false;
		gc->tx_next = &gc->tx_queue[0];
		spin_unlock_irq(&gc->tx_lock);
	}

	if (gchar->ep_out) {
		/* RESET RX buffer state and pointers */
		ep = gchar->ep_out;
		gc->rx_cancel = true;
		tasklet_kill(&gc->rx_task);
		spin_lock_irq(&gc->rx_lock);

		for (i = 0 ; i < GC_QUEUE_SIZE; i++) {
			queue = &gc->rx_queue[i];
			if (queue->busy) {
				spin_unlock_irq(&gc->rx_lock);
				status = usb_ep_dequeue(ep, queue->r);
				spin_lock_irq(&gc->rx_lock);
			}
		}
		usb_ep_fifo_flush(ep);
		kfifo_reset(&gc->rx_fifo);
		gc->rx_cancel = false;
		gc->rx_next = &gc->rx_queue[0];
		spin_unlock_irq(&gc->rx_lock);
		/* trigger RX */
		tasklet_schedule(&gc->rx_task);
	}
}
