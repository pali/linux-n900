/*
 * drivers/media/video/omap/omap34xcam-daemon.h
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

#ifndef OMAP34XXCAM_DAEMON_H
#define OMAP34XXCAM_DAEMON_H

#define OMAP34XXCAM_DAEMON_REQ_MAX_SIZE		(1 << 18)
#define OMAP34XXCAM_DAEMON_REQ_STACK_ALLOC	2048

#include <linux/videodev2.h>

struct omap34xxcam_videodev;

/* User application -> driver */
#define VIDIOC_DAEMON_REQ \
	_IOWR('V', BASE_VIDIOC_PRIVATE + 20,  struct omap34xxcam_daemon_req)

/* Daemon -> driver */
#define VIDIOC_DAEMON_INSTALL			\
	_IO('V', BASE_VIDIOC_PRIVATE + 21)
#define VIDIOC_DAEMON_SET_EVENTS			\
	_IOW('V', BASE_VIDIOC_PRIVATE + 22,  __u32)
#define VIDIOC_DAEMON_DAEMON_REQ_GET			\
	_IOWR('V', BASE_VIDIOC_PRIVATE + 23,		\
	      struct omap34xxcam_daemon_daemon_req)
#define VIDIOC_DAEMON_DAEMON_REQ_COMPLETE		\
	_IOW('V', BASE_VIDIOC_PRIVATE + 24,		\
	     struct omap34xxcam_daemon_daemon_req)

/*
 * @max_size: allocated size of the blob
 * @size: actual size of the blob
 */
struct omap34xxcam_daemon_req {
	__u32 max_size;
	__u32 size;
	__u32 type;
	void *blob;
};

/*
 * @sync: Is this a synchronous req?
 * @req: request
 */
struct omap34xxcam_daemon_daemon_req {
	union {
		__u32 sync; /* get */
		int rval;   /* complete */
	} u;
	struct omap34xxcam_daemon_req req;
};

#include "omap34xxcam-daemon-req.h"

#ifdef __KERNEL__

#include <media/videobuf-core.h>

#define OMAP34XXCAM_DAEMON_SYNC		(1<<0)
#define OMAP34XXCAM_DAEMON_ASYNC	(1<<1)

#define OMAP34XXCAM_DAEMON_REQUEST_USER_START		1
#define OMAP34XXCAM_DAEMON_REQUEST_DAEMON_START		2
#define OMAP34XXCAM_DAEMON_REQUEST_DAEMON_FINISH	3
#define OMAP34XXCAM_DAEMON_REQUEST_USER_FINISH		0

struct omap34xxcam_daemon {
	/* Synchronise access to daemon / this structure. */
	struct mutex mutex;
	/* Daemon file if it's there. */
	struct file *file;
	/* Mutual exclusion from daemon events. */
	struct mutex request_mutex;
	int request_state;
	wait_queue_head_t poll_wait;
	struct semaphore begin;
	struct semaphore finish;
	struct omap34xxcam_daemon_req *req;
	int req_rval;
	/* event_lock serialises the rest of the fields */
	spinlock_t event_lock;
	u32 req_pending; /* sync / async */
	u32 event_mask;
	struct omap34xxcam_daemon_event event;
};

int omap34xxcam_daemon_req(struct omap34xxcam_videodev *vdev,
			   struct omap34xxcam_daemon_req *req,
			   struct file *file);
int omap34xxcam_daemon_req_user(struct omap34xxcam_videodev *vdev,
				struct omap34xxcam_daemon_req *_req,
				struct file *file);
int omap34xxcam_daemon_daemon_req_get_user(
	struct omap34xxcam_videodev *vdev,
	struct omap34xxcam_daemon_daemon_req *get,
	struct file *file);
int omap34xxcam_daemon_daemon_req_complete_user(
	struct omap34xxcam_videodev *vdev,
	struct omap34xxcam_daemon_daemon_req *complete,
	struct file *file);
void omap34xxcam_daemon_init(struct omap34xxcam_videodev *vdev);
int omap34xxcam_daemon_install(struct file *file);
int omap34xxcam_daemon_release(struct omap34xxcam_videodev *vdev,
			       struct file *file);
void omap34xxcam_daemon_event_cb(unsigned long status, int (*arg1)
				 (struct videobuf_buffer *vb), void *arg2);
int omap34xxcam_daemon_set_events(struct omap34xxcam_videodev *vdev, u32 *mask,
				  struct file *file);

#endif /* __KERNEL__ */

#endif
