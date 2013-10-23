/*
 * linux/drivers/video/omap2/dss/display.c
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

#define DSS_SUBSYS_NAME "DISPLAY"

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/list.h>
#include <linux/platform_device.h>

#include <mach/display.h>
#include "dss.h"

static int num_displays;
static LIST_HEAD(display_list);

static ssize_t display_name_show(struct omap_display *display, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n", display->name);
}

static ssize_t display_enabled_show(struct omap_display *display, char *buf)
{
	bool enabled = display->state != OMAP_DSS_DISPLAY_DISABLED;

	return snprintf(buf, PAGE_SIZE, "%d\n", enabled);
}

static ssize_t display_enabled_store(struct omap_display *display,
		const char *buf, size_t size)
{
	bool enabled, r;

	enabled = simple_strtoul(buf, NULL, 10);

	if (enabled != (display->state != OMAP_DSS_DISPLAY_DISABLED)) {
		if (enabled) {
			r = display->enable(display);
			if (r)
				return r;
		} else {
			display->disable(display);
		}
	}

	return size;
}

static ssize_t display_upd_mode_show(struct omap_display *display, char *buf)
{
	enum omap_dss_update_mode mode = OMAP_DSS_UPDATE_AUTO;
	if (display->get_update_mode)
		mode = display->get_update_mode(display);
	return snprintf(buf, PAGE_SIZE, "%d\n", mode);
}

static ssize_t display_upd_mode_store(struct omap_display *display,
		const char *buf, size_t size)
{
	int val, r;
	enum omap_dss_update_mode mode;

	val = simple_strtoul(buf, NULL, 10);

	switch (val) {
		case OMAP_DSS_UPDATE_DISABLED:
		case OMAP_DSS_UPDATE_AUTO:
		case OMAP_DSS_UPDATE_MANUAL:
			mode = (enum omap_dss_update_mode)val;
			break;
		default:
			return -EINVAL;
	}

	if ((r = display->set_update_mode(display, mode)))
		return r;

	return size;
}

static ssize_t display_tear_show(struct omap_display *display, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n",
			display->get_te ? display->get_te(display) : 0);
}

static ssize_t display_tear_store(struct omap_display *display,
		const char *buf, size_t size)
{
	unsigned long te;
	int r;

	if (!display->enable_te || !display->get_te)
		return -ENOENT;

	te = simple_strtoul(buf, NULL, 0);

	if ((r = display->enable_te(display, te)))
		return r;

	return size;
}

static ssize_t display_timings_show(struct omap_display *display, char *buf)
{
	struct omap_video_timings t;

	if (!display->get_timings)
		return -ENOENT;

	display->get_timings(display, &t);

	return snprintf(buf, PAGE_SIZE, "%u,%u/%u/%u/%u,%u/%u/%u/%u\n",
			t.pixel_clock,
			t.x_res, t.hfp, t.hbp, t.hsw,
			t.y_res, t.vfp, t.vbp, t.vsw);
}

static ssize_t display_timings_store(struct omap_display *display,
		const char *buf, size_t size)
{
	struct omap_video_timings t;
	int r, found;

	if (!display->set_timings || !display->check_timings)
		return -ENOENT;

	found = 0;
#ifdef CONFIG_OMAP2_DSS_VENC
	if (strncmp("pal", buf, 3) == 0) {
		t = omap_dss_pal_timings;
		found = 1;
	} else if (strncmp("ntsc", buf, 4) == 0) {
		t = omap_dss_ntsc_timings;
		found = 1;
	}
#endif
	if (!found && sscanf(buf, "%u,%hu/%hu/%hu/%hu,%hu/%hu/%hu/%hu",
				&t.pixel_clock,
				&t.x_res, &t.hfp, &t.hbp, &t.hsw,
				&t.y_res, &t.vfp, &t.vbp, &t.vsw) != 9)
		return -EINVAL;

	if ((r = display->check_timings(display, &t)))
		return r;

	display->set_timings(display, &t);

	return size;
}

static ssize_t display_rotate_show(struct omap_display *display, char *buf)
{
	int rotate;
	if (!display->get_rotate)
		return -ENOENT;
	rotate = display->get_rotate(display);
	return snprintf(buf, PAGE_SIZE, "%u\n", rotate);
}

static ssize_t display_rotate_store(struct omap_display *display,
		const char *buf, size_t size)
{
	unsigned long rot;
	int r;

	if (!display->set_rotate || !display->get_rotate)
		return -ENOENT;

	rot = simple_strtoul(buf, NULL, 0);

	if ((r = display->set_rotate(display, rot)))
		return r;

	return size;
}

static ssize_t display_mirror_show(struct omap_display *display, char *buf)
{
	int mirror;
	if (!display->get_mirror)
		return -ENOENT;
	mirror = display->get_mirror(display);
	return snprintf(buf, PAGE_SIZE, "%u\n", mirror);
}

static ssize_t display_mirror_store(struct omap_display *display,
		const char *buf, size_t size)
{
	unsigned long mirror;
	int r;

	if (!display->set_mirror || !display->get_mirror)
		return -ENOENT;

	mirror = simple_strtoul(buf, NULL, 0);

	if ((r = display->set_mirror(display, mirror)))
		return r;

	return size;
}

static ssize_t display_panel_name_show(struct omap_display *display, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n",
			display->panel ? display->panel->name : "");
}

static ssize_t display_ctrl_name_show(struct omap_display *display, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%s\n",
			display->ctrl ? display->ctrl->name : "");
}

struct display_attribute {
	struct attribute attr;
	ssize_t (*show)(struct omap_display *, char *);
	ssize_t	(*store)(struct omap_display *, const char *, size_t);
};

#define DISPLAY_ATTR(_name, _mode, _show, _store) \
	struct display_attribute display_attr_##_name = \
	__ATTR(_name, _mode, _show, _store)

static DISPLAY_ATTR(name, S_IRUGO, display_name_show, NULL);
static DISPLAY_ATTR(enabled, S_IRUGO|S_IWUSR,
		display_enabled_show, display_enabled_store);
static DISPLAY_ATTR(update_mode, S_IRUGO|S_IWUSR,
		display_upd_mode_show, display_upd_mode_store);
static DISPLAY_ATTR(tear_elim, S_IRUGO|S_IWUSR,
		display_tear_show, display_tear_store);
static DISPLAY_ATTR(timings, S_IRUGO|S_IWUSR,
		display_timings_show, display_timings_store);
static DISPLAY_ATTR(rotate, S_IRUGO|S_IWUSR,
		display_rotate_show, display_rotate_store);
static DISPLAY_ATTR(mirror, S_IRUGO|S_IWUSR,
		display_mirror_show, display_mirror_store);
static DISPLAY_ATTR(panel_name, S_IRUGO, display_panel_name_show, NULL);
static DISPLAY_ATTR(ctrl_name, S_IRUGO, display_ctrl_name_show, NULL);

static struct attribute *display_sysfs_attrs[] = {
	&display_attr_name.attr,
	&display_attr_enabled.attr,
	&display_attr_update_mode.attr,
	&display_attr_tear_elim.attr,
	&display_attr_timings.attr,
	&display_attr_rotate.attr,
	&display_attr_mirror.attr,
	&display_attr_panel_name.attr,
	&display_attr_ctrl_name.attr,
	NULL
};

static ssize_t display_attr_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct omap_display *display;
	struct display_attribute *display_attr;

	display = container_of(kobj, struct omap_display, kobj);
	display_attr = container_of(attr, struct display_attribute, attr);

	if (!display_attr->show)
		return -ENOENT;

	return display_attr->show(display, buf);
}

static ssize_t display_attr_store(struct kobject *kobj, struct attribute *attr,
		const char *buf, size_t size)
{
	struct omap_display *display;
	struct display_attribute *display_attr;

	display = container_of(kobj, struct omap_display, kobj);
	display_attr = container_of(attr, struct display_attribute, attr);

	if (!display_attr->store)
		return -ENOENT;

	return display_attr->store(display, buf, size);
}

static struct sysfs_ops display_sysfs_ops = {
	.show = display_attr_show,
	.store = display_attr_store,
};

static struct kobj_type display_ktype = {
	.sysfs_ops = &display_sysfs_ops,
	.default_attrs = display_sysfs_attrs,
};

static void default_get_resolution(struct omap_display *display,
			u16 *xres, u16 *yres)
{
	*xres = display->panel->timings.x_res;
	*yres = display->panel->timings.y_res;
}

static void default_configure_overlay(struct omap_overlay *ovl)
{
	unsigned low, high, size;
	enum omap_burst_size burst;
	enum omap_plane plane = ovl->id;

	burst = OMAP_DSS_BURST_16x32;
	size = 16 * 32 / 8;

	dispc_set_burst_size(plane, burst);

	high = dispc_get_plane_fifo_size(plane) - 1;
	low = dispc_get_plane_fifo_size(plane) - size;

	dispc_setup_plane_fifo(plane, low, high);
}

static int default_wait_vsync(struct omap_display *display)
{
	unsigned long timeout = msecs_to_jiffies(500);
	u32 irq;

	if (display->type == OMAP_DISPLAY_TYPE_VENC)
		irq = DISPC_IRQ_EVSYNC_ODD;
	else
		irq = DISPC_IRQ_VSYNC;

	return omap_dispc_wait_for_irq_interruptible_timeout(irq, timeout);
}

static int default_get_recommended_bpp(struct omap_display *display)
{
	if (display->panel->recommended_bpp)
		return display->panel->recommended_bpp;

	switch (display->type) {
	case OMAP_DISPLAY_TYPE_DPI:
		if (display->hw_config.u.dpi.data_lines == 24)
			return 24;
		else
			return 16;

	case OMAP_DISPLAY_TYPE_DBI:
	case OMAP_DISPLAY_TYPE_DSI:
		if (display->ctrl->pixel_size == 24)
			return 24;
		else
			return 16;
	case OMAP_DISPLAY_TYPE_VENC:
	case OMAP_DISPLAY_TYPE_SDI:
		return 24;
		return 24;
	default:
		BUG();
	}
}

void dss_init_displays(struct platform_device *pdev)
{
	struct omap_dss_board_info *pdata = pdev->dev.platform_data;
	int i, r;

	INIT_LIST_HEAD(&display_list);

	num_displays = 0;

	for (i = 0; i < pdata->num_displays; ++i) {
		struct omap_display *display;

		switch (pdata->displays[i]->type) {
		case OMAP_DISPLAY_TYPE_DPI:
#ifdef CONFIG_OMAP2_DSS_RFBI
		case OMAP_DISPLAY_TYPE_DBI:
#endif
#ifdef CONFIG_OMAP2_DSS_SDI
		case OMAP_DISPLAY_TYPE_SDI:
#endif
#ifdef CONFIG_OMAP2_DSS_DSI
		case OMAP_DISPLAY_TYPE_DSI:
#endif
#ifdef CONFIG_OMAP2_DSS_VENC
		case OMAP_DISPLAY_TYPE_VENC:
#endif
			break;
		default:
			DSSERR("Support for display '%s' not compiled in.\n",
					pdata->displays[i]->name);
			continue;
		}

		display = kzalloc(sizeof(*display), GFP_KERNEL);

		/*atomic_set(&display->ref_count, 0);*/
		display->ref_count = 0;

		display->hw_config = *pdata->displays[i];
		display->type = pdata->displays[i]->type;
		display->name = pdata->displays[i]->name;

		display->get_resolution = default_get_resolution;
		display->get_recommended_bpp = default_get_recommended_bpp;
		display->configure_overlay = default_configure_overlay;
		display->wait_vsync = default_wait_vsync;

		switch (display->type) {
		case OMAP_DISPLAY_TYPE_DPI:
			dpi_init_display(display);
			break;
#ifdef CONFIG_OMAP2_DSS_RFBI
		case OMAP_DISPLAY_TYPE_DBI:
			rfbi_init_display(display);
			break;
#endif
#ifdef CONFIG_OMAP2_DSS_VENC
		case OMAP_DISPLAY_TYPE_VENC:
			venc_init_display(display);
			break;
#endif
#ifdef CONFIG_OMAP2_DSS_SDI
		case OMAP_DISPLAY_TYPE_SDI:
			sdi_init_display(display);
			break;
#endif
#ifdef CONFIG_OMAP2_DSS_DSI
		case OMAP_DISPLAY_TYPE_DSI:
			dsi_init_display(display);
			break;
#endif
		default:
			BUG();
		}

		r = kobject_init_and_add(&display->kobj, &display_ktype,
				&pdev->dev.kobj, "display%d", num_displays);

		if (r) {
			DSSERR("failed to create sysfs file\n");
			continue;
		}

		num_displays++;

		list_add_tail(&display->list, &display_list);
	}
}

void dss_uninit_displays(struct platform_device *pdev)
{
	struct omap_display *display;

	while (!list_empty(&display_list)) {
		display = list_first_entry(&display_list,
				struct omap_display, list);
		list_del(&display->list);
		kobject_del(&display->kobj);
		kobject_put(&display->kobj);
		kfree(display);
	}

	num_displays = 0;
}

int dss_suspend_all_displays(void)
{
	int r;
	struct omap_display *display;

	list_for_each_entry(display, &display_list, list) {
		if (display->state != OMAP_DSS_DISPLAY_ACTIVE) {
			display->activate_after_resume = 0;
			continue;
		}

		if (!display->suspend) {
			DSSERR("display '%s' doesn't implement suspend\n",
					display->name);
			r = -ENOSYS;
			goto err;
		}

		r = display->suspend(display);

		if (r)
			goto err;

		display->activate_after_resume = 1;
	}

	return 0;
err:
	/* resume all displays that were suspended */
	dss_resume_all_displays();
	return r;
}

int dss_resume_all_displays(void)
{
	int r;
	struct omap_display *display;

	list_for_each_entry(display, &display_list, list) {
		if (display->activate_after_resume && display->resume) {
			r = display->resume(display);
			if (r)
				return r;
		}

		display->activate_after_resume = 0;
	}

	return 0;
}

int omap_dss_get_num_displays(void)
{
	return num_displays;
}
EXPORT_SYMBOL(omap_dss_get_num_displays);

struct omap_display *dss_get_display(int no)
{
	int i = 0;
	struct omap_display *display;

	list_for_each_entry(display, &display_list, list) {
		if (i++ == no)
			return display;
	}

	return NULL;
}

struct omap_display *omap_dss_get_display(int no)
{
	struct omap_display *display;

	display = dss_get_display(no);

	if (!display)
		return NULL;

	switch (display->type) {
	case OMAP_DISPLAY_TYPE_VENC:
		break;

	case OMAP_DISPLAY_TYPE_DPI:
	case OMAP_DISPLAY_TYPE_SDI:
		if (display->panel == NULL)
			return NULL;
		break;

	case OMAP_DISPLAY_TYPE_DBI:
	case OMAP_DISPLAY_TYPE_DSI:
		if (display->panel == NULL || display->ctrl == NULL)
			return NULL;
		break;

	default:
		return NULL;
	}

	if (display->ctrl) {
		if (!try_module_get(display->ctrl->owner))
			goto err0;

		if (display->ctrl->init)
			if (display->ctrl->init(display) != 0)
				goto err1;
	}

	if (display->panel) {
		if (!try_module_get(display->panel->owner))
			goto err2;

		if (display->panel->init)
			if (display->panel->init(display) != 0)
				goto err3;
	}

	display->ref_count++;
	/*
	if (atomic_cmpxchg(&display->ref_count, 0, 1) != 0)
		return 0;
*/

	return display;
err3:
	if (display->panel)
		module_put(display->panel->owner);
err2:
	if (display->ctrl && display->ctrl->cleanup)
		display->ctrl->cleanup(display);
err1:
	if (display->ctrl)
		module_put(display->ctrl->owner);
err0:
	return NULL;
}
EXPORT_SYMBOL(omap_dss_get_display);

void omap_dss_put_display(struct omap_display *display)
{
	if (--display->ref_count > 0)
		return;
/*
	if (atomic_cmpxchg(&display->ref_count, 1, 0) != 1)
		return;
*/
	if (display->ctrl) {
		if (display->ctrl->cleanup)
			display->ctrl->cleanup(display);
		module_put(display->ctrl->owner);
	}

	if (display->panel) {
		if (display->panel->cleanup)
			display->panel->cleanup(display);
		module_put(display->panel->owner);
	}
}
EXPORT_SYMBOL(omap_dss_put_display);

void omap_dss_register_ctrl(struct omap_ctrl *ctrl)
{
	struct omap_display *display;

	list_for_each_entry(display, &display_list, list) {
		if (display->hw_config.ctrl_name &&
		    strcmp(display->hw_config.ctrl_name, ctrl->name) == 0) {
			display->ctrl = ctrl;
			DSSDBG("ctrl '%s' registered\n", ctrl->name);
		}
	}
}
EXPORT_SYMBOL(omap_dss_register_ctrl);

void omap_dss_register_panel(struct omap_panel *panel)
{
	struct omap_display *display;

	list_for_each_entry(display, &display_list, list) {
		if (display->hw_config.panel_name &&
		    strcmp(display->hw_config.panel_name, panel->name) == 0) {
			display->panel = panel;
			DSSDBG("panel '%s' registered\n", panel->name);
		}
	}
}
EXPORT_SYMBOL(omap_dss_register_panel);

void omap_dss_unregister_ctrl(struct omap_ctrl *ctrl)
{
	struct omap_display *display;

	list_for_each_entry(display, &display_list, list) {
		if (display->hw_config.ctrl_name &&
		    strcmp(display->hw_config.ctrl_name, ctrl->name) == 0)
			display->ctrl = NULL;
	}
}
EXPORT_SYMBOL(omap_dss_unregister_ctrl);

void omap_dss_unregister_panel(struct omap_panel *panel)
{
	struct omap_display *display;

	list_for_each_entry(display, &display_list, list) {
		if (display->hw_config.panel_name &&
		    strcmp(display->hw_config.panel_name, panel->name) == 0)
			display->panel = NULL;
	}
}
EXPORT_SYMBOL(omap_dss_unregister_panel);
