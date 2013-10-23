/*
 * DMIF-S99AL-V225
 */

/*#define DEBUG*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/spi/spi.h>
#include <linux/jiffies.h>
#include <linux/sched.h>
#include <linux/backlight.h>
#include <linux/fb.h>

#include <mach/display.h>
#include <mach/dma.h>

#define MIPID_CMD_READ_DISP_ID		0x04
#define MIPID_CMD_READ_RED		0x06
#define MIPID_CMD_READ_GREEN		0x07
#define MIPID_CMD_READ_BLUE		0x08
#define MIPID_CMD_READ_DISP_STATUS	0x09
#define MIPID_CMD_RDDSDR		0x0F
#define MIPID_CMD_SLEEP_IN		0x10
#define MIPID_CMD_SLEEP_OUT		0x11
#define MIPID_CMD_DISP_OFF		0x28
#define MIPID_CMD_DISP_ON		0x29

#define MIPID_VER_LPH8923		3
#define MIPID_VER_LS041Y3		4
#define MIPID_VER_L4F00311		8

#ifdef DEBUG
#define DBG(format, ...) printk(KERN_DEBUG "Nevada: " format, ## __VA_ARGS__)
#else
#define DBG(format, ...)
#endif

struct nevada_device {
	struct backlight_device *bl_dev;
	int		enabled;
	int		model;
	int		revision;
	u8		display_id[3];
	unsigned int	saved_bklight_level;
	unsigned long	hw_guard_end;		/* next value of jiffies
						   when we can issue the
						   next sleep in/out command */
	unsigned long	hw_guard_wait;		/* max guard time in jiffies */

	struct spi_device	*spi;
	struct mutex		mutex;
	struct omap_panel	panel;
	struct omap_display	*display;
};


static void nevada_transfer(struct nevada_device *md, int cmd,
			      const u8 *wbuf, int wlen, u8 *rbuf, int rlen)
{
	struct spi_message	m;
	struct spi_transfer	*x, xfer[4];
	u16			w;
	int			r;

	BUG_ON(md->spi == NULL);

	spi_message_init(&m);

	memset(xfer, 0, sizeof(xfer));
	x = &xfer[0];

	cmd &=  0xff;
	x->tx_buf = &cmd;
	x->bits_per_word = 9;
	x->len = 2;
	spi_message_add_tail(x, &m);

	if (wlen) {
		x++;
		x->tx_buf = wbuf;
		x->len = wlen;
		x->bits_per_word = 9;
		spi_message_add_tail(x, &m);
	}

	if (rlen) {
		x++;
		x->rx_buf = &w;
		x->len = 1;
		spi_message_add_tail(x, &m);

		if (rlen > 1) {
			/* Arrange for the extra clock before the first
			 * data bit.
			 */
			x->bits_per_word = 9;
			x->len		 = 2;

			x++;
			x->rx_buf	 = &rbuf[1];
			x->len		 = rlen - 1;
			spi_message_add_tail(x, &m);
		}
	}

	r = spi_sync(md->spi, &m);
	if (r < 0)
		dev_dbg(&md->spi->dev, "spi_sync %d\n", r);

	if (rlen)
		rbuf[0] = w & 0xff;
}

static inline void nevada_cmd(struct nevada_device *md, int cmd)
{
	nevada_transfer(md, cmd, NULL, 0, NULL, 0);
}

static inline void nevada_write(struct nevada_device *md,
			       int reg, const u8 *buf, int len)
{
	nevada_transfer(md, reg, buf, len, NULL, 0);
}

static inline void nevada_read(struct nevada_device *md,
			      int reg, u8 *buf, int len)
{
	nevada_transfer(md, reg, NULL, 0, buf, len);
}

#if 0
static void send_init_string(struct nevada_device *md)
{
	u8 initpar1[] = { 0xa1, 0x90, 0x86, 0x00, 0x00, 0x00 };
	u8 initpar2[] = { 0xa0, 0x9f, 0x80, 0x8e, 0xae, 0x90, 0x8e, 0 };
	u8 initpar3[] = { 0x0c, 0x0c, 0x00, 0x00, 0x0a, 0x0a };

	DBG("nevada: sending init string\n");

	nevada_write(md, 0xc2, initpar1, sizeof(initpar1));
	nevada_write(md, 0xb8, initpar2, sizeof(initpar2));
	nevada_write(md, 0xc0, initpar3, sizeof(initpar3));
}
#endif

static void hw_guard_start(struct nevada_device *md, int guard_msec)
{
	md->hw_guard_wait = msecs_to_jiffies(guard_msec);
	md->hw_guard_end = jiffies + md->hw_guard_wait;
}

static void hw_guard_wait(struct nevada_device *md)
{
	unsigned long wait = md->hw_guard_end - jiffies;

	if ((long)wait > 0 && wait <= md->hw_guard_wait) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(wait);
	}
}

static void set_sleep_mode(struct nevada_device *md, int on)
{
	int cmd, sleep_time;

	if (on)
		cmd = MIPID_CMD_SLEEP_IN;
	else
		cmd = MIPID_CMD_SLEEP_OUT;
	hw_guard_wait(md);
	nevada_cmd(md, cmd);
	hw_guard_start(md, 120);
	/*
	 * When disabling the
	 * panel we'll sleep for the duration of 2 frames, so that the
	 * controller can still provide the PCLK,HS,VS signals. */
	if (on)
		sleep_time = 50;
	else
		sleep_time = 5;
	msleep(sleep_time);
}

static void set_display_state(struct nevada_device *md, int enabled)
{
	int cmd = enabled ? MIPID_CMD_DISP_ON : MIPID_CMD_DISP_OFF;

	nevada_cmd(md, cmd);
}



static int panel_enabled(struct nevada_device *md)
{
	u32 disp_status;
	int enabled;

	nevada_read(md, MIPID_CMD_READ_DISP_STATUS, (u8 *)&disp_status, 4);
	disp_status = __be32_to_cpu(disp_status);
	enabled = (disp_status & (1 << 17)) && (disp_status & (1 << 10));
	dev_dbg(&md->spi->dev,
		"LCD panel %s enabled by bootloader (status 0x%04x)\n",
		enabled ? "" : "not ", disp_status);
	DBG("status %#08x\n", disp_status);
	return enabled;
}

static void panel_test_dump(struct nevada_device *md)
{
	{
		u8 r, g, b;
		nevada_read(md, 0x6, &r, 1);
		nevada_read(md, 0x7, &g, 1);
		nevada_read(md, 0x8, &b, 1);
		DBG("rgb %x,%x,%x\n", r, g, b);

	}

	{
		u32 val;
		nevada_read(md, MIPID_CMD_READ_DISP_STATUS, (u8 *)&val, 4);
		val = __be32_to_cpu(val);
		DBG("status %#08x\n", val);
	}

	{
		u8 val;
		nevada_read(md, 0x5, (u8 *)&val, 1);
		DBG("parity errors %#x\n", val);
	}

	{
		u8 val;
		nevada_read(md, 0xc, (u8 *)&val, 1);
		DBG("pixformat %#x == 0x71\n", val);
	}
	{
		u8 val;
		nevada_read(md, 0xe, (u8 *)&val, 1);
		DBG("signal mode %#x: %s %s %s %s %s\n", val,
				(val & (1<<5)) ? "HS" : "",
				(val & (1<<4)) ? "VS" : "",
				(val & (1<<3)) ? "PC" : "",
				(val & (1<<2)) ? "DE" : "",
				(val & (1<<0)) ? "parity error" : ""
				);
	}
	{
		u8 val;
		nevada_read(md, 0xa, (u8 *)&val, 1);
		DBG("power mode %#x: %s %s %s %s %s\n", val,
				(val & (1<<7)) ? "Booster" : "",
				(val & (1<<5)) ? "Partial" : "",
				(val & (1<<4)) ? "SleepOut" : "SleepIn",
				(val & (1<<3)) ? "Normal" : "",
				(val & (1<<2)) ? "DispOn" : "DispOff"
				);
	}
}


static int panel_detect(struct nevada_device *md)
{
	nevada_read(md, MIPID_CMD_READ_DISP_ID, md->display_id, 3);
	dev_dbg(&md->spi->dev, "MIPI display ID: %02x%02x%02x\n",
		md->display_id[0], md->display_id[1], md->display_id[2]);

	DBG("MIPI display ID: %02x%02x%02x\n",
		md->display_id[0], md->display_id[1], md->display_id[2]);

	/* only TX is connected, we can't read from nevada */
#if 0
	switch (md->display_id[0]) {
	case 0xe3:
		md->model = MIPID_VER_L4F00311;
		md->panel.name = "nevada";
		break;
	default:
		md->panel.name = "unknown";
		dev_err(&md->spi->dev, "invalid display ID\n");
		return -ENODEV;
	}
#else
		md->model = MIPID_VER_L4F00311;
		md->panel.name = "nevada";
#endif

	md->revision = md->display_id[1];

	pr_info("omapfb: %s rev %02x LCD detected\n",
			md->panel.name, md->revision);

	return 0;
}



static int nevada_panel_enable(struct omap_display *display)
{
	int r;
	struct nevada_device *md =
		(struct nevada_device *)display->panel->priv;

	DBG("nevada_panel_enable\n");

	mutex_lock(&md->mutex);

	if (display->hw_config.panel_enable)
		display->hw_config.panel_enable(display);

	r = panel_detect(md);
	if (r) {
		mutex_unlock(&md->mutex);
		return r;
	}

	md->enabled = panel_enabled(md);

	if (md->enabled) {
		DBG("panel already enabled\n");
		; /*nevada_esd_start_check(md);*/
	} else {
		; /*md->saved_bklight_level = nevada_get_bklight_level(panel);*/
	}


	if (md->enabled) {
		mutex_unlock(&md->mutex);
		return 0;
	}

	/*nevada_cmd(md, 0x1);*/ /* SW reset */
	/*msleep(120);*/

	/*send_init_string(md);*/

	set_sleep_mode(md, 0);
	md->enabled = 1;

	/*panel_test_dump(md);*/

	/*for(r = 0; r < 500; r++)*/
	/*send_init_string(md);*/

	set_display_state(md, 1);
	/*nevada_set_bklight_level(panel, md->saved_bklight_level);*/

	panel_test_dump(md);
	nevada_cmd(md, 0x13); /* normal mode XXX */

	/*msleep(500);*/
	panel_test_dump(md);

	mutex_unlock(&md->mutex);
	return 0;
}

static void nevada_panel_disable(struct omap_display *display)
{
	struct nevada_device *md =
		(struct nevada_device *)display->panel->priv;

	DBG("nevada_panel_disable\n");

	mutex_lock(&md->mutex);

	if (!md->enabled) {
		mutex_unlock(&md->mutex);
		return;
	}
	/*md->saved_bklight_level = nevada_get_bklight_level(panel);*/
	/*nevada_set_bklight_level(panel, 0);*/

	if (display->hw_config.set_backlight)
		display->hw_config.set_backlight(display, 0);

	set_display_state(md, 0);
	set_sleep_mode(md, 1);
	md->enabled = 0;


	if (display->hw_config.panel_disable)
		display->hw_config.panel_disable(display);

	mutex_unlock(&md->mutex);
}

static int nevada_bl_update_status(struct backlight_device *dev)
{
	struct nevada_device *md = dev_get_drvdata(&dev->dev);
	struct omap_display *display = md->display;
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

static int nevada_bl_get_intensity(struct backlight_device *dev)
{
	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		return dev->props.brightness;

	return 0;
}

static struct backlight_ops omapbl_ops = {
	.get_brightness = nevada_bl_get_intensity,
	.update_status  = nevada_bl_update_status,
};

static int nevada_panel_init(struct omap_display *display)
{
	struct nevada_device *md =
		(struct nevada_device *)display->panel->priv;
	struct backlight_device *bldev;

	DBG("nevada_panel_init\n");

	mutex_init(&md->mutex);
	md->display = display;

	bldev = backlight_device_register("nevada", &md->spi->dev,
			md, &omapbl_ops);
	md->bl_dev = bldev;

	bldev->props.fb_blank = FB_BLANK_UNBLANK;
	bldev->props.power = FB_BLANK_UNBLANK;
	bldev->props.max_brightness = 127;
	bldev->props.brightness = 127;

	nevada_bl_update_status(bldev);

	return 0;
}

static int nevada_run_test(struct omap_display *display, int test_num)
{
	return 0;
}

static struct omap_panel nevada_panel = {
	.owner		= THIS_MODULE,
	.name		= "panel-nevada",
	.init		= nevada_panel_init,
	/*.remove	= nevada_cleanup,*/
	.enable		= nevada_panel_enable,
	.disable	= nevada_panel_disable,
	.run_test	= nevada_run_test,

	/*
	 * 640*360 = 230400 pixels
	 * 640*360*60 = 13824000 pixels per second
	 *
	 */
	.timings = {
		.x_res = 640,
		.y_res = 360,

		.pixel_clock	= ((640+4+4+4) * (360+4+4+4) * 60) / 1000,
		.hsw		= 2,
		.hfp		= 10,
		.hbp		= 20,

		.vsw		= 3,
		.vfp		= 3,
		.vbp		= 3,
	},
	.config		= OMAP_DSS_LCD_TFT,

	/* supported modes: 12bpp(444), 16bpp(565), 18bpp(666),  24bpp(888)
	 * resolutions */
};

static int nevada_spi_probe(struct spi_device *spi)
{
	struct nevada_device *md;

	DBG("nevada_spi_probe\n");

	md = kzalloc(sizeof(*md), GFP_KERNEL);
	if (md == NULL) {
		dev_err(&spi->dev, "out of memory\n");
		return -ENOMEM;
	}

	spi->mode = SPI_MODE_0;
	md->spi = spi;
	dev_set_drvdata(&spi->dev, md);
	md->panel = nevada_panel;
	nevada_panel.priv = md;

	omap_dss_register_panel(&nevada_panel);

	return 0;
}

static int nevada_spi_remove(struct spi_device *spi)
{
	struct nevada_device *md = dev_get_drvdata(&spi->dev);
	struct backlight_device *dev = md->bl_dev;

	DBG("nevada_spi_remove\n");

	backlight_device_unregister(dev);
	omap_dss_unregister_panel(&nevada_panel);

	/*nevada_disable(&md->panel);*/
	kfree(md);

	return 0;
}

static struct spi_driver nevada_spi_driver = {
	.driver = {
		.name	= "nevada",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe	= nevada_spi_probe,
	.remove	= __devexit_p(nevada_spi_remove),
};

static int __init nevada_init(void)
{
	DBG("nevada_init\n");
	return spi_register_driver(&nevada_spi_driver);
}

static void __exit nevada_exit(void)
{
	DBG("nevada_exit\n");
	spi_unregister_driver(&nevada_spi_driver);
}

module_init(nevada_init);
module_exit(nevada_exit);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("Caucasus LCD Driver");
MODULE_LICENSE("GPL");
