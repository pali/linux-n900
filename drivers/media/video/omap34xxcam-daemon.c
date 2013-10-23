/*
 * drivers/media/video/omap/omap34xcam-daemon.c
 *
 * OMAP 3 camera driver daemon support.
 *
 * Copyright (C) 2008 Nokia Corporation.
 *
 * Contact: Sakari Ailus <sakari.ailus@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/sched.h>
#include <linux/vmalloc.h>

#include <media/v4l2-common.h>

#include "isp/isp.h"

#include "omap34xxcam.h"

/* Kernel requests stuff from daemon. */
int omap34xxcam_daemon_req(struct omap34xxcam_videodev *vdev,
			   struct omap34xxcam_daemon_req *req,
			   struct file *file)
{
	struct omap34xxcam_daemon *d = &vdev->daemon;
	unsigned long flags;
	static int missing = 10;
	int rval;

	if (!d->file) {
		if (missing > 0) {
			missing--;
			dev_info(&vdev->vfd->dev, "%s: daemon is missing!\n",
				 __func__);
		}
		return 0;
	}

	if (req->max_size > OMAP34XXCAM_DAEMON_REQ_MAX_SIZE
	    || req->size > req->max_size)
		return -EFBIG;

	if (d->file == file) {
		dev_info(&vdev->vfd->dev, "%s: invalid ioctl for daemon!\n",
			 __func__);
		return -EINVAL;
	}

	mutex_lock(&d->request_mutex);
	d->request_state = OMAP34XXCAM_DAEMON_REQUEST_USER_START;

	spin_lock_irqsave(&d->event_lock, flags);
	d->req_pending |= OMAP34XXCAM_DAEMON_SYNC;
	spin_unlock_irqrestore(&d->event_lock, flags);

	wake_up_all(&d->poll_wait);

	d->req = req;
	up(&d->begin);
	down(&d->finish);
	rval = d->req_rval;

	d->request_state = OMAP34XXCAM_DAEMON_REQUEST_USER_FINISH;

	mutex_unlock(&d->request_mutex);

	return rval;
}

/*
 * User space requests stuff from daemon. The same as above but
 * expects user-space pointers.
 */
int omap34xxcam_daemon_req_user(struct omap34xxcam_videodev *vdev,
				struct omap34xxcam_daemon_req *req,
				struct file *file)
{
	struct omap34xxcam_daemon *d = &vdev->daemon;
	void __user *blob_ptr;
	int rval = 0;
	size_t myblob_size;
	size_t stack_alloc;

	if (!d->file)
		return -ENOIOCTLCMD;

	if (req->max_size > OMAP34XXCAM_DAEMON_REQ_MAX_SIZE
	    || req->size > req->max_size)
		return -EFBIG;

	if (req->max_size > OMAP34XXCAM_DAEMON_REQ_STACK_ALLOC) {
		myblob_size = 0;
		stack_alloc = 0;
	} else {
		myblob_size = req->max_size;
		stack_alloc = 1;
	}

	{
		char myblob[myblob_size];
		void *tmp;

		if (stack_alloc)
			tmp = myblob;
		else {
			tmp = vmalloc(req->size);
			if (tmp == NULL)
				return -ENOMEM;
		}

		blob_ptr = req->blob;
		req->blob = tmp;

/* 		printk(KERN_INFO "%s: request size %d, blob %p\n", */
/* 		       __func__, req->size, req->blob); */
		if (copy_from_user(tmp, blob_ptr, req->size)) {
			printk(KERN_INFO "%s: copy_from_user failed\n",
			       __func__);
			rval = -EFAULT;
			goto out_free;
		}

		rval = omap34xxcam_daemon_req(vdev, req, file);
		if (rval) {
			printk(KERN_INFO "%s: request failed, error %d\n",
			       __func__, rval);
			goto out_free;
		}

		if (req->max_size > OMAP34XXCAM_DAEMON_REQ_MAX_SIZE
		    || req->size > req->max_size) {
			rval = -EFBIG;
			goto out_free;
		}

		req->blob = blob_ptr;
		if (copy_to_user(blob_ptr, tmp, req->size)) {
			printk(KERN_INFO "%s: copy_to_user failed\n", __func__);
			rval = -EFAULT;
		}

	out_free:
		if (!stack_alloc)
			vfree(tmp);

/* 		printk(KERN_INFO "%s: request end\n", __func__); */
	}
	return rval;
}

/* Get an event. Only daemon calls this. */
int omap34xxcam_daemon_daemon_req_get_user(
	struct omap34xxcam_videodev *vdev,
	struct omap34xxcam_daemon_daemon_req *get,
	struct file *file)
{
	struct omap34xxcam_daemon *d = &vdev->daemon;
	unsigned long flags;
	u32 pending;
	int rval;

	mutex_lock(&d->mutex);
	if (d->file != file) {
		rval = -EBUSY;
		goto out;
	}

	spin_lock_irqsave(&d->event_lock, flags);
	pending = d->req_pending;
	spin_unlock_irqrestore(&d->event_lock, flags);

/* 	printk(KERN_INFO "%s: pending %x\n", __func__, pending); */

	if (pending & OMAP34XXCAM_DAEMON_SYNC) {
		get->u.sync = 1;

		rval = omap34xxcam_daemon_daemon_req_sync(vdev, get);
		if (!rval) {
			spin_lock_irqsave(&d->event_lock, flags);
			d->req_pending &= ~OMAP34XXCAM_DAEMON_SYNC;
			spin_unlock_irqrestore(&d->event_lock, flags);
		}
	} else if (pending & OMAP34XXCAM_DAEMON_ASYNC) {
		get->u.sync = 0;

		rval = omap34xxcam_daemon_daemon_req_async(vdev, get);
		if (!rval) {
			spin_lock_irqsave(&d->event_lock, flags);
			d->req_pending &= ~OMAP34XXCAM_DAEMON_ASYNC;
			spin_unlock_irqrestore(&d->event_lock, flags);
		}
	} else {
		rval = -EINVAL;
	}

out:
	mutex_unlock(&d->mutex);
	return rval;
}

/* Complete an event. Only daemon calls this. */
int omap34xxcam_daemon_daemon_req_complete_user(
	struct omap34xxcam_videodev *vdev,
	struct omap34xxcam_daemon_daemon_req *complete,
	struct file *file)
{
	struct omap34xxcam_daemon *d = &vdev->daemon;
	int rval = 0;

	mutex_lock(&d->mutex);
	if (d->file != file) {
		rval = -EBUSY;
		goto out;
	}

	complete->u.rval = d->req_rval;
/* 	printk(KERN_INFO "%s: reqest rval %d\n", __func__, d->req_rval); */

	if (!d->req) {
		rval = -EINVAL;
		goto out;
	}

	if (d->req->max_size < complete->req.size) {
		d->req_rval = -EFBIG;
		rval = -EFBIG;
		goto out_up;
	}

	d->req->size = complete->req.size;

	if (copy_from_user(d->req->blob, complete->req.blob,
			   d->req->size)) {
		printk(KERN_INFO "%s: copy_from_user failed\n", __func__);
		d->req_rval = -EINVAL;
		rval = -EFAULT;
		goto out_up;
	}

out_up:
/* 	d->req_rval = complete->u.rval; */
	d->request_state = OMAP34XXCAM_DAEMON_REQUEST_DAEMON_FINISH;
	up(&d->finish);

out:
	mutex_unlock(&d->mutex);
	return 0;
}

void omap34xxcam_daemon_init(struct omap34xxcam_videodev *vdev)
{
	struct omap34xxcam_daemon *d = &vdev->daemon;

	mutex_init(&d->mutex);
	mutex_init(&d->request_mutex);
	init_waitqueue_head(&d->poll_wait);
	sema_init(&d->begin, 0);
	sema_init(&d->finish, 0);
	spin_lock_init(&d->event_lock);
}

int omap34xxcam_daemon_install(struct file *file)
{
	struct omap34xxcam_fh *fh = file->private_data;
	struct omap34xxcam_videodev *vdev = fh->vdev;
	struct omap34xxcam_daemon *d = &vdev->daemon;
	int rval = 0;

/* 	if (!capable(CAP_SYS_ADMIN)) */
/* 		return -EPERM; */

	mutex_lock(&vdev->mutex);
	mutex_lock(&d->mutex);

	if (d->file) {
		mutex_unlock(&d->mutex);
		mutex_unlock(&vdev->mutex);
		return -EBUSY;
	}

	d->file = file;

	mutex_unlock(&d->mutex);

	/* Drop us from use count, except the modules. */
	if (atomic_dec_return(&vdev->users) == 0) {
		omap34xxcam_slave_power_set(vdev, V4L2_POWER_OFF,
					    OMAP34XXCAM_SLAVE_POWER_ALL);
		isp_put();
	}
	mutex_unlock(&vdev->mutex);

	return rval;
}

int omap34xxcam_daemon_release(struct omap34xxcam_videodev *vdev,
			       struct file *file)
{
	struct omap34xxcam_daemon *d = &vdev->daemon;

	if (d->file != file)
		return 0;

	mutex_lock(&d->mutex);

	/*  printk(KERN_ALERT "%s: state %d\n", __func__,
	 *  d->request_state); */
	switch (d->request_state) {
	case OMAP34XXCAM_DAEMON_REQUEST_USER_START:
		down(&d->begin);
	case OMAP34XXCAM_DAEMON_REQUEST_DAEMON_START:
		d->req_rval = -EBUSY;
		d->request_state =
			OMAP34XXCAM_DAEMON_REQUEST_DAEMON_FINISH;
		up(&d->finish);
		d->request_state =
			OMAP34XXCAM_DAEMON_REQUEST_DAEMON_FINISH;
		break;
	case OMAP34XXCAM_DAEMON_REQUEST_DAEMON_FINISH:
		break;
	case OMAP34XXCAM_DAEMON_REQUEST_USER_FINISH:
		break;
	}
	d->file = NULL;

	mutex_unlock(&d->mutex);

	return 1;
}

void omap34xxcam_daemon_event_cb(unsigned long status, int (*arg1)
				 (struct videobuf_buffer *vb), void *arg2)
{
	struct omap34xxcam_videodev *vdev =
		(struct omap34xxcam_videodev *)arg1;
	struct omap34xxcam_daemon *d = &vdev->daemon;
	struct timeval stamp;
	unsigned long flags;
	u32 event = 0;

	if (status & HIST_DONE)
		event |= OMAP34XXCAM_DAEMON_EVENT_HIST_DONE;
	if (status & H3A_AWB_DONE)
		event |= OMAP34XXCAM_DAEMON_EVENT_H3A_AWB_DONE;
	if (status & H3A_AF_DONE)
		event |= OMAP34XXCAM_DAEMON_EVENT_H3A_AF_DONE;
	if (status & HS_VS)
		event |= OMAP34XXCAM_DAEMON_EVENT_HS_VS;

	spin_lock_irqsave(&d->event_lock, flags);

	event &= d->event_mask;
	if (!event) {
		spin_unlock_irqrestore(&d->event_lock, flags);
		return;
	}

	spin_unlock_irqrestore(&d->event_lock, flags);

	/* Enable interrupts during do_gettimeofday */
	do_gettimeofday(&stamp);

	spin_lock_irqsave(&d->event_lock, flags);

	if (event & OMAP34XXCAM_DAEMON_EVENT_HIST_DONE)
		d->event.hist_done_stamp = stamp;
	if (event & OMAP34XXCAM_DAEMON_EVENT_H3A_AWB_DONE)
		d->event.h3a_awb_done_stamp = stamp;
	if (event & OMAP34XXCAM_DAEMON_EVENT_H3A_AF_DONE)
		d->event.h3a_af_done_stamp = stamp;
	if (event & OMAP34XXCAM_DAEMON_EVENT_HS_VS)
		d->event.hs_vs_stamp = stamp;

	d->event.mask |= event;

	if (d->event.mask) {
		d->req_pending |= OMAP34XXCAM_DAEMON_ASYNC;
		wake_up_all(&d->poll_wait);
	}

	spin_unlock_irqrestore(&d->event_lock, flags);
}

int omap34xxcam_daemon_set_events(struct omap34xxcam_videodev *vdev, u32 *mask,
				  struct file *file)
{
	struct omap34xxcam_daemon *d = &vdev->daemon;
	unsigned long flags;
	int rval = 0;

	mutex_lock(&d->mutex);

	if (d->file != file) {
		rval = -EBUSY;
		goto out;
	}

	spin_lock_irqsave(&d->event_lock, flags);
	d->event_mask = *mask;
	spin_unlock_irqrestore(&d->event_lock, flags);

out:
	mutex_unlock(&d->mutex);

	return rval;
}
