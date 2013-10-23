/*
 * drivers/media/video/omap/omap34xcam-daemon-req.c
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
#include <linux/omap34xxcam-daemon.h>

#include <media/v4l2-common.h>

#include "isp/isp.h"

#include "omap34xxcam.h"

/*
 * request handlers for specific request --- to be called from
 * application context
 */

int omap34xxcam_daemon_req_hw_init(struct omap34xxcam_videodev *vdev)
{
	struct omap34xxcam_daemon_req req;

	req.size = req.max_size = 0;
	req.type = OMAP34XXCAM_DAEMON_REQ_HW_INIT;
	req.blob = NULL;

	return omap34xxcam_daemon_req(vdev, &req, NULL);
}

int omap34xxcam_daemon_req_hw_reconfig(struct omap34xxcam_videodev *vdev,
				       u32 what)
{
	struct omap34xxcam_daemon_req req;
	struct omap34xxcam_daemon_req_hw_reconfig hw_reconfig;

	req.size = req.max_size = sizeof(hw_reconfig);
	req.type = OMAP34XXCAM_DAEMON_REQ_HW_RECONFIG;
	req.blob = &hw_reconfig;

	hw_reconfig.mask = what;

	return omap34xxcam_daemon_req(vdev, &req, NULL);
}

/* request handlers --- to be called from daemon context */

/* Any synchronous request. */
int omap34xxcam_daemon_daemon_req_sync(
	struct omap34xxcam_videodev *vdev,
	struct omap34xxcam_daemon_daemon_req *get)
{
	struct omap34xxcam_daemon *d = &vdev->daemon;

	if (down_interruptible(&d->begin))
		return -ERESTARTSYS;

	d->request_state = OMAP34XXCAM_DAEMON_REQUEST_DAEMON_START;

	if (get->req.max_size < d->req->size) {
		d->req_rval = -E2BIG;
		up(&d->finish);
		return -E2BIG;
	}
	get->req.size = d->req->size;
	get->req.type = d->req->type;

/* 	printk(KERN_INFO "%s: size %d\n", __func__, get->req.size); */
/* 	printk(KERN_INFO "%s: maximum size %d\n", */
/* 	       __func__, get->req.max_size); */
/* 	printk(KERN_INFO "%s: blob %p\n",__func__,get->req.blob); */

	if (copy_to_user(get->req.blob, d->req->blob, d->req->size)) {
		printk(KERN_INFO "%s: copy_to_user failed\n", __func__);
		d->req_rval = -EINVAL;
		up(&d->finish);
		return -EFAULT;
	}

	return 0;
}

/* The only async request is to get ISP events. */
int omap34xxcam_daemon_daemon_req_async(
	struct omap34xxcam_videodev *vdev,
	struct omap34xxcam_daemon_daemon_req *get)
{
	struct omap34xxcam_daemon *d = &vdev->daemon;
	unsigned long flags;
	int rval = 0;

	if (get->req.max_size < sizeof(d->event))
		return -E2BIG;

	get->req.size = sizeof(d->event);
	get->req.type = OMAP34XXCAM_DAEMON_REQ_EVENTS;

	spin_lock_irqsave(&d->event_lock, flags);
	if (copy_to_user(get->req.blob, &d->event, sizeof(d->event)))
		rval = -EFAULT;
	spin_unlock_irqrestore(&d->event_lock, flags);

	return rval;
}
