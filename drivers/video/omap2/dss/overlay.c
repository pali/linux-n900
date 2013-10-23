/*
 * linux/drivers/video/omap2/dss/overlay.c
 *
 * Copyright (C) 2009 Nokia Corporation
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

#define DSS_SUBSYS_NAME "OVERLAY"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>
#include <linux/platform_device.h>

#include <mach/display.h>

#include "dss.h"

static int num_overlays;
static struct list_head overlay_list;

static ssize_t overlay_name_show(struct omap_overlay *ovl, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", ovl->name);
}

static ssize_t overlay_manager_show(struct omap_overlay *ovl, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n",
			ovl->manager ? ovl->manager->name : "<none>");
}

static ssize_t overlay_manager_store(struct omap_overlay *ovl, const char *buf, size_t size)
{
	int i, r;
	struct omap_overlay_manager *mgr = NULL;
	int len = size;

	if (buf[size-1] == '\n')
		--len;

	if (len > 0) {
		for (i = 0; i < omap_dss_get_num_overlay_managers(); ++i) {
			mgr = omap_dss_get_overlay_manager(i);

			if (strncmp(buf, mgr->name, len) == 0)
				break;

			mgr = NULL;
		}
	}

	if (len > 0 && mgr == NULL)
		return -EINVAL;

	if (mgr)
		DSSDBG("manager %s found\n", mgr->name);

	if (mgr != ovl->manager) {
		/* detach old manager */
		if (ovl->manager) {
			r = ovl->unset_manager(ovl);
			if (r) {
				DSSERR("detach failed\n");
				return r;
			}
		}

		if (mgr) {
			r = ovl->set_manager(ovl, mgr);
			if (r) {
				DSSERR("Failed to attach overlay\n");
				return r;
			}
		}
	}

	if (ovl->manager) {
		omap_dss_lock();
		r = ovl->manager->apply(ovl->manager);
		omap_dss_unlock();
		if (r)
			return r;
	}

	return size;
}

static ssize_t overlay_input_size_show(struct omap_overlay *ovl, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d,%d\n",
			ovl->info.width, ovl->info.height);
}

static ssize_t overlay_screen_width_show(struct omap_overlay *ovl, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", ovl->info.screen_width);
}

static ssize_t overlay_position_show(struct omap_overlay *ovl, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d,%d\n",
			ovl->info.pos_x, ovl->info.pos_y);
}

static ssize_t overlay_position_store(struct omap_overlay *ovl,
		const char *buf, size_t size)
{
	int r;
	char *last;
	struct omap_overlay_info info;

	ovl->get_overlay_info(ovl, &info);

	info.pos_x = simple_strtoul(buf, &last, 10);
	++last;
	if (last - buf >= size)
		return -EINVAL;

	info.pos_y = simple_strtoul(last, &last, 10);

	if ((r = ovl->set_overlay_info(ovl, &info)))
		return r;

	if (ovl->manager) {
		omap_dss_lock();
		r = ovl->manager->apply(ovl->manager);
		omap_dss_unlock();
		if (r)
			return r;
	}

	return size;
}

static ssize_t overlay_output_size_show(struct omap_overlay *ovl, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d,%d\n",
			ovl->info.out_width, ovl->info.out_height);
}

static ssize_t overlay_output_size_store(struct omap_overlay *ovl,
		const char *buf, size_t size)
{
	int r;
	char *last;
	struct omap_overlay_info info;

	ovl->get_overlay_info(ovl, &info);

	info.out_width = simple_strtoul(buf, &last, 10);
	++last;
	if (last - buf >= size)
		return -EINVAL;

	info.out_height = simple_strtoul(last, &last, 10);

	if ((r = ovl->set_overlay_info(ovl, &info)))
		return r;

	if (ovl->manager) {
		omap_dss_lock();
		r = ovl->manager->apply(ovl->manager);
		omap_dss_unlock();
		if (r)
			return r;
	}

	return size;
}

static ssize_t overlay_enabled_show(struct omap_overlay *ovl, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", ovl->info.enabled);
}

static ssize_t overlay_enabled_store(struct omap_overlay *ovl, const char *buf, size_t size)
{
	int r;
	struct omap_overlay_info info;

	ovl->get_overlay_info(ovl, &info);

	info.enabled = simple_strtoul(buf, NULL, 10);

	if ((r = ovl->set_overlay_info(ovl, &info)))
		return r;

	if (ovl->manager) {
		omap_dss_lock();
		r = ovl->manager->apply(ovl->manager);
		omap_dss_unlock();

		if (r)
			return r;
	}

	return size;
}

static ssize_t overlay_global_alpha_show(struct omap_overlay *ovl, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
			ovl->info.global_alpha);
}

static ssize_t overlay_global_alpha_store(struct omap_overlay *ovl,
		const char *buf, size_t size)
{
	int r;
	struct omap_overlay_info info;

	ovl->get_overlay_info(ovl, &info);

	/* Video1 plane does not support global alpha
	 * to always make it 255 completely opaque
	 */
	if (ovl->id == OMAP_DSS_VIDEO1)
		info.global_alpha = 255;
	else
		info.global_alpha = simple_strtoul(buf, NULL, 10);

	if ((r = ovl->set_overlay_info(ovl, &info)))
		return r;

	if (ovl->manager) {
		omap_dss_lock();
		r = ovl->manager->apply(ovl->manager);
		omap_dss_unlock();
		if (r)
			return r;
	}

	return size;
}

struct overlay_attribute {
	struct attribute attr;
	ssize_t (*show)(struct omap_overlay *, char *);
	ssize_t	(*store)(struct omap_overlay *, const char *, size_t);
};

#define OVERLAY_ATTR(_name, _mode, _show, _store) \
	struct overlay_attribute overlay_attr_##_name = \
	__ATTR(_name, _mode, _show, _store)

static OVERLAY_ATTR(name, S_IRUGO, overlay_name_show, NULL);
static OVERLAY_ATTR(manager, S_IRUGO|S_IWUSR,
		overlay_manager_show, overlay_manager_store);
static OVERLAY_ATTR(input_size, S_IRUGO, overlay_input_size_show, NULL);
static OVERLAY_ATTR(screen_width, S_IRUGO, overlay_screen_width_show, NULL);
static OVERLAY_ATTR(position, S_IRUGO|S_IWUSR,
		overlay_position_show, overlay_position_store);
static OVERLAY_ATTR(output_size, S_IRUGO|S_IWUSR,
		overlay_output_size_show, overlay_output_size_store);
static OVERLAY_ATTR(enabled, S_IRUGO|S_IWUSR,
		overlay_enabled_show, overlay_enabled_store);
static OVERLAY_ATTR(global_alpha, S_IRUGO|S_IWUSR,
		overlay_global_alpha_show, overlay_global_alpha_store);

static struct attribute *overlay_sysfs_attrs[] = {
	&overlay_attr_name.attr,
	&overlay_attr_manager.attr,
	&overlay_attr_input_size.attr,
	&overlay_attr_screen_width.attr,
	&overlay_attr_position.attr,
	&overlay_attr_output_size.attr,
	&overlay_attr_enabled.attr,
	&overlay_attr_global_alpha.attr,
	NULL
};

static ssize_t overlay_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct omap_overlay *overlay;
	struct overlay_attribute *overlay_attr;

	overlay = container_of(kobj, struct omap_overlay, kobj);
	overlay_attr = container_of(attr, struct overlay_attribute, attr);

	if (!overlay_attr->show)
		return -ENOENT;

	return overlay_attr->show(overlay, buf);
}

static ssize_t overlay_attr_store(struct kobject *kobj, struct attribute *attr,
		const char *buf, size_t size)
{
	struct omap_overlay *overlay;
	struct overlay_attribute *overlay_attr;

	overlay = container_of(kobj, struct omap_overlay, kobj);
	overlay_attr = container_of(attr, struct overlay_attribute, attr);

	if (!overlay_attr->store)
		return -ENOENT;

	return overlay_attr->store(overlay, buf, size);
}

static struct sysfs_ops overlay_sysfs_ops = {
	.show = overlay_attr_show,
	.store = overlay_attr_store,
};

static struct kobj_type overlay_ktype = {
	.sysfs_ops = &overlay_sysfs_ops,
	.default_attrs = overlay_sysfs_attrs,
};

/* Check if overlay parameters are compatible with display */
int dss_check_overlay(struct omap_overlay *ovl, struct omap_display *display)
{
	struct omap_overlay_info *info;
	u16 outw, outh;
	u16 dw, dh;

	if (!display)
		return 0;

	if (!ovl->info.enabled)
		return 0;

	info = &ovl->info;

	if (info->paddr == 0) {
		DSSDBG("check_overlay failed: paddr 0\n");
		return -EINVAL;
	}

	display->get_resolution(display, &dw, &dh);

	DSSDBG("check_overlay %d: (%d,%d %dx%d -> %dx%d) disp (%dx%d)\n",
			ovl->id,
			info->pos_x, info->pos_y,
			info->width, info->height,
			info->out_width, info->out_height,
			dw, dh);

	if ((ovl->caps & OMAP_DSS_OVL_CAP_SCALE) == 0) {
		outw = info->width;
		outh = info->height;
	} else {
		if (info->out_width == 0)
			outw = info->width;
		else
			outw = info->out_width;

		if (info->out_height == 0)
			outh = info->height;
		else
			outh = info->out_height;
	}

	if (dw < info->pos_x + outw) {
		DSSDBG("check_overlay failed 1: %d < %d + %d\n",
				dw, info->pos_x, outw);
		return -EINVAL;
	}

	if (dh < info->pos_y + outh) {
		DSSDBG("check_overlay failed 2: %d < %d + %d\n",
				dh, info->pos_y, outh);
		return -EINVAL;
	}

	if ((ovl->supported_modes & info->color_mode) == 0) {
		DSSERR("overlay doesn't support mode %d\n", info->color_mode);
		return -EINVAL;
	}

	return 0;
}

static int dss_ovl_set_overlay_info(struct omap_overlay *ovl,
		struct omap_overlay_info *info)
{
	int r;
	struct omap_overlay_info old_info;

	old_info = ovl->info;
	ovl->info = *info;

	if (ovl->manager) {
		r = dss_check_overlay(ovl, ovl->manager->display);
		if (r) {
			ovl->info = old_info;
			return r;
		}
	}

	return 0;
}

static void dss_ovl_get_overlay_info(struct omap_overlay *ovl,
		struct omap_overlay_info *info)
{
	*info = ovl->info;
}

static int omap_dss_set_manager(struct omap_overlay *ovl,
		struct omap_overlay_manager *mgr)
{
	int r;

	if (ovl->manager) {
		DSSERR("overlay '%s' already has a manager '%s'\n",
				ovl->name, ovl->manager->name);
	}

	r = dss_check_overlay(ovl, mgr->display);
	if (r)
		return r;

	ovl->manager = mgr;

	return 0;
}

static int omap_dss_unset_manager(struct omap_overlay *ovl)
{
	if (!ovl->manager) {
		DSSERR("failed to detach overlay: manager not set\n");
		return -EINVAL;
	}

	ovl->manager = NULL;

	return 0;
}

int omap_dss_get_num_overlays(void)
{
	return num_overlays;
}
EXPORT_SYMBOL(omap_dss_get_num_overlays);

struct omap_overlay *omap_dss_get_overlay(int num)
{
	int i = 0;
	struct omap_overlay *ovl;

	list_for_each_entry(ovl, &overlay_list, list) {
		if (i++ == num)
			return ovl;
	}

	return NULL;
}
EXPORT_SYMBOL(omap_dss_get_overlay);

static void omap_dss_add_overlay(struct omap_overlay *overlay)
{
	++num_overlays;
	list_add_tail(&overlay->list, &overlay_list);
}

static struct omap_overlay *dispc_overlays[3];

void dss_overlay_setup_dispc_manager(struct omap_overlay_manager *mgr)
{
	mgr->num_overlays = 3;
	mgr->overlays = dispc_overlays;
}

void dss_init_overlays(struct platform_device *pdev)
{
	int i, r;
	struct omap_overlay_manager *lcd_mgr;
	struct omap_overlay_manager *tv_mgr;
	struct omap_dss_board_info *pdata = pdev->dev.platform_data;

	INIT_LIST_HEAD(&overlay_list);

	num_overlays = 0;

	for (i = 0; i < 3; ++i) {
		struct omap_overlay *ovl;
		ovl = kzalloc(sizeof(*ovl), GFP_KERNEL);

		BUG_ON(ovl == NULL);

		switch (i) {
		case 0:
			ovl->name = "gfx";
			ovl->id = OMAP_DSS_GFX;
			ovl->supported_modes = cpu_is_omap34xx() ?
				OMAP_DSS_COLOR_GFX_OMAP3 :
				OMAP_DSS_COLOR_GFX_OMAP2;
			ovl->caps = OMAP_DSS_OVL_CAP_DISPC;
			ovl->info.global_alpha = 255;
			ovl->info.fifo_threshold_low =
				pdata->fifo_thresholds[OMAP_DSS_GFX].low;
			ovl->info.fifo_threshold_high =
				pdata->fifo_thresholds[OMAP_DSS_GFX].high;
			break;
		case 1:
			ovl->name = "vid1";
			ovl->id = OMAP_DSS_VIDEO1;
			ovl->supported_modes = cpu_is_omap34xx() ?
				OMAP_DSS_COLOR_VID1_OMAP3 :
				OMAP_DSS_COLOR_VID_OMAP2;
			ovl->caps = OMAP_DSS_OVL_CAP_SCALE |
				OMAP_DSS_OVL_CAP_DISPC;
			ovl->info.global_alpha = 255;
			ovl->info.fifo_threshold_low =
				pdata->fifo_thresholds[OMAP_DSS_VIDEO1].low;
			ovl->info.fifo_threshold_high =
				pdata->fifo_thresholds[OMAP_DSS_VIDEO1].high;
			break;
		case 2:
			ovl->name = "vid2";
			ovl->id = OMAP_DSS_VIDEO2;
			ovl->supported_modes = cpu_is_omap34xx() ?
				OMAP_DSS_COLOR_VID2_OMAP3 :
				OMAP_DSS_COLOR_VID_OMAP2;
			ovl->caps = OMAP_DSS_OVL_CAP_SCALE |
				OMAP_DSS_OVL_CAP_DISPC;
			ovl->info.global_alpha = 255;
			ovl->info.fifo_threshold_low =
				pdata->fifo_thresholds[OMAP_DSS_VIDEO2].low;
			ovl->info.fifo_threshold_high =
				pdata->fifo_thresholds[OMAP_DSS_VIDEO2].high;
			break;
		}

		ovl->set_manager = &omap_dss_set_manager;
		ovl->unset_manager = &omap_dss_unset_manager;
		ovl->set_overlay_info = &dss_ovl_set_overlay_info;
		ovl->get_overlay_info = &dss_ovl_get_overlay_info;

		omap_dss_add_overlay(ovl);

		r = kobject_init_and_add(&ovl->kobj, &overlay_ktype,
				&pdev->dev.kobj, "overlay%d", i);

		if (r) {
			DSSERR("failed to create sysfs file\n");
			continue;
		}

		dispc_overlays[i] = ovl;
	}

	lcd_mgr = omap_dss_get_overlay_manager(OMAP_DSS_OVL_MGR_LCD);
	tv_mgr = omap_dss_get_overlay_manager(OMAP_DSS_OVL_MGR_TV);

#ifdef L4_EXAMPLE
	/* setup L4 overlay as an example */
	{
		static struct omap_overlay ovl = {
			.name = "l4-ovl",
			.supported_modes = OMAP_DSS_COLOR_RGB24U,
			.set_manager = &omap_dss_set_manager,
			.unset_manager = &omap_dss_unset_manager,
			.setup_input = &omap_dss_setup_overlay_input,
			.setup_output = &omap_dss_setup_overlay_output,
			.enable = &omap_dss_enable_overlay,
		};

		static struct omap_overlay_manager mgr = {
			.name = "l4",
			.num_overlays = 1,
			.overlays = &ovl,
			.set_display = &omap_dss_set_display,
			.unset_display = &omap_dss_unset_display,
			.apply = &ovl_mgr_apply_l4,
			.supported_displays =
				OMAP_DISPLAY_TYPE_DBI | OMAP_DISPLAY_TYPE_DSI,
		};

		omap_dss_add_overlay(&ovl);
		omap_dss_add_overlay_manager(&mgr);
		omap_dss_set_manager(&ovl, &mgr);
	}
#endif
}

/* connect overlays to the new device, if not already connected. if force
 * selected, connect always. */
void dss_recheck_connections(struct omap_display *display, bool force)
{
	int i;
	struct omap_overlay_manager *lcd_mgr;
	struct omap_overlay_manager *tv_mgr;
	struct omap_overlay_manager *mgr = NULL;

	lcd_mgr = omap_dss_get_overlay_manager(OMAP_DSS_OVL_MGR_LCD);
	tv_mgr = omap_dss_get_overlay_manager(OMAP_DSS_OVL_MGR_TV);

	if (display->type != OMAP_DISPLAY_TYPE_VENC) {
		if (!lcd_mgr->display || force) {
			if (lcd_mgr->display)
				lcd_mgr->unset_display(lcd_mgr);
			lcd_mgr->set_display(lcd_mgr, display);
			mgr = lcd_mgr;
		}
	}

	if (display->type == OMAP_DISPLAY_TYPE_VENC) {
		if (!tv_mgr->display || force) {
			if (tv_mgr->display)
				tv_mgr->unset_display(tv_mgr);
			tv_mgr->set_display(tv_mgr, display);
			mgr = tv_mgr;
		}
	}

	if (mgr) {
		for (i = 0; i < 3; i++) {
			struct omap_overlay *ovl;
			ovl = omap_dss_get_overlay(i);
			if (!ovl->manager || force) {
				if (ovl->manager)
					omap_dss_unset_manager(ovl);
				omap_dss_set_manager(ovl, mgr);
			}
		}
	}
}

void dss_uninit_overlays(struct platform_device *pdev)
{
	struct omap_overlay *ovl;

	while (!list_empty(&overlay_list)) {
		ovl = list_first_entry(&overlay_list,
				struct omap_overlay, list);
		list_del(&ovl->list);
		kobject_del(&ovl->kobj);
		kobject_put(&ovl->kobj);
		kfree(ovl);
	}

	num_overlays = 0;
}

