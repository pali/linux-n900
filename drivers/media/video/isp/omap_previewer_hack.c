/*
 * drivers/media/video/isp/omap_previewer.c
 *
 * Wrapper for Preview module in TI's OMAP3430 ISP
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#include <linux/mutex.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <media/v4l2-dev.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include "isp.h"
#include "ispreg.h"
#include "isppreview.h"

#define PREV_IOC_BASE			'P'
#define PREV_REQBUF			_IOWR(PREV_IOC_BASE, 1,\
						struct v4l2_requestbuffers)
#define PREV_QUERYBUF			_IOWR(PREV_IOC_BASE, 2,\
							struct v4l2_buffer)
#define PREV_SET_PARAM			_IOW(PREV_IOC_BASE, 3,\
							struct prev_params)
#define PREV_PREVIEW			_IOR(PREV_IOC_BASE, 5, int)
#define PREV_GET_CROPSIZE		_IOR(PREV_IOC_BASE, 7,\
							struct prev_cropsize)
#define PREV_QUEUEBUF			_IOWR(PREV_IOC_BASE, 8,\
							struct v4l2_buffer)
#define PREV_IOC_MAXNR			8

#define MAX_IMAGE_WIDTH			3300

#define PREV_INWIDTH_8BIT		0	/* pixel width of 8 bitS */
#define PREV_INWIDTH_10BIT		1	/* pixel width of 10 bits */

#define PREV_32BYTES_ALIGN_MASK		0xFFFFFFE0
#define PREV_16PIX_ALIGN_MASK		0xFFFFFFF0

/* list of structures */

/* device structure keeps track of global information */
struct prev_device {
	unsigned char opened;			/* state of the device */
	struct completion wfc;
	struct mutex prevwrap_mutex;
	/* spinlock for in/out videbuf queue */
	spinlock_t inout_vbq_lock;
	/* spinlock for lsc videobuf queues */
	spinlock_t lsc_vbq_lock;
	struct videobuf_queue_ops vbq_ops;	/* videobuf queue operations */
	dma_addr_t isp_addr_read;		/* Input/Output address */
	dma_addr_t isp_addr_lsc;  /* lsc address */
	struct device *isp;
	struct prev_size_params size_params;
};

/* per-filehandle data structure */
struct prev_fh {
	/* in/out videobuf queue */
	enum v4l2_buf_type inout_type;
	struct videobuf_queue inout_vbq;
	/* lsc videobuf queue */
	enum v4l2_buf_type lsc_type;
	struct videobuf_queue lsc_vbq;
	/* device structure */
	struct prev_device *device;
};

#define OMAP_PREV_NAME		"omap-previewer" /* "omap3hack"  */

#define BIT_SET(var,shift,mask,val)		\
	do {					\
		var = (var & ~(mask << shift))	\
			| (val << shift);	\
	} while (0)


#define ISP_CTRL_SBL_SHARED_RPORTB	(1 << 28)
#define ISP_CTRL_SBL_SHARED_RPORTA	(1 << 27)
#define SBL_SHARED_RPORTB			28
#define SBL_RD_RAM_EN				18

/* structure to know crop size */
struct prev_cropsize {
	int hcrop;
	int vcrop;
};

static struct isp_interface_config prevwrap_config = {
	.ccdc_par_ser = ISP_NONE,
	.dataline_shift = 0,
	.hsvs_syncdetect = ISPCTRL_SYNC_DETECT_VSRISE,
	.strobe = 0,
	.prestrobe = 0,
	.shutter = 0,
	.wait_hs_vs = 0,
};

static u32 isp_ctrl;
static int prev_major = -1;
static struct device *prev_dev;
static struct class *prev_class;
static struct prev_device *prevdevice;
static struct platform_driver omap_previewer_driver;
static u32 prev_bufsize;
static u32 lsc_bufsize;

/**
 * prev_calculate_crop - Calculate crop size according to device parameters
 * @device: Structure containing ISP preview wrapper global information
 * @crop: Structure containing crop size
 *
 * This function is used to calculate frame size reduction depending on
 * the features enabled by the application.
 **/
static int prev_calculate_crop(struct prev_device *device,
						struct prev_cropsize *crop)
{
	struct isp_device *isp = dev_get_drvdata(device->isp);
	int ret;
	struct isp_pipeline pipe;

	pipe.ccdc_out_w = pipe.ccdc_out_w_img =
		device->size_params.hsize;
	pipe.ccdc_out_h = device->size_params.vsize;

	ret = isppreview_try_pipeline(&isp->isp_prev, &pipe);

	crop->hcrop = pipe.prv_out_w;
	crop->vcrop = pipe.prv_out_h;

	return ret;
}

/**
 * prev_hw_setup - Stores the desired configuration in the proper HW registers
 * @config: Structure containing the desired configuration for ISP preview
 *          module.
 *
 * Reads the structure sent, and modifies the desired registers.
 *
 * Always returns 0.
 **/
static int prev_hw_setup(struct prev_params *config)
{
	struct prev_device *device = prevdevice;
	struct isp_device *isp = dev_get_drvdata(device->isp);
	struct isp_prev_device *isp_prev = &isp->isp_prev;

	if (config->features & PREV_AVERAGER)
		isppreview_config_averager(isp_prev, config->average);
	else
		isppreview_config_averager(isp_prev, 0);

	if (config->features & PREV_INVERSE_ALAW)
		isppreview_enable_invalaw(isp_prev, 1);
	else
		isppreview_enable_invalaw(isp_prev, 0);

	if (config->features & PREV_HORZ_MEDIAN_FILTER) {
		isppreview_config_hmed(isp_prev, config->hmf_params);
		isppreview_enable_hmed(isp_prev, 1);
	} else
		isppreview_enable_hmed(isp_prev, 0);

	if (config->features & PREV_DARK_FRAME_SUBTRACT) {
		isppreview_set_darkaddr(isp_prev, config->drkf_params.addr);
		isppreview_config_darklineoffset(isp_prev,
						 config->drkf_params.offset);
		isppreview_enable_drkframe(isp_prev, 1);
	} else
		isppreview_enable_drkframe(isp_prev, 0);

	if (config->features & PREV_LENS_SHADING) {
		isppreview_config_drkf_shadcomp(isp_prev,
						config->lens_shading_shift);
		isppreview_enable_shadcomp(isp_prev, 1);
	} else
		isppreview_enable_shadcomp(isp_prev, 0);

	return 0;
}

/**
 * prev_validate_params - Validate configuration parameters for Preview Wrapper
 * @params: Structure containing configuration parameters
 *
 * Validate configuration parameters for Preview Wrapper
 *
 * Returns 0 if successful, or -EINVAL if a parameter value is invalid.
 **/
static int prev_validate_params(struct prev_params *params)
{
	if (!params) {
		dev_err(prev_dev, "validate_params: error in argument");
		goto err_einval;
	}

	if ((params->features & PREV_AVERAGER) == PREV_AVERAGER) {
		if ((params->average != NO_AVE)
					&& (params->average != AVE_2_PIX)
					&& (params->average != AVE_4_PIX)
					&& (params->average != AVE_8_PIX)) {
			dev_err(prev_dev, "validate_params: wrong pix average\n");
			goto err_einval;
		} else if (((params->average == AVE_2_PIX)
					&& (params->size_params.hsize % 2))
					|| ((params->average == AVE_4_PIX)
					&& (params->size_params.hsize % 4))
					|| ((params->average == AVE_8_PIX)
					&& (params->size_params.hsize % 8))) {
			dev_err(prev_dev, "validate_params: "
					"wrong pix average for input size\n");
			goto err_einval;
		}
	}

	if ((params->size_params.pixsize != PREV_INWIDTH_8BIT)
					&& (params->size_params.pixsize
					!= PREV_INWIDTH_10BIT)) {
		dev_err(prev_dev, "validate_params: wrong pixsize\n");
		goto err_einval;
	}

	if (params->size_params.hsize > MAX_IMAGE_WIDTH
					|| params->size_params.hsize < 0) {
		dev_err(prev_dev, "validate_params: wrong hsize\n");
		goto err_einval;
	}

	if ((params->pix_fmt != YCPOS_YCrYCb)
					&& (YCPOS_YCbYCr != params->pix_fmt)
					&& (YCPOS_CbYCrY != params->pix_fmt)
					&& (YCPOS_CrYCbY != params->pix_fmt)) {
		dev_err(prev_dev, "validate_params: wrong pix_fmt");
		goto err_einval;
	}

	if ((params->features & PREV_DARK_FRAME_SUBTRACT)
						&& (params->features
						& PREV_DARK_FRAME_CAPTURE)) {
		dev_err(prev_dev, "validate_params: DARK FRAME CAPTURE and "
						"SUBSTRACT cannot be enabled "
						"at same time\n");
		goto err_einval;
	}
#if 0
	if (params->features & PREV_DARK_FRAME_SUBTRACT) {
		/* Is it truth place ??? */

		if (!params->drkf_params.addr
					|| (params->drkf_params.offset % 32)) {
			dev_err(prev_dev, "validate_params: dark frame address\n");
			goto err_einval;
		}
	}

	if (params->features & PREV_LENS_SHADING)
		if ((params->lens_shading_shift > 7)
					|| !params->drkf_params.addr
					|| (params->drkf_params.offset % 32)) {
			dev_err(prev_dev, "validate_params: lens shading shift\n");
			goto err_einval;
		}
#endif
	if ((params->size_params.in_pitch <= 0)
				|| (params->size_params.in_pitch % 32)) {
		params->size_params.in_pitch =
				(params->size_params.hsize * 2) & 0xFFE0;
		dev_err(prev_dev, "Error in in_pitch; new value = %d\n",
						params->size_params.in_pitch);
	}

	return 0;
err_einval:
	return -EINVAL;
}

/**
 * preview_isr - Callback from ISP driver for ISP Preview Interrupt
 * @status: ISP IRQ0STATUS register value
 * @arg1: Structure containing ISP preview wrapper global information
 * @arg2: Currently not used
 **/
static void prev_isr(unsigned long status, isp_vbq_callback_ptr arg1,
								void *arg2)
{
	struct prev_device *device = (struct prev_device *)arg1;

	if ((status & PREV_DONE) != PREV_DONE)
		return;

	if (device)
		complete(&device->wfc);
}

/*
 * Set shared ports for using dark frame (lens shading)
 */
static void prev_set_isp_ctrl(u16 mode)
{
	struct prev_device *device = prevdevice;
	u32 val;

	val = isp_reg_readl(device->isp, OMAP3_ISP_IOMEM_MAIN, ISP_CTRL);

	isp_ctrl = val;

	/* Read port used by preview module data read */
	val &= ~ISP_CTRL_SBL_SHARED_RPORTA;

	if (mode & (PREV_DARK_FRAME_SUBTRACT | PREV_LENS_SHADING)) {
		/* Read port used by preview module dark frame read */
		val &= ~ISP_CTRL_SBL_SHARED_RPORTB;
	}

	BIT_SET(val, SBL_RD_RAM_EN, 0x1, 0x1);

	/* write ISP CTRL register */
	isp_reg_writel(device->isp, val, OMAP3_ISP_IOMEM_MAIN, ISP_CTRL);
}

/*
 * Set old isp shared port configuration
 */
static void prev_unset_isp_ctrl(void)
{
	struct prev_device *device = prevdevice;
	u32 val;

	val = isp_reg_readl(device->isp, OMAP3_ISP_IOMEM_MAIN, ISP_CTRL);

	if (isp_ctrl & ISP_CTRL_SBL_SHARED_RPORTB)
		val |= ISP_CTRL_SBL_SHARED_RPORTB;

	if (isp_ctrl & ISP_CTRL_SBL_SHARED_RPORTA)
		val |= ISP_CTRL_SBL_SHARED_RPORTA;

	if (isp_ctrl & (1 << SBL_RD_RAM_EN))
		val &= ~(1 << SBL_RD_RAM_EN);

	/* write ISP CTRL register */
	isp_reg_writel(device->isp, val, OMAP3_ISP_IOMEM_MAIN, ISP_CTRL);
}

static void isp_enable_interrupts(struct device *dev, int is_raw)
{
	isp_reg_writel(dev, IRQ0ENABLE_PRV_DONE_IRQ,
		       OMAP3_ISP_IOMEM_MAIN, ISP_IRQ0ENABLE);
}

/**
 * prev_do_preview - Performs the Preview process
 * @device: Structure containing ISP preview wrapper global information
 * @arg: Currently not used
 *
 * Returns 0 if successful, or -EINVAL if the sent parameters are invalid.
 **/
static int prev_do_preview(struct prev_device *device)
{
	struct isp_device *isp = dev_get_drvdata(device->isp);
	struct isp_prev_device *isp_prev = &isp->isp_prev;
	struct prev_params *config = &isp_prev->params;
	int bpp, size;
	int ret = 0;
	struct isp_pipeline pipe;

	memset(&pipe, 0, sizeof(pipe));
	pipe.pix.pixelformat = V4L2_PIX_FMT_UYVY;

	prev_set_isp_ctrl(config->features);

	if (device->size_params.pixsize == PREV_INWIDTH_8BIT)
		bpp = 1;
	else
		bpp = 2;

	size = device->size_params.hsize *
			device->size_params.vsize * bpp;

	pipe.prv_in = PRV_RAW_MEM;
	pipe.prv_out = PREVIEW_MEM;

	isppreview_set_skip(isp_prev, 2, 0);

	pipe.ccdc_out_w = pipe.ccdc_out_w_img
		= device->size_params.hsize;
	pipe.ccdc_out_h = device->size_params.vsize & ~0xf;

	ret = isppreview_try_pipeline(&isp->isp_prev, &pipe);
	if (ret) {
		dev_err(prev_dev, "ERROR while try size!\n");
		goto out;
	}

	ret = isppreview_s_pipeline(isp_prev, &pipe);
	if (ret) {
		dev_err(prev_dev, "ERROR while config size!\n");
		goto out;
	}

	ret = isppreview_config_inlineoffset(isp_prev, pipe.prv_out_w * bpp);
	if (ret) {
		dev_err(prev_dev, "ERROR while config inline offset!\n");
		goto out;
	}

	ret = isppreview_config_outlineoffset(isp_prev,
					      pipe.prv_out_w * bpp - 32);
	if (ret) {
		dev_err(prev_dev, "ERROR while config outline offset!\n");
		goto out;
	}

	config->drkf_params.addr = device->isp_addr_lsc;

	prev_hw_setup(config);

	ret = isppreview_set_inaddr(isp_prev, device->isp_addr_read);
	if (ret) {
		dev_err(prev_dev, "ERROR while set read addr!\n");
		goto out;
	}

	ret = isppreview_set_outaddr(isp_prev, device->isp_addr_read);
	if (ret) {
		dev_err(prev_dev, "ERROR while set write addr!\n");
		goto out;
	}

	ret = isp_set_callback(device->isp, CBK_PREV_DONE, prev_isr,
			       (void *)device, (void *)NULL);
	if (ret) {
		dev_err(prev_dev, "ERROR while setting Previewer callback!\n");
		goto out;
	}
	isp_configure_interface(device->isp, &prevwrap_config);

	isp_start(device->isp);

	isp_enable_interrupts(device->isp, 0);

	isppreview_enable(isp_prev, 1);

	wait_for_completion_interruptible(&device->wfc);

#if 0
	if (device->isp_addr_read) {
		ispmmu_vunmap(device->isp_addr_read);
		device->isp_addr_read = 0;
	}
#endif

	ret = isp_unset_callback(device->isp, CBK_PREV_DONE);

	prev_unset_isp_ctrl();

	isp_stop(device->isp);

out:
	return ret;
}

static int previewer_vb_lock_vma(struct videobuf_buffer *vb, int lock)
{
	unsigned long start, end;
	struct vm_area_struct *vma;
	int rval = 0;

	if (vb->memory == V4L2_MEMORY_MMAP)
		return 0;

	if (current->flags & PF_EXITING) {
		/*
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
 * previewer_vbq_release - Videobuffer queue release
 * @q: Structure containing the videobuffer queue.
 * @vb: Structure containing the videobuffer used for previewer processing.
 **/
static void previewer_vbq_release(struct videobuf_queue *q,
						struct videobuf_buffer *vb)
{
	struct prev_fh *fh = q->priv_data;
	struct prev_device *device = fh->device;

	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		ispmmu_vunmap(device->isp, device->isp_addr_read);
		device->isp_addr_read = 0;
		spin_lock(&device->inout_vbq_lock);
		vb->state = VIDEOBUF_NEEDS_INIT;
		spin_unlock(&device->inout_vbq_lock);
	} else if (q->type == V4L2_BUF_TYPE_PRIVATE) {
		ispmmu_vunmap(device->isp, device->isp_addr_lsc);
		device->isp_addr_lsc = 0;
		spin_lock(&device->lsc_vbq_lock);
		vb->state = VIDEOBUF_NEEDS_INIT;
		spin_unlock(&device->lsc_vbq_lock);
	}

	previewer_vb_lock_vma(vb, 0);
	if (vb->memory != V4L2_MEMORY_MMAP) {
		videobuf_dma_unmap(q, videobuf_to_dma(vb));
		videobuf_dma_free(videobuf_to_dma(vb));
	}

}

/**
 * previewer_vbq_setup - Sets up the videobuffer size and validates count.
 * @q: Structure containing the videobuffer queue.
 * @cnt: Number of buffers requested
 * @size: Size in bytes of the buffer used for previewing
 *
 * Always returns 0.
 **/
static int previewer_vbq_setup(struct videobuf_queue *q,
							unsigned int *cnt,
							unsigned int *size)
{
	struct prev_fh *fh = q->priv_data;
	struct prev_device *device = fh->device;
	u32 bpp = 1;

	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
		spin_lock(&device->inout_vbq_lock);

		if (*cnt <= 0)
			*cnt = 1;

		if (*cnt > VIDEO_MAX_FRAME)
			*cnt = VIDEO_MAX_FRAME;

		if (!device->size_params.hsize ||
				!device->size_params.vsize) {
			dev_err(prev_dev, "Can't setup inout buffer size\n");
			spin_unlock(&device->inout_vbq_lock);
			return -EINVAL;
		}

		if (device->size_params.pixsize == PREV_INWIDTH_10BIT)
			bpp = 2;

		*size = prev_bufsize = bpp * device->size_params.hsize
					* device->size_params.vsize;
		spin_unlock(&device->inout_vbq_lock);

	} else if (q->type == V4L2_BUF_TYPE_PRIVATE) {
		spin_lock(&device->lsc_vbq_lock);
		if (*cnt <= 0)
			*cnt = 1;

		if (*cnt > 1)
			*cnt = 1;

		if (!device->size_params.hsize ||
				!device->size_params.vsize) {
			dev_err(prev_dev, "Can't setup lsc buffer size\n");
			spin_unlock(&device->lsc_vbq_lock);
			return -EINVAL;
		}

		/* upsampled lsc table size - for now bpp = 2 */
		bpp = 2;
		*size = lsc_bufsize = bpp * device->size_params.hsize *
					    device->size_params.vsize;

		spin_unlock(&device->lsc_vbq_lock);
	} else {
		return -EINVAL;
	}

	return 0;
}

/**
 * previewer_vbq_prepare - Videobuffer is prepared and mmapped.
 * @q: Structure containing the videobuffer queue.
 * @vb: Structure containing the videobuffer used for previewer processing.
 * @field: Type of field to set in videobuffer device.
 *
 * Returns 0 if successful, or -EINVAL if buffer couldn't get allocated, or
 * -EIO if the ISP MMU mapping fails
 **/
static int previewer_vbq_prepare(struct videobuf_queue *q,
						struct videobuf_buffer *vb,
						enum v4l2_field field)
{
	struct prev_fh *fh = q->priv_data;
	struct prev_device *device = fh->device;
	int err = -EINVAL;
	unsigned int isp_addr;
	struct videobuf_dmabuf *dma = videobuf_to_dma(vb);

	if (q->type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {

		spin_lock(&device->inout_vbq_lock);

		if (vb->baddr) {
			vb->size = prev_bufsize;
			vb->bsize = prev_bufsize;
		} else {
			spin_unlock(&device->inout_vbq_lock);
			dev_err(prev_dev, "No user buffer allocated\n");
			goto out;
		}

		vb->width = device->size_params.hsize;
		vb->height = device->size_params.vsize;
		vb->field = field;
		spin_unlock(&device->inout_vbq_lock);

		if (vb->state == VIDEOBUF_NEEDS_INIT) {
			err = previewer_vb_lock_vma(vb, 1);
			if (err)
				goto buf_init_err1;

			err = videobuf_iolock(q, vb, NULL);
			if (err)
				goto buf_init_err1;

			isp_addr = ispmmu_vmap(device->isp,
					       dma->sglist, dma->sglen);
			if (!isp_addr)
				err = -EIO;
			else
				device->isp_addr_read = isp_addr;
		}

buf_init_err1:
		if (!err)
			vb->state = VIDEOBUF_PREPARED;
		else
			previewer_vbq_release(q, vb);

	} else if (q->type == V4L2_BUF_TYPE_PRIVATE) {

		spin_lock(&device->lsc_vbq_lock);

		if (vb->baddr) {
			vb->size = lsc_bufsize;
			vb->bsize = lsc_bufsize;
		} else {
			spin_unlock(&device->lsc_vbq_lock);
			dev_err(prev_dev, "No user buffer allocated\n");
			goto out;
		}

		vb->width = device->size_params.hsize;
		vb->height = device->size_params.vsize;
		vb->field = field;
		spin_unlock(&device->lsc_vbq_lock);

		if (vb->state == VIDEOBUF_NEEDS_INIT) {
			err = previewer_vb_lock_vma(vb, 1);
			if (err)
				goto buf_init_err2;

			err = videobuf_iolock(q, vb, NULL);
			if (err)
				goto buf_init_err2;

			isp_addr = ispmmu_vmap(device->isp,
					       dma->sglist, dma->sglen);
			if (!isp_addr)
				err = -EIO;
			else
				device->isp_addr_lsc = isp_addr;
		}

buf_init_err2:
		if (!err)
			vb->state = VIDEOBUF_PREPARED;
		else
			previewer_vbq_release(q, vb);

	} else {
		return -EINVAL;
	}

out:
	return err;
}

static void previewer_vbq_queue(struct videobuf_queue *q,
					struct videobuf_buffer *vb)
{
	return;
}

/**
 * previewer_open - Initializes and opens the Preview Wrapper
 * @inode: Inode structure associated with the Preview Wrapper
 * @filp: File structure associated with the Preview Wrapper
 *
 * Returns 0 if successful, -EACCES if its unable to initialize default config,
 * -EBUSY if its already opened or the ISP module is not available, or -ENOMEM
 * if its unable to allocate the device in kernel space memory.
 **/
static int previewer_open(struct inode *inode, struct file *filp)
{
	int ret = 0;
	struct prev_device *device = prevdevice;
	struct prev_fh *fh;
	struct device *isp;
	struct isp_device *isp_dev;

	if (device->opened || (filp->f_flags & O_NONBLOCK)) {
		dev_err(prev_dev, "previewer_open: device is already "
					"opened\n");
		return -EBUSY;
	}

	fh = kzalloc(sizeof(struct prev_fh), GFP_KERNEL);
	if (NULL == fh) {
		ret = -ENOMEM;
		goto err_fh;
	}

	isp = isp_get();
	if (!isp) {
		printk(KERN_ERR "Can't enable ISP clocks (ret %d)\n", ret);
		ret = -EACCES;
		goto err_isp;
	}
	device->isp = isp;
	isp_dev = dev_get_drvdata(isp);

	ret = isppreview_request(&isp_dev->isp_prev);
	if (ret) {
		dev_err(prev_dev, "Can't acquire isppreview\n");
		goto err_prev;
	}

	device->opened = 1;

	filp->private_data = fh;
	fh->inout_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fh->lsc_type = V4L2_BUF_TYPE_PRIVATE;
	fh->device = device;

	videobuf_queue_sg_init(&fh->inout_vbq, &device->vbq_ops, NULL,
					&device->inout_vbq_lock, fh->inout_type,
					V4L2_FIELD_NONE,
					sizeof(struct videobuf_buffer), fh);

	videobuf_queue_sg_init(&fh->lsc_vbq, &device->vbq_ops, NULL,
					&device->lsc_vbq_lock, fh->lsc_type,
					V4L2_FIELD_NONE,
					sizeof(struct videobuf_buffer), fh);

	init_completion(&device->wfc);
	device->wfc.done = 0;
	mutex_init(&device->prevwrap_mutex);

	return 0;

err_prev:
	isp_put();
err_isp:
	kfree(fh);
err_fh:
	return ret;
}

/**
 * previewer_release - Releases Preview Wrapper and frees up allocated memory
 * @inode: Inode structure associated with the Preview Wrapper
 * @filp: File structure associated with the Preview Wrapper
 *
 * Always returns 0.
 **/
static int previewer_release(struct inode *inode, struct file *filp)
{
	struct prev_fh *fh = filp->private_data;
	struct prev_device *device = fh->device;
	struct videobuf_queue *q1 = &fh->inout_vbq;
	struct videobuf_queue *q2 = &fh->lsc_vbq;
	struct isp_device *isp = dev_get_drvdata(device->isp);

	device->opened = 0;
	videobuf_mmap_free(q1);
	videobuf_mmap_free(q2);
	isppreview_free(&isp->isp_prev);
	isp_put();
	prev_bufsize = 0;
	lsc_bufsize = 0;
	filp->private_data = NULL;
	kfree(fh);

	return 0;
}

/**
 * previewer_mmap - Memory maps the Preview Wrapper module.
 * @file: File structure associated with the Preview Wrapper
 * @vma: Virtual memory area structure.
 *
 * Returns 0 if successful, or returned value by the videobuf_mmap_mapper()
 * function.
 **/
static int previewer_mmap(struct file *file, struct vm_area_struct *vma)
{
/*
	struct prev_fh *fh = file->private_data;

	dev_dbg(prev_dev, "previewer_mmap\n");

	return videobuf_mmap_mapper(&fh->inout_vbq, vma);
*/
	return -EINVAL;
}

#define COPY_USERTABLE(dst, src, size)					\
	if (src) {							\
		if (!dst)						\
			return -EACCES;					\
		if (copy_from_user(dst, src, (size) * sizeof(*(dst))))	\
			return -EFAULT;					\
	}

/* Copy preview module configuration into use */
static int previewer_set_param(struct prev_device *device,
			       struct prev_params __user *uparams)
{
	struct isp_device *isp_dev = dev_get_drvdata(device->isp);
	struct prev_params *config = &isp_dev->isp_prev.params;
	/* Here it should be safe to allocate 420 bytes from stack */
	struct prev_params p;
	struct prev_params *params = &p;
	int ret;

	if (copy_from_user(params, uparams, sizeof(*params)))
		return -EFAULT;
	ret = prev_validate_params(params);
	if (ret < 0)
		return -EINVAL;

	config->features = params->features;
	config->pix_fmt = params->pix_fmt;
	config->cfa.cfafmt = params->cfa.cfafmt;

	/* struct ispprev_cfa */
	config->cfa.cfa_gradthrs_vert = params->cfa.cfa_gradthrs_vert;
	config->cfa.cfa_gradthrs_horz = params->cfa.cfa_gradthrs_horz;
	COPY_USERTABLE(config->cfa.cfa_table, params->cfa.cfa_table,
			ISPPRV_CFA_TBL_SIZE);

	/* struct ispprev_csup csup */
	config->csup.gain = params->csup.gain;
	config->csup.thres = params->csup.thres;
	config->csup.hypf_en = params->csup.hypf_en;

	COPY_USERTABLE(config->ytable, params->ytable, ISPPRV_YENH_TBL_SIZE);

	/* struct ispprev_nf nf */
	config->nf.spread = params->nf.spread;
	memcpy(&config->nf.table, &params->nf.table, sizeof(config->nf.table));

	/* struct ispprev_dcor dcor */
	config->dcor.couplet_mode_en = params->dcor.couplet_mode_en;
	memcpy(&config->dcor.detect_correct, &params->dcor.detect_correct,
		sizeof(config->dcor.detect_correct));

	/* struct ispprev_gtable gtable */
	COPY_USERTABLE(config->gtable.redtable, params->gtable.redtable,
		ISPPRV_GAMMA_TBL_SIZE);
	COPY_USERTABLE(config->gtable.greentable, params->gtable.greentable,
		ISPPRV_GAMMA_TBL_SIZE);
	COPY_USERTABLE(config->gtable.bluetable, params->gtable.bluetable,
		ISPPRV_GAMMA_TBL_SIZE);

	/* struct ispprev_wbal wbal */
	config->wbal.dgain = params->wbal.dgain;
	config->wbal.coef3 = params->wbal.coef3;
	config->wbal.coef2 = params->wbal.coef2;
	config->wbal.coef1 = params->wbal.coef1;
	config->wbal.coef0 = params->wbal.coef0;

	/* struct ispprev_blkadj blk_adj */
	config->blk_adj.red = params->blk_adj.red;
	config->blk_adj.green = params->blk_adj.green;
	config->blk_adj.blue = params->blk_adj.blue;

	/* struct ispprev_rgbtorgb rgb2rgb */
	memcpy(&config->rgb2rgb.matrix, &params->rgb2rgb.matrix,
		sizeof(config->rgb2rgb.matrix));
	memcpy(&config->rgb2rgb.offset, &params->rgb2rgb.offset,
		sizeof(config->rgb2rgb.offset));

	/* struct ispprev_csc rgb2ycbcr */
	memcpy(&config->rgb2ycbcr.matrix, &params->rgb2ycbcr.matrix,
		sizeof(config->rgb2ycbcr.matrix));
	memcpy(&config->rgb2ycbcr.offset, &params->rgb2ycbcr.offset,
		sizeof(config->rgb2ycbcr.offset));

	/* struct ispprev_hmed hmf_params */
	config->hmf_params.odddist = params->hmf_params.odddist;
	config->hmf_params.evendist = params->hmf_params.evendist;
	config->hmf_params.thres = params->hmf_params.thres;

	/* struct prev_darkfrm_params drkf_params not set here */

	config->lens_shading_shift = params->lens_shading_shift;
	config->average = params->average;
	config->contrast = params->contrast;
	config->brightness = params->brightness;

	device->size_params = params->size_params;

	return 0;
}

/**
 * previewer_ioctl - I/O control function for Preview Wrapper
 * @inode: Inode structure associated with the Preview Wrapper.
 * @file: File structure associated with the Preview Wrapper.
 * @cmd: Type of command to execute.
 * @arg: Argument to send to requested command.
 *
 * Returns 0 if successful, -1 if bad command passed or access is denied,
 * -EFAULT if copy_from_user() or copy_to_user() fails, -EINVAL if parameter
 * validation fails or parameter structure is not present
 **/
static int previewer_ioctl(struct inode *inode, struct file *file,
					unsigned int cmd, unsigned long arg)
{
	int ret = 0;
	struct prev_fh *fh = file->private_data;
	struct prev_device *device = fh->device;

	switch (cmd) {
	case PREV_REQBUF: {
		struct v4l2_requestbuffers req;

		if (copy_from_user(&req, (void *)arg, sizeof(req)))
			goto err_efault;

		if (mutex_lock_interruptible(&device->prevwrap_mutex))
			goto err_eintr;

		if (req.type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			ret = videobuf_reqbufs(&fh->inout_vbq, &req);
		else if (req.type == V4L2_BUF_TYPE_PRIVATE)
			ret = videobuf_reqbufs(&fh->lsc_vbq, &req);
		else
			ret = -EINVAL;

		mutex_unlock(&device->prevwrap_mutex);

		if (ret)
			goto out;

		if (copy_to_user((void *)arg, &req, sizeof(req)))
			goto err_efault;

		break;
	}

	case PREV_QUERYBUF: {
		struct v4l2_buffer b;

		if (copy_from_user(&b, (void *)arg, sizeof(b)))
			goto err_efault;

		if (mutex_lock_interruptible(&device->prevwrap_mutex))
			goto err_eintr;

		if (b.type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			ret = videobuf_querybuf(&fh->inout_vbq, &b);
		else if (b.type == V4L2_BUF_TYPE_PRIVATE)
			ret = videobuf_querybuf(&fh->lsc_vbq, &b);
		else
			ret = -EINVAL;

		mutex_unlock(&device->prevwrap_mutex);

		if (ret)
			goto out;

		if (copy_to_user((void *)arg, &b, sizeof(b)))
			goto err_efault;

		break;
	}

	case PREV_QUEUEBUF: {
		struct v4l2_buffer b;

		if (copy_from_user(&b, (void *)arg, sizeof(b)))
			goto err_efault;

		if (mutex_lock_interruptible(&device->prevwrap_mutex))
			goto err_eintr;

		if (b.type == V4L2_BUF_TYPE_VIDEO_CAPTURE)
			ret = videobuf_qbuf(&fh->inout_vbq, &b);
		else if (b.type == V4L2_BUF_TYPE_PRIVATE)
			ret = videobuf_qbuf(&fh->lsc_vbq, &b);
		else
			ret = -EINVAL;

		mutex_unlock(&device->prevwrap_mutex);

		if (ret)
			goto out;

		if (copy_to_user((void *)arg, &b, sizeof(b)))
			goto err_efault;

		break;
	}

	case PREV_SET_PARAM:
		if (mutex_lock_interruptible(&device->prevwrap_mutex))
			goto err_eintr;
		ret = previewer_set_param(device, (struct prev_params *)arg);
		mutex_unlock(&device->prevwrap_mutex);
		break;

	case PREV_PREVIEW:
		if (mutex_lock_interruptible(&device->prevwrap_mutex))
			goto err_eintr;
		ret = prev_do_preview(device);
		mutex_unlock(&device->prevwrap_mutex);
		break;

	case PREV_GET_CROPSIZE: {
		struct prev_cropsize outputsize;

		memset(&outputsize, 0, sizeof(outputsize));
		ret = prev_calculate_crop(device, &outputsize);
		if (ret)
			break;

		if (copy_to_user((struct prev_cropsize *)arg, &outputsize,
						sizeof(struct prev_cropsize)))
			ret = -EFAULT;
		break;
	}

	default:
		dev_err(prev_dev, "previewer_ioctl: Invalid Command Value\n");
		ret = -EINVAL;
	}
out:
	return ret;
err_efault:
	return -EFAULT;
err_eintr:
	return -EINTR;
}

/**
 * previewer_platform_release - Acts when Reference count is zero
 * @device: Structure containing ISP preview wrapper global information
 *
 * This is called when the reference count goes to zero
 **/
static void previewer_platform_release(struct device *device)
{
}

static struct file_operations prev_fops = {
	.owner = THIS_MODULE,
	.open = previewer_open,
	.release = previewer_release,
	.mmap = previewer_mmap,
	.ioctl = previewer_ioctl,
};

static struct platform_device omap_previewer_device = {
	.name = OMAP_PREV_NAME,
	.id = -1,
	.dev = {
		.release = previewer_platform_release,
	}
};

/**
 * previewer_probe - Checks for device presence
 * @pdev: Structure containing details of the current device.
 *
 * Always returns 0
 **/
static int previewer_probe(struct platform_device *pdev)
{
	return 0;
}

/**
 * previewer_remove - Handles the removal of the driver
 * @pdev: Structure containing details of the current device.
 *
 * Always returns 0.
 **/
static int previewer_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver omap_previewer_driver = {
	.probe = previewer_probe,
	.remove = previewer_remove,
	.driver = {
		.owner = THIS_MODULE,
		.name = OMAP_PREV_NAME,
	},
};

/**
 * omap_previewer_init - Initialization of Preview Wrapper
 *
 * Returns 0 if successful, -ENOMEM if could not allocate memory, -ENODEV if
 * could not register the wrapper as a character device, or other errors if the
 * device or driver can't register.
 **/
static int __init omap_previewer_init(void)
{
	int ret;
	struct prev_device *device;

	device = kzalloc(sizeof(struct prev_device), GFP_KERNEL);
	if (!device) {
		printk(KERN_ERR OMAP_PREV_NAME
				" could not allocate memory\n");
		return -ENOMEM;
	}
	prev_major = register_chrdev(0, OMAP_PREV_NAME, &prev_fops);

	if (prev_major < 0) {
		printk(KERN_ERR OMAP_PREV_NAME " initialization "
				"failed. could not register character "
				"device\n");
		kfree(device);
		return -ENODEV;
	}

	ret = platform_driver_register(&omap_previewer_driver);
	if (ret) {
		printk(KERN_ERR OMAP_PREV_NAME
				"failed to register platform driver!\n");
		goto fail2;
	}
	ret = platform_device_register(&omap_previewer_device);
	if (ret) {
		printk(KERN_ERR OMAP_PREV_NAME
			" failed to register platform device!\n");
		goto fail3;
	}

	prev_class = class_create(THIS_MODULE, OMAP_PREV_NAME);
	if (!prev_class)
		goto fail4;

	prev_dev = device_create(prev_class, prev_dev, MKDEV(prev_major, 0),
				NULL, OMAP_PREV_NAME);

	device->opened = 0;

	device->vbq_ops.buf_setup = previewer_vbq_setup;
	device->vbq_ops.buf_prepare = previewer_vbq_prepare;
	device->vbq_ops.buf_release = previewer_vbq_release;
	device->vbq_ops.buf_queue = previewer_vbq_queue;
	spin_lock_init(&device->inout_vbq_lock);
	spin_lock_init(&device->lsc_vbq_lock);
	prevdevice = device;
	return 0;

fail4:
	platform_device_unregister(&omap_previewer_device);
fail3:
	platform_driver_unregister(&omap_previewer_driver);
fail2:
	unregister_chrdev(prev_major, OMAP_PREV_NAME);

	kfree(device);

	return ret;
}

/**
 * omap_previewer_exit - Close of Preview Wrapper
 **/
static void __exit omap_previewer_exit(void)
{
	device_destroy(prev_class, MKDEV(prev_major, 0));
	class_destroy(prev_class);
	platform_device_unregister(&omap_previewer_device);
	platform_driver_unregister(&omap_previewer_driver);
	unregister_chrdev(prev_major, OMAP_PREV_NAME);

	kfree(prevdevice);
	prev_major = -1;
}

module_init(omap_previewer_init);
module_exit(omap_previewer_exit);

MODULE_AUTHOR("Texas Instruments");
MODULE_DESCRIPTION("OMAP ISP Previewer");
MODULE_LICENSE("GPL");

