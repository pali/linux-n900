/*
 * ispstat.h
 *
 * Copyright (C) 2009 Nokia Corporation
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
 *
 */

#ifndef ISPSTAT_H
#define ISPSTAT_H

#include "isp.h"

struct ispstat_buffer {
	unsigned long iommu_addr;
	void *virt_addr;
	dma_addr_t dma_addr;
	struct timeval ts;
	u32 config_counter;
	u32 frame_number;
};

struct ispstat {
	spinlock_t lock;		/* Lock for this struct */

	u8 dma_buf;
	unsigned int nbufs;
	struct ispstat_buffer *buf;
	unsigned int buf_size;
	unsigned int buf_alloc_size;
	struct ispstat_buffer *active_buf;
	struct ispstat_buffer *locked_buf;
	unsigned int frame_number;
	unsigned int max_frame;
	unsigned int config_counter;

	struct device *dev;
	char *tag;		/* ispstat instantiation tag */
};

int ispstat_buf_queue(struct ispstat *stat);
struct ispstat_buffer *ispstat_buf_next(struct ispstat *stat);
struct ispstat_buffer *ispstat_buf_get(struct ispstat *stat,
				       void __user *ptr,
				       unsigned int frame_number);
void ispstat_buf_release(struct ispstat *stat);
void ispstat_bufs_free(struct ispstat *stat);
int ispstat_bufs_alloc(struct ispstat *stat,
		       unsigned int size, int dma_buf);
int ispstat_init(struct device *dev, char *tag, struct ispstat *stat,
		 unsigned int nbufs, unsigned int max_frame);
void ispstat_free(struct ispstat *stat);

#endif /* ISPSTAT_H */
