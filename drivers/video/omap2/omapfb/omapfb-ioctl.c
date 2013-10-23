/*
 * linux/drivers/video/omap2/omapfb-ioctl.c
 *
 * Copyright (C) 2008 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
 *
 * Some code and ideas taken from drivers/video/omap/ driver
 * by Imre Deak.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/fb.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/omapfb.h>
#include <linux/vmalloc.h>

#include <mach/display.h>
#include <mach/vrfb.h>
#include <mach/vram.h>

#include "omapfb.h"

static int omapfb_setup_plane(struct fb_info *fbi, struct omapfb_plane_info *pi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omap_display *display = fb2display(fbi);
	struct omap_overlay *ovl;
	struct omap_overlay_info info;
	int r = 0;

	DBG("omapfb_setup_plane\n");

	omapfb_lock(fbdev);

	if (ofbi->num_overlays != 1) {
		r = -EINVAL;
		goto out;
	}

	/* XXX uses only the first overlay */
	ovl = ofbi->overlays[0];

	if (pi->enabled && !ofbi->region.size) {
		/*
		 * This plane's memory was freed, can't enable it
		 * until it's reallocated.
		 */
		r = -EINVAL;
		goto out;
	}

	ovl->get_overlay_info(ovl, &info);

	info.pos_x = pi->pos_x;
	info.pos_y = pi->pos_y;
	info.out_width = pi->out_width;
	info.out_height = pi->out_height;
	info.enabled = pi->enabled;

	omap_dss_lock();

	r = ovl->set_overlay_info(ovl, &info);
	if (r)
		goto unlock;

	if (ovl->manager) {
		r = ovl->manager->apply(ovl->manager);
		if (r)
			goto unlock;
	}

	if (display) {
		u16 w, h;

		if (display->sync)
			display->sync(display);

		display->get_resolution(display, &w, &h);

		if (display->update)
			display->update(display, 0, 0, w, h);
	}

unlock:
	omap_dss_unlock();
out:
	omapfb_unlock(fbdev);
	if (r)
		dev_err(fbdev->dev, "setup_plane failed\n");
	return r;
}

static int omapfb_query_plane(struct fb_info *fbi, struct omapfb_plane_info *pi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;

	omapfb_lock(fbdev);

	if (ofbi->num_overlays != 1) {
		memset(pi, 0, sizeof(*pi));
	} else {
		struct omap_overlay_info *ovli;
		struct omap_overlay *ovl;

		ovl = ofbi->overlays[0];
		ovli = &ovl->info;

		pi->pos_x = ovli->pos_x;
		pi->pos_y = ovli->pos_y;
		pi->enabled = ovli->enabled;
		pi->channel_out = 0; /* xxx */
		pi->mirror = 0;
		pi->out_width = ovli->out_width;
		pi->out_height = ovli->out_height;
	}

	omapfb_unlock(fbdev);

	return 0;
}

static int omapfb_setup_mem(struct fb_info *fbi, struct omapfb_mem_info *mi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omapfb2_mem_region *rg;
	int r, i;
	size_t size;

	if (mi->type > OMAPFB_MEMTYPE_MAX)
		return -EINVAL;

	size = PAGE_ALIGN(mi->size);

	rg = &ofbi->region;

	omapfb_lock(fbdev);

	for (i = 0; i < ofbi->num_overlays; i++) {
		if (ofbi->overlays[i]->info.enabled) {
			r = -EBUSY;
			goto out;
		}
	}

	if (rg->size != size || rg->type != mi->type) {
		r = omapfb_realloc_fbmem(fbi, size, mi->type);
		if (r) {
			dev_err(fbdev->dev, "realloc fbmem failed\n");
			goto out;
		}
	}

	r = 0;
out:
	omapfb_unlock(fbdev);

	return r;
}

static int omapfb_query_mem(struct fb_info *fbi, struct omapfb_mem_info *mi)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omapfb2_mem_region *rg;

	rg = &ofbi->region;
	memset(mi, 0, sizeof(*mi));

	omapfb_lock(fbdev);
	mi->size = rg->size;
	mi->type = rg->type;
	omapfb_unlock(fbdev);

	return 0;
}

static int omapfb_update_window(struct fb_info *fbi,
		u32 x, u32 y, u32 w, u32 h)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omap_display *display = fb2display(fbi);
	u16 dw, dh;

	if (!display)
		return 0;

	if (w == 0 || h == 0)
		return 0;

	display->get_resolution(display, &dw, &dh);

	if (x + w > dw || y + h > dh)
		return -EINVAL;

	omapfb_lock(fbdev);
	omap_dss_lock();
	display->update(display, x, y, w, h);
	omap_dss_unlock();
	omapfb_unlock(fbdev);

	return 0;
}

static int omapfb_set_update_mode(struct fb_info *fbi,
				   enum omapfb_update_mode mode)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omap_display *display = fb2display(fbi);
	enum omap_dss_update_mode um;
	int r;

	if (!display || !display->set_update_mode)
		return -EINVAL;

	switch (mode) {
	case OMAPFB_UPDATE_DISABLED:
		um = OMAP_DSS_UPDATE_DISABLED;
		break;

	case OMAPFB_AUTO_UPDATE:
		um = OMAP_DSS_UPDATE_AUTO;
		break;

	case OMAPFB_MANUAL_UPDATE:
		um = OMAP_DSS_UPDATE_MANUAL;
		break;

	default:
		return -EINVAL;
	}

	omapfb_lock(fbdev);
	omap_dss_lock();
	r = display->set_update_mode(display, um);
	omap_dss_unlock();
	omapfb_unlock(fbdev);

	return r;
}

static int omapfb_get_update_mode(struct fb_info *fbi,
		enum omapfb_update_mode *mode)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omap_display *display = fb2display(fbi);
	enum omap_dss_update_mode m;

	if (!display || !display->get_update_mode)
		return -EINVAL;

	omapfb_lock(fbdev);
	m = display->get_update_mode(display);
	omapfb_unlock(fbdev);

	switch (m) {
	case OMAP_DSS_UPDATE_DISABLED:
		*mode = OMAPFB_UPDATE_DISABLED;
		break;
	case OMAP_DSS_UPDATE_AUTO:
		*mode = OMAPFB_AUTO_UPDATE;
		break;
	case OMAP_DSS_UPDATE_MANUAL:
		*mode = OMAPFB_MANUAL_UPDATE;
		break;
	default:
		BUG();
	}

	return 0;
}

/* XXX this color key handling is a hack... */
static struct omapfb_color_key omapfb_color_keys[2];

static int _omapfb_set_color_key(struct omap_overlay_manager *mgr,
		struct omapfb_color_key *ck)
{
	enum omap_dss_color_key_type kt;

	if (!mgr->set_default_color ||
			!mgr->set_trans_key_type_and_value ||
			!mgr->enable_trans_key)
		return 0;

	if (ck->key_type == OMAPFB_COLOR_KEY_DISABLED) {
		mgr->enable_trans_key(mgr, 0);
		omapfb_color_keys[mgr->id] = *ck;
		return 0;
	}

	switch(ck->key_type) {
	case OMAPFB_COLOR_KEY_GFX_DST:
		kt = OMAP_DSS_COLOR_KEY_GFX_DST;
		break;
	case OMAPFB_COLOR_KEY_VID_SRC:
		kt = OMAP_DSS_COLOR_KEY_VID_SRC;
		break;
	default:
		return -EINVAL;
	}

	omap_dss_lock();
	mgr->set_default_color(mgr, ck->background);
	mgr->set_trans_key_type_and_value(mgr, kt, ck->trans_key);
	mgr->enable_trans_key(mgr, 1);
	omap_dss_unlock();

	omapfb_color_keys[mgr->id] = *ck;

	return 0;
}

static int omapfb_set_color_key(struct fb_info *fbi,
		struct omapfb_color_key *ck)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	int r;
	int i;
	struct omap_overlay_manager *mgr = NULL;

	omapfb_lock(fbdev);

	for (i = 0; i < ofbi->num_overlays; i++) {
		if (ofbi->overlays[i]->manager) {
			mgr = ofbi->overlays[i]->manager;
			break;
		}
	}

	if (!mgr) {
		r = -EINVAL;
		goto err;
	}

	if (!mgr->set_default_color ||
			!mgr->set_trans_key_type_and_value ||
			!mgr->enable_trans_key) {
		r = -ENODEV;
		goto err;
	}

	r = _omapfb_set_color_key(mgr, ck);
err:
	omapfb_unlock(fbdev);

	return r;
}

static int omapfb_get_color_key(struct fb_info *fbi,
		struct omapfb_color_key *ck)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omap_overlay_manager *mgr = NULL;
	int r = 0;
	int i;

	omapfb_lock(fbdev);

	for (i = 0; i < ofbi->num_overlays; i++) {
		if (ofbi->overlays[i]->manager) {
			mgr = ofbi->overlays[i]->manager;
			break;
		}
	}

	if (!mgr) {
		r = -EINVAL;
		goto err;
	}

	if (!mgr->set_default_color ||
			!mgr->set_trans_key_type_and_value ||
			!mgr->enable_trans_key) {
		r = -ENODEV;
		goto err;
	}

	*ck = omapfb_color_keys[mgr->id];
err:
	omapfb_unlock(fbdev);

	return r;
}

static int omapfb_memory_read(struct fb_info *fbi,
		struct omapfb_memory_read *mr)
{
	struct omap_display *display = fb2display(fbi);
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	void *buf;
	int r;

	if (!display || !display->memory_read)
		return -ENOENT;

	if (!access_ok(VERIFY_WRITE, mr->buffer, mr->buffer_size))
		return -EFAULT;

	if (mr->w * mr->h * 3 > mr->buffer_size)
		return -EINVAL;

	buf = vmalloc(mr->buffer_size);
	if (!buf) {
		DBG("vmalloc failed\n");
		return -ENOMEM;
	}

	omapfb_lock(fbdev);

	omap_dss_lock();
	r = display->memory_read(display, buf, mr->buffer_size,
			mr->x, mr->y, mr->w, mr->h);

	omap_dss_unlock();
	if (r > 0) {
		if (copy_to_user(mr->buffer, buf, mr->buffer_size))
			r = -EFAULT;
	}

	vfree(buf);

	omapfb_unlock(fbdev);

	return r;
}

int omapfb_get_ovl_colormode(struct omapfb2_device *fbdev,
			     struct omapfb_ovl_colormode *mode)
{
	int ovl_idx = mode->overlay_idx;
	int mode_idx = mode->mode_idx;
	struct omap_overlay *ovl;
	enum omap_color_mode supported_modes;
	struct fb_var_screeninfo var;
	int i;

	if (ovl_idx >= fbdev->num_overlays)
		return -ENODEV;
	ovl = fbdev->overlays[ovl_idx];
	supported_modes = ovl->supported_modes;

	mode_idx = mode->mode_idx;

	for (i = 0; i < sizeof(supported_modes) * 8; i++) {
		if (!(supported_modes & (1 << i)))
			continue;
		/*
		 * It's possible that the FB doesn't support a mode
		 * that is supported by the overlay, so call the
		 * following here.
		 */
		if (dss_mode_to_fb_mode(1 << i, &var) < 0)
			continue;

		mode_idx--;
		if (mode_idx < 0)
			break;
	}

	if (i == sizeof(supported_modes) * 8)
		return -ENOENT;

	mode->bits_per_pixel = var.bits_per_pixel;
	mode->nonstd = var.nonstd;
	mode->red = var.red;
	mode->green = var.green;
	mode->blue = var.blue;
	mode->transp = var.transp;

	return 0;
}

int omapfb_ioctl(struct fb_info *fbi, unsigned int cmd, unsigned long arg)
{
	struct omapfb_info *ofbi = FB2OFB(fbi);
	struct omapfb2_device *fbdev = ofbi->fbdev;
	struct omap_display *display = fb2display(fbi);

	union {
		struct omapfb_update_window_old	uwnd_o;
		struct omapfb_update_window	uwnd;
		struct omapfb_plane_info	plane_info;
		struct omapfb_caps		caps;
		struct omapfb_mem_info          mem_info;
		struct omapfb_color_key		color_key;
		struct omapfb_ovl_colormode	ovl_colormode;
		enum omapfb_update_mode		update_mode;
		int test_num;
		struct omapfb_memory_read	memory_read;
		struct omapfb_vram_info		vram_info;
	} p;

	int r = 0;

	switch (cmd) {
	case OMAPFB_SYNC_GFX:
		DBG("ioctl SYNC_GFX\n");
		if (!display || !display->sync) {
			/* DSS1 never returns an error here, so we neither */
			/*r = -EINVAL;*/
			break;
		}

		omapfb_lock(fbdev);
		omap_dss_lock();
		r = display->sync(display);
		omap_dss_unlock();
		omapfb_unlock(fbdev);
		break;

	case OMAPFB_UPDATE_WINDOW_OLD:
		DBG("ioctl UPDATE_WINDOW_OLD\n");
		if (!display || !display->update) {
			r = -EINVAL;
			break;
		}

		if (copy_from_user(&p.uwnd_o,
					(void __user *)arg,
					sizeof(p.uwnd_o))) {
			r = -EFAULT;
			break;
		}

		r = omapfb_update_window(fbi, p.uwnd_o.x, p.uwnd_o.y,
				p.uwnd_o.width, p.uwnd_o.height);
		break;

	case OMAPFB_UPDATE_WINDOW:
		DBG("ioctl UPDATE_WINDOW\n");
		if (!display || !display->update) {
			r = -EINVAL;
			break;
		}

		if (copy_from_user(&p.uwnd, (void __user *)arg,
					sizeof(p.uwnd))) {
			r = -EFAULT;
			break;
		}

		r = omapfb_update_window(fbi, p.uwnd.x, p.uwnd.y,
				p.uwnd.width, p.uwnd.height);
		break;

	case OMAPFB_SETUP_PLANE:
		DBG("ioctl SETUP_PLANE\n");
		if (copy_from_user(&p.plane_info, (void __user *)arg,
					sizeof(p.plane_info)))
			r = -EFAULT;
		else
			r = omapfb_setup_plane(fbi, &p.plane_info);
		break;

	case OMAPFB_QUERY_PLANE:
		DBG("ioctl QUERY_PLANE\n");
		r = omapfb_query_plane(fbi, &p.plane_info);
		if (r < 0)
			break;
		if (copy_to_user((void __user *)arg, &p.plane_info,
					sizeof(p.plane_info)))
			r = -EFAULT;
		break;

	case OMAPFB_SETUP_MEM:
		DBG("ioctl SETUP_MEM\n");
		if (copy_from_user(&p.mem_info, (void __user *)arg,
					sizeof(p.mem_info)))
			r = -EFAULT;
		else
			r = omapfb_setup_mem(fbi, &p.mem_info);
		break;

	case OMAPFB_QUERY_MEM:
		DBG("ioctl QUERY_MEM\n");
		r = omapfb_query_mem(fbi, &p.mem_info);
		if (r < 0)
			break;
		if (copy_to_user((void __user *)arg, &p.mem_info,
					sizeof(p.mem_info)))
			r = -EFAULT;
		break;

	case OMAPFB_GET_CAPS:
		DBG("ioctl GET_CAPS\n");
		if (!display) {
			r = -EINVAL;
			break;
		}

		memset(&p.caps, 0, sizeof(p.caps));
		p.caps.ctrl = display->caps;

		if (copy_to_user((void __user *)arg, &p.caps, sizeof(p.caps)))
			r = -EFAULT;
		break;

	case OMAPFB_GET_OVERLAY_COLORMODE:
		DBG("ioctl GET_OVERLAY_COLORMODE\n");
		if (copy_from_user(&p.ovl_colormode, (void __user *)arg,
				   sizeof(p.ovl_colormode))) {
			r = -EFAULT;
			break;
		}
		r = omapfb_get_ovl_colormode(fbdev, &p.ovl_colormode);
		if (r < 0)
			break;
		if (copy_to_user((void __user *)arg, &p.ovl_colormode,
				 sizeof(p.ovl_colormode)))
			r = -EFAULT;
		break;

	case OMAPFB_SET_UPDATE_MODE:
		DBG("ioctl SET_UPDATE_MODE\n");
		if (get_user(p.update_mode, (int __user *)arg))
			r = -EFAULT;
		else
			r = omapfb_set_update_mode(fbi, p.update_mode);
		break;

	case OMAPFB_GET_UPDATE_MODE:
		DBG("ioctl GET_UPDATE_MODE\n");
		r = omapfb_get_update_mode(fbi, &p.update_mode);
		if (r)
			break;
		if (put_user(p.update_mode,
					(enum omapfb_update_mode __user *)arg))
			r = -EFAULT;
		break;

	case OMAPFB_SET_COLOR_KEY:
		DBG("ioctl SET_COLOR_KEY\n");
		if (copy_from_user(&p.color_key, (void __user *)arg,
				   sizeof(p.color_key)))
			r = -EFAULT;
		else
			r = omapfb_set_color_key(fbi, &p.color_key);
		break;

	case OMAPFB_GET_COLOR_KEY:
		DBG("ioctl GET_COLOR_KEY\n");
		if ((r = omapfb_get_color_key(fbi, &p.color_key)) < 0)
			break;
		if (copy_to_user((void __user *)arg, &p.color_key,
				 sizeof(p.color_key)))
			r = -EFAULT;
		break;

	case OMAPFB_WAITFORVSYNC:
		DBG("ioctl WAITFORVSYNC\n");
		if (!display) {
			r = -EINVAL;
			break;
		}

		omap_dss_lock();
		r = display->wait_vsync(display);
		omap_dss_unlock();
		break;

	/* LCD and CTRL tests do the same thing for backward
	 * compatibility */
	case OMAPFB_LCD_TEST:
		DBG("ioctl LCD_TEST\n");
		if (get_user(p.test_num, (int __user *)arg)) {
			r = -EFAULT;
			break;
		}
		if (!display || !display->run_test) {
			r = -EINVAL;
			break;
		}

		omap_dss_lock();
		r = display->run_test(display, p.test_num);
		omap_dss_unlock();

		break;

	case OMAPFB_CTRL_TEST:
		DBG("ioctl CTRL_TEST\n");
		if (get_user(p.test_num, (int __user *)arg)) {
			r = -EFAULT;
			break;
		}
		if (!display || !display->run_test) {
			r = -EINVAL;
			break;
		}

		omap_dss_lock();
		r = display->run_test(display, p.test_num);
		omap_dss_unlock();

		break;

	case OMAPFB_MEMORY_READ:
		DBG("ioctl MEMORY_READ\n");

		if (copy_from_user(&p.memory_read, (void __user *)arg,
					sizeof(p.memory_read))) {
			r = -EFAULT;
			break;
		}

		r = omapfb_memory_read(fbi, &p.memory_read);

		break;

	case OMAPFB_GET_VRAM_INFO: {
		unsigned long vram, free, largest;

		DBG("ioctl GET_VRAM_INFO\n");

		omap_vram_get_info(&vram, &free, &largest);
		p.vram_info.total = vram;
		p.vram_info.free = free;
		p.vram_info.largest_free_block = largest;

		if (copy_to_user((void __user *)arg, &p.vram_info,
					sizeof(p.vram_info)))
			r = -EFAULT;
		break;
	}

	default:
		dev_err(fbdev->dev, "Unknown ioctl 0x%x\n", cmd);
		r = -EINVAL;
	}

	if (r < 0)
		DBG("ioctl failed: %d\n", r);

	return r;
}


