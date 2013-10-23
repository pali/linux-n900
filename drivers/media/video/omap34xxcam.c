/*
 * omap34xxcam.c
 *
 * Copyright (C) 2006--2009 Nokia Corporation
 * Copyright (C) 2007--2009 Texas Instruments
 *
 * Contact: Sakari Ailus <sakari.ailus@nokia.com>
 *          Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
 *
 * Originally based on the OMAP 2 camera driver.
 *
 * Written by Sakari Ailus <sakari.ailus@nokia.com>
 *            Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
 *            Sergio Aguirre <saaguirre@ti.com>
 *            Mohit Jalori
 *            Sameer Venkatraman
 *            Leonides Martinez
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

#include <linux/videodev2.h>
#include <linux/version.h>
#include <asm/pgalloc.h>

#include <media/v4l2-common.h>
#include <media/v4l2-ioctl.h>

#include "omap34xxcam.h"
#include "isp/isp.h"

#define OMAP34XXCAM_VERSION KERNEL_VERSION(0, 0, 0)

/* global variables */
static struct omap34xxcam_device *omap34xxcam;

/*
 *
 * Sensor handling.
 *
 */

/**
 * omap34xxcam_slave_power_set - set slave power state
 * @vdev: per-video device data structure
 * @power: new power state
 */
int omap34xxcam_slave_power_set(struct omap34xxcam_videodev *vdev,
				enum v4l2_power power, int mask)
{
	int rval = 0, i = 0;
	int start, end, dir;

	BUG_ON(!mutex_is_locked(&vdev->mutex));

	if (power != V4L2_POWER_OFF) {
		/* Sensor has to be powered on first */
		start = 0;
		end = OMAP34XXCAM_SLAVE_FLASH;
		dir = 1;
	} else {
		/* Sensor has to be powered off last */
		start = OMAP34XXCAM_SLAVE_FLASH;
		end = 0;
		dir = -1;
	}

	for (i = start; i != end + dir; i += dir) {
		if (vdev->slave[i] == v4l2_int_device_dummy())
			continue;

		if (!(mask & (1 << i))
		    || power == vdev->power_state[i])
			continue;

		rval = vidioc_int_s_power(vdev->slave[i], power);

		if (rval && power != V4L2_POWER_OFF) {
			power = V4L2_POWER_OFF;
			goto out;
		}

		vdev->power_state[i] = power;
	}

	return 0;

out:
	for (i -= dir; i != start - dir; i -= dir) {
		if (vdev->slave[i] == v4l2_int_device_dummy())
			continue;

		if (!(mask & (1 << i)))
			continue;

		vidioc_int_s_power(vdev->slave[i], power);
		vdev->power_state[i] = power;
	}

	return rval;
}

/**
 * omap34xxcam_update_vbq - Updates VBQ with completed input buffer
 * @vb: ptr. to standard V4L2 video buffer structure
 *
 * Updates video buffer queue with completed buffer passed as
 * input parameter.  Also updates ISP H3A timestamp and field count
 * statistics.
 */
void omap34xxcam_vbq_complete(struct videobuf_buffer *vb, void *priv)
{
	struct omap34xxcam_fh *fh = priv;

	do_gettimeofday(&vb->ts);
	vb->field_count = atomic_add_return(2, &fh->field_count);

	wake_up(&vb->done);
}

/**
 * omap34xxcam_vbq_setup - Calcs size and num of buffs allowed in queue
 * @vbq: ptr. to standard V4L2 video buffer queue structure
 * @cnt: ptr to location to hold the count of buffers to be in the queue
 * @size: ptr to location to hold the size of a frame
 *
 * Calculates the number of buffers of current image size that can be
 * supported by the available capture memory.
 */
static int omap34xxcam_vbq_setup(struct videobuf_queue *vbq, unsigned int *cnt,
				 unsigned int *size)
{
	struct omap34xxcam_fh *fh = vbq->priv_data;
	struct omap34xxcam_videodev *vdev = fh->vdev;

	if (*cnt <= 0)
		*cnt = VIDEO_MAX_FRAME;	/* supply a default number of buffers */

	if (*cnt > VIDEO_MAX_FRAME)
		*cnt = VIDEO_MAX_FRAME;

	*size = vdev->pix.sizeimage;

	while (*size * *cnt > fh->vdev->vdev_sensor_config.capture_mem)
		(*cnt)--;

	return isp_vbq_setup(vdev->cam->isp, vbq, cnt, size);
}

static int omap34xxcam_vb_lock_vma(struct videobuf_buffer *vb, int lock)
{
	unsigned long start, end;
	struct vm_area_struct *vma;
	int rval = 0;

	if (vb->memory == V4L2_MEMORY_MMAP)
		return 0;

	if (current->flags & PF_EXITING) {
		/**
		 * task is getting shutdown.
		 * current->mm could have been released.
		 *
		 * For locking, we return error.
		 * For unlocking, the subsequent release of
		 * buffer should set things right
		 */
		if (lock)
			return -EINVAL;
		else
			return 0;
	}

	end = vb->baddr + vb->bsize;

	down_write(&current->mm->mmap_sem);
	spin_lock(&current->mm->page_table_lock);

	for (start = vb->baddr; ; ) {
		unsigned int newflags;

		vma = find_vma(current->mm, start);
		if (!vma || vma->vm_start > start) {
				rval = -ENOMEM;
			goto out;
		}

		newflags = vma->vm_flags | VM_LOCKED;
		if (!lock)
			newflags &= ~VM_LOCKED;

		vma->vm_flags = newflags;

		if (vma->vm_end >= end)
			break;

		start = vma->vm_end;
	}

out:
	spin_unlock(&current->mm->page_table_lock);
	up_write(&current->mm->mmap_sem);
	return rval;
}

/**
 * omap34xxcam_vbq_release - Free resources for input VBQ and VB
 * @vbq: ptr. to standard V4L2 video buffer queue structure
 * @vb: ptr to standard V4L2 video buffer structure
 *
 * Unmap and free all memory associated with input VBQ and VB, also
 * unmap the address in ISP MMU.  Reset the VB state.
 */
static void omap34xxcam_vbq_release(struct videobuf_queue *vbq,
				    struct videobuf_buffer *vb)
{
	struct omap34xxcam_fh *fh = vbq->priv_data;
	struct omap34xxcam_videodev *vdev = fh->vdev;
	struct device *isp = vdev->cam->isp;

	if (!vbq->streaming) {
		isp_vbq_release(isp, vbq, vb);
		omap34xxcam_vb_lock_vma(vb, 0);
		videobuf_dma_unmap(vbq, videobuf_to_dma(vb));
		videobuf_dma_free(videobuf_to_dma(vb));
		vb->state = VIDEOBUF_NEEDS_INIT;
	}
	return;
}

/**
 * omap34xxcam_vbq_prepare - V4L2 video ops buf_prepare handler
 * @vbq: ptr. to standard V4L2 video buffer queue structure
 * @vb: ptr to standard V4L2 video buffer structure
 * @field: standard V4L2 field enum
 *
 * Verifies there is sufficient locked memory for the requested
 * buffer, or if there is not, allocates, locks and initializes
 * it.
 */
static int omap34xxcam_vbq_prepare(struct videobuf_queue *vbq,
				   struct videobuf_buffer *vb,
				   enum v4l2_field field)
{
	struct omap34xxcam_fh *fh = vbq->priv_data;
	struct omap34xxcam_videodev *vdev = fh->vdev;
	struct device *isp = vdev->cam->isp;

	int err = 0;

	/*
	 * Accessing pix here is okay since it's constant while
	 * streaming is on (and we only get called then).
	 */
	if (vb->baddr) {
		/* This is a userspace buffer. */
		if (vdev->pix.sizeimage > vb->bsize)
			/* The buffer isn't big enough. */
			return -EINVAL;
	} else {
		if (vb->state != VIDEOBUF_NEEDS_INIT
		    && vdev->pix.sizeimage > vb->bsize)
			/*
			 * We have a kernel bounce buffer that has
			 * already been allocated.
			 */
			omap34xxcam_vbq_release(vbq, vb);
	}

	vb->size = vdev->pix.bytesperline * vdev->pix.height;
	vb->width = vdev->pix.width;
	vb->height = vdev->pix.height;
	vb->field = field;

	if (vb->state == VIDEOBUF_NEEDS_INIT) {
		err = omap34xxcam_vb_lock_vma(vb, 1);
		if (err)
			goto buf_init_err;

		err = videobuf_iolock(vbq, vb, NULL);
		if (err)
			goto buf_init_err;

		/* isp_addr will be stored locally inside isp code */
		err = isp_vbq_prepare(isp, vbq, vb, field);
	}

buf_init_err:
	if (!err)
		vb->state = VIDEOBUF_PREPARED;
	else
		omap34xxcam_vbq_release(vbq, vb);

	return err;
}

/**
 * omap34xxcam_vbq_queue - V4L2 video ops buf_queue handler
 * @vbq: ptr. to standard V4L2 video buffer queue structure
 * @vb: ptr to standard V4L2 video buffer structure
 *
 * Maps the video buffer to sgdma and through the isp, sets
 * the isp buffer done callback and sets the video buffer state
 * to active.
 */
static void omap34xxcam_vbq_queue(struct videobuf_queue *vbq,
				  struct videobuf_buffer *vb)
{
	struct omap34xxcam_fh *fh = vbq->priv_data;
	struct omap34xxcam_videodev *vdev = fh->vdev;
	struct device *isp = vdev->cam->isp;

	isp_buf_queue(isp, vb, omap34xxcam_vbq_complete, (void *)fh);
}

static struct videobuf_queue_ops omap34xxcam_vbq_ops = {
	.buf_setup = omap34xxcam_vbq_setup,
	.buf_prepare = omap34xxcam_vbq_prepare,
	.buf_queue = omap34xxcam_vbq_queue,
	.buf_release = omap34xxcam_vbq_release,
};

/*
 *
 * IOCTL interface.
 *
 */

/**
 * vidioc_querycap - V4L2 query capabilities IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @cap: ptr to standard V4L2 capability structure
 *
 * Fill in the V4L2 capabliity structure for the camera device
 */
static int vidioc_querycap(struct file *file, void *fh,
			   struct v4l2_capability *cap)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;

	strlcpy(cap->driver, CAM_SHORT_NAME, sizeof(cap->driver));
	strlcpy(cap->card, vdev->vfd->name, sizeof(cap->card));
	cap->version = OMAP34XXCAM_VERSION;
	if (vdev->vdev_sensor != v4l2_int_device_dummy())
		cap->capabilities = V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_STREAMING;

	return 0;
}

/**
 * vidioc_enum_fmt_vid_cap - V4L2 enumerate format capabilities IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @f: ptr to standard V4L2 format description structure
 *
 * Fills in enumerate format capabilities information for sensor (if SOC
 * sensor attached) or ISP (if raw sensor attached).
 */
static int vidioc_enum_fmt_vid_cap(struct file *file, void *fh,
				   struct v4l2_fmtdesc *f)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	int rval;

	if (vdev->vdev_sensor == v4l2_int_device_dummy())
		return -EINVAL;

	if (vdev->vdev_sensor_config.sensor_isp)
		rval = vidioc_int_enum_fmt_cap(vdev->vdev_sensor, f);
	else
		rval = isp_enum_fmt_cap(f);

	return rval;
}

/**
 * vidioc_g_fmt_vid_cap - V4L2 get format capabilities IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @f: ptr to standard V4L2 format structure
 *
 * Fills in format capabilities for sensor (if SOC sensor attached) or ISP
 * (if raw sensor attached).
 */
static int vidioc_g_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;

	if (vdev->vdev_sensor == v4l2_int_device_dummy())
		return -EINVAL;

	mutex_lock(&vdev->mutex);
	f->fmt.pix = vdev->pix;
	mutex_unlock(&vdev->mutex);

	return 0;
}

static int try_pix_parm(struct omap34xxcam_videodev *vdev,
			struct v4l2_pix_format *best_pix_in,
			struct v4l2_pix_format *wanted_pix_out,
			struct v4l2_fract *best_ival)
{
	int fps;
	int fmtd_index;
	int rval;
	struct v4l2_pix_format best_pix_out;
	struct device *isp = vdev->cam->isp;

	if (best_ival->numerator == 0
	    || best_ival->denominator == 0)
		*best_ival = vdev->vdev_sensor_config.ival_default;

	fps = best_ival->denominator / best_ival->numerator;

	memset(best_pix_in, 0, sizeof(*best_pix_in));

	best_ival->denominator = 0;
	best_pix_out.height = INT_MAX >> 1;
	best_pix_out.width = best_pix_out.height;

	for (fmtd_index = 0; ; fmtd_index++) {
		int size_index;
		struct v4l2_fmtdesc fmtd;

		fmtd.index = fmtd_index;
		fmtd.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		rval = vidioc_int_enum_fmt_cap(vdev->vdev_sensor, &fmtd);
		if (rval)
			break;
		dev_dbg(&vdev->vfd->dev, "trying fmt %8.8x (%d)\n",
			fmtd.pixelformat, fmtd_index);
		/*
		 * Get supported resolutions.
		 */
		for (size_index = 0; ; size_index++) {
			struct v4l2_frmsizeenum frms;
			struct v4l2_pix_format pix_tmp_in, pix_tmp_out;
			int ival_index;

			frms.index = size_index;
			frms.pixel_format = fmtd.pixelformat;

			rval = vidioc_int_enum_framesizes(vdev->vdev_sensor,
							  &frms);
			if (rval)
				break;

			pix_tmp_in.pixelformat = frms.pixel_format;
			pix_tmp_in.width = frms.discrete.width;
			pix_tmp_in.height = frms.discrete.height;
			pix_tmp_out = *wanted_pix_out;
			/* Don't do upscaling. */
			if (pix_tmp_out.width > pix_tmp_in.width)
				pix_tmp_out.width = pix_tmp_in.width;
			if (pix_tmp_out.height > pix_tmp_in.height)
				pix_tmp_out.height = pix_tmp_in.height;
			rval = isp_try_fmt_cap(isp, &pix_tmp_in, &pix_tmp_out);
			if (rval)
				return rval;

			dev_dbg(&vdev->vfd->dev, "this w %d\th %d\tfmt %8.8x\t"
				"-> w %d\th %d\t fmt %8.8x"
				"\twanted w %d\th %d\t fmt %8.8x\n",
				pix_tmp_in.width, pix_tmp_in.height,
				pix_tmp_in.pixelformat,
				pix_tmp_out.width, pix_tmp_out.height,
				pix_tmp_out.pixelformat,
				wanted_pix_out->width, wanted_pix_out->height,
				wanted_pix_out->pixelformat);

#define IS_SMALLER_OR_EQUAL(pix1, pix2)				\
			((pix1)->width + (pix1)->height		\
			 < (pix2)->width + (pix2)->height)
#define SIZE_DIFF(pix1, pix2)						\
			(abs((pix1)->width - (pix2)->width)		\
			 + abs((pix1)->height - (pix2)->height))

			/*
			 * Don't use modes that are farther from wanted size
			 * that what we already got.
			 */
			if (SIZE_DIFF(&pix_tmp_out, wanted_pix_out)
			    > SIZE_DIFF(&best_pix_out, wanted_pix_out)) {
				dev_dbg(&vdev->vfd->dev, "size diff bigger: "
					"w %d\th %d\tw %d\th %d\n",
					pix_tmp_out.width, pix_tmp_out.height,
					best_pix_out.width,
					best_pix_out.height);
				continue;
			}

			/*
			 * There's an input mode that can provide output
			 * closer to wanted.
			 */
			if (SIZE_DIFF(&pix_tmp_out, wanted_pix_out)
			    < SIZE_DIFF(&best_pix_out, wanted_pix_out)) {
				/* Force renegotation of fps etc. */
				best_ival->denominator = 0;
				dev_dbg(&vdev->vfd->dev, "renegotiate: "
					"w %d\th %d\tw %d\th %d\n",
					pix_tmp_out.width, pix_tmp_out.height,
					best_pix_out.width,
					best_pix_out.height);
			}

			for (ival_index = 0; ; ival_index++) {
				struct v4l2_frmivalenum frmi;

				frmi.index = ival_index;
				frmi.pixel_format = frms.pixel_format;
				frmi.width = frms.discrete.width;
				frmi.height = frms.discrete.height;
				/* FIXME: try to fix standard... */
				frmi.reserved[0] = 0xdeafbeef;

				rval = vidioc_int_enum_frameintervals(
					vdev->vdev_sensor, &frmi);
				if (rval)
					break;

				dev_dbg(&vdev->vfd->dev, "fps %d\n",
					frmi.discrete.denominator
					/ frmi.discrete.numerator);

				if (best_ival->denominator == 0)
					goto do_it_now;

				if (best_pix_in->width == 0)
					goto do_it_now;

				/*
				 * We aim to use maximum resolution
				 * from the sensor, provided that the
				 * fps is at least as close as on the
				 * current mode.
				 */
#define FPS_ABS_DIFF(fps, ival) abs(fps - (ival).denominator / (ival).numerator)

				/* Select mode with closest fps. */
				if (FPS_ABS_DIFF(fps, frmi.discrete)
				    < FPS_ABS_DIFF(fps, *best_ival)) {
					dev_dbg(&vdev->vfd->dev, "closer fps: "
						"fps %d\t fps %d\n",
						FPS_ABS_DIFF(fps,
							      frmi.discrete),
						FPS_ABS_DIFF(fps, *best_ival));
					goto do_it_now;
				}

				/*
				 * Select bigger resolution if it's available
				 * at same fps.
				 */
				if (frmi.width + frmi.height
				    > best_pix_in->width + best_pix_in->height
				    && FPS_ABS_DIFF(fps, frmi.discrete)
				    <= FPS_ABS_DIFF(fps, *best_ival)) {
					dev_dbg(&vdev->vfd->dev, "bigger res, "
						"same fps: "
						"w %d\th %d\tw %d\th %d\n",
						frmi.width, frmi.height,
						best_pix_in->width,
						best_pix_in->height);
					goto do_it_now;
				}

				dev_dbg(&vdev->vfd->dev, "falling through\n");

				continue;

do_it_now:
				*best_ival = frmi.discrete;
				best_pix_out = pix_tmp_out;
				best_pix_in->width = frmi.width;
				best_pix_in->height = frmi.height;
				best_pix_in->pixelformat = frmi.pixel_format;

				dev_dbg(&vdev->vfd->dev,
					"best_pix_in: w %d\th %d\tfmt %8.8x"
					"\tival %d/%d\n",
					best_pix_in->width,
					best_pix_in->height,
					best_pix_in->pixelformat,
					best_ival->numerator,
					best_ival->denominator);
			}
		}
	}

	if (best_ival->denominator == 0)
		return -EINVAL;

	*wanted_pix_out = best_pix_out;

	dev_dbg(&vdev->vfd->dev, "w %d, h %d, fmt %8.8x -> w %d, h %d\n",
		best_pix_in->width, best_pix_in->height,
		best_pix_in->pixelformat,
		best_pix_out.width, best_pix_out.height);

	return 0;
}

static int s_pix_parm(struct omap34xxcam_videodev *vdev,
		      struct v4l2_pix_format *best_pix,
		      struct v4l2_pix_format *pix,
		      struct v4l2_fract *best_ival)
{
	struct device *isp = vdev->cam->isp;
	struct v4l2_streamparm a;
	struct v4l2_format fmt;
	struct v4l2_format old_fmt;
	int rval;

	rval = try_pix_parm(vdev, best_pix, pix, best_ival);
	if (rval)
		return rval;

	rval = isp_s_fmt_cap(isp, best_pix, pix);
	if (rval)
		return rval;

	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix = *best_pix;
	vidioc_int_g_fmt_cap(vdev->vdev_sensor, &old_fmt);
	rval = vidioc_int_s_fmt_cap(vdev->vdev_sensor, &fmt);
	if (rval)
		return rval;

	a.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	a.parm.capture.timeperframe = *best_ival;
	rval = vidioc_int_s_parm(vdev->vdev_sensor, &a);

	return rval;
}

/**
 * vidioc_s_fmt_vid_cap - V4L2 set format capabilities IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @f: ptr to standard V4L2 format structure
 *
 * Attempts to set input format with the sensor driver (first) and then the
 * ISP.  Returns the return code from vidioc_g_fmt_vid_cap().
 */
static int vidioc_s_fmt_vid_cap(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct v4l2_pix_format pix_tmp;
	struct v4l2_fract timeperframe;
	int rval;

	if (vdev->vdev_sensor == v4l2_int_device_dummy())
		return -EINVAL;

	omap34xxcam_daemon_req_hw_reconfig(
		vdev,
		OMAP34XXCAM_DAEMON_HW_RECONFIG_FMT);

	mutex_lock(&vdev->mutex);
	if (vdev->streaming) {
		rval = -EBUSY;
		goto out;
	}

	vdev->want_pix = f->fmt.pix;

	timeperframe = vdev->want_timeperframe;

	rval = s_pix_parm(vdev, &pix_tmp, &f->fmt.pix, &timeperframe);
	if (!rval)
		vdev->pix = f->fmt.pix;

out:
	mutex_unlock(&vdev->mutex);

	return rval;
}

/**
 * vidioc_try_fmt_vid_cap - V4L2 try format capabilities IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @f: ptr to standard V4L2 format structure
 *
 * Checks if the given format is supported by the sensor driver and
 * by the ISP.
 */
static int vidioc_try_fmt_vid_cap(struct file *file, void *fh,
				  struct v4l2_format *f)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct v4l2_pix_format pix_tmp;
	struct v4l2_fract timeperframe;
	int rval;

	if (vdev->vdev_sensor == v4l2_int_device_dummy())
		return -EINVAL;

	mutex_lock(&vdev->mutex);

	timeperframe = vdev->want_timeperframe;

	rval = try_pix_parm(vdev, &pix_tmp, &f->fmt.pix, &timeperframe);

	mutex_unlock(&vdev->mutex);

	return rval;
}

/**
 * vidioc_reqbufs - V4L2 request buffers IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @b: ptr to standard V4L2 request buffers structure
 *
 * Attempts to get a buffer from the buffer queue associated with the
 * fh through the video buffer library API.
 */
static int vidioc_reqbufs(struct file *file, void *fh,
			  struct v4l2_requestbuffers *b)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	int rval;

	if (vdev->vdev_sensor == v4l2_int_device_dummy())
		return -EINVAL;

	mutex_lock(&vdev->mutex);
	if (vdev->streaming) {
		mutex_unlock(&vdev->mutex);
		return -EBUSY;
	}

	rval = videobuf_reqbufs(&ofh->vbq, b);

	mutex_unlock(&vdev->mutex);

	/*
	 * Either videobuf_reqbufs failed or the buffers are not
	 * memory-mapped (which would need special attention).
	 */
	if (rval < 0 || b->memory != V4L2_MEMORY_MMAP)
		goto out;

out:
	return rval;
}

/**
 * vidioc_querybuf - V4L2 query buffer IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @b: ptr to standard V4L2 buffer structure
 *
 * Attempts to fill in the v4l2_buffer structure for the buffer queue
 * associated with the fh through the video buffer library API.
 */
static int vidioc_querybuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct omap34xxcam_fh *ofh = fh;

	return videobuf_querybuf(&ofh->vbq, b);
}

/**
 * vidioc_qbuf - V4L2 queue buffer IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @b: ptr to standard V4L2 buffer structure
 *
 * Attempts to queue the v4l2_buffer on the buffer queue
 * associated with the fh through the video buffer library API.
 */
static int vidioc_qbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct omap34xxcam_fh *ofh = fh;

	return videobuf_qbuf(&ofh->vbq, b);
}

/**
 * vidioc_dqbuf - V4L2 dequeue buffer IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @b: ptr to standard V4L2 buffer structure
 *
 * Attempts to dequeue the v4l2_buffer from the buffer queue
 * associated with the fh through the video buffer library API.  If the
 * buffer is a user space buffer, then this function will also requeue it,
 * as user does not expect to do this.
 */
static int vidioc_dqbuf(struct file *file, void *fh, struct v4l2_buffer *b)
{
	struct omap34xxcam_fh *ofh = fh;
	int rval;

videobuf_dqbuf_again:
	rval = videobuf_dqbuf(&ofh->vbq, b, file->f_flags & O_NONBLOCK);

	/*
	 * This is a hack. We don't want to show -EIO to the user
	 * space. Requeue the buffer and try again if we're not doing
	 * this in non-blocking mode.
	 */
	if (rval == -EIO) {
		videobuf_qbuf(&ofh->vbq, b);
		if (!(file->f_flags & O_NONBLOCK))
			goto videobuf_dqbuf_again;
		/*
		 * We don't have a videobuf_buffer now --- maybe next
		 * time...
		 */
		rval = -EAGAIN;
	}

	return rval;
}

/**
 * vidioc_streamon - V4L2 streamon IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @i: V4L2 buffer type
 *
 * Attempts to start streaming by enabling the sensor interface and turning
 * on video buffer streaming through the video buffer library API.  Upon
 * success the function returns 0, otherwise an error code is returned.
 */
static int vidioc_streamon(struct file *file, void *fh, enum v4l2_buf_type i)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct device *isp = vdev->cam->isp;
	int rval;

	if (vdev->vdev_sensor == v4l2_int_device_dummy())
		return -EINVAL;

	mutex_lock(&vdev->mutex);
	if (vdev->streaming) {
		rval = -EBUSY;
		goto out;
	}

	rval = omap34xxcam_slave_power_set(vdev, V4L2_POWER_ON,
					   OMAP34XXCAM_SLAVE_POWER_ALL);
	if (rval) {
		dev_dbg(&vdev->vfd->dev,
			"omap34xxcam_slave_power_set failed\n");
		goto out;
	}

	isp_start(isp);

	isp_set_callback(isp, CBK_CATCHALL, omap34xxcam_daemon_event_cb,
			 (void *)vdev, NULL);

	rval = videobuf_streamon(&ofh->vbq);
	if (rval) {
		isp_stop(isp);
		omap34xxcam_slave_power_set(
			vdev, V4L2_POWER_STANDBY,
			OMAP34XXCAM_SLAVE_POWER_ALL);
	} else
		vdev->streaming = file;

out:
	mutex_unlock(&vdev->mutex);

	if (!rval)
		omap34xxcam_daemon_req_hw_reconfig(
			vdev,
			OMAP34XXCAM_DAEMON_HW_RECONFIG_STREAMON);

	return rval;
}

/**
 * vidioc_streamoff - V4L2 streamoff IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @i: V4L2 buffer type
 *
 * Attempts to stop streaming by flushing all scheduled work, waiting on
 * any queued buffers to complete and then stopping the ISP and turning
 * off video buffer streaming through the video buffer library API.  Upon
 * success the function returns 0, otherwise an error code is returned.
 */
static int vidioc_streamoff(struct file *file, void *fh, enum v4l2_buf_type i)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct device *isp = vdev->cam->isp;
	struct videobuf_queue *q = &ofh->vbq;
	int rval;

	omap34xxcam_daemon_req_hw_reconfig(
		vdev, OMAP34XXCAM_DAEMON_HW_RECONFIG_STREAMOFF);

	mutex_lock(&vdev->mutex);

	if (vdev->streaming == file)
		isp_stop(isp);

	rval = videobuf_streamoff(q);
	if (!rval) {
		vdev->streaming = NULL;

		omap34xxcam_slave_power_set(vdev, V4L2_POWER_STANDBY,
					    OMAP34XXCAM_SLAVE_POWER_ALL);
		isp_unset_callback(isp, CBK_CATCHALL);
	}

	mutex_unlock(&vdev->mutex);

	return rval;
}

/**
 * vidioc_enum_input - V4L2 enumerate input IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @inp: V4L2 input type information structure
 *
 * Fills in v4l2_input structure.  Returns 0.
 */
static int vidioc_enum_input(struct file *file, void *fh,
			     struct v4l2_input *inp)
{
	if (inp->index > 0)
		return -EINVAL;

	strlcpy(inp->name, "camera", sizeof(inp->name));
	inp->type = V4L2_INPUT_TYPE_CAMERA;

	return 0;
}

/**
 * vidioc_g_input - V4L2 get input IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @i: address to hold index of input supported
 *
 * Sets index to 0.
 */
static int vidioc_g_input(struct file *file, void *fh, unsigned int *i)
{
	*i = 0;

	return 0;
}

/**
 * vidioc_s_input - V4L2 set input IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @i: index of input selected
 *
 * 0 is only index supported.
 */
static int vidioc_s_input(struct file *file, void *fh, unsigned int i)
{
	if (i > 0)
		return -EINVAL;

	return 0;
}

/**
 * vidioc_queryctrl - V4L2 query control IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @a: standard V4L2 query control ioctl structure
 *
 * If the requested control is supported, returns the control information
 * in the v4l2_queryctrl structure.  Otherwise, returns -EINVAL if the
 * control is not supported.  If the sensor being used is a "smart sensor",
 * this request is passed to the sensor driver, otherwise the ISP is
 * queried and if it does not support the requested control, the request
 * is forwarded to the "raw" sensor driver to see if it supports it.
 */
static int vidioc_queryctrl(struct file *file, void *fh,
			    struct v4l2_queryctrl *a)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct v4l2_queryctrl a_tmp;
	int best_slave = -1;
	u32 best_ctrl = (u32)-1;
	int i;

	if (vdev->vdev_sensor_config.sensor_isp)
		return vidioc_int_queryctrl(vdev->vdev_sensor, a);

	/* No next flags: try slaves directly. */
	if (!(a->id & V4L2_CTRL_FLAG_NEXT_CTRL)) {
		for (i = 0; i <= OMAP34XXCAM_SLAVE_FLASH; i++) {
			if (!vidioc_int_queryctrl(vdev->slave[i], a))
				return 0;
		}
		return isp_queryctrl(a);
	}

	/* Find slave with smallest next control id. */
	for (i = 0; i <= OMAP34XXCAM_SLAVE_FLASH; i++) {
		a_tmp = *a;

		if (vidioc_int_queryctrl(vdev->slave[i], &a_tmp))
			continue;

		if (a_tmp.id < best_ctrl) {
			best_slave = i;
			best_ctrl = a_tmp.id;
		}
	}

	a_tmp = *a;
	if (!isp_queryctrl(&a_tmp)) {
		if (a_tmp.id < best_ctrl) {
			*a = a_tmp;

			return 0;
		}
	}

	if (best_slave == -1)
		return -EINVAL;

	a->id = best_ctrl;
	return vidioc_int_queryctrl(vdev->slave[best_slave], a);
}

/**
 * vidioc_querymenu - V4L2 query menu IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @a: standard V4L2 query menu ioctl structure
 *
 * If the requested control is supported, returns the menu information
 * in the v4l2_querymenu structure.  Otherwise, returns -EINVAL if the
 * control is not supported or is not a menu.  If the sensor being used
 * is a "smart sensor", this request is passed to the sensor driver,
 * otherwise the ISP is queried and if it does not support the requested
 * menu control, the request is forwarded to the "raw" sensor driver to
 * see if it supports it.
 */
static int vidioc_querymenu(struct file *file, void *fh,
			    struct v4l2_querymenu *a)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	int i;

	if (vdev->vdev_sensor_config.sensor_isp)
		return vidioc_int_querymenu(vdev->vdev_sensor, a);

	/* Try slaves directly. */
	for (i = 0; i <= OMAP34XXCAM_SLAVE_FLASH; i++) {
		if (!vidioc_int_querymenu(vdev->slave[i], a))
			return 0;
	}
	return isp_querymenu(a);
}

static int vidioc_g_ext_ctrls(struct file *file, void *fh,
			      struct v4l2_ext_controls *a)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct device *isp = vdev->cam->isp;
	int i, ctrl_idx, rval = 0;

	mutex_lock(&vdev->mutex);

	for (ctrl_idx = 0; ctrl_idx < a->count; ctrl_idx++) {
		struct v4l2_control ctrl;

		ctrl.id = a->controls[ctrl_idx].id;

		if (vdev->vdev_sensor_config.sensor_isp) {
			rval = vidioc_int_g_ctrl(vdev->vdev_sensor, &ctrl);
		} else {
			for (i = 0; i <= OMAP34XXCAM_SLAVE_FLASH; i++) {
				rval = vidioc_int_g_ctrl(vdev->slave[i], &ctrl);
				if (!rval)
					break;
			}
		}

		if (rval)
			rval = isp_g_ctrl(isp, &ctrl);

		if (rval) {
			a->error_idx = ctrl_idx;
			break;
		}

		a->controls[ctrl_idx].value = ctrl.value;
	}

	mutex_unlock(&vdev->mutex);

	return rval;
}

static int vidioc_s_ext_ctrls(struct file *file, void *fh,
			      struct v4l2_ext_controls *a)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct device *isp = vdev->cam->isp;
	int i, ctrl_idx, rval = 0;

	mutex_lock(&vdev->mutex);

	for (ctrl_idx = 0; ctrl_idx < a->count; ctrl_idx++) {
		struct v4l2_control ctrl;

		ctrl.id = a->controls[ctrl_idx].id;
		ctrl.value = a->controls[ctrl_idx].value;

		if (vdev->vdev_sensor_config.sensor_isp) {
			rval = vidioc_int_s_ctrl(vdev->vdev_sensor, &ctrl);
		} else {
			for (i = 0; i <= OMAP34XXCAM_SLAVE_FLASH; i++) {
				rval = vidioc_int_s_ctrl(vdev->slave[i], &ctrl);
				if (!rval)
					break;
			}
		}

		if (rval)
			rval = isp_s_ctrl(isp, &ctrl);

		if (rval) {
			a->error_idx = ctrl_idx;
			break;
		}

		a->controls[ctrl_idx].value = ctrl.value;
	}

	mutex_unlock(&vdev->mutex);

	return rval;
}

/**
 * vidioc_g_parm - V4L2 get parameters IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @a: standard V4L2 stream parameters structure
 *
 * If request is for video capture buffer type, handles request by
 * forwarding to sensor driver.
 */
static int vidioc_g_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	int rval;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	mutex_lock(&vdev->mutex);
	rval = vidioc_int_g_parm(vdev->vdev_sensor, a);
	mutex_unlock(&vdev->mutex);

	return rval;
}

/**
 * vidioc_s_parm - V4L2 set parameters IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @a: standard V4L2 stream parameters structure
 *
 * If request is for video capture buffer type, handles request by
 * first getting current stream parameters from sensor, then forwarding
 * request to set new parameters to sensor driver.  It then attempts to
 * enable the sensor interface with the new parameters.  If this fails, it
 * reverts back to the previous parameters.
 */
static int vidioc_s_parm(struct file *file, void *fh, struct v4l2_streamparm *a)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct v4l2_pix_format pix_tmp_sensor, pix_tmp;
	int rval;

	if (a->type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
		return -EINVAL;

	if (vdev->vdev_sensor == v4l2_int_device_dummy())
		return -EINVAL;

	mutex_lock(&vdev->mutex);
	if (vdev->streaming) {
		rval = -EBUSY;
		goto out;
	}

	vdev->want_timeperframe = a->parm.capture.timeperframe;

	pix_tmp = vdev->want_pix;

	rval = s_pix_parm(vdev, &pix_tmp_sensor, &pix_tmp,
			  &a->parm.capture.timeperframe);

out:
	mutex_unlock(&vdev->mutex);

	return rval;
}

/**
 * vidioc_cropcap - V4L2 crop capture IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @a: standard V4L2 crop capture structure
 *
 * If using a "smart" sensor, just forwards request to the sensor driver,
 * otherwise fills in the v4l2_cropcap values locally.
 */
static int vidioc_cropcap(struct file *file, void *fh, struct v4l2_cropcap *a)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct v4l2_cropcap *cropcap = a;
	int rval;

	if (vdev->vdev_sensor == v4l2_int_device_dummy())
		return -EINVAL;

	mutex_lock(&vdev->mutex);

	rval = vidioc_int_cropcap(vdev->vdev_sensor, a);

	if (rval && !vdev->vdev_sensor_config.sensor_isp) {
		struct v4l2_format f;

		/* cropcap failed, try to do this via g_fmt_cap */
		rval = vidioc_int_g_fmt_cap(vdev->vdev_sensor, &f);
		if (!rval) {
			cropcap->bounds.top = 0;
			cropcap->bounds.left = 0;
			cropcap->bounds.width = f.fmt.pix.width;
			cropcap->bounds.height = f.fmt.pix.height;
			cropcap->defrect = cropcap->bounds;
			cropcap->pixelaspect.numerator = 1;
			cropcap->pixelaspect.denominator = 1;
		}
	}

	mutex_unlock(&vdev->mutex);

	return rval;
}

/**
 * vidioc_g_crop - V4L2 get capture crop IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @a: standard V4L2 crop structure
 *
 * If using a "smart" sensor, just forwards request to the sensor driver,
 * otherwise calls the isp functions to fill in current crop values.
 */
static int vidioc_g_crop(struct file *file, void *fh, struct v4l2_crop *a)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct device *isp = vdev->cam->isp;
	int rval = 0;

	if (vdev->vdev_sensor == v4l2_int_device_dummy())
		return -EINVAL;

	mutex_lock(&vdev->mutex);

	if (vdev->vdev_sensor_config.sensor_isp)
		rval = vidioc_int_g_crop(vdev->vdev_sensor, a);
	else
		rval = isp_g_crop(isp, a);

	mutex_unlock(&vdev->mutex);

	return rval;
}

/**
 * vidioc_s_crop - V4L2 set capture crop IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @a: standard V4L2 crop structure
 *
 * If using a "smart" sensor, just forwards request to the sensor driver,
 * otherwise calls the isp functions to set the current crop values.
 */
static int vidioc_s_crop(struct file *file, void *fh, struct v4l2_crop *a)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct device *isp = vdev->cam->isp;
	int rval = 0;

	if (vdev->vdev_sensor == v4l2_int_device_dummy())
		return -EINVAL;

	omap34xxcam_daemon_req_hw_reconfig(
		vdev,
		OMAP34XXCAM_DAEMON_HW_RECONFIG_CROP);

	mutex_lock(&vdev->mutex);

	if (vdev->vdev_sensor_config.sensor_isp)
		rval = vidioc_int_s_crop(vdev->vdev_sensor, a);
	else
		rval = isp_s_crop(isp, a);

	mutex_unlock(&vdev->mutex);

	return rval;
}

static int vidioc_enum_framesizes(struct file *file, void *fh,
				  struct v4l2_frmsizeenum *frms)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct v4l2_pix_format pix_in;
	struct v4l2_pix_format pix_out;
	struct v4l2_fract ival;
	u32 pixel_format;
	int rval;

	if (vdev->vdev_sensor == v4l2_int_device_dummy())
		return -EINVAL;

	mutex_lock(&vdev->mutex);

	if (vdev->vdev_sensor_config.sensor_isp) {
		rval = vidioc_int_enum_framesizes(vdev->vdev_sensor, frms);
		goto done;
	}

	pixel_format = frms->pixel_format;
	frms->pixel_format = -1;	/* ISP does format conversion */
	rval = vidioc_int_enum_framesizes(vdev->vdev_sensor, frms);
	frms->pixel_format = pixel_format;

	if (rval < 0)
		goto done;

	/* Let the ISP pipeline mangle the frame size as it sees fit. */
	memset(&pix_out, 0, sizeof(pix_out));
	pix_out.width = frms->discrete.width;
	pix_out.height = frms->discrete.height;
	pix_out.pixelformat = frms->pixel_format;

	ival.numerator = 0;
	ival.denominator = 0;
	rval = try_pix_parm(vdev, &pix_in, &pix_out, &ival);
	if (rval < 0)
		goto done;

	frms->discrete.width = pix_out.width;
	frms->discrete.height = pix_out.height;

done:
	mutex_unlock(&vdev->mutex);
	return rval;
}

static int vidioc_enum_frameintervals(struct file *file, void *fh,
				      struct v4l2_frmivalenum *frmi)
{
	struct omap34xxcam_fh *ofh = fh;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct v4l2_frmsizeenum frms;
	unsigned int frmi_width;
	unsigned int frmi_height;
	unsigned int width;
	unsigned int height;
	unsigned int max_dist;
	unsigned int dist;
	u32 pixel_format;
	unsigned int i;
	int rval;

	mutex_lock(&vdev->mutex);

	if (vdev->vdev_sensor_config.sensor_isp) {
		rval = vidioc_int_enum_frameintervals(vdev->vdev_sensor, frmi);
		goto done;
	}

	/*
	 * Frame size enumeration returned sizes mangled by the ISP.
	 * We can't pass the size directly to the sensor for frame
	 * interval enumeration, as they will not be recognized by the
	 * sensor driver. Enumerate the native sensor sizes and select
	 * the one closest to the requested size.
	 */

	for (i = 0, max_dist = (unsigned int)-1; ; ++i) {
		frms.index = i;
		frms.pixel_format = -1;
		rval = vidioc_int_enum_framesizes(vdev->vdev_sensor,
			&frms);
		if (rval < 0)
			break;

		/*
		 * The distance between frame sizes is the size in
		 * pixels of the non-overlapping regions.
		 */
		dist = min(frms.discrete.width, frmi->width)
		     * min(frms.discrete.height, frmi->height);
		dist = frms.discrete.width * frms.discrete.height
		     + frmi->width * frmi->height
		     - 2*dist;

		if (dist < max_dist) {
			width = frms.discrete.width;
			height = frms.discrete.height;
			max_dist = dist;
		}
	}

	if (max_dist == (unsigned int)-1) {
		rval = -EINVAL;
		goto done;
	}

	pixel_format = frmi->pixel_format;
	frmi_width = frmi->width;
	frmi_height = frmi->height;

	frmi->pixel_format = -1;	/* ISP does format conversion */
	frmi->width = width;
	frmi->height = height;
	rval = vidioc_int_enum_frameintervals(vdev->vdev_sensor, frmi);

	frmi->pixel_format = pixel_format;
	frmi->height = frmi_height;
	frmi->width = frmi_width;

done:
	mutex_unlock(&vdev->mutex);
	return rval;
}

/**
 * vidioc_default - private IOCTL handler
 * @file: ptr. to system file structure
 * @fh: ptr to hold address of omap34xxcam_fh struct (per-filehandle data)
 * @cmd: ioctl cmd value
 * @arg: ioctl arg value
 *
 * If the sensor being used is a "smart sensor", this request is returned to
 * caller with -EINVAL err code.  Otherwise if the control id is the private
 * VIDIOC_PRIVATE_ISP_AEWB_REQ to update the analog gain or exposure,
 * then this request is forwared directly to the sensor to incorporate the
 * feedback. The request is then passed on to the ISP private IOCTL handler,
 * isp_handle_private()
 */
static int vidioc_default(struct file *file, void *fh, int cmd, void *arg)
{
	struct omap34xxcam_fh *ofh = file->private_data;
	struct omap34xxcam_videodev *vdev = ofh->vdev;
	struct device *isp = vdev->cam->isp;
	int rval;

	if (vdev->vdev_sensor_config.sensor_isp) {
		rval = -EINVAL;
	} else {
		switch (cmd) {
		case VIDIOC_ENUM_FRAMESIZES:
			rval = vidioc_enum_framesizes(file, fh, arg);
			goto out;
		case VIDIOC_ENUM_FRAMEINTERVALS:
			rval = vidioc_enum_frameintervals(file, fh, arg);
			goto out;
		case VIDIOC_DAEMON_REQ:
			rval = omap34xxcam_daemon_req_user(vdev, arg, file);
			goto out;
		case VIDIOC_DAEMON_INSTALL:
			rval = omap34xxcam_daemon_install(file);
			goto out;
		case VIDIOC_DAEMON_SET_EVENTS:
			rval = omap34xxcam_daemon_set_events(vdev, arg, file);
			goto out;
		case VIDIOC_DAEMON_DAEMON_REQ_GET:
			rval = omap34xxcam_daemon_daemon_req_get_user(vdev,
								      arg,
								      file);
			goto out;
		case VIDIOC_DAEMON_DAEMON_REQ_COMPLETE:
			rval = omap34xxcam_daemon_daemon_req_complete_user
				(vdev, arg, file);
			goto out;
		case VIDIOC_PRIVATE_ISP_AEWB_REQ:
		{
			/* Need to update sensor first */
			struct isph3a_aewb_data *data;
			struct v4l2_control vc;

			data = (struct isph3a_aewb_data *) arg;
			if (data->update & SET_EXPOSURE) {
				dev_dbg(&vdev->vfd->dev, "using "
					"VIDIOC_PRIVATE_ISP_AEWB_REQ to set "
					"exposure is deprecated!\n");
				vc.id = V4L2_CID_EXPOSURE;
				vc.value = data->shutter;
				mutex_lock(&vdev->mutex);
				rval = vidioc_int_s_ctrl(vdev->vdev_sensor,
							 &vc);
				mutex_unlock(&vdev->mutex);
				if (rval)
					goto out;
			}
			if (data->update & SET_ANALOG_GAIN) {
				dev_dbg(&vdev->vfd->dev, "using "
					"VIDIOC_PRIVATE_ISP_AEWB_REQ to set "
					"gain is deprecated!\n");
				vc.id = V4L2_CID_GAIN;
				vc.value = data->gain;
				mutex_lock(&vdev->mutex);
				rval = vidioc_int_s_ctrl(vdev->vdev_sensor,
							 &vc);
				mutex_unlock(&vdev->mutex);
				if (rval)
					goto out;
			}
		}
		break;
		case VIDIOC_PRIVATE_ISP_AF_REQ: {
			/* Need to update lens first */
			struct isp_af_data *data;
			struct v4l2_control vc;

			if (!vdev->vdev_lens) {
				rval = -EINVAL;
				goto out;
			}
			data = (struct isp_af_data *) arg;
			if (data->update & LENS_DESIRED_POSITION) {
				dev_dbg(&vdev->vfd->dev, "using "
					"VIDIOC_PRIVATE_ISP_AF_REQ to set "
					"lens position is deprecated!\n");
				vc.id = V4L2_CID_FOCUS_ABSOLUTE;
				vc.value = data->desired_lens_direction;
				mutex_lock(&vdev->mutex);
				rval = vidioc_int_s_ctrl(vdev->vdev_lens, &vc);
				mutex_unlock(&vdev->mutex);
				if (rval)
					goto out;
			}
		}
			break;
		}

		mutex_lock(&vdev->mutex);
		rval = isp_handle_private(isp, cmd, arg);
		mutex_unlock(&vdev->mutex);
	}
out:
	return rval;
}

/*
 *
 * File operations.
 *
 */

static long omap34xxcam_unlocked_ioctl(struct file *file, unsigned int cmd,
				       unsigned long arg)
{
	return (long)video_ioctl2(file->f_dentry->d_inode, file, cmd, arg);
}

/**
 * omap34xxcam_poll - file operations poll handler
 * @file: ptr. to system file structure
 * @wait: system poll table structure
 *
 */
static unsigned int omap34xxcam_poll(struct file *file,
				     struct poll_table_struct *wait)
{
	struct omap34xxcam_fh *fh = file->private_data;
	struct omap34xxcam_videodev *vdev = fh->vdev;
	struct videobuf_buffer *vb;

	if (file == vdev->daemon.file) {
		unsigned long flags;
		u32 pending;

		poll_wait(file, &vdev->daemon.poll_wait, wait);

		spin_lock_irqsave(&vdev->daemon.event_lock, flags);
		pending = vdev->daemon.req_pending;
		spin_unlock_irqrestore(&vdev->daemon.event_lock, flags);

		if (pending)
			return POLLIN | POLLRDNORM;
		else
			return 0;
	}

	mutex_lock(&vdev->mutex);
	if (vdev->streaming != file) {
		mutex_unlock(&vdev->mutex);
		return POLLERR;
	}
	mutex_unlock(&vdev->mutex);

	mutex_lock(&fh->vbq.vb_lock);
	if (list_empty(&fh->vbq.stream)) {
		mutex_unlock(&fh->vbq.vb_lock);
		return POLLERR;
	}
	vb = list_entry(fh->vbq.stream.next, struct videobuf_buffer, stream);
	mutex_unlock(&fh->vbq.vb_lock);

	poll_wait(file, &vb->done, wait);

	if (vb->state == VIDEOBUF_DONE || vb->state == VIDEOBUF_ERROR)
		return POLLIN | POLLRDNORM;

	return 0;
}

/**
 * omap34xxcam_mmap - file operations mmap handler
 * @file: ptr. to system file structure
 * @vma: system virt. mem. area structure
 *
 * Maps a virtual memory area via the video buffer API
 */
static int omap34xxcam_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct omap34xxcam_fh *fh = file->private_data;
	return videobuf_mmap_mapper(&fh->vbq, vma);
}

/**
 * omap34xxcam_open - file operations open handler
 * @inode: ptr. to system inode structure
 * @file: ptr. to system file structure
 *
 * Allocates and initializes the per-filehandle data (omap34xxcam_fh),
 * enables the sensor, opens/initializes the ISP interface and the
 * video buffer queue.  Note that this function will allow multiple
 * file handles to be open simultaneously, however only the first
 * handle opened will initialize the ISP.  It is the application
 * responsibility to only use one handle for streaming and the others
 * for control only.
 * This function returns 0 upon success and -ENODEV upon error.
 */
static int omap34xxcam_open(struct inode *inode, struct file *file)
{
	int rval = 0;
	struct omap34xxcam_videodev *vdev = NULL;
	struct omap34xxcam_device *cam = omap34xxcam;
	struct device *isp;
	struct omap34xxcam_fh *fh;
	struct v4l2_format sensor_format;
	int first_user = 0;
	int i;

	for (i = 0; i < OMAP34XXCAM_VIDEODEVS; i++) {
		if (cam->vdevs[i].vfd
		    && cam->vdevs[i].vfd->minor ==
		    iminor(file->f_dentry->d_inode)) {
			vdev = &cam->vdevs[i];
			break;
		}
	}

	if (!vdev || !vdev->vfd)
		return -ENODEV;

	fh = kzalloc(sizeof(*fh), GFP_KERNEL);
	if (fh == NULL)
		return -ENOMEM;

	fh->vdev = vdev;

	mutex_lock(&vdev->mutex);
	for (i = 0; i <= OMAP34XXCAM_SLAVE_FLASH; i++) {
		if (vdev->slave[i] != v4l2_int_device_dummy()
		    && !try_module_get(vdev->slave[i]->module)) {
			mutex_unlock(&vdev->mutex);
			dev_err(&vdev->vfd->dev, "can't try_module_get %s\n",
				vdev->slave[i]->name);
			rval = -ENODEV;
			goto out_try_module_get;
		}
	}

	if (atomic_inc_return(&vdev->users) == 1) {
		first_user = 1;
		isp = isp_get();
		if (!isp) {
			rval = -EBUSY;
			dev_err(&vdev->vfd->dev, "can't get isp\n");
			goto out_isp_get;
		}
		cam->isp = isp;
		if (omap34xxcam_slave_power_set(vdev, V4L2_POWER_STANDBY,
						OMAP34XXCAM_SLAVE_POWER_ALL)) {
			dev_err(&vdev->vfd->dev, "can't power up slaves\n");
			rval = -EBUSY;
			goto out_slave_power_set_standby;
		}
	}

	if (vdev->vdev_sensor == v4l2_int_device_dummy() || !first_user)
		goto out_no_pix;

	/* Get the format the sensor is using. */
	rval = vidioc_int_g_fmt_cap(vdev->vdev_sensor, &sensor_format);
	if (rval) {
		dev_err(&vdev->vfd->dev,
			"can't get current pix from sensor!\n");
		goto out_vidioc_int_g_fmt_cap;
	}

	if (!vdev->pix.width)
		vdev->pix = sensor_format.fmt.pix;

	if (!vdev->vdev_sensor_config.sensor_isp) {
		struct v4l2_pix_format pix;
		struct v4l2_fract timeperframe =
			vdev->want_timeperframe;

		rval = s_pix_parm(vdev, &pix, &vdev->pix, &timeperframe);
		if (rval) {
			dev_err(&vdev->vfd->dev,
				"isp doesn't like the sensor!\n");
			goto out_isp_s_fmt_cap;
		}
	}

out_no_pix:
	mutex_unlock(&vdev->mutex);

	if (first_user && vdev->daemon.file) {
		rval = omap34xxcam_daemon_req_hw_init(vdev);
		if (rval) {
			mutex_lock(&vdev->mutex);
			goto out_slave_power_set_standby;
		}
	}

	file->private_data = fh;

	spin_lock_init(&fh->vbq_lock);

	videobuf_queue_sg_init(&fh->vbq, &omap34xxcam_vbq_ops, NULL,
				&fh->vbq_lock, V4L2_BUF_TYPE_VIDEO_CAPTURE,
				V4L2_FIELD_NONE,
				sizeof(struct videobuf_buffer), fh);

	return 0;

out_isp_s_fmt_cap:
out_vidioc_int_g_fmt_cap:
	omap34xxcam_slave_power_set(vdev, V4L2_POWER_OFF,
				    OMAP34XXCAM_SLAVE_POWER_ALL);
out_slave_power_set_standby:
	isp_put();

out_isp_get:
	atomic_dec(&vdev->users);
	mutex_unlock(&vdev->mutex);

out_try_module_get:
	for (i--; i >= 0; i--)
		if (vdev->slave[i] != v4l2_int_device_dummy())
			module_put(vdev->slave[i]->module);

	kfree(fh);

	return rval;
}

/**
 * omap34xxcam_release - file operations release handler
 * @inode: ptr. to system inode structure
 * @file: ptr. to system file structure
 *
 * Complement of omap34xxcam_open.  This function will flush any scheduled
 * work, disable the sensor, close the ISP interface, stop the
 * video buffer queue from streaming and free the per-filehandle data
 * (omap34xxcam_fh).  Note that because multiple open file handles
 * are allowed, this function will only close the ISP and disable the
 * sensor when the last open file handle (by count) is closed.
 * This function returns 0.
 */
static int omap34xxcam_release(struct inode *inode, struct file *file)
{
	struct omap34xxcam_fh *fh = file->private_data;
	struct omap34xxcam_videodev *vdev = fh->vdev;
	struct device *isp = vdev->cam->isp;
	int i;

	if (omap34xxcam_daemon_release(vdev, file))
		goto daemon_out;

	mutex_lock(&vdev->mutex);
	if (vdev->streaming == file) {
		isp_stop(isp);
		videobuf_streamoff(&fh->vbq);
		omap34xxcam_slave_power_set(vdev, V4L2_POWER_STANDBY,
					    OMAP34XXCAM_SLAVE_POWER_ALL);
		vdev->streaming = NULL;
	}

	if (atomic_dec_return(&vdev->users) == 0) {
		omap34xxcam_slave_power_set(vdev, V4L2_POWER_OFF,
					    OMAP34XXCAM_SLAVE_POWER_ALL);
		isp_put();
	}
	mutex_unlock(&vdev->mutex);

daemon_out:
	file->private_data = NULL;

	for (i = 0; i <= OMAP34XXCAM_SLAVE_FLASH; i++)
		if (vdev->slave[i] != v4l2_int_device_dummy())
			module_put(vdev->slave[i]->module);

	kfree(fh);

	return 0;
}

static struct file_operations omap34xxcam_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.unlocked_ioctl = omap34xxcam_unlocked_ioctl,
	.poll = omap34xxcam_poll,
	.mmap = omap34xxcam_mmap,
	.open = omap34xxcam_open,
	.release = omap34xxcam_release,
};

static void omap34xxcam_vfd_name_update(struct omap34xxcam_videodev *vdev)
{
	struct video_device *vfd = vdev->vfd;
	int i;

	strlcpy(vfd->name, CAM_SHORT_NAME, sizeof(vfd->name));
	for (i = 0; i <= OMAP34XXCAM_SLAVE_FLASH; i++) {
		strlcat(vfd->name, "/", sizeof(vfd->name));
		if (vdev->slave[i] == v4l2_int_device_dummy())
			continue;
		strlcat(vfd->name, vdev->slave[i]->name, sizeof(vfd->name));
	}
	dev_dbg(&vdev->vfd->dev, "video%d is now %s\n", vfd->num, vfd->name);
}

/**
 * omap34xxcam_device_unregister - V4L2 detach handler
 * @s: ptr. to standard V4L2 device information structure
 *
 * Detach sensor and unregister and release the video device.
 */
static void omap34xxcam_device_unregister(struct v4l2_int_device *s)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	struct omap34xxcam_hw_config hwc;

	BUG_ON(vidioc_int_g_priv(s, &hwc) < 0);

	mutex_lock(&vdev->mutex);

	if (vdev->slave[hwc.dev_type] != v4l2_int_device_dummy()) {
		vdev->slave[hwc.dev_type] = v4l2_int_device_dummy();
		vdev->slaves--;
		omap34xxcam_vfd_name_update(vdev);
	}

	if (vdev->slaves == 0 && vdev->vfd) {
		if (vdev->vfd->minor == -1) {
			/*
			 * The device was never registered, so release the
			 * video_device struct directly.
			 */
			video_device_release(vdev->vfd);
		} else {
			/*
			 * The unregister function will release the
			 * video_device struct as well as
			 * unregistering it.
			 */
			video_unregister_device(vdev->vfd);
		}
		vdev->vfd = NULL;
	}

	mutex_unlock(&vdev->mutex);
}

static const struct v4l2_ioctl_ops omap34xxcam_ioctl_ops = {
	.vidioc_querycap	 = vidioc_querycap,
	.vidioc_enum_fmt_vid_cap = vidioc_enum_fmt_vid_cap,
	.vidioc_g_fmt_vid_cap	 = vidioc_g_fmt_vid_cap,
	.vidioc_s_fmt_vid_cap	 = vidioc_s_fmt_vid_cap,
	.vidioc_try_fmt_vid_cap	 = vidioc_try_fmt_vid_cap,
	.vidioc_reqbufs		 = vidioc_reqbufs,
	.vidioc_querybuf	 = vidioc_querybuf,
	.vidioc_qbuf		 = vidioc_qbuf,
	.vidioc_dqbuf		 = vidioc_dqbuf,
	.vidioc_streamon	 = vidioc_streamon,
	.vidioc_streamoff	 = vidioc_streamoff,
	.vidioc_enum_input	 = vidioc_enum_input,
	.vidioc_g_input		 = vidioc_g_input,
	.vidioc_s_input		 = vidioc_s_input,
	.vidioc_queryctrl	 = vidioc_queryctrl,
	.vidioc_querymenu	 = vidioc_querymenu,
	.vidioc_g_ext_ctrls	 = vidioc_g_ext_ctrls,
	.vidioc_s_ext_ctrls	 = vidioc_s_ext_ctrls,
	.vidioc_g_parm		 = vidioc_g_parm,
	.vidioc_s_parm		 = vidioc_s_parm,
	.vidioc_cropcap		 = vidioc_cropcap,
	.vidioc_g_crop		 = vidioc_g_crop,
	.vidioc_s_crop		 = vidioc_s_crop,
	.vidioc_default		 = vidioc_default,
};

/**
 * omap34xxcam_device_register - V4L2 attach handler
 * @s: ptr. to standard V4L2 device information structure
 *
 * Allocates and initializes the V4L2 video_device structure, initializes
 * the sensor, and finally
 registers the device with V4L2 based on the
 * video_device structure.
 *
 * Returns 0 on success, otherwise an appropriate error code on
 * failure.
 */
static int omap34xxcam_device_register(struct v4l2_int_device *s)
{
	struct omap34xxcam_videodev *vdev = s->u.slave->master->priv;
	struct omap34xxcam_hw_config hwc;
	struct device *isp;
	int rval;

	/* We need to check rval just once. The place is here. */
	if (vidioc_int_g_priv(s, &hwc))
		return -ENODEV;

	if (vdev->index != hwc.dev_index)
		return -ENODEV;

	if (hwc.dev_type < 0 || hwc.dev_type > OMAP34XXCAM_SLAVE_FLASH)
		return -EINVAL;

	if (vdev->slave[hwc.dev_type] != v4l2_int_device_dummy())
		return -EBUSY;

	mutex_lock(&vdev->mutex);
	if (atomic_read(&vdev->users)) {
		printk(KERN_ERR "%s: we're open (%d), can't register\n",
		       __func__, atomic_read(&vdev->users));
		mutex_unlock(&vdev->mutex);
		return -EBUSY;
	}

	vdev->slaves++;
	vdev->slave[hwc.dev_type] = s;
	vdev->slave_config[hwc.dev_type] = hwc;

	if (hwc.dev_type == OMAP34XXCAM_SLAVE_SENSOR) {
		isp = isp_get();
		if (!isp) {
			rval = -EBUSY;
			printk(KERN_ERR "%s: can't get ISP, "
			       "sensor init failed\n", __func__);
			goto err;
		}
		vdev->cam->isp = isp;
	}
	rval = vidioc_int_dev_init(s);
	if (rval)
		goto err_omap34xxcam_slave_init;
	if (hwc.dev_type == OMAP34XXCAM_SLAVE_SENSOR) {
		struct v4l2_format format;

		format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		rval = vidioc_int_g_fmt_cap(vdev->vdev_sensor, &format);
		if (rval)
			rval = -EBUSY;

		vdev->want_pix = format.fmt.pix;
	}
	omap34xxcam_slave_power_set(vdev, V4L2_POWER_OFF, 1 << hwc.dev_type);
	if (hwc.dev_type == OMAP34XXCAM_SLAVE_SENSOR)
		isp_put();

	/* Are we the first slave? */
	if (vdev->slaves == 1) {
		/* initialize the video_device struct */
		vdev->vfd = video_device_alloc();
		if (!vdev->vfd) {
			printk(KERN_ERR "%s: could not allocate "
			       "video device struct\n", __func__);
			rval = -ENOMEM;
			goto err;
		}
		vdev->vfd->release	= video_device_release;
		vdev->vfd->minor	= -1;
		vdev->vfd->fops		= &omap34xxcam_fops;
		vdev->vfd->ioctl_ops	= &omap34xxcam_ioctl_ops;
		video_set_drvdata(vdev->vfd, vdev);

		if (video_register_device(vdev->vfd, VFL_TYPE_GRABBER,
					  hwc.dev_minor) < 0) {
			printk(KERN_ERR "%s: could not register V4L device\n",
				__func__);
			vdev->vfd->minor = -1;
			rval = -EBUSY;
			goto err;
		}
	}

	omap34xxcam_vfd_name_update(vdev);

	mutex_unlock(&vdev->mutex);

	omap34xxcam_daemon_init(vdev);

	return 0;

err_omap34xxcam_slave_init:
	if (hwc.dev_type == OMAP34XXCAM_SLAVE_SENSOR)
		isp_put();

err:
	if (s == vdev->slave[hwc.dev_type]) {
		vdev->slave[hwc.dev_type] = v4l2_int_device_dummy();
		vdev->slaves--;
	}

	mutex_unlock(&vdev->mutex);
	omap34xxcam_device_unregister(s);

	return rval;
}

static struct v4l2_int_master omap34xxcam_master = {
	.attach = omap34xxcam_device_register,
	.detach = omap34xxcam_device_unregister,
};

/*
 *
 * Module initialisation and deinitialisation
 *
 */

static void omap34xxcam_exit(void)
{
	struct omap34xxcam_device *cam = omap34xxcam;
	int i;

	if (!cam)
		return;

	for (i = 0; i < OMAP34XXCAM_VIDEODEVS; i++) {
		if (cam->vdevs[i].cam == NULL)
			continue;

		v4l2_int_device_unregister(&cam->vdevs[i].master);
		cam->vdevs[i].cam = NULL;
	}

	omap34xxcam = NULL;

	kfree(cam);
}

static int __init omap34xxcam_init(void)
{
	struct omap34xxcam_device *cam;
	int i;

	cam = kzalloc(sizeof(*cam), GFP_KERNEL);
	if (!cam) {
		printk(KERN_ERR "%s: could not allocate memory\n", __func__);
		return -ENOMEM;
	}

	omap34xxcam = cam;

	for (i = 0; i < OMAP34XXCAM_VIDEODEVS; i++) {
		struct omap34xxcam_videodev *vdev = &cam->vdevs[i];
		struct v4l2_int_device *m = &vdev->master;

		m->module       = THIS_MODULE;
		strlcpy(m->name, CAM_NAME, sizeof(m->name));
		m->type         = v4l2_int_type_master;
		m->u.master     = &omap34xxcam_master;
		m->priv		= vdev;

		mutex_init(&vdev->mutex);
		vdev->index             = i;
		vdev->cam               = cam;
		vdev->vdev_sensor =
			vdev->vdev_lens =
			vdev->vdev_flash = v4l2_int_device_dummy();

		if (v4l2_int_device_register(m))
			goto err;
	}

	return 0;

err:
	omap34xxcam_exit();
	return -ENODEV;
}

MODULE_AUTHOR("Sakari Ailus <sakari.ailus@nokia.com>");
MODULE_DESCRIPTION("OMAP34xx Video for Linux camera driver");
MODULE_LICENSE("GPL");

late_initcall(omap34xxcam_init);
module_exit(omap34xxcam_exit);
