/*
 * Taal
 */

/*#define DEBUG*/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/backlight.h>
#include <linux/fb.h>

#include <mach/display.h>

/* DSI Virtual channel. Hardcoded for now. */
#define TCH 0

#define DCS_READ_POWER_MODE	0x0a
#define DCS_READ_MADCTL		0x0b
#define DCS_READ_PIXEL_FORMAT	0x0c
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
#define DCS_GET_ID1		0xda
#define DCS_GET_ID2		0xdb
#define DCS_GET_ID3		0xdc

#ifdef DEBUG
#define DBG(format, ...) printk(KERN_DEBUG "Taal: " format, ## __VA_ARGS__)
#else
#define DBG(format, ...)
#endif

struct taal_data {
	struct backlight_device *bldev;

	unsigned long	hw_guard_end;	/* next value of jiffies when we can
					 * issue the next sleep in/out command
					 */
	unsigned long	hw_guard_wait;	/* max guard time in jiffies */

	struct omap_display *display;

	bool enabled;
	u8 rotate;
	bool mirror;
};

static int taal_dcs_read_1(u8 dcs_cmd, u8 *data)
{
	int r;
	u8 buf[1];

	r = dsi_vc_dcs_read(TCH, dcs_cmd, buf, 1);

	if (r < 0) {
		printk(KERN_ERR "Taal read error\n");
		return r;
	}

	*data = buf[0];

	return 0;
}

static int taal_dcs_write_0(u8 dcs_cmd)
{
	return dsi_vc_dcs_write(TCH, &dcs_cmd, 1);
}

static int taal_dcs_write_1(u8 dcs_cmd, u8 param)
{
	u8 buf[2];
	buf[0] = dcs_cmd;
	buf[1] = param;
	return dsi_vc_dcs_write(TCH, buf, 2);
}

static void taal_get_timings(struct omap_display *display,
			struct omap_video_timings *timings)
{
	*timings = display->panel->timings;
}

static void taal_get_resolution(struct omap_display *display,
		u16 *xres, u16 *yres)
{
	struct taal_data *td = (struct taal_data *)display->ctrl->priv;

	if (td->rotate == 0 || td->rotate == 2) {
		*xres = display->panel->timings.x_res;
		*yres = display->panel->timings.y_res;
	} else {
		*yres = display->panel->timings.x_res;
		*xres = display->panel->timings.y_res;
	}
}

static int taal_ctrl_init(struct omap_display *display)
{
	struct taal_data *td;

	DBG("taal_ctrl_init\n");

	td = kzalloc(sizeof(*td), GFP_KERNEL);
	if (td == NULL)
		return -ENOMEM;

	td->display = display;
	display->ctrl->priv = td;

	display->get_timings = taal_get_timings;

	display->get_resolution = taal_get_resolution;

	return 0;
}

static void taal_ctrl_cleanup(struct omap_display *display)
{
	if (display->ctrl->priv)
		kfree(display->ctrl->priv);
}

static void hw_guard_start(struct taal_data *td, int guard_msec)
{
	td->hw_guard_wait = msecs_to_jiffies(guard_msec);
	td->hw_guard_end = jiffies + td->hw_guard_wait;
}

static void hw_guard_wait(struct taal_data *td)
{
	unsigned long wait = td->hw_guard_end - jiffies;

	if ((long)wait > 0 && wait <= td->hw_guard_wait) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(wait);
	}
}

static int taal_sleep_enable(struct omap_display *display, bool enable)
{
	struct taal_data *td = (struct taal_data *)display->ctrl->priv;
	u8 cmd;
	int r;

	hw_guard_wait(td);

	if (enable) {
		cmd = DCS_SLEEP_IN;
		r = dsi_vc_dcs_write_nosync(TCH, &cmd, 1);
	} else {
		cmd = DCS_SLEEP_OUT;
		r = dsi_vc_dcs_write(TCH, &cmd, 1);
	}

	if (r)
		return r;

	hw_guard_start(td, 120);

	r = dsi_vc_send_null(TCH);
	if (r)
		return r;

	msleep(5);

	return 0;
}

static int taal_get_id(void)
{
	u8 id1, id2, id3;
	int r;

	r = taal_dcs_read_1(DCS_GET_ID1, &id1);
	if (r)
		return r;
	r = taal_dcs_read_1(DCS_GET_ID2, &id2);
	if (r)
		return r;
	r = taal_dcs_read_1(DCS_GET_ID3, &id3);
	if (r)
		return r;

	return 0;
}

static int taal_set_addr_mode(u8 rotate, bool mirror)
{
	int r;
	u8 mode;
	int b5, b6, b7;

	r = taal_dcs_read_1(DCS_READ_MADCTL, &mode);
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

	return taal_dcs_write_1(DCS_MEM_ACC_CTRL, mode);
}

static int taal_ctrl_enable(struct omap_display *display)
{
	struct taal_data *td = (struct taal_data *)display->ctrl->priv;
	int r;

	DBG("taal_ctrl_enable\n");

	if (display->hw_config.ctrl_enable) {
		r = display->hw_config.ctrl_enable(display);
		if (r)
			return r;
	}

	/* it seems we have to wait a bit until taal is ready */
	msleep(5);

	r = taal_sleep_enable(display, 0);
	if (r)
		return r;

	r = taal_get_id();
	if (r)
		return r;

	taal_dcs_write_1(DCS_PIXEL_FORMAT, 0x7); /* 24bit/pixel */

	taal_set_addr_mode(td->rotate, td->mirror);

	taal_dcs_write_0(DCS_DISPLAY_ON);

	td->enabled = 1;

	return 0;
}

static void taal_ctrl_disable(struct omap_display *display)
{
	struct taal_data *td = (struct taal_data *)display->ctrl->priv;

	taal_dcs_write_0(DCS_DISPLAY_OFF);
	taal_sleep_enable(display, 1);

	/* wait a bit so that the message goes through */
	msleep(10);

	if (display->hw_config.ctrl_disable)
		display->hw_config.ctrl_disable(display);

	td->enabled = 0;
}

static void taal_ctrl_setup_update(struct omap_display *display,
				    u16 x, u16 y, u16 w, u16 h)
{
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

	dsi_vc_dcs_write(TCH, buf, sizeof(buf));

	buf[0] = DCS_PAGE_ADDR;
	buf[1] = (y1 >> 8) & 0xff;
	buf[2] = (y1 >> 0) & 0xff;
	buf[3] = (y2 >> 8) & 0xff;
	buf[4] = (y2 >> 0) & 0xff;

	dsi_vc_dcs_write(TCH, buf, sizeof(buf));
}

static int taal_ctrl_enable_te(struct omap_display *display, bool enable)
{
	u8 buf[2];

	if (enable) {
		buf[0] = DCS_TEAR_ON;
		buf[1] = 0;	/* only vertical sync */
		dsi_vc_dcs_write(TCH, buf, 2);
	} else {
		buf[0] = DCS_TEAR_OFF;
		dsi_vc_dcs_write(TCH, buf, 1);
	}

	return 0;
}

static int taal_ctrl_rotate(struct omap_display *display, u8 rotate)
{
	struct taal_data *td = (struct taal_data *)display->ctrl->priv;
	int r;

	DBG("taal_ctrl_rotate %d\n", rotate);

	if (td->enabled) {
		r = taal_set_addr_mode(rotate, td->mirror);

		if (r)
			return r;
	}

	td->rotate = rotate;

	return 0;
}

static u8 taal_ctrl_get_rotate(struct omap_display *display)
{
	struct taal_data *td = (struct taal_data *)display->ctrl->priv;
	return td->rotate;
}

static int taal_ctrl_mirror(struct omap_display *display, bool enable)
{
	struct taal_data *td = (struct taal_data *)display->ctrl->priv;
	int r;

	DBG("taal_ctrl_mirror %d\n", enable);

	if (td->enabled) {
		r = taal_set_addr_mode(td->rotate, enable);

		if (r)
			return r;
	}

	td->mirror = enable;

	return 0;
}

static bool taal_ctrl_get_mirror(struct omap_display *display)
{
	struct taal_data *td = (struct taal_data *)display->ctrl->priv;
	return td->mirror;
}

static int taal_run_test(struct omap_display *display, int test_num)
{
	u8 id1, id2, id3;
	int r;

	r = taal_dcs_read_1(DCS_GET_ID1, &id1);
	if (r)
		return r;
	r = taal_dcs_read_1(DCS_GET_ID2, &id2);
	if (r)
		return r;
	r = taal_dcs_read_1(DCS_GET_ID3, &id3);
	if (r)
		return r;

	return 0;
}

static int taal_ctrl_memory_read(struct omap_display *display,
		void *buf, size_t size,
		u16 x, u16 y, u16 w, u16 h)
{
	int r;
	int first = 1;
	int plen;
	unsigned buf_used = 0;

	if (size < w * h * 3)
		return -ENOMEM;

	size = min(w * h * 3,
			display->panel->timings.x_res *
			display->panel->timings.y_res * 3);

	/* plen 1 or 2 goes into short packet. until checksum error is fixed, use
	 * short packets. plen 32 works, but bigger packets seem to cause an
	 * error. */
	if (size % 2)
		plen = 1;
	else
		plen = 2;

	taal_ctrl_setup_update(display, x, y, w, h);

	r = dsi_vc_set_max_rx_packet_size(TCH, plen);
	if (r)
		return r;

	while (buf_used < size) {
		u8 dcs_cmd = first ? 0x2e : 0x3e;
		first = 0;

		r = dsi_vc_dcs_read(TCH, dcs_cmd,
				buf + buf_used, size - buf_used);

		if (r < 0) {
			printk(KERN_ERR "Taal read error\n");
			goto err;
		}

		buf_used += r;

		if (r < plen) {
			printk("short read\n");
			break;
		}
	}

	r = buf_used;

err:
	dsi_vc_set_max_rx_packet_size(TCH, 1);

	return r;
}

static struct omap_ctrl taal_ctrl = {
	.owner = THIS_MODULE,
	.name = "ctrl-taal",
	.init = taal_ctrl_init,
	.cleanup = taal_ctrl_cleanup,
	.enable = taal_ctrl_enable,
	.disable = taal_ctrl_disable,
	.setup_update = taal_ctrl_setup_update,
	.enable_te = taal_ctrl_enable_te,
	.set_rotate = taal_ctrl_rotate,
	.get_rotate = taal_ctrl_get_rotate,
	.set_mirror = taal_ctrl_mirror,
	.get_mirror = taal_ctrl_get_mirror,
	.run_test = taal_run_test,
	.memory_read = taal_ctrl_memory_read,
	.pixel_size = 24,
};


/* PANEL */
static int taal_bl_update_status(struct backlight_device *dev)
{
	struct omap_display *display = dev_get_drvdata(&dev->dev);
	int r;
	int level;

	if (!display->hw_config.set_backlight)
		return -EINVAL;

	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		level = dev->props.brightness;
	else
		level = 0;

	r = display->hw_config.set_backlight(display, level);
	if (r)
		return r;

	return 0;
}

static int taal_bl_get_intensity(struct backlight_device *dev)
{
	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		return dev->props.brightness;

	return 0;
}

static struct backlight_ops taal_bl_ops = {
	.get_brightness = taal_bl_get_intensity,
	.update_status  = taal_bl_update_status,
};

static int taal_panel_init(struct omap_display *display)
{
	struct taal_data *td = (struct taal_data *)display->ctrl->priv;
	struct backlight_device *bldev;

	BUG_ON(display->ctrl->priv == NULL);

	bldev = backlight_device_register("taal", NULL, display, &taal_bl_ops);
	td->bldev = bldev;

	bldev->props.fb_blank = FB_BLANK_UNBLANK;
	bldev->props.power = FB_BLANK_UNBLANK;
	bldev->props.max_brightness = 127;
	bldev->props.brightness = 127;

	taal_bl_update_status(bldev);

	return 0;
}

static void taal_panel_cleanup(struct omap_display *display)
{
	struct taal_data *td = (struct taal_data *)display->ctrl->priv;
	struct backlight_device *bldev = td->bldev;

	bldev->props.power = FB_BLANK_POWERDOWN;
	taal_bl_update_status(bldev);

	backlight_device_unregister(bldev);
}

static int taal_panel_enable(struct omap_display *display)
{
	return 0;
}

static void taal_panel_disable(struct omap_display *display)
{
}

static int taal_panel_suspend(struct omap_display *display)
{
	struct taal_data *td = (struct taal_data *)display->ctrl->priv;
	struct backlight_device *bldev = td->bldev;

	bldev->props.power = FB_BLANK_POWERDOWN;
	taal_bl_update_status(bldev);

	return 0;
}

static int taal_panel_resume(struct omap_display *display)
{
	struct taal_data *td = (struct taal_data *)display->ctrl->priv;
	struct backlight_device *bldev = td->bldev;

	bldev->props.power = FB_BLANK_UNBLANK;
	taal_bl_update_status(bldev);

	return 0;
}

static struct omap_panel taal_panel = {
	.owner		= THIS_MODULE,
	.name		= "panel-taal",
	.init		= taal_panel_init,
	.cleanup	= taal_panel_cleanup,
	.enable		= taal_panel_enable,
	.disable	= taal_panel_disable,
	.suspend	= taal_panel_suspend,
	.resume		= taal_panel_resume,

	.config		= OMAP_DSS_LCD_TFT,

	.timings = {
		.x_res = 864,
		.y_res = 480,
	},
};

static int __init taal_init(void)
{
	DBG("taal_init\n");

	omap_dss_register_ctrl(&taal_ctrl);
	omap_dss_register_panel(&taal_panel);

	return 0;
}

static void __exit taal_exit(void)
{
	DBG("taal_exit\n");

	omap_dss_unregister_panel(&taal_panel);
	omap_dss_unregister_ctrl(&taal_ctrl);
}

module_init(taal_init);
module_exit(taal_exit);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("Taal Driver");
MODULE_LICENSE("GPL");
