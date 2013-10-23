/*
 * TC358710XBG "GF" DSI hub
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Tomi Valkeinen <tomi.valkeinen@nokia.com>
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

/*#define DEBUG*/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/backlight.h>
#include <linux/fb.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/semaphore.h>
#include <linux/hrtimer.h>

#include <plat/display.h>
#include <plat/ctrl-gf.h>

/* DSI Virtual channel. Hardcoded for now. */
#define VC_GF 3
#define VC_GFFB 2
#define VC_HIMALAYA 0

#define DCS_READ_NUM_ERRORS	0x05
#define DCS_READ_POWER_MODE	0x0a
#define DCS_READ_MADCTL		0x0b
#define DCS_READ_PIXEL_FORMAT	0x0c
#define DCS_RDDSDR		0x0f
#define DCS_SLEEP_IN		0x10
#define DCS_SLEEP_OUT		0x11
#define DCS_DISPLAY_OFF		0x28
#define DCS_DISPLAY_ON		0x29
#define DCS_COLUMN_ADDR		0x2a
#define DCS_PAGE_ADDR		0x2b
#define DCS_MEMORY_WRITE	0x2c
#define DCS_TEAR_OFF		0x34
#define DCS_TEAR_ON		0x35
#define DCS_MEM_ACC_CTRL	0x36
#define DCS_PIXEL_FORMAT	0x3a
#define DCS_BRIGHTNESS		0x51
#define DCS_CTRL_DISPLAY	0x53
#define DCS_WRITE_CABC		0x55
#define DCS_READ_CABC		0x56
#define DCS_GET_ID1		0xda
#define DCS_GET_ID2		0xdb
#define DCS_GET_ID3		0xdc

#define GF_RDSTAT		0x82
#define GF_REFCLKF		0x83
#define GF_SETPRIBR		0x84
#define GF_IFCLKSRC		0x86
#define GF_DSIHIFBR		0x87
#define GF_VCMAP		0x93
#define GF_DSILPTIM		0xa0
#define GF_DSILNCFG		0xa1
#define GF_DSILNIDLE		0xa2
#define GF_DSITXMODE		0xa3
#define GF_EXTCMDWR		0xfd

#define GF_ENPERPOR		0x80
#define GF_DISPERPOR		0x81

#define GF_FBGWRC		0x82
#define GF_FB1GC		0x83
#define GF_FB1CGM		0x84
#define GF_FB1CSC		0x85
#define GF_FB1PUA		0x86
#define GF_FB1WAT		0x87

#define GF_RS1GC		0x8d
#define GF_RS1RSC		0x8e
#define GF_RS1PRA		0x8f
#define GF_RS1VTC		0x90
#define GF_RS1TET		0x91
#define GF_RS1TED		0x92
#define GF_RS1USP		0x93
#define GF_RDDSIERR		0xa4
#define GF_TESRC		0xb0
#define GF_EXTALYWR		0xc0
#define GF_EXTALYRD		0xc1

/* EXT commands */
#define GF_EXT_RS1DCM		0x00

#define GF_FMT_STAT_STR_LEN 255

#define MEASURE_PERF
/*#define MEASURE_PERF_SHOW_ALWAYS*/

static irqreturn_t gf_te_isr(int irq, void *data);
static void gf_frame_timeout_work_callback(struct work_struct *work);
static enum hrtimer_restart gf_timer_handler(struct hrtimer *handle);
static void gf_calibrate_work_callback(struct work_struct *work);

enum gf_update_te {
	GF_UPDATE_NO_TE,
	GF_UPDATE_EXT_TE,
	GF_UPDATE_DSI_TE,
};

enum gf_update_mode {
	GF_UPDATE_NORMAL,	/* update in one part */
	GF_UPDATE_RL,		/* right first, then left */
	GF_UPDATE_LR,		/* left first, then right */
	GF_UPDATE_BT,		/* bottom first, then top */
	GF_UPDATE_TB,		/* top first, then bottom */
};

enum gf_update_state {
	GF_UPD_NONE,
	GF_UPD_FRAME1_WAIT_START,
	GF_UPD_FRAME1_ONGOING,
	GF_UPD_FRAME2_WAIT_START,
	GF_UPD_FRAME2_ONGOING,
};

struct gf_data {
	struct semaphore lock;

	struct backlight_device *bldev;

	unsigned long	hw_guard_end;	/* next value of jiffies when we can
					 * issue the next sleep in/out command
					 */
	unsigned long	hw_guard_wait;	/* max guard time in jiffies */

	struct omap_dss_device *dssdev;

	bool enabled;
	u8 rotate;
	bool mirror;

	bool te_enabled;

	atomic_t upd_state;
	struct {
		u16 x;
		u16 y;
		u16 w;
		u16 h;
	} update_region;
	struct delayed_work frame_timeout_work;
	enum gf_update_te update_te;
	enum gf_update_mode update_mode;

	bool intro_printed;
	bool panel_intro_printed;

	unsigned cabc_mode;

	struct clk *sysclk;

	struct regulator *vpnl_reg;
	struct regulator *vddi_reg;

	struct hrtimer hrtimer;
	u32 timer_us;

#ifdef MEASURE_PERF
	struct {
		ktime_t phase0;	/* update */
		ktime_t phase1;	/* timer */
		ktime_t phase2;	/* cb1 */
		ktime_t phase3;	/* TE */
		ktime_t phase4;	/* cb2 */
	} perf;
#endif

	bool calibrating;
	int num_calib_values;
	ktime_t calib_values[30];
	struct work_struct calibrate_work;
};

enum dsi_tx_mode {
	DSI_TX_MODE_ALWAYS_LP = 0,
	DSI_TX_MODE_ALWAYS_HS = 1,
	DSI_TX_MODE_FOLLOW_HOST = 2,
};

static int _gf_enable_te(struct omap_dss_device *dssdev, bool enable);

static void hw_guard_start(struct gf_data *td, int guard_msec)
{
	td->hw_guard_wait = msecs_to_jiffies(guard_msec);
	td->hw_guard_end = jiffies + td->hw_guard_wait;
}

static void hw_guard_wait(struct gf_data *td)
{
	unsigned long wait = td->hw_guard_end - jiffies;

	if ((long)wait > 0 && wait <= td->hw_guard_wait) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(wait);
	}
}


static int gf_sleep_in(struct gf_data *td)
{
	u8 cmd;
	int r;

	hw_guard_wait(td);

	cmd = DCS_SLEEP_IN;
	r = dsi_vc_dcs_write_nosync(VC_GF, &cmd, 1);
	if (r)
		return r;

	hw_guard_start(td, 120);

	/* wait for SLEEP_IN to complete */
	msleep(5);

	return 0;
}

static int gf_sleep_out(struct gf_data *td)
{
	int r;
	u8 buf[] = { DCS_SLEEP_OUT };
	u8 diag;

	hw_guard_wait(td);

	r = dsi_vc_dcs_write_nosync(VC_GF, buf, sizeof(buf));
	if (r)
		return r;

	hw_guard_start(td, 120);

	/* wait for SLEEP_OUT to complete */
	msleep(4);

	r = dsi_vc_dcs_read_1(VC_GF, DCS_RDDSDR, &diag);
	if (r)
		return r;

	if (!(diag & (1 << 6)))
		return -EIO;

	return 0;
}

static int dcs_get_id(int channel, u8 *id1, u8 *id2, u8 *id3)
{
	int r;

	r = dsi_vc_dcs_read_1(channel, DCS_GET_ID1, id1);
	if (r)
		return r;
	r = dsi_vc_dcs_read_1(channel, DCS_GET_ID2, id2);
	if (r)
		return r;
	r = dsi_vc_dcs_read_1(channel, DCS_GET_ID3, id3);
	if (r)
		return r;

	return 0;
}

static int gf_read_status(struct omap_dss_device *dssdev, u16 *stat)
{
	int r;
	u8 b1, b2;

	*stat = 0;

	r = dsi_vc_dcs_read_2(VC_GF, GF_RDSTAT, &b1, &b2);
	if (r)
		return r;

	*stat = (b1 << 8) | b2;

	return 0;
}

static size_t gf_format_status(u16 stat, char *buf, size_t len)
{
	if (len < GF_FMT_STAT_STR_LEN)
		return -EINVAL;

	buf[0] = 0;

	if (stat & (1 << 0))
		strcat(buf, "BUSY ");
	if (stat & (1 << 1))
		strcat(buf, "ETO ");
	if (stat & (1 << 2))
		strcat(buf, "EBIT ");
	if (stat & (1 << 3))
		strcat(buf, "EPER ");
	if (stat & (1 << 4))
		strcat(buf, "EHST ");
	if (stat & (1 << 5))
		strcat(buf, "EBUF ");
	if (stat & (1 << 6))
		strcat(buf, "(reserved) ");
	if (stat & (1 << 7))
		strcat(buf, "EDSI ");
	if (stat & (1 << 8))
		strcat(buf, "LCD1BUSY ");
	if (stat & (1 << 9))
		strcat(buf, "LCD2BUSY ");
	if (stat & (1 << 10))
		strcat(buf, "BFCBUSY ");

	return strlen(buf);
}

static int gf_read_dsi_errors(struct omap_dss_device *dssdev,
		u16 *stat1, u16 *stat2)
{
	int r;
	u8 buf[6];

	r = dsi_vc_dcs_read(VC_GF, GF_RDDSIERR, buf, sizeof(buf));
	if (r < 0)
		return r;

	*stat1 = (buf[0] << 8) | buf[1];
	*stat2 = (buf[2] << 8) | buf[3];

	return 0;
}

static void gf_get_timings(struct omap_dss_device *dssdev,
		struct omap_video_timings *timings)
{
	*timings = dssdev->panel.timings;
}

static void gf_get_resolution(struct omap_dss_device *dssdev,
		u16 *xres, u16 *yres)
{
	struct gf_data *data = dev_get_drvdata(&dssdev->dev);
	int rotate = data->rotate;

	/* HACK to get himalaya look like portrait scanned */
	rotate = (rotate + 3) % 4;

	if (rotate == 0 || rotate == 2) {
		*xres = dssdev->panel.timings.x_res;
		*yres = dssdev->panel.timings.y_res;
	} else {
		*yres = dssdev->panel.timings.x_res;
		*xres = dssdev->panel.timings.y_res;
	}

}

#define HIMALAYA_WIDTH	48960
#define HIMALAYA_HEIGHT	88128

static void gf_get_dimensions(struct omap_dss_device *dssdev,
			u32 *width, u32 *height)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);

	if (td->rotate == 0 || td->rotate == 2) {
		*width = HIMALAYA_WIDTH;
		*height = HIMALAYA_HEIGHT;
	} else {
		*width = HIMALAYA_HEIGHT;
		*height = HIMALAYA_WIDTH;
	}
}

static ssize_t gf_hw_revision_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	u8 id1, id2, id3;
	int r;

	down(&td->lock);

	if (td->enabled) {
		dsi_bus_lock();
		r = dcs_get_id(VC_GF, &id1, &id2, &id3);
		dsi_bus_unlock();
	} else {
		r = -ENODEV;
	}

	up(&td->lock);

	if (r)
		return r;

	return snprintf(buf, PAGE_SIZE, "%02x.%02x.%02x\n", id1, id2, id3);
}

static ssize_t gf_panel_hw_revision_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	u8 id1, id2, id3;
	int r;

	down(&td->lock);

	if (!td->enabled) {
		up(&td->lock);
		return -ENODEV;
	}

	dsi_bus_lock();

	r = dcs_get_id(VC_HIMALAYA, &id1, &id2, &id3);
	if (r)
		goto err;

	dsi_bus_unlock();

	up(&td->lock);

	return snprintf(buf, PAGE_SIZE, "%02x.%02x.%02x\n", id1, id2, id3);
err:
	dsi_bus_unlock();
	up(&td->lock);
	return r;
}

static ssize_t gf_status_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	u16 stat;
	int r;
	char str[GF_FMT_STAT_STR_LEN];
	size_t len;

	down(&td->lock);

	if (td->enabled) {
		dsi_bus_lock();
		r = gf_read_status(dssdev, &stat);
		dsi_bus_unlock();
	} else {
		r = -ENODEV;
	}

	up(&td->lock);

	if (r)
		return r;

	len = gf_format_status(stat, str, sizeof(str));

	return snprintf(buf, PAGE_SIZE, "%s\n", str);
}

static ssize_t gf_dsi_err_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	u16 stat1 = 0, stat2 = 0;
	int r;

	down(&td->lock);

	if (td->enabled) {
		dsi_bus_lock();
		r = gf_read_dsi_errors(dssdev, &stat1, &stat2);
		dsi_bus_unlock();
	} else {
		r = -ENODEV;
	}

	up(&td->lock);

	if (r)
		return r;

	return snprintf(buf, PAGE_SIZE, "%x %x\n", stat1, stat2);
}

static const char *cabc_modes[] = {
	"off",		/* used also always when CABC is not supported */
	"ui",
	"still-image",
	"moving-image",
};

static ssize_t show_cabc_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	const char *mode_str;
	int mode;
	int len;

	down(&td->lock);
	mode = td->cabc_mode;
	up(&td->lock);

	mode_str = "unknown";
	if (mode >= 0 && mode < ARRAY_SIZE(cabc_modes))
		mode_str = cabc_modes[mode];
	len = snprintf(buf, PAGE_SIZE, "%s\n", mode_str);

	return len < PAGE_SIZE - 1 ? len : PAGE_SIZE - 1;
}

static ssize_t store_cabc_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(cabc_modes); i++) {
		if (sysfs_streq(cabc_modes[i], buf))
			break;
	}

	if (i == ARRAY_SIZE(cabc_modes))
		return -EINVAL;

	down(&td->lock);

	if (td->enabled) {
		dsi_bus_lock();
		dsi_vc_dcs_write_1(VC_HIMALAYA, DCS_WRITE_CABC, i);
		dsi_bus_unlock();
	}

	td->cabc_mode = i;

	up(&td->lock);

	return count;
}

static ssize_t show_cabc_available_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int len;
	int i;

	for (i = 0, len = 0;
	     len < PAGE_SIZE && i < ARRAY_SIZE(cabc_modes); i++)
		len += snprintf(&buf[len], PAGE_SIZE - len, "%s%s%s",
			i ? " " : "", cabc_modes[i],
			i == ARRAY_SIZE(cabc_modes) - 1 ? "\n" : "");

	return len < PAGE_SIZE ? len : PAGE_SIZE - 1;
}

static DEVICE_ATTR(hw_revision, S_IRUGO, gf_hw_revision_show, NULL);
static DEVICE_ATTR(panel_hw_revision, S_IRUGO,
		gf_panel_hw_revision_show, NULL);
static DEVICE_ATTR(status, S_IRUGO, gf_status_show, NULL);
static DEVICE_ATTR(dsi_err, S_IRUGO, gf_dsi_err_show, NULL);
static DEVICE_ATTR(cabc_mode, S_IRUGO | S_IWUSR,
		show_cabc_mode, store_cabc_mode);
static DEVICE_ATTR(cabc_available_modes, S_IRUGO,
		show_cabc_available_modes, NULL);

static struct attribute *gf_attrs[] = {
	&dev_attr_hw_revision.attr,
	&dev_attr_panel_hw_revision.attr,
	&dev_attr_status.attr,
	&dev_attr_dsi_err.attr,
	&dev_attr_cabc_mode.attr,
	&dev_attr_cabc_available_modes.attr,
	NULL,
};

static struct attribute_group gf_attr_group = {
	.attrs = gf_attrs,
};

static int gf_bl_update_status(struct backlight_device *dev)
{
	struct omap_dss_device *dssdev = dev_get_drvdata(&dev->dev);
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int r;
	int level;

	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		level = dev->props.brightness;
	else
		level = 0;

	dev_dbg(&dssdev->dev, "update brightness to %d\n", level);

	down(&td->lock);

	r = 0;

	if (td->enabled) {
		dsi_bus_lock();
		r = dsi_vc_dcs_write_1(VC_HIMALAYA, DCS_BRIGHTNESS, level);
		dsi_bus_unlock();
	}

	up(&td->lock);

	return r;
}

static int gf_bl_get_intensity(struct backlight_device *dev)
{
	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		return dev->props.brightness;

	return 0;
}

static struct backlight_ops gf_bl_ops = {
	.get_brightness = gf_bl_get_intensity,
	.update_status  = gf_bl_update_status,
};

static void gf_hw_reset(struct omap_dss_device *dssdev)
{
	struct ctrl_gf_platform_data *pdata =
		(struct ctrl_gf_platform_data *)dssdev->data;
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);

	/* clk has to be enabled during reset */
	clk_enable(td->sysclk);

	/* ensure that clk has been on for a while */
	msleep(1);

	/* first set the reset high, so that GF notices the falling edge */
	gpio_set_value(pdata->panel_gpio, 1);
	udelay(10);
	/* reset GF */
	gpio_set_value(pdata->reset_gpio, 0);
	/* keep reset 10us or more */
	udelay(10);
	gpio_set_value(pdata->reset_gpio, 1);
	/* wait 2ms after reset */
	msleep(2);

	clk_disable(td->sysclk);
}

static void himalaya_hw_reset(struct omap_dss_device *dssdev)
{
	struct ctrl_gf_platform_data *pdata =
		(struct ctrl_gf_platform_data *)dssdev->data;

	/* first set the reset high, so that himalaya notices the falling
	 * edge */
	gpio_set_value(pdata->panel_gpio, 1);
	udelay(10);
	/* reset the panel */
	gpio_set_value(pdata->panel_gpio, 0);
	/* assert reset for at least 10us */
	udelay(10);
	gpio_set_value(pdata->panel_gpio, 1);
	/* wait 5ms after releasing reset */
	msleep(5);
}

static int gf_probe(struct omap_dss_device *dssdev)
{
	struct gf_data *td;
	struct ctrl_gf_platform_data *pdata =
		(struct ctrl_gf_platform_data *)dssdev->data;
	struct backlight_device *bldev;
	int r;
	int v;

	const struct omap_video_timings gf_panel_timings = {
		.x_res		= 480,
		.y_res		= 864,
	};

	dev_dbg(&dssdev->dev, "probe\n");

	dssdev->panel.config = OMAP_DSS_LCD_TFT;
	dssdev->panel.timings = gf_panel_timings;
	dssdev->ctrl.pixel_size = 24;

	td = kzalloc(sizeof(*td), GFP_KERNEL);
	if (!td) {
		r = -ENOMEM;
		goto err0;
	}
	td->dssdev = dssdev;

	sema_init(&td->lock, 1);

	dev_set_drvdata(&dssdev->dev, td);

	r = sysfs_create_group(&dssdev->dev.kobj, &gf_attr_group);
	if (r) {
		dev_err(&dssdev->dev, "failed to create sysfs files\n");
		goto err1;
	}


	r = gpio_request(pdata->pdx_gpio, "Grande Finale PDXx");
	if (r < 0) {
		dev_err(&dssdev->dev, "unable to get PDX GPIO\n");
		goto err2;
	}

	/* initialize to deep sleep mode */
	gpio_direction_output(pdata->pdx_gpio, 0);


	r = gpio_request(pdata->reset_gpio, "Grande Finale RESX");
	if (r < 0) {
		dev_err(&dssdev->dev, "unable to get RESX GPIO\n");
		goto err3;
	}

	/* initialize to non-reset state */
	gpio_direction_output(pdata->reset_gpio, 1);


	r = gpio_request(pdata->panel_gpio, "Himalaya RESX");
	if (r < 0) {
		dev_err(&dssdev->dev, "unable to get Himalaya RESX GPIO\n");
		goto err4;
	}

	/* initialize to non-reset state */
	gpio_direction_output(pdata->panel_gpio, 1);

	td->calibrating = true;
	td->num_calib_values = 0;
	INIT_WORK(&td->calibrate_work, gf_calibrate_work_callback);

	hrtimer_init(&td->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	td->hrtimer.function = gf_timer_handler;

	r = gpio_request(pdata->te_gpio, "gf irq");
	if (r) {
		dev_err(&dssdev->dev, "GPIO request failed\n");
		goto err5;
	}

	gpio_direction_input(pdata->te_gpio);

	r = request_irq(gpio_to_irq(pdata->te_gpio), gf_te_isr,
			IRQF_DISABLED | IRQF_TRIGGER_RISING,
			"gf vsync", dssdev);

	if (r) {
		dev_err(&dssdev->dev, "IRQ request failed\n");
		gpio_free(pdata->te_gpio);
		goto err5;
	}

	INIT_DELAYED_WORK_DEFERRABLE(&td->frame_timeout_work,
			gf_frame_timeout_work_callback);

	td->sysclk = clk_get(&dssdev->dev, pdata->sysclk_name);
	if (IS_ERR(td->sysclk)) {
		dev_err(&dssdev->dev, "can't get %s\n", pdata->sysclk_name);
		r = PTR_ERR(td->sysclk);
		td->sysclk = NULL;
		goto err_te;
	}


	td->vpnl_reg = regulator_get(&dssdev->dev, "VPNL");
	if (IS_ERR(td->vpnl_reg)) {
		dev_err(&dssdev->dev, "failed to get VPNL regulator\n");
		r = PTR_ERR(td->vpnl_reg);
		goto err6;
	}

	v = regulator_get_voltage(td->vpnl_reg);
	if (v < 2300000 || v > 4800000) {
		r = regulator_set_voltage(td->vpnl_reg, 2300000, 4800000);
		if (r) {
			dev_err(&dssdev->dev, "failed to set VPNL voltage\n");
			goto err7;
		}
	}

	r = regulator_enable(td->vpnl_reg);
	if (r) {
		dev_err(&dssdev->dev, "failed to enable VPNL regulator\n");
		goto err7;
	}


	td->vddi_reg = regulator_get(&dssdev->dev, "VDDI");
	if (IS_ERR(td->vddi_reg)) {
		dev_err(&dssdev->dev, "failed to get VDDI regulator\n");
		r = PTR_ERR(td->vddi_reg);
		goto err8;
	}

	v = regulator_get_voltage(td->vddi_reg);
	if (v < 1650000 || v > 1950000) {
		r = regulator_set_voltage(td->vddi_reg, 1650000, 1950000);
		if (r) {
			dev_err(&dssdev->dev, "failed to set VDDI voltage\n");
			goto err8;
		}
	}

	r = regulator_enable(td->vddi_reg);
	if (r) {
		dev_err(&dssdev->dev, "failed to enable VDDI regulator\n");
		goto err9;
	}

	bldev = backlight_device_register("himalaya", &dssdev->dev, dssdev,
			&gf_bl_ops);
	if (IS_ERR(bldev)) {
		r = PTR_ERR(bldev);
		goto err10;
	}

	td->bldev = bldev;

	bldev->props.fb_blank = FB_BLANK_UNBLANK;
	bldev->props.power = FB_BLANK_UNBLANK;
	bldev->props.max_brightness = 255;
	bldev->props.brightness = 255;

	gf_bl_update_status(bldev);

	gf_hw_reset(dssdev);
	himalaya_hw_reset(dssdev);

	return 0;
err10:
	regulator_disable(td->vddi_reg);
err9:
	regulator_put(td->vddi_reg);
err8:
	regulator_disable(td->vpnl_reg);
err7:
	regulator_put(td->vpnl_reg);
err6:
	clk_put(td->sysclk);
err_te:
	free_irq(gpio_to_irq(pdata->te_gpio), dssdev);
	gpio_free(pdata->te_gpio);
err5:
	gpio_free(pdata->panel_gpio);
err4:
	gpio_free(pdata->reset_gpio);
err3:
	gpio_free(pdata->pdx_gpio);
err2:
	sysfs_remove_group(&dssdev->dev.kobj, &gf_attr_group);
err1:
	kfree(td);
err0:
	return r;
}

static void gf_remove(struct omap_dss_device *dssdev)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	struct ctrl_gf_platform_data *pdata =
		(struct ctrl_gf_platform_data *)dssdev->data;
	struct backlight_device *bldev;

	dev_dbg(&dssdev->dev, "remove\n");

	bldev = td->bldev;
	bldev->props.power = FB_BLANK_POWERDOWN;
	gf_bl_update_status(bldev);
	backlight_device_unregister(bldev);

	regulator_disable(td->vddi_reg);
	regulator_disable(td->vpnl_reg);
	regulator_put(td->vddi_reg);
	regulator_put(td->vpnl_reg);

	clk_put(td->sysclk);

	free_irq(gpio_to_irq(pdata->te_gpio), dssdev);
	gpio_free(pdata->te_gpio);
	gpio_free(pdata->panel_gpio);
	gpio_free(pdata->reset_gpio);
	gpio_free(pdata->pdx_gpio);

	sysfs_remove_group(&dssdev->dev.kobj, &gf_attr_group);

	kfree(td);
}

static void gf_deep_sleep_disable(struct omap_dss_device *dssdev)
{
	struct ctrl_gf_platform_data *pdata =
		(struct ctrl_gf_platform_data *)dssdev->data;
	/* wake GF from deep sleep */
	gpio_set_value(pdata->pdx_gpio, 1);
	/* wait for GF to wake up */
	msleep(5);
}

static void gf_deep_sleep_enable(struct omap_dss_device *dssdev)
{
	struct ctrl_gf_platform_data *pdata =
		(struct ctrl_gf_platform_data *)dssdev->data;
	/* put GF to deep sleep */
	gpio_set_value(pdata->pdx_gpio, 0);
	/* wait for GF to go to sleep */
	msleep(5);
}

static int gf_set_lane_conf(int lanes_a, int lanes_b, int lanes_c)
{
	u8 buf[] = {
		GF_DSILNCFG,
		lanes_a | (lanes_b << 2) | (lanes_c << 4),
	};
	return dsi_vc_dcs_write(VC_GF, buf, sizeof(buf));
}

static int gf_set_transfer_mode(enum dsi_tx_mode mode_a,
		enum dsi_tx_mode mode_b)
{
	u8 buf[] = {
		GF_DSITXMODE,
		mode_a | (mode_b << 2)
	};
	return dsi_vc_dcs_write(VC_GF, buf, sizeof(buf));
}

static int gf_set_vcmap(int vc0, int out0, int vc1, int out1,
		int vc2, int out2)
{
	u8 buf[] = {
		GF_VCMAP,
		out0 | (vc0 << 2),
		out1 | (vc1 << 2),
		out2 | (vc2 << 2),
	};

	return dsi_vc_dcs_write(VC_GF, buf, sizeof(buf));
}

static int gf_set_lane_idle_state(int clk_a, int d0_a, int d1_a,
		int clk_b, int d0_b, int d1_b)
{
	u8 buf[] = {
		GF_DSILNIDLE,
		clk_a | (d0_a << 2) | (d1_a << 3),
		clk_b | (d0_b << 2) | (d1_b << 3),
		0,
	};
	return dsi_vc_dcs_write(VC_GF, buf, sizeof(buf));
}

static int gf_check_status(struct omap_dss_device *dssdev)
{
	int r;
	u16 stat;
	char buf[GF_FMT_STAT_STR_LEN];
	size_t len;

	r = gf_read_status(dssdev, &stat);
	if (r < 0)
		return r;

	if (stat == 0)
		return 0;

	len = gf_format_status(stat, buf, sizeof(buf));
	if (len < 0)
		return r;

	dev_err(&dssdev->dev, "RDSTAT: %04x: %s\n", stat, buf);

	return 0;
}

static int gf_set_refclk(int div)
{
	int r;
	u8 buf[] = {
		GF_REFCLKF,
		div,
	};

	r = dsi_vc_dcs_write_nosync(VC_GF, buf, sizeof(buf));
	if (r)
		return r;

	/* wait for REFCLKF to complete */
	msleep(5);

	return 0;
}

static int gf_set_lp_timings(int div_a, int div_b, int div_c)
{
	u8 buf[] = {
		GF_DSILPTIM,
		div_a,
		div_b,
		div_c,
	};

	return dsi_vc_dcs_write_nosync(VC_GF, buf, sizeof(buf));
}

static int gf_set_pri_datarate(unsigned rate_mbps)
{
	u8 buf[] = { GF_SETPRIBR, 0 };
	int r;
	unsigned rate = rate_mbps;

	if (rate < 80)
		return -EINVAL;

	rate = (rate - 80) / 4 + 1;

	buf[1] = rate;

	r = dsi_vc_dcs_write(VC_GF, buf, sizeof(buf));
	if (r)
		return r;

	/* wait for SETPRIBR to complete */
	msleep(1);

	return 0;
}

static int gf_set_host_datarate(unsigned rate_mbps)
{
	u8 buf[] = { GF_DSIHIFBR, 0 };
	unsigned rate = rate_mbps;

	if (rate < 80)
		return -EINVAL;

	rate = (rate - 80) / 4 + 1;

	buf[1] = rate;

	return dsi_vc_dcs_write(VC_GF, buf, sizeof(buf));
}

static int gf_set_if_clk_src(int sel_a, int sel_b, int sel_c, int sel_dbi)
{
	u8 buf[] = {
		GF_IFCLKSRC,
		sel_a | (sel_b << 1) | (sel_c << 2) | (sel_dbi << 3),
	};

	return dsi_vc_dcs_write(VC_GF, buf, sizeof(buf));
}

static int gf_configure_hs_tx_cap(void)
{
	/* Configures DSI HS TX Capacitance for DSI */
	u8 buf[] = { GF_EXTCMDWR, 0x22, 0x00 };
	return dsi_vc_dcs_write(VC_GF, buf, sizeof(buf));
}

static int himalaya_set_addr_mode(u8 rotate, bool mirror)
{
	int r;
	u8 mode;
	int b5, b6, b7;

	/* HACK to get himalaya look like portrait scanned */
	rotate = (rotate + 3) % 4;

	r = dsi_vc_dcs_read_1(VC_HIMALAYA, DCS_READ_MADCTL, &mode);
	if (r)
		return r;

	switch (rotate) {
	default:
	case 0:
		b7 = 0;
		b6 = 0;
		b5 = 0;
		break;
	case 1:
		b7 = 0;
		b6 = 1;
		b5 = 1;
		break;
	case 2:
		b7 = 1;
		b6 = 1;
		b5 = 0;
		break;
	case 3:
		b7 = 1;
		b6 = 0;
		b5 = 1;
		break;
	}

	if (mirror)
		b6 = !b6;

	mode &= ~((1<<7) | (1<<6) | (1<<5));
	mode |= (b7 << 7) | (b6 << 6) | (b5 << 5);

	return dsi_vc_dcs_write_1(VC_HIMALAYA, DCS_MEM_ACC_CTRL, mode);
}

static int himalaya_set_update_window(u16 x, u16 y, u16 w, u16 h)
{
	int r;
	u16 x1 = x;
	u16 x2 = x + w - 1;
	u16 y1 = y;
	u16 y2 = y + h - 1;

	u8 buf[5];
	buf[0] = DCS_COLUMN_ADDR;
	buf[1] = (x1 >> 8) & 0xff;
	buf[2] = (x1 >> 0) & 0xff;
	buf[3] = (x2 >> 8) & 0xff;
	buf[4] = (x2 >> 0) & 0xff;

	r = dsi_vc_dcs_write_nosync(VC_HIMALAYA, buf, sizeof(buf));
	if (r)
		return r;

	buf[0] = DCS_PAGE_ADDR;
	buf[1] = (y1 >> 8) & 0xff;
	buf[2] = (y1 >> 0) & 0xff;
	buf[3] = (y2 >> 8) & 0xff;
	buf[4] = (y2 >> 0) & 0xff;

	r = dsi_vc_dcs_write_nosync(VC_HIMALAYA, buf, sizeof(buf));
	if (r)
		return r;

	return 0;
}

static int himalaya_set_update_window_sync(u16 x, u16 y, u16 w, u16 h)
{
	int r;

	r = himalaya_set_update_window(x, y, w, h);
	if (r)
		return r;

	return dsi_vc_send_bta_sync(VC_HIMALAYA);
}

static int himalaya_enable(struct omap_dss_device *dssdev)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	u8 id1, id2, id3;
	int r;

	dev_dbg(&dssdev->dev, "himalaya enable\n");

	r = dsi_vc_dcs_write_0(VC_HIMALAYA, DCS_SLEEP_OUT);
	if (r)
		goto err0;

	msleep(5);

	r = dcs_get_id(VC_HIMALAYA, &id1, &id2, &id3);
	if (r != 0)
		goto err1;

	if (!td->panel_intro_printed) {
		dev_info(&dssdev->dev, "himalaya ID %x.%x.%x\n",
				id1, id2, id3);
		td->panel_intro_printed = true;
	}

	r = dsi_vc_dcs_write_1(VC_HIMALAYA, DCS_BRIGHTNESS, 0xff);
	if (r)
		goto err1;
	r = dsi_vc_dcs_write_1(VC_HIMALAYA, DCS_CTRL_DISPLAY,
			(1<<2) | (1<<5)); /* BL | BCTRL */
	if (r)
		goto err1;
	r = dsi_vc_dcs_write_1(VC_HIMALAYA, DCS_PIXEL_FORMAT,
			0x7);	/* 24bit/pixel */
	if (r)
		goto err1;

	r = dsi_vc_dcs_write_1(VC_HIMALAYA, DCS_WRITE_CABC, td->cabc_mode);
	if (r)
		goto err1;

	r = himalaya_set_addr_mode(td->rotate, td->mirror);
	if (r)
		goto err1;

	r = dsi_vc_dcs_write_0(VC_HIMALAYA, DCS_DISPLAY_ON);
	if (r)
		goto err1;

	dev_dbg(&dssdev->dev, "himalaya enable done\n");

	return 0;
err1:
	dsi_vc_dcs_write_0(VC_HIMALAYA, DCS_SLEEP_IN);
err0:
	dev_err(&dssdev->dev, "himalaya enable failed\n");
	return r;
}

static int himalaya_disable(struct omap_dss_device *dssdev)
{
	int r;

	r = dsi_vc_dcs_write_0(VC_HIMALAYA, DCS_DISPLAY_OFF);

	if (!r) {
		r = dsi_vc_dcs_write_0(VC_HIMALAYA, DCS_SLEEP_IN);
		/* wait a bit so that the message goes through */
		msleep(10);
	}

	if (r) {
		dev_err(&dssdev->dev,
				"error disabling panel, issuing HW reset\n");
		himalaya_hw_reset(dssdev);
	}

	return 0;
}

static int gf_power_on(struct omap_dss_device *dssdev)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	u8 id1, id2, id3;
	int r;

	dev_dbg(&dssdev->dev, "power on\n");

	clk_enable(td->sysclk);

	gf_deep_sleep_disable(dssdev);

	r = omapdss_dsi_display_enable(dssdev);
	if (r)
		goto err1;

	gf_hw_reset(dssdev);
	himalaya_hw_reset(dssdev);

	r = gf_set_refclk(0x01); /* refclock 38.4MHz */
	if (r)
		goto err2;

	r = dcs_get_id(VC_GF, &id1, &id2, &id3);
	if (r)
		goto err2;

	if (!td->intro_printed) {
		dev_info(&dssdev->dev, "TC358710XBG revision %02x.%02x.%02x\n",
				id1, id2, id3);
		td->intro_printed = true;
	}

	r = gf_sleep_out(td);
	if (r)
		goto err2;

	/* NOTE: GF has a bug, and the following values should never be used
	 * for max packet size: 0x1, 0x11, 0x83, 0xda, 0xdb, 0xdc */
	r = dsi_vc_set_max_rx_packet_size(VC_GF, 16);
	if (r)
		goto err3;

	r = gf_check_status(dssdev);
	if (r)
		goto err3;

	r = gf_set_host_datarate(256*2);
	if (r)
		goto err3;


	/* DSI-A LP = 38.4MHz / 2 / (1+1) = 9.6MHz
	 * DSI-B LP = 256 * 2 / 8 / 2 / (1+4) = 6.4MHz
	 */
	r = gf_set_lp_timings(1, 4, 0);
	if (r)
		goto err3;

	r = gf_set_pri_datarate(256*2);
	if (r)
		goto err3;

	/* PRIBR for all ports */
	r = gf_set_if_clk_src(0, 0, 0, 0);
	if (r)
		goto err3;

	/* DSI-A: 2 lanes, DSI-B: 2 lanes */
	r = gf_set_lane_conf(2, 2, 0);
	if (r)
		goto err3;

	/* DSI-A: follow host, DSI-B: follow host */
	r = gf_set_transfer_mode(DSI_TX_MODE_FOLLOW_HOST,
			DSI_TX_MODE_FOLLOW_HOST);
	if (r)
		goto err3;

	/* VC0 mapped to DSI-A as VC0
	 * VC1 mapped to DSI-B as VC0 */
	r = gf_set_vcmap(0, 0, 0, 1, 0, 0);
	if (r)
		goto err3;

	r = gf_set_lane_idle_state(1, 1, 1,
			1, 1, 1);
	if (r)
		goto err3;


	r = gf_configure_hs_tx_cap();
	if (r)
		goto err3;

	r = dcs_get_id(VC_GF, &id1, &id2, &id3);
	if (r)
		goto err3;

	r = gf_check_status(dssdev);
	if (r)
		goto err3;

	r = himalaya_enable(dssdev);
	if (r)
		goto err4;

	r = _gf_enable_te(dssdev, td->te_enabled);
	if (r)
		goto err5;

	omapdss_dsi_vc_enable_hs(0, true);
	omapdss_dsi_vc_enable_hs(1, true);
	omapdss_dsi_vc_enable_hs(2, true);
	omapdss_dsi_vc_enable_hs(3, true);

	r = gf_check_status(dssdev);
	if (r)
		goto err5;

	td->enabled = 1;

	dev_dbg(&dssdev->dev, "power on done\n");

	return 0;

err5:
	himalaya_disable(dssdev);
err4:
	/* XXX buf sleep in */
err3:
	gf_sleep_in(td);
	/* wait a bit so that the message goes through DSI pipeline */
	msleep(10);
err2:
	omapdss_dsi_display_disable(dssdev, true, false);
err1:
	gf_deep_sleep_enable(dssdev);

	clk_disable(td->sysclk);

	dev_dbg(&dssdev->dev, "setup failed\n");

	return r;

}

static void gf_power_off(struct omap_dss_device *dssdev)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "power off\n");

	r = himalaya_disable(dssdev);

	if (!r)
		r = gf_sleep_in(td);

	/* wait a bit so that the message goes through DSI pipeline */
	if (!r)
		msleep(10);

	omapdss_dsi_display_disable(dssdev, true, false);

	gf_deep_sleep_enable(dssdev);

	if (r) {
		dev_err(&dssdev->dev,
				"error during power off, issuing reset\n");
		gf_hw_reset(dssdev);
		himalaya_hw_reset(dssdev);
	}

	clk_disable(td->sysclk);

	td->enabled = 0;

	dev_dbg(&dssdev->dev, "power off done\n");

	return;
}

static int gf_enable(struct omap_dss_device *dssdev)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "enable\n");

	down(&td->lock);
	dsi_bus_lock();

	if (dssdev->state != OMAP_DSS_DISPLAY_DISABLED) {
		r = -EINVAL;
		goto err;
	}

	r = gf_power_on(dssdev);
	if (r)
		goto err;

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	dsi_bus_unlock();
	up(&td->lock);

	return 0;
err:
	dsi_bus_unlock();
	up(&td->lock);
	return r;
}

static void gf_disable(struct omap_dss_device *dssdev)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);

	dev_dbg(&dssdev->dev, "disable\n");

	down(&td->lock);
	dsi_bus_lock();

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE)
		gf_power_off(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;

	dsi_bus_unlock();
	up(&td->lock);
}

static int gf_suspend(struct omap_dss_device *dssdev)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);

	dev_dbg(&dssdev->dev, "suspend\n");

	down(&td->lock);
	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE) {
		up(&td->lock);
		return -EINVAL;
	}

	dsi_bus_lock();
	gf_power_off(dssdev);
	dsi_bus_unlock();

	dssdev->state = OMAP_DSS_DISPLAY_SUSPENDED;

	up(&td->lock);

	return 0;
}

static int gf_resume(struct omap_dss_device *dssdev)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "resume\n");

	down(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_SUSPENDED) {
		up(&td->lock);
		return -EINVAL;
	}

	dsi_bus_lock();
	r = gf_power_on(dssdev);
	if (r)
		dssdev->state = OMAP_DSS_DISPLAY_DISABLED;
	else
		dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	dsi_bus_unlock();
	up(&td->lock);

	return r;
}

#ifdef MEASURE_PERF
static void gf_print_perf(struct omap_dss_device *dssdev)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	u32 p01, p12, p23, p34;
	u32 p03, p04;
	ktime_t p0 = td->perf.phase0;
	ktime_t p1 = td->perf.phase1;
	ktime_t p2 = td->perf.phase2;
	ktime_t p3 = td->perf.phase3;
	ktime_t p4 = td->perf.phase4;
#ifdef MEASURE_PERF_SHOW_ALWAYS
	const bool show = true;
#else
	const bool show = false;
#endif

	switch (td->update_mode) {
	case GF_UPDATE_NORMAL:
		switch (td->update_te) {
		case GF_UPDATE_NO_TE:
			p04 = (u32)ktime_to_us(ktime_sub(p4, p0));

			if (show || p04 > 15000)
				dev_warn(&dssdev->dev, "p04(%u)\n", p04);
			break;

		case GF_UPDATE_DSI_TE:
			p04 = (u32)ktime_to_us(ktime_sub(p4, p0));

			if (show || p04 > 17000 * 2)
				dev_warn(&dssdev->dev, "p04(%u)\n", p04);
			break;

		case GF_UPDATE_EXT_TE:
			p03 = (u32)ktime_to_us(ktime_sub(p3, p0));
			p34 = (u32)ktime_to_us(ktime_sub(p4, p3));

			if (show || p03 > 20000 || p34 > 15000)
				dev_warn(&dssdev->dev, "p01(%u) p14(%u)\n",
						p03, p34);
			break;

		default:
			BUG();
		}
		break;

	case GF_UPDATE_RL:
	case GF_UPDATE_LR:
	case GF_UPDATE_BT:
	case GF_UPDATE_TB:
		p01 = (u32)ktime_to_us(ktime_sub(p1, p0));
		p12 = (u32)ktime_to_us(ktime_sub(p2, p1));
		p23 = (u32)ktime_to_us(ktime_sub(p3, p2));
		p34 = (u32)ktime_to_us(ktime_sub(p4, p3));

		if (show || p01 > 20000 || p12 > 7000 || p23 > 5000 ||
				p34 > 7000)
			dev_warn(&dssdev->dev,
					"p01(%u) p12(%u) p23(%u) p34(%u)\n",
					p01, p12, p23, p34);
		break;

	default:
		BUG();
	}


}
#endif

static void gf_update_error(struct omap_dss_device *dssdev)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int old;

	old = atomic_xchg(&td->upd_state, GF_UPD_NONE);
	if (old != GF_UPD_NONE) {
		dev_err(&dssdev->dev, "update error, cleaning up\n");
		cancel_delayed_work(&td->frame_timeout_work);
		omap_dss_unlock_cache();

		dev_err(&dssdev->dev, "performing LCD reset\n");
		gf_power_off(dssdev);
		gf_hw_reset(dssdev);
		gf_power_on(dssdev);

		dsi_bus_unlock();
		up(&td->lock);
	} else {
		dev_warn(&dssdev->dev, "update error, already handled\n");
	}
}

static int gf_prepare_update(struct omap_dss_device *dssdev)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	u16 x = td->update_region.x;
	u16 y = td->update_region.y;
	u16 w = td->update_region.w;
	u16 h = td->update_region.h;
	int r;
	bool enlarge_ovls;

	enlarge_ovls = td->update_mode == GF_UPDATE_NORMAL;

	r = omap_dsi_prepare_update(dssdev, &x, &y, &w, &h, enlarge_ovls);
	if (r)
		return r;

	td->update_region.x = x;
	td->update_region.y = y;
	td->update_region.w = w;
	td->update_region.h = h;

	if (td->update_te == GF_UPDATE_DSI_TE)
		r = himalaya_set_update_window_sync(x, y, w, h);
	else
		r = himalaya_set_update_window(x, y, w, h);

	return r;
}

static int gf_start_update(struct omap_dss_device *dssdev,
		void (*callback)(int, void *))
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	u16 x = td->update_region.x;
	u16 y = td->update_region.y;
	u16 w = td->update_region.w;
	u16 h = td->update_region.h;
	int r;

	r = omap_dsi_update(dssdev, VC_HIMALAYA, x, y, w, h,
			callback, dssdev);
	return r;
}

static void gf_framedone_cb_2(int err, void *data)
{
	struct omap_dss_device *dssdev = data;
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int old;

#ifdef MEASURE_PERF
	td->perf.phase4 = ktime_get();
#endif

	if (err) {
		dev_err(&dssdev->dev, "framedone2 err %d\n", err);
		return; /* Let timeout handle this */
	}

	old = atomic_xchg(&td->upd_state, GF_UPD_NONE);
	if (old == GF_UPD_NONE) {
		dev_warn(&dssdev->dev, "timeout before framedone2\n");
		return; /* Timeout already handled this */
	}

	BUG_ON(old != GF_UPD_FRAME2_ONGOING);

	cancel_delayed_work(&td->frame_timeout_work);
	omap_dss_unlock_cache();

#ifdef MEASURE_PERF
	gf_print_perf(dssdev);
#endif

	dsi_bus_unlock();
	up(&td->lock);
}

static void gf_framedone_cb_1(int err, void *data)
{
	struct omap_dss_device *dssdev = data;
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int r;
	int old;

#ifdef MEASURE_PERF
	ktime_t kt1, kt2;
	kt1 = ktime_get();
	td->perf.phase2 = ktime_get();
#endif

	if (err) {
		dev_err(&dssdev->dev, "framedone1 err %d\n", err);
		return; /* Let timeout handle this */
	}

	switch (td->update_mode) {
	case GF_UPDATE_RL:
		td->update_region.x = 0;
		break;
	case GF_UPDATE_LR:
		td->update_region.x = td->update_region.w;
		break;
	case GF_UPDATE_BT:
		td->update_region.y = 0;
		break;
	case GF_UPDATE_TB:
		td->update_region.y = td->update_region.h;
		break;
	default:
		break;
	};

	r = gf_prepare_update(dssdev);
	if (r) {
		dev_err(&dssdev->dev,
				"prepare_update failed for second frame\n");
		goto err;
	}

	old = atomic_cmpxchg(&td->upd_state, GF_UPD_FRAME1_ONGOING,
			GF_UPD_FRAME2_WAIT_START);
	if (old == GF_UPD_NONE) {
		dev_warn(&dssdev->dev, "timeout before framedone1\n");
		return; /* Timeout already handled this */
	}

	BUG_ON(old != GF_UPD_FRAME1_ONGOING);

#ifdef MEASURE_PERF
	kt2 = ktime_get();
	kt1 = ktime_sub(kt2, kt1);
	if ((u32)ktime_to_us(kt1) > 500)
		dev_warn(&dssdev->dev, "fd_cb_1 %u\n", (u32)ktime_to_us(kt1));
#endif

	return;
err:
	gf_update_error(dssdev);
}

static enum hrtimer_restart gf_timer_handler(struct hrtimer *handle)
{
	struct gf_data *td = container_of(handle,
			struct gf_data, hrtimer);
	struct omap_dss_device *dssdev = td->dssdev;
	int old;
	int r;
#ifdef MEASURE_PERF
	ktime_t kt1, kt2;
	kt1 = ktime_get();
#endif

	switch (td->update_mode) {
	case GF_UPDATE_RL:
	case GF_UPDATE_LR:
	case GF_UPDATE_BT:
	case GF_UPDATE_TB:
		old = atomic_cmpxchg(&td->upd_state, GF_UPD_FRAME1_WAIT_START,
				GF_UPD_FRAME1_ONGOING);

		if (old == GF_UPD_FRAME1_WAIT_START) {
#ifdef MEASURE_PERF
			td->perf.phase1 = ktime_get();
#endif
			r = gf_start_update(dssdev, gf_framedone_cb_1);
			if (r)
				goto err;
		}
		break;

	default:
		break;
	}

#ifdef MEASURE_PERF
	kt2 = ktime_get();
	kt1 = ktime_sub(kt2, kt1);
	if ((u32)ktime_to_us(kt1) > 500)
		dev_warn(&dssdev->dev, "timer %u\n", (u32)ktime_to_us(kt1));
#endif

	return HRTIMER_NORESTART;
err:
	/* Let timeout handle this */
	dev_err(&dssdev->dev, "timer_handler: start_update failed\n");

	return HRTIMER_NORESTART;
}


static irqreturn_t gf_te_isr(int irq, void *data)
{
	struct omap_dss_device *dssdev = data;
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int old;
	int r;
#ifdef MEASURE_PERF
	ktime_t kt1, kt2;
	kt1 = ktime_get();
#endif

	if (td->calibrating) {
		if (td->num_calib_values < ARRAY_SIZE(td->calib_values)) {
			td->calib_values[td->num_calib_values++] = ktime_get();

			if (td->num_calib_values ==
					ARRAY_SIZE(td->calib_values)) {
				schedule_work(&td->calibrate_work);
			}
		}

		return IRQ_HANDLED;
	}

	switch (td->update_mode) {
	case GF_UPDATE_RL:
	case GF_UPDATE_LR:
	case GF_UPDATE_BT:
	case GF_UPDATE_TB:
		hrtimer_start(&td->hrtimer, ktime_set(0, td->timer_us * 1000),
				HRTIMER_MODE_REL);
		/* Fallthrough */

	case GF_UPDATE_NORMAL:
		old = atomic_cmpxchg(&td->upd_state, GF_UPD_FRAME2_WAIT_START,
				GF_UPD_FRAME2_ONGOING);

		if (old == GF_UPD_FRAME2_WAIT_START) {
#ifdef MEASURE_PERF
			td->perf.phase3 = ktime_get();
#endif
			r = gf_start_update(dssdev, gf_framedone_cb_2);
			if (r)
				goto err;
		}
		break;

		old = atomic_cmpxchg(&td->upd_state, GF_UPD_FRAME2_WAIT_START,
				GF_UPD_FRAME2_ONGOING);

		if (old == GF_UPD_FRAME2_WAIT_START) {
#ifdef MEASURE_PERF
			td->perf.phase3 = ktime_get();
#endif
			r = gf_start_update(dssdev,
					gf_framedone_cb_2);
			if (r)
				goto err;
		}
		break;
	default:
		break;
	}

#ifdef MEASURE_PERF
	kt2 = ktime_get();
	kt1 = ktime_sub(kt2, kt1);
	if ((u32)ktime_to_us(kt1) > 500)
		dev_warn(&dssdev->dev, "te %u\n", (u32)ktime_to_us(kt1));
#endif

	return IRQ_HANDLED;
err:
	/* Let timeout handle this */
	dev_err(&dssdev->dev, "te_isr: start_update failed\n");

	return IRQ_HANDLED;
}

static void gf_frame_timeout_work_callback(struct work_struct *work)
{
	struct gf_data *td = container_of(work, struct gf_data,
			frame_timeout_work.work);
	struct omap_dss_device *dssdev = td->dssdev;

	dev_err(&dssdev->dev, "Frame not sent for 500ms!\n");
	gf_update_error(dssdev);
}

static int gf_update(struct omap_dss_device *dssdev,
		u16 x, u16 y, u16 w, u16 h)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int r;
	int old;

	dev_dbg(&dssdev->dev, "update %d, %d, %d x %d\n", x, y, w, h);

	down(&td->lock);
	dsi_bus_lock();

	if (!td->enabled) {
		r = 0;
		goto err1;
	}

	if (!td->te_enabled || td->calibrating) {
		td->update_te = GF_UPDATE_NO_TE;
		td->update_mode = GF_UPDATE_NORMAL;
	} else {
		int rot;

		td->update_te = GF_UPDATE_EXT_TE;

		rot = (td->rotate + 3) % 4;

		switch (rot) {
		case 0:
			td->update_mode = GF_UPDATE_NORMAL;
			break;
		case 1:
			td->update_mode = GF_UPDATE_LR;
			break;
		case 2:
			td->update_mode = GF_UPDATE_BT;
			break;
		case 3:
			td->update_mode = GF_UPDATE_RL;
			break;
		}
	}

	switch (td->update_mode) {
	case GF_UPDATE_RL:
		gf_get_resolution(dssdev, &w, &h);
		x = w / 2;
		y = 0;
		w = w / 2;
		break;
	case GF_UPDATE_LR:
		gf_get_resolution(dssdev, &w, &h);
		x = 0;
		y = 0;
		w = w / 2;
		break;
	case GF_UPDATE_BT:
		gf_get_resolution(dssdev, &w, &h);
		x = 0;
		y = h / 2;
		h = h / 2;
		break;
	case GF_UPDATE_TB:
		gf_get_resolution(dssdev, &w, &h);
		x = 0;
		y = 0;
		h = h / 2;
		break;
	default:
		break;
	}

	td->update_region.x = x;
	td->update_region.y = y;
	td->update_region.w = w;
	td->update_region.h = h;
	barrier();

	omap_dss_lock_cache();

	r = gf_prepare_update(dssdev);
	if (r)
		goto err2;

	/* Before changing the upd_state state, take care of the extremely
	 * unlikely case that 1) timeout occurs, and the timeout handler is
	 * scheduled (but not run), 2) framedone_cb_2() is called on a
	 * successful, but rather delayed framedone, and sets upd_state to
	 * GF_UPD_NONE, 3) this function is called, and sets upd_state !=
	 * GF_UPD_NONE below, and finally 4) timeout handler is run, messing
	 * up the update. */
	cancel_delayed_work_sync(&td->frame_timeout_work);

	/* Also take care of DSI timeout occurring after frame_timeout_work. */
	omap_dsi_cancel_update_sync(dssdev);

#ifdef MEASURE_PERF
	td->perf.phase0 = ktime_get();
#endif


	if (td->update_te == GF_UPDATE_NO_TE ||
			td->update_te == GF_UPDATE_DSI_TE) {
		old = atomic_xchg(&td->upd_state, GF_UPD_FRAME2_ONGOING);
		BUG_ON(old != GF_UPD_NONE);

		r = gf_start_update(dssdev, gf_framedone_cb_2);
		if (r)
			goto err2;
	} else if (td->update_mode == GF_UPDATE_NORMAL) {
		old = atomic_xchg(&td->upd_state, GF_UPD_FRAME2_WAIT_START);
		BUG_ON(old != GF_UPD_NONE);
	} else {
		old = atomic_xchg(&td->upd_state, GF_UPD_FRAME1_WAIT_START);
		BUG_ON(old != GF_UPD_NONE);
	}

	schedule_delayed_work(&td->frame_timeout_work, msecs_to_jiffies(500));

	/* Note: no dsi_bus_unlock or up here. The unlocking is done in
	 * gf_framedone_cb_2() when the frame has been sent, or in
	 * gf_update_error() if errors have occurred. */

	return 0;
err2:
	omap_dss_unlock_cache();
err1:
	dsi_bus_unlock();
	up(&td->lock);
	return r;
}

static void gf_calibrate_work_callback(struct work_struct *work)
{
	struct gf_data *td = container_of(work, struct gf_data,
			calibrate_work);
	struct omap_dss_device *dssdev = td->dssdev;
	int i;
	int frame_num;
	u32 frame_us;
	u32 vsync_us;
	ktime_t t;

#ifdef MEASURE_PERF_SHOW_ALWAYS
	for (i = 0; i < td->num_calib_values; ++i) {
		if (i > 0) {
			t = ktime_sub(td->calib_values[i],
					td->calib_values[i - 1]);
		} else {
			t = ktime_set(0, 0);
		}

		dev_info(&dssdev->dev, "%u\t%8u\n",
				(u32)ktime_to_us(td->calib_values[i]),
				(u32)ktime_to_us(t));
	}
#endif

	frame_num = 0;
	frame_us = 0;
	/* XXX: skip first few values. They seem to be too high. Perhaps
	 * omapdss initialization has some irq blocking code */
	for (i = 6; i < td->num_calib_values; ++i) {
		t = ktime_sub(td->calib_values[i],
				td->calib_values[i - 1]);

		frame_us += (u32)ktime_to_us(t);
		frame_num++;
	}

	frame_us /= frame_num;

	WARN_ON(frame_us < 14000 || frame_us > 19000);

	/* Himalaya vsync seems to be about 64 lines */
	vsync_us = frame_us * 64 / (864 + 64);
	td->timer_us =  vsync_us + (frame_us - vsync_us) / 2;

#ifdef MEASURE_PERF
	dev_info(&dssdev->dev, "vsync %u us, frame %u us, timer %u us\n",
			vsync_us, frame_us, td->timer_us);
#endif

	td->calibrating = false;
}


static int gf_sync(struct omap_dss_device *dssdev)
{
	dev_dbg(&dssdev->dev, "sync\n");

	dsi_bus_lock();
	dsi_bus_unlock();

	dev_dbg(&dssdev->dev, "sync done\n");

	return 0;
}

static int _gf_enable_te(struct omap_dss_device *dssdev, bool enable)
{
	int r;

	dev_dbg(&dssdev->dev, "enable te %d\n", enable);

	if (enable) {
		r = dsi_vc_dcs_write_1(VC_HIMALAYA, DCS_TEAR_ON, 0);
		if (r)
			return r;

		r = dsi_vc_dcs_write_1(VC_GF, GF_TESRC, (1 << 7) | (0));
		if (r)
			return r;

		r = dsi_vc_dcs_write_1(VC_GF, DCS_TEAR_ON, 0);
		if (r)
			return r;
	} else {
		r = dsi_vc_dcs_write_0(VC_HIMALAYA, DCS_TEAR_OFF);
		if (r)
			return r;

		r = dsi_vc_dcs_write_0(VC_GF, DCS_TEAR_OFF);
		if (r)
			return r;
	}

	return 0;
}

static int gf_enable_te(struct omap_dss_device *dssdev, bool enable)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	down(&td->lock);

	if (td->te_enabled == enable) {
		up(&td->lock);
		return 0;
	}

	if (!td->enabled) {
		td->te_enabled = enable;
		up(&td->lock);
		return 0;
	}

	dsi_bus_lock();
	r = _gf_enable_te(dssdev, enable);
	td->te_enabled = enable;
	dsi_bus_unlock();

	up(&td->lock);

	return r;
}

static int gf_get_te(struct omap_dss_device *dssdev)
{
	int r;
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	down(&td->lock);
	r = td->te_enabled;
	up(&td->lock);
	return r;
}

static int gf_rotate(struct omap_dss_device *dssdev, u8 rotate)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "rotate %d\n", rotate);

	down(&td->lock);

	if (td->rotate == rotate)
		goto end;

	dsi_bus_lock();

	if (td->enabled) {
		r = himalaya_set_addr_mode(rotate, td->mirror);
		if (r)
			goto err;
	}

	td->rotate = rotate;

	dsi_bus_unlock();
end:
	up(&td->lock);
	return 0;
err:
	dsi_bus_unlock();
	up(&td->lock);
	return r;
}

static u8 gf_get_rotate(struct omap_dss_device *dssdev)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	down(&td->lock);
	r = td->rotate;
	up(&td->lock);

	return r;
}

static int gf_mirror(struct omap_dss_device *dssdev, bool enable)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "mirror %d\n", enable);

	down(&td->lock);
	if (td->mirror == enable)
		goto end;
	dsi_bus_lock();
	if (td->enabled) {
		r = himalaya_set_addr_mode(td->rotate, enable);
		if (r)
			goto err;
	}

	td->mirror = enable;

	dsi_bus_unlock();
end:
	up(&td->lock);
	return 0;
err:
	dsi_bus_unlock();
	up(&td->lock);
	return r;
}

static bool gf_get_mirror(struct omap_dss_device *dssdev)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	down(&td->lock);
	r = td->mirror;
	up(&td->lock);

	return r;
}

static int gf_set_update_mode(struct omap_dss_device *dssdev,
		enum omap_dss_update_mode mode)
{
	if (mode != OMAP_DSS_UPDATE_MANUAL)
		return -EINVAL;

	return 0;
}

static enum omap_dss_update_mode gf_get_update_mode(
		struct omap_dss_device *dssdev)
{
	return OMAP_DSS_UPDATE_MANUAL;
}

static int gf_run_test(struct omap_dss_device *dssdev, int test_num)
{
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	u8 id1, id2, id3;
	int r;

	down(&td->lock);

	if (!td->enabled) {
		up(&td->lock);
		return -ENODEV;
	}

	dsi_bus_lock();

	r = dcs_get_id(VC_GF, &id1, &id2, &id3);

	dsi_bus_unlock();

	up(&td->lock);

	return r;
}

static int gf_memory_read(struct omap_dss_device *dssdev,
		void *buf, size_t buf_size,
		u16 x, u16 y, u16 w, u16 h)
{
	int r;
	int first = 1;
	int plen;
	unsigned buf_used = 0;
	struct gf_data *td = dev_get_drvdata(&dssdev->dev);
	size_t size;
	int vc;

	dev_err(&dssdev->dev, "memory read %d,%d %dx%d\n", x, y, w, h);

	down(&td->lock);

	if (!td->enabled) {
		r = -ENODEV;
		goto err0;
	}

	size = min(w * h * 3,
			dssdev->panel.timings.x_res *
			dssdev->panel.timings.y_res * 3);

	if (buf_size < size) {
		r = -ENOMEM;
		goto err0;
	}

	dsi_bus_lock();

	vc = VC_HIMALAYA;

	r = himalaya_set_update_window_sync(x, y, w, h);
	if (r)
		goto err1;

	if (size % 2)
		plen = 1;
	else
		plen = 2;

	r = dsi_vc_set_max_rx_packet_size(vc, plen);
	if (r)
		goto err1;

	while (buf_used < size) {
		u8 dcs_cmd = first ? 0x2e : 0x3e;
		first = 0;

		r = dsi_vc_dcs_read(vc, dcs_cmd,
				buf + buf_used, size - buf_used);


		if (r < 0) {
			dev_err(&dssdev->dev, "read error\n");
			goto err2;
		}

		buf_used += r;

		if (r < plen) {
			dev_err(&dssdev->dev, "short read %d < %d\n", r, plen);
			break;
		}

		if (signal_pending(current)) {
			dev_err(&dssdev->dev, "signal pending, "
					"aborting memory read\n");
			r = -ERESTARTSYS;
			goto err2;
		}
	}

	r = buf_used;

	dsi_vc_set_max_rx_packet_size(vc, 1);

	dsi_bus_unlock();
	up(&td->lock);

	dev_dbg(&dssdev->dev, "memory read done\n");

	return r;
err2:
	dsi_vc_set_max_rx_packet_size(vc, 1);
err1:
	dsi_bus_unlock();
err0:
	up(&td->lock);
	dev_err(&dssdev->dev, "memory read failed\n");
	return r;
}

static struct omap_dss_driver gf_driver = {
	.probe		= gf_probe,
	.remove		= gf_remove,

	.enable		= gf_enable,
	.disable	= gf_disable,
	.suspend	= gf_suspend,
	.resume		= gf_resume,

	.set_update_mode = gf_set_update_mode,
	.get_update_mode = gf_get_update_mode,

	.update		= gf_update,
	.sync		= gf_sync,

	.get_resolution	= gf_get_resolution,
	.get_dimensions = gf_get_dimensions,
	.get_recommended_bpp = omapdss_default_get_recommended_bpp,

	.enable_te	= gf_enable_te,
	.get_te		= gf_get_te,

	.get_timings	= gf_get_timings,

	.set_rotate	= gf_rotate,
	.get_rotate	= gf_get_rotate,
	.set_mirror	= gf_mirror,
	.get_mirror	= gf_get_mirror,

	.run_test	= gf_run_test,
	.memory_read	= gf_memory_read,

	.driver         = {
		.name   = "gf",
		.owner  = THIS_MODULE,
	},
};

static int __init gf_init(void)
{
	omap_dss_register_driver(&gf_driver);

	return 0;
}

static void __exit gf_exit(void)
{
	omap_dss_unregister_driver(&gf_driver);
}

module_init(gf_init);
module_exit(gf_exit);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("GF Driver");
MODULE_LICENSE("GPL");
