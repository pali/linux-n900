/*
 * Nokia DSI command mode panel driver
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
#include <linux/workqueue.h>
#include <linux/regulator/consumer.h>
#include <linux/semaphore.h>
#include <linux/hrtimer.h>

#include <plat/display.h>
#include <plat/panel-nokia-dsi.h>

/* DSI Virtual channel. Hardcoded for now. */
#define TCH 0

#define DCS_NOP			0x00
#define DCS_READ_NUM_ERRORS	0x05
#define DCS_READ_POWER_MODE	0x0a
#define DCS_READ_MADCTL		0x0b
#define DCS_READ_PIXEL_FORMAT	0x0c
#define DCS_RDDSDR		0x0f
#define DCS_SLEEP_IN		0x10
#define DCS_SLEEP_OUT		0x11
#define DCS_PARTIAL_MODE_ON	0x12
#define DCS_NORMAL_MODE_ON	0x13
#define DCS_DISPLAY_OFF		0x28
#define DCS_DISPLAY_ON		0x29
#define DCS_COLUMN_ADDR		0x2a
#define DCS_PAGE_ADDR		0x2b
#define DCS_MEMORY_WRITE	0x2c
#define DCS_PARTIAL_AREA	0x30
#define DCS_TEAR_OFF		0x34
#define DCS_TEAR_ON		0x35
#define DCS_MEM_ACC_CTRL	0x36
#define DCS_IDLE_MODE_OFF	0x38
#define DCS_IDLE_MODE_ON	0x39
#define DCS_PIXEL_FORMAT	0x3a
#define DCS_WRITE_TEAR_SCANLINE	0x44
#define DCS_BRIGHTNESS		0x51
#define DCS_READ_BRIGHTNESS	0x52
#define DCS_WRITE_CTRL_DISPLAY	0x53
#define DCS_READ_CTRL_DISPLAY	0x54
#define DCS_WRITE_CABC		0x55
#define DCS_READ_CABC		0x56
#define DCS_GET_ID1		0xda
#define DCS_GET_ID2		0xdb
#define DCS_GET_ID3		0xdc

#define MEASURE_PERF
/*#define MEASURE_PERF_SHOW_ALWAYS*/

enum pnd_ctrl_display_bits {
	PND_CTRLD_BL            = 1 << 2,
	PND_CTRLD_DD            = 1 << 3,
	PND_CTRLD_BCTRL         = 1 << 5,
	PND_CTRLD_HBM_SHIFT	= 6,
	PND_CTRLD_HBM           = 3 << PND_CTRLD_HBM_SHIFT,
};

/* High Brightness Mode luminance levels in cd/m^2 for DCS_WRITE_CTRL_DISPLAY */
enum pnd_hbm {
	PND_HBM_OFF		= 0,
	PND_HBM_400		= 1,
	PND_HBM_450		= 2, /* equals PND_HBM_500 on SMD Pyrenees */
	PND_HBM_500		= 3,
	PND_HBM_MAX		= PND_HBM_500,
};

enum pnd_acl_mode {
	PND_ACL_OFF	= 0x00,
	PND_ACL_ON_80P	= 0x10,
	PND_ACL_ON_70P	= 0x11,
	PND_ACL_ON_60P	= 0x12,
};

/* Bits of DCS_READ_MADCTL and DCS_MEM_ACC_CTRL commands */
enum pnd_madctl_bits {
	PND_PAGE_ADDRESS_ORDER_BT	= 1 << 7,
	PND_COL_ADDRESS_ORDER_RL	= 1 << 6,
	PND_COL_ORDER_REVERSE		= 1 << 5,
	PND_REFRESH_BOTTOM_TOP		= 1 << 4,
	PND_BGR_ORDER			= 1 << 3,
	PND_REFRESH_RIGHT_LEFT		= 1 << 2,
	PND_SWITCH_BTW_SEGMENT_RAM	= 1 << 1,
	PND_SWITCH_BTW_COMMON_RAM	= 1 << 0,
};

/* Bits of DCS_READ_POWER_MODE command */
enum pnd_power_mode_bits {
	PND_BOOSTER_VOLTAGE	= 1 << 7,
	PND_IDLE_MODE		= 1 << 6,
	PND_PARTIAL_MODE	= 1 << 5,
	PND_SLEEP_OUT		= 1 << 4,
	PND_NORMAL_MODE		= 1 << 3,
	PND_DISPLAY_ON		= 1 << 2,
};

static irqreturn_t pnd_te_isr(int irq, void *data);
static int _pnd_enable_te(struct omap_dss_device *dssdev, bool enable);

static void pnd_frame_timeout_work_callback(struct work_struct *work);
static enum hrtimer_restart pnd_timer_handler(struct hrtimer *handle);
static void pnd_set_timer(struct omap_dss_device *dssdev);

static void pnd_ulps_work(struct work_struct *work);

static int pnd_write_display_current_settings(struct omap_dss_device *dssdev);
static int pnd_power_on(struct omap_dss_device *dssdev);
static void pnd_power_off(struct omap_dss_device *dssdev);
static void pnd_hw_reset(struct omap_dss_device *dssdev);
static int pnd_panel_reset(struct omap_dss_device *dssdev);

enum pnd_update_te {
	PND_UPD_TE_NONE,
	PND_UPD_TE_EXT,
	PND_UPD_TE_DSI,
};

enum pnd_update_mode {
	PND_UPD_MODE_NORMAL,	/* update in one part */
	PND_UPD_MODE_RL,	/* right first, then left */
	PND_UPD_MODE_LR,	/* left first, then right */
	PND_UPD_MODE_BT,	/* bottom first, then top */
	PND_UPD_MODE_TB,	/* top first, then bottom */
};

enum pnd_update_state {
	PND_UPD_STATE_NONE,
	PND_UPD_STATE_FRAME1_WAIT,
	PND_UPD_STATE_FRAME1_ONGOING,
	PND_UPD_STATE_FRAME2_WAIT,
	PND_UPD_STATE_FRAME2_ONGOING,
};

struct panel_regulator {
	struct regulator *regulator;
	const char *name;
	int min_uV;
	int max_uV;
};

static void free_regulators(struct panel_regulator *regulators, int n)
{
	int i;

	for (i = 0; i < n; i++) {
		/* disable/put in reverse order */
		regulator_disable(regulators[n - i - 1].regulator);
		regulator_put(regulators[n - i - 1].regulator);
	}
}

static int init_regulators(struct omap_dss_device *dssdev,
			struct panel_regulator *regulators, int n)
{
	int r, i, v;

	for (i = 0; i < n; i++) {
		struct regulator *reg;

		reg = regulator_get(&dssdev->dev, regulators[i].name);
		if (IS_ERR(reg)) {
			dev_err(&dssdev->dev, "failed to get regulator %s\n",
				regulators[i].name);
			r = PTR_ERR(reg);
			goto err;
		}

		/* FIXME: better handling of fixed vs. variable regulators */
		v = regulator_get_voltage(reg);
		if (v < regulators[i].min_uV || v > regulators[i].max_uV) {
			r = regulator_set_voltage(reg, regulators[i].min_uV,
						regulators[i].max_uV);
			if (r) {
				dev_err(&dssdev->dev,
					"failed to set regulator %s voltage\n",
					regulators[i].name);
				regulator_put(reg);
				goto err;
			}
		}

		r = regulator_enable(reg);
		if (r) {
			dev_err(&dssdev->dev, "failed to enable regulator %s\n",
				regulators[i].name);
			regulator_put(reg);
			goto err;
		}

		regulators[i].regulator = reg;
	}

	return 0;

err:
	free_regulators(regulators, i);

	return r;
}

static struct panel_regulator himalaya_regulators[] = {
	{
		.name = "VPNL",
		.min_uV = 2300000,
		.max_uV = 4800000,
	},
	{
		.name = "VDDI",
		.min_uV = 1650000,
		.max_uV = 1950000,
	},
};

/**
 * struct panel_config - panel configuration
 * @name: panel name
 * @type: panel type
 * @panel_id: panel ID (ID3)
 * @width: panel width in micrometers
 * @height: panel height in micrometers
 * @timings: panel resolution
 * @sleep: various panel specific delays, passed to msleep() if non-zero
 * @reset_sequence: reset sequence timings, passed to udelay() if non-zero
 * @regulators: array of panel regulators
 * @num_regulators: number of regulators in the array
 */
struct panel_config {
	const char *name;
	int type;
	unsigned char panel_id;

	u32 width;
	u32 height;
	struct omap_video_timings timings;

	struct {
		unsigned int sleep_in;
		unsigned int sleep_out;
		unsigned int hw_reset;
		unsigned int enable_te;
	} sleep;

	struct {
		unsigned int high;
		unsigned int low;
	} reset_sequence;

	struct panel_regulator *regulators;
	int num_regulators;
};

enum {
	PANEL_TAAL,
	PANEL_HIMALAYA,
	PANEL_PYRENEES,
};

static struct panel_config panel_configs[] = {
	{
		.name		= "taal",
		.type		= PANEL_TAAL,
		.panel_id	= 0x7d,
		.width		= 84240,
		.height		= 46800,
		.timings	= {
			.x_res		= 864,
			.y_res		= 480,
		},
		.sleep		= {
			.sleep_in	= 5,
			.sleep_out	= 5,
			.hw_reset	= 5,
			.enable_te	= 100, /* possible panel bug */
		},
		.reset_sequence	= {
			.high		= 10,
			.low		= 10,
		},
	},
	{
		.name		= "himalaya",
		.type		= PANEL_HIMALAYA,
		.panel_id	= 0x8a,
		.width		= 48960,
		.height		= 88128,
		.timings = {
			.x_res		= 480,
			.y_res		= 864,
			.vfp		= 58,
			.vbp		= 6,
		},
		.sleep = {
			.sleep_in	= 5,
			.sleep_out	= 5,
			.hw_reset	= 5,
		},
		.reset_sequence	= {
			.high		= 10,
			.low		= 10,
		},
		.regulators		= himalaya_regulators,
		.num_regulators		= ARRAY_SIZE(himalaya_regulators),
	},
	{
		.name		= "pyrenees",
		.type		= PANEL_PYRENEES,
		.panel_id	= 0x96,
		.width		= 48240,
		.height		= 86832,
		.timings = {
			.x_res		= 480,
			.y_res		= 864,
			.vsw		= 188,
		},
		.sleep = {
			.sleep_in	= 5,
			.sleep_out	= 5,
			.hw_reset	= 5,
		},
		.reset_sequence	= {
			.high		= 10,
			.low		= 10,
		},
		/* Reuse Himalaya regulator configuration */
		.regulators		= himalaya_regulators,
		.num_regulators		= ARRAY_SIZE(himalaya_regulators),
	},
};

struct update_region {
	u16 x;
	u16 y;
	u16 w;
	u16 h;
};

/**
 * struct pnd_data - panel runtime data
 * @lock: lock protecting this structure
 * @bldev: backlight device
 * @hw_guard_end: next value of jiffies when we can issue the next
 *                sleep in/out command
 * @hw_guard_wait: max guard time in jiffies
 * @dssdev: DSS device pointer
 * @id1: %DCS_GET_ID1 command response
 * @id2: %DCS_GET_ID2 command response
 * @id3: %DCS_GET_ID3 command response
 * @enabled: if true, the panel is enabled from driver point of view
 * @display_on: if true, the %DCS_DISPLAY_ON command has been issued
 * @rotate: rotation in terms of the panel for %DCS_MEM_ACC_CTRL command
 * @mirror: panel mirroring for %DCS_MEM_ACC_CTRL command
 * @te_enabled: tearing effect line, see %DCS_TEAR_OFF and %DCS_TEAR_ON commands
 * @wq: workqueue for ESD check, ULPS, two part update calibration, etc.
 * @err_wq: independent workqueue for error handling (to avoid deadlocks)
 * @esd_timeout: ESD check interval in ms, 0 for disabled
 * @esd_work: ESD check work
 * @ulps_enabled: if true, the panel is in Ultra Low Power State (ULPS)
 * @ulps_timeout: timeout for entering ULPS in ms, 0 for disabled
 * @ulps_work: ULPS enter work
 * @lpm_enabled: if true, the panel is in Low Power Mode (LPM)
 * @upd_state: two-part update state
 * @original_update_region: update region for original update call
 * @frame_timeout_work: timeout work for frame tranmission
 * @update_te: tear effect in update, see &enum pnd_update_te
 * @update_mode: update mode, see &enum pnd_update_mode
 * @use_dsi_bl: if true, use %DCS_BRIGHTNESS command to adjust backlight
 * @cabc_broken: if true, the panel CABC is broken
 * @cabc_mode: CABC or ACL mode, depending on panel, for %DCS_WRITE_CABC command
 * @display_dimming: if true, use dimming (fading) in backlight adjustment
 * @hbm: High Brightness Mode (HBM) value for %DCS_WRITE_CTRL_DISPLAY command
 * @intro_printed: if true, the driver startup message has been printed
 * @panel_config: pointer to &struct panel_config of the panel
 * @hrtimer: timer for two-part update
 * @timer: HR timer value for mid display
 * @calibration_work: work for starting calibration of frame_us
 * @calib_lock: lock for the calibration related fields
 * @calibrating: true if the frame_us value is being calibrated
 * @nsamples: number of good samples measured
 * @nbadsamples: number of bad (out of range) samples measured
 * @frame_us_tot: total time for nsamples for calculating average
 * @frame_us: measured frame time for calculating timer
 * @time_prev: start time of previous te irq
 */
struct pnd_data {
	struct semaphore lock;

	struct backlight_device *bldev;

	unsigned long hw_guard_end;
	unsigned long hw_guard_wait;

	struct omap_dss_device *dssdev;

	unsigned char id1;
	unsigned char id2;
	unsigned char id3;

	bool enabled;
	bool display_on;
	u8 rotate;
	bool mirror;

	bool te_enabled;
	atomic_t te_irq_count;

	struct workqueue_struct *wq;
	struct workqueue_struct *err_wq;

	unsigned esd_timeout;
	struct delayed_work esd_work;

	bool ulps_enabled;
	unsigned ulps_timeout;
	struct delayed_work ulps_work;

	bool lpm_enabled;

	atomic_t upd_state;
	struct update_region original_update_region;
	struct update_region update_region;
	struct delayed_work frame_timeout_work;
	enum pnd_update_te update_te;
	enum pnd_update_mode update_mode;

	bool use_dsi_bl;

	bool cabc_broken;
	unsigned cabc_mode;
	bool display_dimming;
	bool dimming_broken;
	enum pnd_hbm hbm;

	bool intro_printed;

	struct panel_config *panel_config;

	struct hrtimer hrtimer;
	ktime_t timer;
	bool reverse_te_irq_and_timer;

#ifdef MEASURE_PERF
	struct {
		/* 0 = update */
		/* 1 = timer */
		/* 2 = cb1 */
		/* 3 = TE */
		/* 4 = cb2 */
		ktime_t phase[5];
	} perf;
#endif

	struct delayed_work calibration_work;
	spinlock_t calib_lock;
	bool calibrating;
	int nsamples;
	int nbadsamples;
	u32 frame_us_tot;
	u32 frame_us;
	ktime_t time_prev;
};

static inline struct nokia_dsi_panel_data
*get_panel_data(const struct omap_dss_device *dssdev)
{
	return (struct nokia_dsi_panel_data *) dssdev->data;
}

static void pnd_esd_work(struct work_struct *work);

static void hw_guard_start(struct pnd_data *td, int guard_msec)
{
	td->hw_guard_wait = msecs_to_jiffies(guard_msec);
	td->hw_guard_end = jiffies + td->hw_guard_wait;
}

static void hw_guard_wait(struct pnd_data *td)
{
	unsigned long wait = td->hw_guard_end - jiffies;

	if ((long)wait > 0 && wait <= td->hw_guard_wait) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(wait);
	}
}

#define DISABLE_HS_MODE_FOR_READING_HACK

#ifndef DISABLE_HS_MODE_FOR_READING_HACK
static inline int pnd_dcs_read(u8 dcs_cmd, u8 *buf, int buflen)
{
	return dsi_vc_dcs_read(TCH, dcs_cmd, buf, buflen);
}
#else
/*
 * HACK: Workaround bug on some Pyrenees panels where reading fails if a read
 * command is issued in HS mode. For now, always disable HS mode for reading,
 * for all panels.
 */
static int pnd_dcs_read(u8 dcs_cmd, u8 *buf, int buflen)
{
	bool hs;
	int r;

	hs = omapdss_dsi_vc_is_hs_enabled(TCH);
	if (hs)
		omapdss_dsi_vc_enable_hs(TCH, false);

	r = dsi_vc_dcs_read(TCH, dcs_cmd, buf, buflen);

	if (hs)
		omapdss_dsi_vc_enable_hs(TCH, true);

	return r;
}
#endif

static int pnd_dcs_read_1(u8 dcs_cmd, u8 *data)
{
	int r;
	u8 buf[1];

	r = pnd_dcs_read(dcs_cmd, buf, 1);

	if (r < 0)
		return r;

	*data = buf[0];

	return 0;
}

static inline int pnd_dcs_write_nosync_0(u8 dcs_cmd)
{
	return dsi_vc_dcs_write_nosync(TCH, &dcs_cmd, 1);
}

static inline int pnd_dcs_write_nosync_1(u8 dcs_cmd, u8 param)
{
	u8 buf[2];
	buf[0] = dcs_cmd;
	buf[1] = param;
	return dsi_vc_dcs_write_nosync(TCH, buf, 2);
}

static int pnd_dcs_write_0(u8 dcs_cmd)
{
	return dsi_vc_dcs_write(TCH, &dcs_cmd, 1);
}

static int pnd_dcs_write_1(u8 dcs_cmd, u8 param)
{
	u8 buf[2];
	buf[0] = dcs_cmd;
	buf[1] = param;
	return dsi_vc_dcs_write(TCH, buf, 2);
}

static int pnd_dcs_write_2(u8 dcs_cmd, u8 param1, u8 param2)
{
	u8 buf[3];
	buf[0] = dcs_cmd;
	buf[1] = param1;
	buf[2] = param2;
	return dsi_vc_dcs_write(TCH, buf, 3);
}

static int pnd_sleep_in(struct pnd_data *td)

{
	u8 cmd;
	int r;

	hw_guard_wait(td);

	cmd = DCS_SLEEP_IN;
	r = dsi_vc_dcs_write_nosync(TCH, &cmd, 1);
	if (r)
		return r;

	hw_guard_start(td, 120);

	if (td->panel_config->sleep.sleep_in)
		msleep(td->panel_config->sleep.sleep_in);

	return 0;
}

static int pnd_sleep_out(struct pnd_data *td)
{
	int r;

	hw_guard_wait(td);

	r = pnd_dcs_write_0(DCS_SLEEP_OUT);
	if (r)
		return r;

	hw_guard_start(td, 120);

	if (td->panel_config->sleep.sleep_out)
		msleep(td->panel_config->sleep.sleep_out);

	return 0;
}

static int pnd_get_id(u8 *id1, u8 *id2, u8 *id3)
{
	int r;

	r = pnd_dcs_read_1(DCS_GET_ID1, id1);
	if (r)
		return r;
	r = pnd_dcs_read_1(DCS_GET_ID2, id2);
	if (r)
		return r;
	r = pnd_dcs_read_1(DCS_GET_ID3, id3);
	if (r)
		return r;

	return 0;
}

static int pnd_set_addr_mode(struct omap_dss_device *dssdev, u8 rotate,
		bool mirror)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int r;
	u8 mode;
	int b5, b6, b7;

	r = pnd_dcs_read_1(DCS_READ_MADCTL, &mode);
	if (r)
		return r;

	/* HACK: pyrenees TS1.1 support */
	if (td->id1 == 0xfe && td->id2 == 0x80 && td->id3 == 0x96)
		mode |= 9;

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

	return pnd_dcs_write_1(DCS_MEM_ACC_CTRL, mode);
}

static int pnd_set_update_window(struct omap_dss_device *dssdev,
				u16 x, u16 y, u16 w, u16 h)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	int r;
	u16 x1, x2, y1, y2;
	u8 buf[5];

	if (panel_data->partial_area.height) {
		if (td->rotate == 0 || td->rotate == 2)
			y += panel_data->partial_area.offset;
		else
			x += panel_data->partial_area.offset;
	}

	x1 = x;
	x2 = x + w - 1;
	y1 = y;
	y2 = y + h - 1;

	buf[0] = DCS_COLUMN_ADDR;
	buf[1] = (x1 >> 8) & 0xff;
	buf[2] = (x1 >> 0) & 0xff;
	buf[3] = (x2 >> 8) & 0xff;
	buf[4] = (x2 >> 0) & 0xff;

	r = dsi_vc_dcs_write_nosync(TCH, buf, sizeof(buf));
	if (r)
		return r;

	buf[0] = DCS_PAGE_ADDR;
	buf[1] = (y1 >> 8) & 0xff;
	buf[2] = (y1 >> 0) & 0xff;
	buf[3] = (y2 >> 8) & 0xff;
	buf[4] = (y2 >> 0) & 0xff;

	r = dsi_vc_dcs_write_nosync(TCH, buf, sizeof(buf));
	if (r)
		return r;

	return 0;
}

static int pnd_set_update_window_sync(struct omap_dss_device *dssdev,
				u16 x, u16 y, u16 w, u16 h)
{
	int r;

	r = pnd_set_update_window(dssdev, x, y, w, h);
	if (r)
		return r;

	return dsi_vc_send_bta_sync(TCH);
}

static int pnd_set_partial_area(u16 start_row, u16 height)
{
	u16 end_row = start_row + height - 1;
	u8 partial_area[] = {
		DCS_PARTIAL_AREA,
		(start_row >> 8) & 0xff, start_row & 0xff,
		(end_row >> 8) & 0xff, end_row & 0xff,
	};

	return dsi_vc_dcs_write(TCH, partial_area, sizeof(partial_area));
}

#define CALIBRATE_DELAY			5000
#define CALIBRATE_RETRY_DELAY		500
#define CALIBRATE_POWER_ON_DELAY	0

static void pnd_queue_calibration_work(struct omap_dss_device *dssdev,
					unsigned long delay)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);

	if (panel_data->use_ext_te && !td->lpm_enabled &&
		!td->reverse_te_irq_and_timer) {
		dev_dbg(&dssdev->dev, "calibration: start in %lu ms\n", delay);

		queue_delayed_work(td->wq, &td->calibration_work,
				msecs_to_jiffies(delay));
	}
}

static void pnd_cancel_calibration_work(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);

	if (panel_data->use_ext_te)
		cancel_delayed_work(&td->calibration_work);
}

static void pnd_queue_esd_work(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);

	if (td->esd_timeout > 0 && td->lpm_enabled == false)
		queue_delayed_work(td->wq, &td->esd_work,
				msecs_to_jiffies(td->esd_timeout));
}

static void pnd_cancel_esd_work(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);

	cancel_delayed_work(&td->esd_work);
}

static void pnd_queue_ulps_work(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);

	if (td->ulps_timeout > 0)
		queue_delayed_work(td->wq, &td->ulps_work,
				msecs_to_jiffies(td->ulps_timeout));
}

static void pnd_cancel_ulps_work(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);

	cancel_delayed_work(&td->ulps_work);
}

static int pnd_enter_ulps(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	int r;

	if (td->ulps_enabled)
		return 0;

	pnd_cancel_ulps_work(dssdev);

	r = _pnd_enable_te(dssdev, false);
	if (r)
		goto err;

	disable_irq(gpio_to_irq(panel_data->ext_te_gpio));

	omapdss_dsi_display_disable(dssdev, false, true);

	td->ulps_enabled = true;

	return 0;

err:
	dev_err(&dssdev->dev, "enter ULPS failed");
	pnd_panel_reset(dssdev);

	td->ulps_enabled = false;

	pnd_queue_ulps_work(dssdev);

	return r;
}

static int pnd_exit_ulps(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	int te_irq_count;
	int r;

	if (!td->ulps_enabled)
		return 0;

	r = omapdss_dsi_display_enable(dssdev);
	if (r)
		goto err;

	omapdss_dsi_vc_enable_hs(TCH, true);

	r = _pnd_enable_te(dssdev, true);
	if (r)
		goto err;

	te_irq_count = atomic_read(&td->te_irq_count);

	enable_irq(gpio_to_irq(panel_data->ext_te_gpio));

	/*
	 * HACK: Ignore any TE irqs that occur upon enable irq, as they are
	 * bound to be incorrectly timed. The irq handler will skip the first
	 * two irqs, but if enable irq does *not* cause an irq, increment the
	 * irq count, in order to ignore as few irqs as possible.
	 */
	atomic_cmpxchg(&td->te_irq_count, te_irq_count, te_irq_count + 1);

	pnd_queue_ulps_work(dssdev);

	td->ulps_enabled = false;

	return 0;

err:
	dev_err(&dssdev->dev, "exit ULPS failed");
	r = pnd_panel_reset(dssdev);

	enable_irq(gpio_to_irq(panel_data->ext_te_gpio));
	td->ulps_enabled = false;

	pnd_queue_ulps_work(dssdev);

	return r;
}

static int pnd_wake_up(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);

	if (td->ulps_enabled)
		return pnd_exit_ulps(dssdev);

	pnd_cancel_ulps_work(dssdev);
	pnd_queue_ulps_work(dssdev);
	return 0;
}

static int _pnd_enable_lpm(struct omap_dss_device *dssdev, bool enable)
{
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	int r;

	if (!panel_data->partial_area.height) {
		if (enable)
			r = pnd_dcs_write_0(DCS_PARTIAL_MODE_ON);
		else
			r = pnd_dcs_write_0(DCS_NORMAL_MODE_ON);
		if (r)
			return r;
	}

	if (enable)
		r = pnd_dcs_write_0(DCS_IDLE_MODE_ON);
	else
		r = pnd_dcs_write_0(DCS_IDLE_MODE_OFF);

	if (!r)
		pm_optimizer_enable_dss_lpm(enable);

	return r;
}

static int pnd_bl_update_status(struct backlight_device *dev)
{
	struct omap_dss_device *dssdev = dev_get_drvdata(&dev->dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	int r;
	int level;

	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		level = dev->props.brightness;
	else
		level = 0;

	dev_dbg(&dssdev->dev, "update brightness to %d\n", level);

	down(&td->lock);

	if (td->use_dsi_bl) {
		if (td->enabled) {
			dsi_bus_lock();

			r = pnd_wake_up(dssdev);
			if (!r)
				r = pnd_dcs_write_1(DCS_BRIGHTNESS, level);

			dsi_bus_unlock();
		} else {
			r = 0;
		}
	} else {
		if (!panel_data->set_backlight)
			r = -EINVAL;
		else
			r = panel_data->set_backlight(dssdev, level);
	}

	up(&td->lock);

	return r;
}

static int pnd_bl_get_intensity(struct backlight_device *dev)
{
	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		return dev->props.brightness;

	return 0;
}

static struct backlight_ops pnd_bl_ops = {
	.get_brightness = pnd_bl_get_intensity,
	.update_status  = pnd_bl_update_status,
};

/* Note: WRITE_TEAR_SCANLINE for TE irq only works on SMD Pyrenees */
static int _pnd_reverse_te_irq_and_timer(struct omap_dss_device *dssdev,
					bool reverse)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	u16 scanline;
	int r;

	if (reverse)
		scanline = dssdev->panel.timings.y_res / 2;
	else
		scanline = 0;

	r = pnd_dcs_write_2(DCS_WRITE_TEAR_SCANLINE,
			scanline >> 8, scanline & 0xff);
	if (r)
		return r;

	td->reverse_te_irq_and_timer = reverse;

	return 0;
}

/*
 * ACL and CABC are mutually exclusive, at least in currently supported panels,
 * and use the same register. If this ever changes, the code reusing CABC for
 * ACL needs to be revisited.
 *
 * Note: ACL causes framerate to drop in SMD Pyrenees, causing tear due to
 * two-part update timing errors. This is worked around by using reversed TE irq
 * and hrtimer handlers for SMD Pyrenees.
 */
static int _pnd_set_cabc_mode(struct omap_dss_device *dssdev, u8 cabc)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);

	/* Fake success on broken CABC. */
	if (td->cabc_broken)
		return 0;

	return pnd_dcs_write_1(DCS_WRITE_CABC, cabc);
}

static int _pnd_enable_dimming(struct omap_dss_device *dssdev, bool enable)
{
	int r;
	u8 ctrl;

	r = pnd_dcs_read_1(DCS_READ_CTRL_DISPLAY, &ctrl);
	if (r)
		return r;

	if (enable)
		ctrl |= PND_CTRLD_DD;
	else
		ctrl &= ~PND_CTRLD_DD;

	return pnd_dcs_write_1(DCS_WRITE_CTRL_DISPLAY, ctrl);
}

static int _pnd_set_hbm(struct omap_dss_device *dssdev, enum pnd_hbm hbm)
{
	int r;
	u8 ctrl;

	BUG_ON(hbm < PND_HBM_OFF || hbm > PND_HBM_MAX);

	r = pnd_dcs_read_1(DCS_READ_CTRL_DISPLAY, &ctrl);
	if (r)
		return r;

	ctrl = (ctrl & ~PND_CTRLD_HBM) | (hbm << PND_CTRLD_HBM_SHIFT);
	r = pnd_dcs_write_1(DCS_WRITE_CTRL_DISPLAY, ctrl);
	if (r)
		return r;

	return 0;
}

static void pnd_get_timings(struct omap_dss_device *dssdev,
			struct omap_video_timings *timings)
{
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);

	*timings = dssdev->panel.timings;

	if (panel_data->partial_area.height)
		timings->y_res = panel_data->partial_area.height;
}

static void _pnd_get_resolution(struct omap_dss_device *dssdev,
				u16 *xres, u16 *yres, bool partial)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	u16 x, y;

	x = dssdev->panel.timings.x_res;
	if (partial && panel_data->partial_area.height)
		y = panel_data->partial_area.height;
	else
		y = dssdev->panel.timings.y_res;

	if (td->rotate == 0 || td->rotate == 2) {
		*xres = x;
		*yres = y;
	} else {
		*xres = y;
		*yres = x;
	}
}

static void pnd_get_resolution(struct omap_dss_device *dssdev,
		u16 *xres, u16 *yres)
{
	_pnd_get_resolution(dssdev, xres, yres, true);
}

static void pnd_get_dimensions(struct omap_dss_device *dssdev,
				u32 *width, u32 *height)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	u32 w, h;

	w = td->panel_config->width;
	if (panel_data->partial_area.height) {
		/* REVISIT: handle overflows */
		h = td->panel_config->height * panel_data->partial_area.height
			/ dssdev->panel.timings.y_res;
	} else {
		h = td->panel_config->height;
	}

	if (td->rotate == 0 || td->rotate == 2) {
		*width = w;
		*height = h;
	} else {
		*width = h;
		*height = w;
	}
}

static int pnd_run_self_diag(struct omap_dss_device *dssdev,
			      bool *self_diag_fail)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	u8 read_madctl, read_madctl_esd, read_power_mode;
	int r;

	*self_diag_fail = false; /* OK by default */

	if (td->enabled == false) {
		/*
		 *  If panel state is active, but the powers are off, it means
		 * that there has been error at some point. So jump directly
		 * out.
		 */
		return -EIO;
	}

	r = pnd_wake_up(dssdev);
	if (r) {
		dev_err(&dssdev->dev, "pnd_wake_up failed in ESD %d\n", r);
		return r;
	}

	/* Re-write settings to the panel */
	if (td->display_on) {
		u32 brightness = td->bldev->props.brightness;

		r = pnd_write_display_current_settings(dssdev);
		if (r)
			return r;

		r = pnd_dcs_write_nosync_1(DCS_BRIGHTNESS, brightness);
		if (r)
			return r;

		if (td->te_enabled)
			r = pnd_dcs_write_nosync_1(DCS_TEAR_ON, 0);/* VS-mode */
		else
			r = pnd_dcs_write_nosync_0(DCS_TEAR_OFF);
		if (r)
			return r;

		/* Check the display is really powered */
		r = pnd_dcs_read_1(DCS_READ_POWER_MODE, &read_power_mode);
		if (r ||
		    ((read_power_mode & PND_BOOSTER_VOLTAGE) == 0) ||
		    ((read_power_mode & PND_DISPLAY_ON)      == 0) ||
		    ((read_power_mode & PND_SLEEP_OUT)       == 0)) {
			*self_diag_fail = true;
			dev_err(&dssdev->dev, "Display voltage error 0x%X %d\n",
						read_power_mode, r);
		}
	}

	r = pnd_dcs_read_1(DCS_READ_MADCTL, &read_madctl);
	if (r) {
		dev_err(&dssdev->dev, "Failed to read madctl 1 %d\n", r);
		return r;
	}

	/* Set "wrong" COL order, later it is visible in the display status */
	r = pnd_dcs_write_nosync_1(DCS_MEM_ACC_CTRL,
				read_madctl ^ PND_COL_ADDRESS_ORDER_RL);
	if (r) {
		dev_err(&dssdev->dev, "Failed to write madctl 1 %d\n", r);
		/*
		 * Don't leave the function in here, if writing was OK but
		 * error got, display should be still restored
		 */
	}

	r = pnd_dcs_read_1(DCS_READ_MADCTL, &read_madctl_esd);
	if (r) {
		dev_err(&dssdev->dev, "Failed to read madctl 2 %d\n", r);
		/* Don't leave the function */
	}

	if (read_madctl_esd != (read_madctl ^ PND_COL_ADDRESS_ORDER_RL)) {
		/* status didn't react to MADCTL-command */
		*self_diag_fail = true;
		dev_err(&dssdev->dev, "Display madctl error 0x%X\n",
					read_madctl_esd);
	}

	/* Restore the correct MADCTL */
	r = pnd_dcs_write_nosync_1(DCS_MEM_ACC_CTRL, read_madctl);
	if (r) {
		/* Try again */
		dev_err(&dssdev->dev, "Failed to write madctl 2 %d\n", r);
		r = pnd_dcs_write_nosync_1(DCS_MEM_ACC_CTRL, read_madctl);
	}

	return r;
}

/*
 * Self-test for TE pin. The pin is tested by confirming that
 * the TE interrupt count increases after waiting ~two frames.
 */
static int pnd_run_te_test(struct omap_dss_device *dssdev, bool *te_test_fail)
{
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	signed te_count_1, te_count_2;
	bool disable_te = false;
	int r;

	*te_test_fail = false;

	if (!panel_data->use_ext_te)
		return 0;

	r = pnd_wake_up(dssdev);
	if (r)
		return r;

	/*
	 * Enable TE from the panel, if it's not currently enabled, and disable
	 * it afterwards
	 */
	if (!td->te_enabled) {
		r = _pnd_enable_te(dssdev, true);
		if (r)
			return r;
		disable_te = true;
	}

	/*
	 * This test presumes that the TE irq is enabled when the panel is
	 * enabled and DSI is not in ULPS (ie. pnd_wakeu_up() has been called.
	 * Also one assumption is that pull-down-resistor is enabled for TE.
	 */
	te_count_1 = atomic_read(&td->te_irq_count);
	msleep(17 * 2); /* Wait ~2 times refresh duration (60Hz) */
	te_count_2 = atomic_read(&td->te_irq_count);

	if (disable_te) {
		r = _pnd_enable_te(dssdev, false);
		if (r)
			return r;
	}

	if (te_count_1 == te_count_2) {
		dev_err(&dssdev->dev, "TE pin test failed\n");
		*te_test_fail = true;
	}

	return 0;
}

/*
 * Self-test for RESX pin. The pin is tested by using the RESX
 * pin to reset the panel, and confirming that the power mode changes as
 * the panel goes from SLEEP_OUT to SLEEP_IN state.
 */
static int pnd_run_resx_test(struct omap_dss_device *dssdev,
				bool *resx_test_fail)
{
	u8 mode1, mode2;
	int r;

	*resx_test_fail = false;

	r = pnd_wake_up(dssdev);
	if (r)
		return r;

	/* presumes the panel is in SLEEP_OUT mode */
	r = pnd_dcs_read_1(DCS_READ_POWER_MODE, &mode1);
	if (r)
		goto out;

	pnd_hw_reset(dssdev);

	r = pnd_dcs_read_1(DCS_READ_POWER_MODE, &mode2);
	if (r)
		goto out;

	if (mode1 == mode2) {
		dev_err(&dssdev->dev, "RESX pin test failed\n");
		*resx_test_fail = true;
	}

out:
	/* reset the panel to return to normal state */
	pnd_panel_reset(dssdev);

	return r;
}

static ssize_t pnd_show_num_errors(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	u8 errors;
	int r;

	down(&td->lock);

	if (td->enabled) {
		dsi_bus_lock();

		r = pnd_wake_up(dssdev);
		if (!r)
			r = pnd_dcs_read_1(DCS_READ_NUM_ERRORS, &errors);

		dsi_bus_unlock();
	} else {
		r = -ENODEV;
	}

	up(&td->lock);

	if (r)
		return r;

	return snprintf(buf, PAGE_SIZE, "%d\n", errors);
}

static ssize_t pnd_show_hw_revision(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	u8 id1, id2, id3;

	down(&td->lock);

	id1 = td->id1;
	id2 = td->id2;
	id3 = td->id3;

	up(&td->lock);

	return snprintf(buf, PAGE_SIZE, "%02x.%02x.%02x\n", id1, id2, id3);
}

static const char *cabc_modes[] = {
	"off",		/* used also always when CABC is not supported */
	"ui",
	"still-image",
	"moving-image",
};

static ssize_t pnd_show_cabc_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	const char *mode_str;
	int mode;
	int len;

	mode = td->cabc_mode;

	mode_str = "unknown";
	if (mode >= 0 && mode < ARRAY_SIZE(cabc_modes))
		mode_str = cabc_modes[mode];
	len = snprintf(buf, PAGE_SIZE, "%s\n", mode_str);

	return len < PAGE_SIZE - 1 ? len : PAGE_SIZE - 1;
}

static ssize_t pnd_store_cabc_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int i;
	int r;

	for (i = 0; i < ARRAY_SIZE(cabc_modes); i++) {
		if (sysfs_streq(cabc_modes[i], buf))
			break;
	}

	if (i == ARRAY_SIZE(cabc_modes))
		return -EINVAL;

	down(&td->lock);

	if (td->enabled) {
		dsi_bus_lock();

		r = pnd_wake_up(dssdev);
		if (r)
			goto err;

		r = _pnd_set_cabc_mode(dssdev, i);
		if (r)
			goto err;

		dsi_bus_unlock();
	}

	td->cabc_mode = i;

	up(&td->lock);

	return count;
err:
	dsi_bus_unlock();
	up(&td->lock);
	return r;

}

static ssize_t pnd_show_cabc_available_modes(struct device *dev,
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

static ssize_t pnd_store_dimming(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned long t;
	int r;

	r = strict_strtoul(buf, 10, &t);
	if (r)
		return r;

	down(&td->lock);

	if (td->display_dimming != !!t && td->enabled) {
		dsi_bus_lock();

		r = pnd_wake_up(dssdev);
		if (r)
			goto err;

		r = _pnd_enable_dimming(dssdev, t);
		if (r)
			goto err;

		dsi_bus_unlock();
	}

	td->display_dimming = t;

	up(&td->lock);

	return count;

err:
	dsi_bus_unlock();
	up(&td->lock);

	return r;
}

static ssize_t pnd_show_dimming(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned t;

	down(&td->lock);
	t = td->display_dimming;
	up(&td->lock);

	return snprintf(buf, PAGE_SIZE, "%u\n", t);
}

static ssize_t pnd_store_esd_timeout(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);

	unsigned long t;
	int r;

	r = strict_strtoul(buf, 10, &t);
	if (r)
		return r;

	down(&td->lock);
	pnd_cancel_esd_work(dssdev);
	td->esd_timeout = t;
	if (td->enabled)
		pnd_queue_esd_work(dssdev);
	up(&td->lock);

	return count;
}

static ssize_t pnd_show_esd_timeout(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned t;

	down(&td->lock);
	t = td->esd_timeout;
	up(&td->lock);

	return snprintf(buf, PAGE_SIZE, "%u\n", t);
}

static ssize_t pnd_store_ulps(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned long t;
	int r;

	r = strict_strtoul(buf, 10, &t);
	if (r)
		return r;

	down(&td->lock);

	if (td->enabled) {
		dsi_bus_lock();

		if (t)
			r = pnd_enter_ulps(dssdev);
		else
			r = pnd_wake_up(dssdev);

		dsi_bus_unlock();
	}

	up(&td->lock);

	if (r)
		return r;

	return count;
}

static ssize_t pnd_show_ulps(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned t;

	down(&td->lock);
	t = td->ulps_enabled;
	up(&td->lock);

	return snprintf(buf, PAGE_SIZE, "%u\n", t);
}

static ssize_t pnd_store_ulps_timeout(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned long t;
	int r;

	r = strict_strtoul(buf, 10, &t);
	if (r)
		return r;

	down(&td->lock);
	td->ulps_timeout = t;

	if (td->enabled) {
		/* pnd_wake_up will restart the timer */
		dsi_bus_lock();
		r = pnd_wake_up(dssdev);
		dsi_bus_unlock();
	}

	up(&td->lock);

	if (r)
		return r;

	return count;
}

static ssize_t pnd_show_ulps_timeout(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned t;

	down(&td->lock);
	t = td->ulps_timeout;
	up(&td->lock);

	return snprintf(buf, PAGE_SIZE, "%u\n", t);
}

static ssize_t pnd_store_lpm(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned long t;
	int r;

	r = strict_strtoul(buf, 10, &t);
	if (r)
		return r;

	down(&td->lock);

	if (td->lpm_enabled != !!t && td->enabled) {
		dsi_bus_lock();

		r = pnd_wake_up(dssdev);
		if (r)
			goto err;

		r = _pnd_enable_lpm(dssdev, t);
		if (r)
			goto err;

		td->lpm_enabled = t;
		if (t) {
			pnd_cancel_esd_work(dssdev);
			pnd_cancel_calibration_work(dssdev);
			td->calibrating = false;
		} else {
			pnd_queue_esd_work(dssdev);
			pnd_queue_calibration_work(dssdev, CALIBRATE_DELAY);
		}

		dsi_bus_unlock();
	}

	td->lpm_enabled = t;
	pnd_set_timer(dssdev);

	up(&td->lock);

	return count;

err:
	dsi_bus_unlock();
	up(&td->lock);

	return r;
}

static ssize_t pnd_show_lpm(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned t;

	down(&td->lock);
	t = td->lpm_enabled;
	up(&td->lock);

	return snprintf(buf, PAGE_SIZE, "%u\n", t);
}

static ssize_t pnd_store_hbm(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned long hbm;
	u8 acl;
	int r;

	r = strict_strtoul(buf, 10, &hbm);
	if (r)
		return r;

	/* Always use automatic current limiting in high brightness mode */
	switch (hbm) {
	case PND_HBM_OFF:
		acl = PND_ACL_OFF;
		break;
	case PND_HBM_400:
		acl = PND_ACL_ON_80P;
		break;
	case PND_HBM_450:
		/* HACK: Hide HBM difference between SMD/LGD Pyrenees */
		hbm = PND_HBM_500;
		acl = PND_ACL_ON_70P;
		break;
	case PND_HBM_500:
	default:
		return -EINVAL;
	}

	down(&td->lock);

	if (td->hbm != hbm && td->enabled) {
		dsi_bus_lock();

		r = pnd_wake_up(dssdev);
		if (r)
			goto err;

		r = _pnd_set_cabc_mode(dssdev, acl);
		if (r)
			goto err;

		r = _pnd_set_hbm(dssdev, hbm);
		if (r)
			goto err;

		dsi_bus_unlock();
	}

	td->hbm = hbm;
	td->cabc_mode = acl;

	up(&td->lock);

	return count;

err:
	dsi_bus_unlock();
	up(&td->lock);

	return r;
}

static ssize_t pnd_show_hbm(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	unsigned t;

	down(&td->lock);
	/* HACK: Hide HBM difference between SMD/LGD Pyrenees */
	t = min_t(typeof(t), td->hbm, PND_HBM_450);
	up(&td->lock);

	return snprintf(buf, PAGE_SIZE, "%u\n", t);
}

static ssize_t pnd_show_self_diag(struct device *dev,
			      struct device_attribute *attr,
			      char *buf)
{
	struct omap_dss_device *dssdev = to_dss_device(dev);
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int ret;
	bool self_diag_fail;

	down(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE) {
		up(&td->lock);
		return -ENODEV;
	}

	dsi_bus_lock();

	ret = pnd_run_self_diag(dssdev, &self_diag_fail);
	if (ret || self_diag_fail)
		goto out;

	ret = pnd_run_te_test(dssdev, &self_diag_fail);
	if (ret || self_diag_fail)
		goto out;

	ret = pnd_run_resx_test(dssdev, &self_diag_fail);
	if (ret || self_diag_fail)
		goto out;

out:
	dsi_bus_unlock();

	up(&td->lock);

	if (!ret)
		ret = snprintf(buf, PAGE_SIZE,
			       self_diag_fail ? "FAIL\n" : "OK\n");

	return ret;
}

/* generic attributes */
static DEVICE_ATTR(num_dsi_errors, S_IRUGO, pnd_show_num_errors, NULL);
static DEVICE_ATTR(hw_revision, S_IRUGO, pnd_show_hw_revision, NULL);
static DEVICE_ATTR(esd_timeout, S_IRUGO | S_IWUSR,
		pnd_show_esd_timeout, pnd_store_esd_timeout);
static DEVICE_ATTR(ulps, S_IRUGO | S_IWUSR,
		pnd_show_ulps, pnd_store_ulps);
static DEVICE_ATTR(ulps_timeout, S_IRUGO | S_IWUSR,
		pnd_show_ulps_timeout, pnd_store_ulps_timeout);
static DEVICE_ATTR(self_diag, S_IRUGO, pnd_show_self_diag, NULL);
static DEVICE_ATTR(dimming, S_IRUGO | S_IWUSR,
		pnd_show_dimming, pnd_store_dimming);

static struct attribute *pnd_attrs[] = {
	&dev_attr_num_dsi_errors.attr,
	&dev_attr_hw_revision.attr,
	&dev_attr_esd_timeout.attr,
	&dev_attr_ulps.attr,
	&dev_attr_ulps_timeout.attr,
	&dev_attr_self_diag.attr,
	&dev_attr_dimming.attr,
	NULL,
};

static struct attribute_group pnd_attr_group = {
	.attrs = pnd_attrs,
};

/* panel specific attributes */
static DEVICE_ATTR(cabc_mode, S_IRUGO | S_IWUSR,
		pnd_show_cabc_mode, pnd_store_cabc_mode);
static DEVICE_ATTR(cabc_available_modes, S_IRUGO,
		pnd_show_cabc_available_modes, NULL);
static DEVICE_ATTR(lpm, S_IRUGO | S_IWUSR, pnd_show_lpm, pnd_store_lpm);
static DEVICE_ATTR(hbm, S_IRUGO | S_IWUSR, pnd_show_hbm, pnd_store_hbm);

/* Taal and Himalaya */
static struct attribute *pnd_attrs_himalaya[] = {
	&dev_attr_cabc_mode.attr,
	&dev_attr_cabc_available_modes.attr,
	NULL,
};

static struct attribute_group pnd_attr_group_himalaya = {
	.attrs = pnd_attrs_himalaya,
};

/* Pyrenees */
static struct attribute *pnd_attrs_pyrenees[] = {
	&dev_attr_lpm.attr,
	&dev_attr_hbm.attr,
	NULL,
};

static struct attribute_group pnd_attr_group_pyrenees = {
	.attrs = pnd_attrs_pyrenees,
};

static int pnd_create_sysfs(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	r = sysfs_create_group(&dssdev->dev.kobj, &pnd_attr_group);
	if (r)
		goto err0;

	switch (td->panel_config->type) {
	case PANEL_TAAL: /* Taal shares attributes with Himalaya */
	case PANEL_HIMALAYA:
		r = sysfs_create_group(&dssdev->dev.kobj,
				&pnd_attr_group_himalaya);
		if (r)
			goto err1;
		break;

	case PANEL_PYRENEES:
		r = sysfs_create_group(&dssdev->dev.kobj,
				&pnd_attr_group_pyrenees);
		if (r)
			goto err1;
		break;
	}

	return 0;

err1:
	sysfs_remove_group(&dssdev->dev.kobj, &pnd_attr_group);
err0:
	return r;
}

static void pnd_remove_sysfs(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);

	switch (td->panel_config->type) {
	case PANEL_TAAL: /* Taal shares attributes with Himalaya */
	case PANEL_HIMALAYA:
		sysfs_remove_group(&dssdev->dev.kobj, &pnd_attr_group_himalaya);
		break;
	case PANEL_PYRENEES:
		sysfs_remove_group(&dssdev->dev.kobj, &pnd_attr_group_pyrenees);
		break;
	}
	sysfs_remove_group(&dssdev->dev.kobj, &pnd_attr_group);
}

static void pnd_calibration_work(struct work_struct *work)
{
	struct pnd_data *td = container_of(work, struct pnd_data,
					calibration_work.work);
	struct omap_dss_device *dssdev = td->dssdev;
	unsigned long flags;

	down(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE || !td->enabled)
		goto out;

	dev_dbg(&dssdev->dev, "calibration: start\n");

	spin_lock_irqsave(&td->calib_lock, flags);

	td->nsamples = -1; /* -1 to record time_prev for first sample */
	td->nbadsamples = 0;
	td->frame_us_tot = 0;
	td->calibrating = true;

	spin_unlock_irqrestore(&td->calib_lock, flags);

out:
	up(&td->lock);
}

/* compute and set half frame timing from frame duration */
static void _pnd_set_timer(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct omap_video_timings *timings;
	u32 vblank_lines;
	u32 vblank_us;
	u32 frame_us;
	u32 timer_us;

	timings = &td->panel_config->timings;
	vblank_lines = timings->vfp + timings->vsw + timings->vbp;

	if (td->reverse_te_irq_and_timer) {
		/* calculate frame us from theoretical FPS */
		if (td->lpm_enabled)
			frame_us = 1000000 / 15;
		else
			frame_us = 1000000 / 46; /* timing ok for 46...60 FPS */
	} else {
		/* measured frame us */
		if (td->lpm_enabled)
			frame_us = td->frame_us * 4; /* 1/4 of original FPS */
		else
			frame_us = td->frame_us;
	}

	vblank_us = frame_us * vblank_lines / (timings->y_res + vblank_lines);

	if (td->reverse_te_irq_and_timer)
		timer_us = (frame_us - vblank_us) / 2;
	else
		timer_us = vblank_us + (frame_us - vblank_us) / 2;

	dev_dbg(&dssdev->dev, "measured frame: %u us, computed frame: %u us, "
		"timer: %u us\n", td->frame_us, frame_us, timer_us);

	td->timer = ktime_set(0, timer_us * 1000);
}

static void pnd_set_timer(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	unsigned long flags;

	if (!panel_data->use_ext_te)
		return;

	spin_lock_irqsave(&td->calib_lock, flags);
	_pnd_set_timer(dssdev);
	spin_unlock_irqrestore(&td->calib_lock, flags);
}

#define CALIBRATE_SAMPLES		30
#define CALIBRATE_MAX_BAD_SAMPLES	5

static void pnd_calibrate(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	ktime_t now;
	int frame_us;

	spin_lock(&td->calib_lock);

	if (td->nsamples < 0) {
		if (++td->nsamples == 0)
			td->time_prev = ktime_get();
		goto out;
	}

	now = ktime_get();
	frame_us = ktime_us_delta(now, td->time_prev);
	td->time_prev = now;

	/* throw away out of range values */
	if (frame_us < 14000 || frame_us > 19000)
		goto err_range;

	td->frame_us_tot += frame_us;
	if (++td->nsamples >= CALIBRATE_SAMPLES) {
		td->frame_us = td->frame_us_tot / td->nsamples;
		_pnd_set_timer(dssdev);
		td->calibrating = false;
		pnd_queue_calibration_work(dssdev, CALIBRATE_DELAY);
	}
out:
	spin_unlock(&td->calib_lock);
	return;

err_range:
	dev_dbg(&dssdev->dev, "calibrate: frame %d us, skipping\n", frame_us);
	if (++td->nbadsamples >= CALIBRATE_MAX_BAD_SAMPLES) {
		dev_warn(&dssdev->dev, "calibrate: %d bad samples, retry\n",
			td->nbadsamples);
		td->calibrating = false;
		pnd_queue_calibration_work(dssdev, CALIBRATE_RETRY_DELAY);
	}
	spin_unlock(&td->calib_lock);
}

static void pnd_hw_reset(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);

	if (panel_data->reset_gpio == -1)
		return;

	gpio_set_value(panel_data->reset_gpio, 1);
	if (td->panel_config->reset_sequence.high)
		udelay(td->panel_config->reset_sequence.high);
	/* reset the panel */
	gpio_set_value(panel_data->reset_gpio, 0);
	/* assert reset */
	if (td->panel_config->reset_sequence.low)
		udelay(td->panel_config->reset_sequence.low);
	gpio_set_value(panel_data->reset_gpio, 1);
	/* wait after releasing reset */
	if (td->panel_config->sleep.hw_reset)
		msleep(td->panel_config->sleep.hw_reset);

	td->display_on = false;
}

static int pnd_probe(struct omap_dss_device *dssdev)
{
	struct pnd_data *td;
	struct backlight_device *bldev;
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	struct panel_config *panel_config = NULL;
	int r, i;

	dev_dbg(&dssdev->dev, "probe\n");

	if (!panel_data || !panel_data->name) {
		r = -EINVAL;
		goto err;
	}

	for (i = 0; i < ARRAY_SIZE(panel_configs); i++) {
		if (strcmp(panel_data->name, panel_configs[i].name) == 0) {
			panel_config = &panel_configs[i];
			break;
		}
	}

	if (!panel_config) {
		r = -EINVAL;
		goto err;
	}

	/* REVISIT: handle overflows */
	if (panel_data->partial_area.offset + panel_data->partial_area.height
		> panel_config->timings.y_res) {
		r = -EINVAL;
		goto err;
	}

	dssdev->panel.config = OMAP_DSS_LCD_TFT;
	dssdev->panel.timings = panel_config->timings;
	dssdev->ctrl.pixel_size = 24;

	td = kzalloc(sizeof(*td), GFP_KERNEL);
	if (!td) {
		r = -ENOMEM;
		goto err;
	}
	td->dssdev = dssdev;
	td->panel_config = panel_config;
	td->esd_timeout = panel_data->esd_timeout;
	td->ulps_enabled = false;
	td->ulps_timeout = panel_data->ulps_timeout;
	td->rotate = panel_data->rotate;

	sema_init(&td->lock, 1);

	atomic_set(&td->upd_state, PND_UPD_STATE_NONE);

	r = init_regulators(dssdev, panel_config->regulators,
			panel_config->num_regulators);
	if (r)
		goto err_reg;

	td->wq = create_singlethread_workqueue("pnd");
	if (td->wq == NULL) {
		dev_err(&dssdev->dev, "can't create workqueue\n");
		r = -ENOMEM;
		goto err_wq;
	}
	INIT_DELAYED_WORK_DEFERRABLE(&td->esd_work, pnd_esd_work);

	INIT_DELAYED_WORK(&td->ulps_work, pnd_ulps_work);

	td->err_wq = create_singlethread_workqueue("pnd/err");
	if (td->err_wq == NULL) {
		dev_err(&dssdev->dev, "can't create error handler workqueue\n");
		r = -ENOMEM;
		goto err_err_wq;
	}
	INIT_DELAYED_WORK_DEFERRABLE(&td->frame_timeout_work,
				pnd_frame_timeout_work_callback);

	dev_set_drvdata(&dssdev->dev, td);

	if (!dssdev->skip_first_init)
		pnd_hw_reset(dssdev);

	/* if no platform set_backlight() defined, presume DSI backlight
	 * control */
	if (!panel_data->set_backlight)
		td->use_dsi_bl = true;

	bldev = backlight_device_register(dev_name(&dssdev->dev), &dssdev->dev,
			dssdev, &pnd_bl_ops);
	if (IS_ERR(bldev)) {
		r = PTR_ERR(bldev);
		goto err_bl;
	}

	td->bldev = bldev;

	bldev->props.fb_blank = FB_BLANK_UNBLANK;
	bldev->props.power = FB_BLANK_UNBLANK;
	if (td->use_dsi_bl) {
		bldev->props.max_brightness = 255;
		bldev->props.brightness = 255;
	} else {
		bldev->props.max_brightness = 127;
		bldev->props.brightness = 127;
	}

	pnd_bl_update_status(bldev);

	if (panel_data->use_ext_te) {
		int gpio = panel_data->ext_te_gpio;

		spin_lock_init(&td->calib_lock);

		/* initial half frame timer value */
		td->frame_us = 16500;
		pnd_set_timer(dssdev);

		INIT_DELAYED_WORK_DEFERRABLE(&td->calibration_work,
					pnd_calibration_work);

		hrtimer_init(&td->hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
		td->hrtimer.function = pnd_timer_handler;

		r = gpio_request(gpio, "pnd TE");
		if (r) {
			dev_err(&dssdev->dev, "GPIO request failed\n");
			goto err_gpio;
		}

		gpio_direction_input(gpio);

		r = request_irq(gpio_to_irq(gpio), pnd_te_isr,
				IRQF_DISABLED | IRQF_TRIGGER_RISING,
				"pnd TE", dssdev);

		if (r) {
			dev_err(&dssdev->dev, "IRQ request failed\n");
			gpio_free(gpio);
			goto err_irq;
		}

		dev_dbg(&dssdev->dev, "Using GPIO TE\n");
	}

	r = pnd_create_sysfs(dssdev);
	if (r) {
		dev_err(&dssdev->dev, "failed to create sysfs files\n");
		goto err_sysfs;
	}

	return 0;
err_sysfs:
	if (panel_data->use_ext_te)
		free_irq(gpio_to_irq(panel_data->ext_te_gpio), dssdev);
err_irq:
	if (panel_data->use_ext_te)
		gpio_free(panel_data->ext_te_gpio);
err_gpio:
	backlight_device_unregister(bldev);
err_bl:
	destroy_workqueue(td->err_wq);
err_err_wq:
	destroy_workqueue(td->wq);
err_wq:
	free_regulators(panel_config->regulators, panel_config->num_regulators);
err_reg:
	kfree(td);
err:
	return r;
}

static void pnd_remove(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	struct backlight_device *bldev;

	dev_dbg(&dssdev->dev, "remove\n");

	pnd_remove_sysfs(dssdev);

	if (panel_data->use_ext_te) {
		int gpio = panel_data->ext_te_gpio;
		free_irq(gpio_to_irq(gpio), dssdev);
		gpio_free(gpio);
	}

	bldev = td->bldev;
	bldev->props.power = FB_BLANK_POWERDOWN;
	pnd_bl_update_status(bldev);
	backlight_device_unregister(bldev);

	pnd_cancel_ulps_work(dssdev);
	pnd_cancel_esd_work(dssdev);
	pnd_cancel_calibration_work(dssdev);
	destroy_workqueue(td->wq);

	cancel_delayed_work_sync(&td->frame_timeout_work);
	destroy_workqueue(td->err_wq);

	/* reset, to be sure that the panel is in a valid state */
	pnd_hw_reset(dssdev);

	free_regulators(td->panel_config->regulators,
			td->panel_config->num_regulators);

	kfree(td);
}

/* HACK: pyrenees TS1.1 support*/
static int pyrenees_init(struct omap_dss_device *dssdev)
{
	int r;

	/* key unlock */
	r = pnd_dcs_write_2(0xf0, 0x5a, 0x5a);
	if (r)
		return r;
	r = pnd_dcs_write_2(0xf1, 0x5a, 0x5a);
	if (r)
		return r;
	r = pnd_dcs_write_2(0xfc, 0x5a, 0x5a);
	if (r)
		return r;

	/* MIPI overdrive */
	r = pnd_dcs_write_1(0xe2, 0x0f);
	if (r)
		return r;
	r = pnd_dcs_write_1(0xe1, 0x04);
	if (r)
		return r;

	/* key lock */
	r = pnd_dcs_write_2(0xf0, 0xa5, 0xa5);
	if (r)
		return r;
	r = pnd_dcs_write_2(0xf1, 0xa5, 0xa5);
	if (r)
		return r;
	r = pnd_dcs_write_2(0xfc, 0xa5, 0xa5);
	if (r)
		return r;

	/* MADCTL */
	r = pnd_dcs_write_1(0x36, 0x09);
	if (r)
		return r;

	return 0;
}

/* Writes basic settings to display panel according to the panel driver
 * current settings. Can be called at any time. */
static int pnd_write_display_current_settings(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	u8 ctrl_display;
	int r;

	ctrl_display = PND_CTRLD_BL | PND_CTRLD_BCTRL;
	if (td->display_dimming)
		ctrl_display |= PND_CTRLD_DD;
	if (td->hbm)
		ctrl_display |= td->hbm << PND_CTRLD_HBM_SHIFT;

	r = pnd_dcs_write_1(DCS_WRITE_CTRL_DISPLAY, ctrl_display);
	if (r)
		return r;

	r = pnd_dcs_write_1(DCS_PIXEL_FORMAT, 0x7); /* 24bit/pixel */
	if (r)
		return r;

	r = pnd_set_addr_mode(dssdev, td->rotate, td->mirror);
	if (r)
		return r;

	r = _pnd_set_cabc_mode(dssdev, td->cabc_mode);
	if (r)
		return r;

	if (td->panel_config->type == PANEL_PYRENEES && td->id1 == 0xfe) {
		/*
		 * SMD Pyrenees drops framerate in ACL, and produces one or two
		 * irregularly timed TE signals after DCS_TEAR_ON. Both of these
		 * problems can be worked around by reversing the TE irq and
		 * hrtimer handlers.
		 */
		r = _pnd_reverse_te_irq_and_timer(dssdev, true);
		if (r)
			return r;
	}

	r = _pnd_enable_lpm(dssdev, td->lpm_enabled);
	if (r)
		return r;

	if (td->display_on) {
		r = pnd_dcs_write_0(DCS_DISPLAY_ON);
		if (r)
			return r;
	}

	return 0;
}

static int pnd_power_on(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	u8 id1, id2, id3, brightness;
	int r;
	bool skip_init = dssdev->skip_first_init;

	if (skip_init)
		dev_info(&dssdev->dev, "skipping HW reset\n");

	r = omapdss_dsi_display_enable(dssdev);
	if (r) {
		dev_err(&dssdev->dev, "failed to enable DSI\n");
		goto err0;
	}

	if (!skip_init)
		pnd_hw_reset(dssdev);

	omapdss_dsi_vc_enable_hs(TCH, false);

	r = pnd_get_id(&id1, &id2, &id3);
	if (r)
		goto err;

	td->id1 = id1;
	td->id2 = id2;
	td->id3 = id3;

	if (td->id3 != td->panel_config->panel_id) {
		dev_warn(&dssdev->dev,
				"panel ID %u does not match the ID of " \
				"panel '%s'\n",
				td->id3, td->panel_config->name);
	}

	/* HACK: SMD Pyrenees TS1.1 support */
	if (td->id1 == 0xfe && td->id2 == 0x80 && td->id3 == 0x96) {
		r = pyrenees_init(dssdev);
		if (r)
			goto err;
	}

	r = pnd_sleep_out(td);
	if (r)
		goto err;

	/*
	 * HACK: SMD Pyrenees versions earlier than 0x85 require a long pause
	 * between sleep out and the first frame
	 */
	if (td->id1 == 0xfe && td->id3 == 0x96 && td->id2 < 0x85)
		msleep(100);

	/* Himalaya CS1.8B & CS1.9 has broken CABC */
	if (td->panel_config->type == PANEL_HIMALAYA &&
			(id2 == 0x91 || id2 == 0x92))
		td->cabc_broken = true;

	/* some Pyrenees panels have broken dimming */
	if (td->panel_config->type == PANEL_PYRENEES &&
		id1 == 0xc1 && (id2 == 0x80 || id2 == 0x81 || id2 == 0x82)) {
		if (!td->dimming_broken)
			sysfs_remove_file(&dssdev->dev.kobj,
					&dev_attr_dimming.attr);
		td->dimming_broken = true;
	}

	if (skip_init) {
		/* Display has been powered up, read brigtness and use it */
		r = pnd_dcs_read_1(DCS_READ_BRIGHTNESS, &brightness);
		if (r)
			goto err;
		td->bldev->props.brightness = brightness;
	}

	r = pnd_dcs_write_1(DCS_BRIGHTNESS, td->bldev->props.brightness);
	if (r)
		goto err;

	if (panel_data->partial_area.height) {
		r = pnd_set_partial_area(panel_data->partial_area.offset,
					panel_data->partial_area.height);
		if (r)
			goto err;

		r = pnd_dcs_write_0(DCS_PARTIAL_MODE_ON);
		if (r)
			goto err;
	}

	r = pnd_write_display_current_settings(dssdev);
	if (r)
		goto err;

	pnd_set_timer(dssdev);

	r = _pnd_enable_te(dssdev, td->te_enabled);
	if (r)
		goto err;

	td->enabled = true;

	if (!td->intro_printed) {
		dev_info(&dssdev->dev, "%s panel revision %02x.%02x.%02x\n",
			td->panel_config->name, id1, id2, id3);
		if (td->cabc_broken)
			dev_info(&dssdev->dev, "CABC disabled\n");
		if (td->dimming_broken)
			dev_info(&dssdev->dev, "dimming disabled\n");
		td->intro_printed = true;
	}

	omapdss_dsi_vc_enable_hs(TCH, true);

	/* First command in HS after power on sometimes fails on SMD Pyrenees */
	if (td->panel_config->type == PANEL_PYRENEES && id1 == 0xfe) {
		r = pnd_dcs_write_0(DCS_NOP);
		if (r)
			dev_warn(&dssdev->dev, "power on workaround\n");
	}

	pnd_queue_calibration_work(dssdev, CALIBRATE_POWER_ON_DELAY);

	return 0;
err:
	dev_err(&dssdev->dev, "error while enabling panel, issuing HW reset\n");

	pnd_hw_reset(dssdev);

	omapdss_dsi_display_disable(dssdev, false, false);
err0:
	/* Note: omapdss_dsi_display_disable() sets skip_first_init to false, so
	 * we need a copy of it */
	if (skip_init) {
		dev_err(&dssdev->dev, "retrying panel power on\n");
		r = pnd_power_on(dssdev);
	}

	return r;
}

static void pnd_power_off(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	/* do nothing if the powers are already off */
	if (!td->enabled)
		return;

	td->calibrating = false;

	r = pnd_dcs_write_0(DCS_DISPLAY_OFF);
	if (!r) {
		r = pnd_sleep_in(td);
		/* HACK: wait a bit so that the message goes through */
		msleep(10);
	}

	if (r) {
		dev_err(&dssdev->dev,
				"error disabling panel, issuing HW reset\n");
		pnd_hw_reset(dssdev);
	}

	omapdss_dsi_display_disable(dssdev, false, true);

	td->display_on = false;
	td->enabled = false;
}

static int pnd_panel_reset(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);

	dev_err(&dssdev->dev, "performing LCD reset\n");

	if (td->enabled) {
		td->calibrating = false;

		omapdss_dsi_display_disable(dssdev, false, false);

		td->display_on = false;
		td->enabled = false;
	}

	/* pnd_power_on will issue a HW reset for the panel */
	return pnd_power_on(dssdev);
}

static int pnd_enable(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "enable\n");

	down(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_DISABLED) {
		r = -EINVAL;
		goto err;
	}

	dsi_bus_lock();

	r = pnd_power_on(dssdev);

	dsi_bus_unlock();

	if (r)
		goto err;

	pnd_queue_esd_work(dssdev);

	dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;

	up(&td->lock);

	return 0;
err:
	dev_dbg(&dssdev->dev, "enable failed\n");
	up(&td->lock);
	return r;
}

static void pnd_disable(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);

	dev_dbg(&dssdev->dev, "disable\n");

	down(&td->lock);

	pnd_cancel_ulps_work(dssdev);
	pnd_cancel_esd_work(dssdev);
	pnd_cancel_calibration_work(dssdev);

	dsi_bus_lock();

	if (dssdev->state == OMAP_DSS_DISPLAY_ACTIVE) {
		pnd_wake_up(dssdev);
		pnd_power_off(dssdev);
	}

	dsi_bus_unlock();

	dssdev->state = OMAP_DSS_DISPLAY_DISABLED;

	up(&td->lock);
}

static int pnd_suspend(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "suspend\n");

	down(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE) {
		r = -EINVAL;
		goto err;
	}

	pnd_cancel_ulps_work(dssdev);
	pnd_cancel_esd_work(dssdev);
	pnd_cancel_calibration_work(dssdev);

	dsi_bus_lock();

	r = pnd_wake_up(dssdev);
	if (!r)
		pnd_power_off(dssdev);

	dsi_bus_unlock();

	dssdev->state = OMAP_DSS_DISPLAY_SUSPENDED;

	up(&td->lock);

	return 0;
err:
	up(&td->lock);
	return r;
}

static int pnd_resume(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "resume\n");

	down(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_SUSPENDED) {
		r = -EINVAL;
		goto err;
	}

	dsi_bus_lock();

	r = pnd_power_on(dssdev);

	dsi_bus_unlock();

	if (!r) {
		dssdev->state = OMAP_DSS_DISPLAY_ACTIVE;
		pnd_queue_esd_work(dssdev);
	}

	up(&td->lock);

	return r;
err:
	up(&td->lock);
	return r;
}



static void pnd_configure_update_region(struct omap_dss_device *dssdev,
		int frame_num)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	u16 dw, dh;
	u16 x, y, w, h;
	u16 ox, oy, ow, oh;
	bool split_horiz, split_vert;
	bool first_half;

	BUG_ON(frame_num != 0 && frame_num != 1);

	ox = td->original_update_region.x;
	oy = td->original_update_region.y;
	ow = td->original_update_region.w;
	oh = td->original_update_region.h;

	split_horiz = false;
	split_vert = false;
	first_half = false;

	switch (td->update_mode) {
	case PND_UPD_MODE_RL:
		first_half = frame_num != 0;
		split_vert = true;
		break;

	case PND_UPD_MODE_LR:
		first_half = frame_num == 0;
		split_vert = true;
		break;

	case PND_UPD_MODE_TB:
		first_half = frame_num == 0;
		split_horiz = true;
		break;

	case PND_UPD_MODE_BT:
		first_half = frame_num != 0;
		split_horiz = true;
		break;

	default:
		break;
	}

	/* use actual, not partial, display resolution for splitting */
	_pnd_get_resolution(dssdev, &dw, &dh, false);

	/* split offsets, half the display, always even */
	dw = (dw >> 1) & ~1;
	dh = (dh >> 1) & ~1;

	if (split_vert) {
		if (first_half) {
			if (ox >= dw) {
				x = y = w = h = 0;
			} else {
				x = ox;
				y = oy;
				w = min((u16)(dw - ox), ow);
				h = oh;
			}
		} else {
			if (ox + ow < dw) {
				x = y = w = h = 0;
			} else {
				x = max(dw, ox);
				y = oy;
				w = ox + ow - x;
				h = oh;
			}
		}
	} else if (split_horiz) {
		if (first_half) {
			if (oy >= dh) {
				x = y = w = h = 0;
			} else {
				x = ox;
				y = oy;
				w = ow;
				h = min((u16)(dh - oy), oh);
			}
		} else {
			if (oy + oh < dh) {
				x = y = w = h = 0;
			} else {
				x = ox;
				y = max(dh, oy);
				w = ow;
				h = oy + oh - y;
			}
		}
	} else {
		x = ox;
		y = oy;
		w = ow;
		h = oh;
	}

	td->update_region.x = x;
	td->update_region.y = y;
	td->update_region.w = w;
	td->update_region.h = h;
}

#ifdef MEASURE_PERF
static void pnd_print_perf(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	u32 p01, p12, p23, p34;
	u32 p03, p04;
	ktime_t p0 = td->perf.phase[0];
	ktime_t p1 = td->perf.phase[1];
	ktime_t p2 = td->perf.phase[2];
	ktime_t p3 = td->perf.phase[3];
	ktime_t p4 = td->perf.phase[4];
	char buf[256];
	char *p;
	bool warn = false;
	bool skip0, skip1;
#ifdef MEASURE_PERF_SHOW_ALWAYS
	const bool show = true;
#else
	const bool show = false;
#endif
	/* one frame takes 16.6ms on average, but we'll give some slack here */
	const int v = 17000 * (td->lpm_enabled ? 4 : 1);

	p01 = p12 = p23 = p34 = p03 = p04 = 0;

	switch (td->update_mode) {
	case PND_UPD_MODE_NORMAL:
		switch (td->update_te) {
		case PND_UPD_TE_NONE:
			p04 = (u32)ktime_to_us(ktime_sub(p4, p0));

			if (p04 > v)
				warn = true;
			break;

		case PND_UPD_TE_DSI:
			p04 = (u32)ktime_to_us(ktime_sub(p4, p0));

			if (p04 > v * 2)
				warn = true;
			break;

		case PND_UPD_TE_EXT:
			p03 = (u32)ktime_to_us(ktime_sub(p3, p0));
			p34 = (u32)ktime_to_us(ktime_sub(p4, p3));

			if (p03 > v || p34 > v)
				warn = true;
			break;

		default:
			BUG();
		}
		break;

	case PND_UPD_MODE_RL:
	case PND_UPD_MODE_LR:
	case PND_UPD_MODE_BT:
	case PND_UPD_MODE_TB:
		p01 = (u32)ktime_to_us(ktime_sub(p1, p0));
		p12 = (u32)ktime_to_us(ktime_sub(p2, p1));
		p23 = (u32)ktime_to_us(ktime_sub(p3, p2));
		p34 = (u32)ktime_to_us(ktime_sub(p4, p3));

		/* we don't need the results, we just need to know
		 * which half was skipped */
		pnd_configure_update_region(dssdev, 0);
		skip0 = td->update_region.w == 0 || td->update_region.h == 0;
		pnd_configure_update_region(dssdev, 1);
		skip1 = td->update_region.w == 0 || td->update_region.h == 0;

		if (skip0) {
			if (p23 > v)
				warn = true;

			if (p34 > v / 2)
				warn = true;
		} else if (skip1) {
			if (p01 > v + v / 2)
				warn = true;

			if (p12 > v / 2)
				warn = true;
		} else {
			if (p01 > v + v / 2)
				warn = true;

			if (p12 + p23 + p34 > v)
				warn = true;
		}

		/*
		 * HACK: SMD Pyrenees panels generate a few irregularly timed TE
		 * irqs after enabling TE, messing things up a bit. Suppress
		 * warnings right after enabling TE.
		 */
		if (atomic_read(&td->te_irq_count) < 5)
			warn = false;
		break;

	default:
		BUG();
	}

	if (!show && !warn)
		return;

	buf[0] = 0;
	p = buf;

	if (warn)
		p += sprintf(p, "warning, slow update: ");

	if (p01 > 0)
		p += sprintf(p, "p01(%u) ", p01);

	if (p12 > 0)
		p += sprintf(p, "p12(%u) ", p12);

	if (p23 > 0)
		p += sprintf(p, "p23(%u) ", p23);

	if (p03 > 0)
		p += sprintf(p, "p03(%u) ", p03);

	if (p34 > 0)
		p += sprintf(p, "p34(%u) ", p34);

	if (p04 > 0)
		p += sprintf(p, "p04(%u) ", p04);

	printk(KERN_WARNING "%s %s: %s\n", dev_driver_string(&dssdev->dev),
			dev_name(&dssdev->dev), buf);
}

static void mark_phase(struct omap_dss_device *dssdev, int num)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	td->perf.phase[num] = ktime_get();
}
#else
#define mark_phase(x, y)
#endif

static void pnd_update_error(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int old;

	old = atomic_xchg(&td->upd_state, PND_UPD_STATE_NONE);
	if (old != PND_UPD_STATE_NONE) {
		dev_err(&dssdev->dev, "update error, cleaning up\n");
		cancel_delayed_work(&td->frame_timeout_work);
		omap_dss_unlock_cache();

		pnd_panel_reset(dssdev);

		dsi_bus_unlock();
		up(&td->lock);
	} else {
		dev_warn(&dssdev->dev, "update error, already handled\n");
	}
}

static int pnd_prepare_update(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	u16 x = td->update_region.x;
	u16 y = td->update_region.y;
	u16 w = td->update_region.w;
	u16 h = td->update_region.h;
	int r;
	bool enlarge_ovls;

	enlarge_ovls = td->update_mode == PND_UPD_MODE_NORMAL;

	r = omap_dsi_prepare_update(dssdev, &x, &y, &w, &h, enlarge_ovls);
	if (r)
		return r;

	td->update_region.x = x;
	td->update_region.y = y;
	td->update_region.w = w;
	td->update_region.h = h;

	if (td->update_te == PND_UPD_TE_DSI)
		r = pnd_set_update_window_sync(dssdev, x, y, w, h);
	else
		r = pnd_set_update_window(dssdev, x, y, w, h);

	return r;
}

static int pnd_start_update(struct omap_dss_device *dssdev,
		void (*callback)(int, void *))
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	u16 x = td->update_region.x;
	u16 y = td->update_region.y;
	u16 w = td->update_region.w;
	u16 h = td->update_region.h;
	int r;

	r = omap_dsi_update(dssdev, TCH, x, y, w, h,
			callback, dssdev);
	return r;
}

static void pnd_framedone_cb_2(int err, void *data)
{
	struct omap_dss_device *dssdev = data;
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int old;

	mark_phase(dssdev, 4);

	if (err) {
		dev_err(&dssdev->dev, "framedone2 err %d\n", err);
		return; /* Let timeout handle this */
	}

	if (!td->display_on) {
		u8 cmd = DCS_DISPLAY_ON;

		err = dsi_vc_dcs_write_nosync(TCH, &cmd, 1);
		if (err) {
			dev_err(&dssdev->dev,
				"framedone2 display on err %d\n", err);
			return; /* Let timeout handle this */
		}

		/* TODO: Optimally we should use BTA to check that DISPLAY_ON
		 * really went through */

		td->display_on = true;
	}

	old = atomic_xchg(&td->upd_state, PND_UPD_STATE_NONE);
	if (old == PND_UPD_STATE_NONE) {
		dev_warn(&dssdev->dev, "timeout before framedone2\n");
		return; /* Timeout already handled this */
	}

	BUG_ON(old != PND_UPD_STATE_FRAME2_ONGOING);

	cancel_delayed_work(&td->frame_timeout_work);
	omap_dss_unlock_cache();

#ifdef MEASURE_PERF
	pnd_print_perf(dssdev);
#endif

	dsi_bus_unlock();
	up(&td->lock);
}

static void pnd_framedone_cb_1(int err, void *data)
{
	struct omap_dss_device *dssdev = data;
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int r;
	int old;
	bool skip;

	mark_phase(dssdev, 2);

	if (err) {
		dev_err(&dssdev->dev, "framedone1 err %d\n", err);
		return; /* Let timeout handle this */
	}

	pnd_configure_update_region(dssdev, 1);

	skip = td->update_region.w == 0 || td->update_region.h == 0;

	if (skip) {
		old = atomic_cmpxchg(&td->upd_state,
				PND_UPD_STATE_FRAME1_ONGOING,
				PND_UPD_STATE_FRAME2_ONGOING);
		if (old == PND_UPD_STATE_NONE) {
			dev_warn(&dssdev->dev, "timeout before framedone1\n");
			return; /* Timeout already handled this */
		}

		mark_phase(dssdev, 3);

		BUG_ON(old != PND_UPD_STATE_FRAME1_ONGOING);

		pnd_framedone_cb_2(0, data);
		return;
	}

	r = pnd_prepare_update(dssdev);
	if (r) {
		dev_err(&dssdev->dev,
				"prepare_update failed for second frame\n");
		goto err;
	}

	barrier();

	old = atomic_cmpxchg(&td->upd_state, PND_UPD_STATE_FRAME1_ONGOING,
			PND_UPD_STATE_FRAME2_WAIT);
	if (old == PND_UPD_STATE_NONE) {
		dev_warn(&dssdev->dev, "timeout before framedone1\n");
		return; /* Timeout already handled this */
	}

	BUG_ON(old != PND_UPD_STATE_FRAME1_ONGOING);

	return;
err:
	pnd_update_error(dssdev);
}

static void pnd_midscreen(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int old;
	int r;

	switch (td->update_mode) {
	case PND_UPD_MODE_RL:
	case PND_UPD_MODE_LR:
	case PND_UPD_MODE_BT:
	case PND_UPD_MODE_TB:
		break;
	default:
		return;
	}

	old = atomic_cmpxchg(&td->upd_state, PND_UPD_STATE_FRAME1_WAIT,
			PND_UPD_STATE_FRAME1_ONGOING);
	if (old == PND_UPD_STATE_FRAME1_WAIT) {
		mark_phase(dssdev, 1);
		r = pnd_start_update(dssdev, pnd_framedone_cb_1);
		if (r) {
			/* Let timeout handle this */
			dev_err(&dssdev->dev, "%s: start_update failed\n",
				__func__);
		}
	}
}

static void pnd_endscreen(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int old;
	int r;

	old = atomic_cmpxchg(&td->upd_state, PND_UPD_STATE_FRAME2_WAIT,
			PND_UPD_STATE_FRAME2_ONGOING);
	if (old == PND_UPD_STATE_FRAME2_WAIT) {
		mark_phase(dssdev, 3);
		r = pnd_start_update(dssdev, pnd_framedone_cb_2);
		if (r) {
			/* Let timeout handle this */
			dev_err(&dssdev->dev, "%s: start_update failed\n",
				__func__);
		}
	}
}

static enum hrtimer_restart pnd_timer_handler(struct hrtimer *handle)
{
	struct pnd_data *td = container_of(handle, struct pnd_data, hrtimer);
	struct omap_dss_device *dssdev = td->dssdev;

	if (td->reverse_te_irq_and_timer)
		pnd_endscreen(dssdev);
	else
		pnd_midscreen(dssdev);

	return HRTIMER_NORESTART;
}

static irqreturn_t pnd_te_isr(int irq, void *data)
{
	struct omap_dss_device *dssdev = data;
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);

	/* HACK: Skip the first two irqs after enabling TE. */
	if (atomic_inc_return(&td->te_irq_count) < 3)
		return IRQ_HANDLED;

	if (td->calibrating)
		pnd_calibrate(dssdev);

	hrtimer_start(&td->hrtimer, td->timer, HRTIMER_MODE_REL);

	if (td->reverse_te_irq_and_timer)
		pnd_midscreen(dssdev);
	else
		pnd_endscreen(dssdev);

	return IRQ_HANDLED;
}

static void pnd_frame_timeout_work_callback(struct work_struct *work)
{
	struct pnd_data *td = container_of(work, struct pnd_data,
			frame_timeout_work.work);
	struct omap_dss_device *dssdev = td->dssdev;

	dev_err(&dssdev->dev, "Frame not sent for 500ms!\n");
	pnd_update_error(dssdev);
}

static int pnd_update(struct omap_dss_device *dssdev,
		u16 x, u16 y, u16 w, u16 h)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	int r;
	int old;
	bool skip;

	dev_dbg(&dssdev->dev, "update %d, %d, %d x %d\n", x, y, w, h);

	down(&td->lock);
	dsi_bus_lock();

	r = pnd_wake_up(dssdev);
	if (r)
		goto err1;

	if (!td->enabled) {
		r = 0;
		goto err1;
	}

	if (!td->te_enabled) {
		td->update_te = PND_UPD_TE_NONE;
		td->update_mode = PND_UPD_MODE_NORMAL;
	} else if (panel_data->use_ext_te) {
		td->update_te = PND_UPD_TE_EXT;

		switch (td->rotate) {
		case 0:
			td->update_mode = PND_UPD_MODE_NORMAL;
			break;
		case 1:
			td->update_mode = PND_UPD_MODE_LR;
			break;
		case 2:
			td->update_mode = PND_UPD_MODE_BT;
			break;
		case 3:
			td->update_mode = PND_UPD_MODE_RL;
			break;
		}
	} else{
		td->update_te = PND_UPD_TE_DSI;
		td->update_mode = PND_UPD_MODE_NORMAL;
	}

	td->original_update_region.x = x;
	td->original_update_region.y = y;
	td->original_update_region.w = w;
	td->original_update_region.h = h;

	/* Before changing the upd_state state, take care of the extremely
	 * unlikely case that 1) timeout occurs, and the timeout handler is
	 * scheduled (but not run), 2) framedone_cb_2() is called on a
	 * successful, but rather delayed framedone, and sets upd_state to
	 * PND_UPD_STATE_NONE, 3) this function is called, and sets upd_state
	 * != PND_UPD_STATE_NONE below, and finally 4) timeout handler is run,
	 * messing up the update. */
	cancel_delayed_work_sync(&td->frame_timeout_work);

	/* Also take care of DSI timeout occurring after frame_timeout_work. */
	omap_dsi_cancel_update_sync(dssdev);

	omap_dss_lock_cache();

	mark_phase(dssdev, 0);

	pnd_configure_update_region(dssdev, 0);

	skip = td->update_region.w == 0 || td->update_region.h == 0;

	if (!skip) {
		r = pnd_prepare_update(dssdev);
		if (r)
			goto err2;
	}

	barrier();

	queue_delayed_work(td->err_wq, &td->frame_timeout_work,
			msecs_to_jiffies(500));

	if (skip) {
		old = atomic_xchg(&td->upd_state, PND_UPD_STATE_FRAME1_ONGOING);

		mark_phase(dssdev, 1);
		pnd_framedone_cb_1(0, dssdev);
	} else if (td->update_te == PND_UPD_TE_NONE ||
			td->update_te == PND_UPD_TE_DSI) {
		old = atomic_xchg(&td->upd_state, PND_UPD_STATE_FRAME2_ONGOING);

		r = pnd_start_update(dssdev, pnd_framedone_cb_2);
		if (r)
			goto err3;
	} else if (td->update_mode == PND_UPD_MODE_NORMAL) {
		old = atomic_xchg(&td->upd_state, PND_UPD_STATE_FRAME2_WAIT);
	} else {
		old = atomic_xchg(&td->upd_state, PND_UPD_STATE_FRAME1_WAIT);
	}

	BUG_ON(old != PND_UPD_STATE_NONE);

	/* Note: no dsi_bus_unlock or up here. The unlocking is done in
	 * pnd_framedone_cb_2() when the frame has been sent, or in
	 * pnd_update_error() if errors have occurred. */

	return 0;
err3:
	atomic_set(&td->upd_state, PND_UPD_STATE_NONE);
	cancel_delayed_work_sync(&td->frame_timeout_work);
err2:
	omap_dss_unlock_cache();
err1:
	dsi_bus_unlock();
	up(&td->lock);
	return r;
}

static int pnd_sync(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);

	dev_dbg(&dssdev->dev, "sync\n");

	down(&td->lock);
	dsi_bus_lock();
	dsi_bus_unlock();
	up(&td->lock);

	dev_dbg(&dssdev->dev, "sync done\n");

	return 0;
}

static int _pnd_enable_te(struct omap_dss_device *dssdev, bool enable)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	struct nokia_dsi_panel_data *panel_data = get_panel_data(dssdev);
	int r;

	if (enable) {
		atomic_set(&td->te_irq_count, 0);
		r = pnd_dcs_write_1(DCS_TEAR_ON, 0);
	} else {
		r = pnd_dcs_write_0(DCS_TEAR_OFF);
	}

	if (!panel_data->use_ext_te)
		omapdss_dsi_enable_te(dssdev, enable);

	if (td->panel_config->sleep.enable_te)
		msleep(td->panel_config->sleep.enable_te);

	return r;
}

static int pnd_enable_te(struct omap_dss_device *dssdev, bool enable)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	down(&td->lock);

	if (td->te_enabled == enable)
		goto end;

	dsi_bus_lock();

	if (td->enabled) {
		r = pnd_wake_up(dssdev);
		if (r)
			goto err;

		r = _pnd_enable_te(dssdev, enable);
		if (r)
			goto err;
	}

	td->te_enabled = enable;

	dsi_bus_unlock();
end:
	up(&td->lock);

	return 0;
err:
	dsi_bus_unlock();
	up(&td->lock);

	return r;
}

static int pnd_get_te(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	down(&td->lock);
	r = td->te_enabled;
	up(&td->lock);

	return r;
}

static int pnd_rotate(struct omap_dss_device *dssdev, u8 rotate)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "rotate %d\n", rotate);

	down(&td->lock);

	if (td->rotate == rotate)
		goto end;

	dsi_bus_lock();

	if (td->enabled) {
		r = pnd_wake_up(dssdev);
		if (r)
			goto err;

		r = pnd_set_addr_mode(dssdev, rotate, td->mirror);
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

static u8 pnd_get_rotate(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	down(&td->lock);
	r = td->rotate;
	up(&td->lock);

	return r;
}

static int pnd_mirror(struct omap_dss_device *dssdev, bool enable)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	dev_dbg(&dssdev->dev, "mirror %d\n", enable);

	down(&td->lock);

	if (td->mirror == enable)
		goto end;

	dsi_bus_lock();
	if (td->enabled) {
		r = pnd_wake_up(dssdev);
		if (r)
			goto err;

		r = pnd_set_addr_mode(dssdev, td->rotate, enable);
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

static bool pnd_get_mirror(struct omap_dss_device *dssdev)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	int r;

	down(&td->lock);
	r = td->mirror;
	up(&td->lock);

	return r;
}

static int pnd_run_test(struct omap_dss_device *dssdev, int test_num)
{
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);
	u8 id1, id2, id3;
	int r;

	down(&td->lock);

	if (!td->enabled) {
		r = -ENODEV;
		goto err1;
	}

	dsi_bus_lock();

	r = pnd_wake_up(dssdev);
	if (r)
		goto err2;

	r = pnd_dcs_read_1(DCS_GET_ID1, &id1);
	if (r)
		goto err2;
	r = pnd_dcs_read_1(DCS_GET_ID2, &id2);
	if (r)
		goto err2;
	r = pnd_dcs_read_1(DCS_GET_ID3, &id3);
	if (r)
		goto err2;

	dsi_bus_unlock();
	up(&td->lock);
	return 0;
err2:
	dsi_bus_unlock();
err1:
	up(&td->lock);
	return r;
}

static int pnd_memory_read(struct omap_dss_device *dssdev,
		void *buf, size_t size,
		u16 x, u16 y, u16 w, u16 h)
{
	int r;
	int first = 1;
	int plen;
	unsigned buf_used = 0;
	struct pnd_data *td = dev_get_drvdata(&dssdev->dev);

	if (size < w * h * 3)
		return -ENOMEM;

	down(&td->lock);

	if (!td->enabled) {
		r = -ENODEV;
		goto err1;
	}

	size = min(w * h * 3,
			dssdev->panel.timings.x_res *
			dssdev->panel.timings.y_res * 3);

	dsi_bus_lock();

	r = pnd_wake_up(dssdev);
	if (r)
		goto err2;

	/* plen 1 or 2 goes into short packet. until checksum error is fixed,
	 * use short packets. plen 32 works, but bigger packets seem to cause
	 * an error. */
	if (size % 2)
		plen = 1;
	else
		plen = 2;

	pnd_set_update_window_sync(dssdev, x, y, w, h);

	r = dsi_vc_set_max_rx_packet_size(TCH, plen);
	if (r)
		goto err2;

	while (buf_used < size) {
		u8 dcs_cmd = first ? 0x2e : 0x3e;
		first = 0;

		r = pnd_dcs_read(dcs_cmd, buf + buf_used, size - buf_used);

		if (r < 0) {
			dev_err(&dssdev->dev, "read error\n");
			goto err3;
		}

		buf_used += r;

		if (r < plen) {
			dev_err(&dssdev->dev, "short read\n");
			break;
		}

		if (signal_pending(current)) {
			dev_err(&dssdev->dev, "signal pending, "
					"aborting memory read\n");
			r = -ERESTARTSYS;
			goto err3;
		}
	}

	r = buf_used;

err3:
	dsi_vc_set_max_rx_packet_size(TCH, 1);
err2:
	dsi_bus_unlock();
err1:
	up(&td->lock);
	return r;
}

static void pnd_ulps_work(struct work_struct *work)
{
	struct pnd_data *td = container_of(work, struct pnd_data,
			ulps_work.work);
	struct omap_dss_device *dssdev = td->dssdev;

	down(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE || !td->enabled) {
		up(&td->lock);
		return;
	}

	dsi_bus_lock();

	pnd_enter_ulps(dssdev);

	dsi_bus_unlock();
	up(&td->lock);
}

static void pnd_esd_work(struct work_struct *work)
{
	struct pnd_data *td = container_of(work, struct pnd_data,
			esd_work.work);
	struct omap_dss_device *dssdev = td->dssdev;
	int ret;
	bool self_diag_fail;
	bool ulps;

	down(&td->lock);

	if (dssdev->state != OMAP_DSS_DISPLAY_ACTIVE)
		goto out;

	dsi_bus_lock();

	/* If ESD check wakes up from ULPS, go back to ULPS afterwards */
	ulps = td->ulps_enabled;

	ret = pnd_run_self_diag(dssdev, &self_diag_fail);
	if (ret || self_diag_fail)
		pnd_panel_reset(dssdev);
	else if (ulps)
		pnd_enter_ulps(dssdev);

	dsi_bus_unlock();
	pnd_queue_esd_work(dssdev);

out:
	up(&td->lock);
}

static int pnd_set_update_mode(struct omap_dss_device *dssdev,
		enum omap_dss_update_mode mode)
{
	if (mode != OMAP_DSS_UPDATE_MANUAL)
		return -EINVAL;
	return 0;
}

static enum omap_dss_update_mode pnd_get_update_mode(
		struct omap_dss_device *dssdev)
{
	return OMAP_DSS_UPDATE_MANUAL;
}

static struct omap_dss_driver pnd_driver = {
	.probe		= pnd_probe,
	.remove		= pnd_remove,

	.enable		= pnd_enable,
	.disable	= pnd_disable,
	.suspend	= pnd_suspend,
	.resume		= pnd_resume,

	.set_update_mode = pnd_set_update_mode,
	.get_update_mode = pnd_get_update_mode,

	.update		= pnd_update,
	.sync		= pnd_sync,

	.get_resolution	= pnd_get_resolution,
	.get_dimensions = pnd_get_dimensions,
	.get_recommended_bpp = omapdss_default_get_recommended_bpp,

	.enable_te	= pnd_enable_te,
	.get_te		= pnd_get_te,

	.set_rotate	= pnd_rotate,
	.get_rotate	= pnd_get_rotate,
	.set_mirror	= pnd_mirror,
	.get_mirror	= pnd_get_mirror,
	.run_test	= pnd_run_test,
	.memory_read	= pnd_memory_read,

	.get_timings	= pnd_get_timings,

	.driver         = {
		.name   = "panel-nokia-dsi",
		.owner  = THIS_MODULE,
	},
};

static int __init pnd_init(void)
{
	omap_dss_register_driver(&pnd_driver);

	return 0;
}

static void __exit pnd_exit(void)
{
	omap_dss_unregister_driver(&pnd_driver);
}

module_init(pnd_init);
module_exit(pnd_exit);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("Nokia DSI Panel Driver");
MODULE_LICENSE("GPL");
