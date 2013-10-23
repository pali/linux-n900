/*
 * v4l2-fh.h
 *
 * V4L2 file handle.
 *
 * Copyright (C) 2009--2010 Nokia Corporation.
 *
 * Contact: Sakari Ailus <sakari.ailus@maxwell.research.nokia.com>
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

#ifndef V4L2_FH_H
#define V4L2_FH_H

#include <linux/list.h>

struct video_device;
struct v4l2_events;

struct v4l2_fh {
	struct list_head	list;
	struct video_device	*vdev;
	struct v4l2_events      *events; /* events, pending and subscribed */
};

int v4l2_fh_init(struct v4l2_fh *fh, struct video_device *vdev);
void v4l2_fh_add(struct v4l2_fh *fh);
void v4l2_fh_del(struct v4l2_fh *fh);
void v4l2_fh_exit(struct v4l2_fh *fh);

#endif /* V4L2_EVENT_H */
