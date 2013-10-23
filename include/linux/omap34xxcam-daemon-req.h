/*
 * drivers/media/video/omap/omap34xcam-daemon-req.h
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

#ifndef OMAP34XXCAM_DAEMON_REQ_H
#define OMAP34XXCAM_DAEMON_REQ_H

/* Synchronous requests */
#define OMAP34XXCAM_DAEMON_REQ_HW_RECONFIG		1000
#define OMAP34XXCAM_DAEMON_REQ_HW_INIT			1001

/*
 * Please start numbering requests defined in daemon source code from
 * this.
 */
#define OMAP34XXCAM_DAEMON_REQ_PRIV_START		10000

/* Asynchronous requests */
#define OMAP34XXCAM_DAEMON_REQ_EVENTS			10

#define OMAP34XXCAM_DAEMON_HW_RECONFIG_STREAMON		(1<<0)
#define OMAP34XXCAM_DAEMON_HW_RECONFIG_STREAMOFF	(1<<1)
#define OMAP34XXCAM_DAEMON_HW_RECONFIG_CROP		(1<<2)
#define OMAP34XXCAM_DAEMON_HW_RECONFIG_FMT		(1<<4)
struct omap34xxcam_daemon_req_hw_reconfig {
	__u32 mask;
};

/* Driver events */
#define OMAP34XXCAM_DAEMON_EVENT_HIST_DONE		(1<<0)
#define OMAP34XXCAM_DAEMON_EVENT_H3A_AWB_DONE		(1<<1)
#define OMAP34XXCAM_DAEMON_EVENT_H3A_AF_DONE		(1<<2)
#define OMAP34XXCAM_DAEMON_EVENT_HS_VS			(1<<3)
struct omap34xxcam_daemon_event {
	__u32 mask;
	struct timeval hist_done_stamp;
	struct timeval h3a_awb_done_stamp;
	struct timeval h3a_af_done_stamp;
	struct timeval hs_vs_stamp;
};

#ifdef __KERNEL__

int omap34xxcam_daemon_req_hw_init(struct omap34xxcam_videodev *vdev);
int omap34xxcam_daemon_req_hw_reconfig(struct omap34xxcam_videodev *vdev,
				       u32 what);

int omap34xxcam_daemon_daemon_req_sync(
	struct omap34xxcam_videodev *vdev,
	struct omap34xxcam_daemon_daemon_req *get);
int omap34xxcam_daemon_daemon_req_async(
	struct omap34xxcam_videodev *vdev,
	struct omap34xxcam_daemon_daemon_req *get);

#endif

#endif
