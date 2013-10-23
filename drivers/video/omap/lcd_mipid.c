/*
 * LCD driver for MIPI DBI-C / DCS compatible LCDs
 *
 * Copyright (C) 2006 Nokia Corporation
 * Author: Imre Deak <imre.deak@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/spi/spi.h>
#include <linux/omapfb.h>

#include <mach/lcd_mipid.h>

#define MIPID_MODULE_NAME		"lcd_mipid"

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
#define MIPID_CMD_WRITE_DISP_BRIGHTNESS	0x51
#define MIPID_CMD_READ_DISP_BRIGHTNESS	0x52
#define MIPID_CMD_WRITE_CTRL_DISP	0x53

#define CTRL_DISP_BRIGHTNESS_CTRL_ON	(1 << 5)
#define CTRL_DISP_AMBIENT_LIGHT_CTRL_ON	(1 << 4)
#define CTRL_DISP_BACKLIGHT_ON		(1 << 2)
#define CTRL_DISP_AUTO_BRIGHTNESS_ON	(1 << 1)

#define MIPID_CMD_READ_CTRL_DISP	0x54
#define MIPID_CMD_WRITE_CABC		0x55
#define MIPID_CMD_READ_CABC		0x56

#define MIPID_VER_LPH8923		1
#define MIPID_VER_LS041Y3		2
#define MIPID_VER_L4F00311		3
#define MIPID_VER_ACX565AKM		4

#define MIPID_ESD_CHECK_PERIOD		msecs_to_jiffies(5000)

#define to_mipid_device(p)		container_of(p, struct mipid_device, \
						panel)
struct mipid_device {
	struct backlight_device *bl_dev;
	int		enabled;
	int		model;
	int		revision;
	u8		display_id[3];
	int		has_bc:1;
	int		has_cabc:1;
	unsigned long	hw_guard_end;		/* next value of jiffies
						   when we can issue the
						   next sleep in/out command */
	unsigned long	hw_guard_wait;		/* max guard time in jiffies */

	struct omapfb_device	*fbdev;
	struct spi_device	*spi;
	struct mutex		mutex;
	struct lcd_panel	panel;

	struct workqueue_struct	*esd_wq;
	struct delayed_work	esd_work;
	void			(*esd_check)(struct mipid_device *m);
};

static void mipid_transfer(struct mipid_device *md, int cmd, const u8 *wbuf,
			   int wlen, u8 *rbuf, int rlen)
{
	struct spi_message	m;
	struct spi_transfer	*x, xfer[5];
	int			r;

	BUG_ON(md->spi == NULL);

	spi_message_init(&m);

	memset(xfer, 0, sizeof(xfer));
	x = &xfer[0];

	cmd &=  0xff;
	x->tx_buf	= &cmd;
	x->bits_per_word= 9;
	x->len		= 2;

	if (rlen > 1 && wlen == 0) {
		/*
		 * Between the command and the response data there is a
		 * dummy clock cycle. Add an extra bit after the command
		 * word to account for this.
		 */
		x->bits_per_word = 10;
		cmd <<= 1;
	}
	spi_message_add_tail(x, &m);

	if (wlen) {
		x++;
		x->tx_buf	= wbuf;
		x->len		= wlen;
		x->bits_per_word= 9;
		spi_message_add_tail(x, &m);
	}

	if (rlen) {
		x++;
		x->rx_buf	= rbuf;
		x->len		= rlen;
		spi_message_add_tail(x, &m);
	}

	r = spi_sync(md->spi, &m);
	if (r < 0)
		dev_dbg(&md->spi->dev, "spi_sync %d\n", r);
}

static inline void mipid_cmd(struct mipid_device *md, int cmd)
{
	mipid_transfer(md, cmd, NULL, 0, NULL, 0);
}

static inline void mipid_write(struct mipid_device *md,
			       int reg, const u8 *buf, int len)
{
	mipid_transfer(md, reg, buf, len, NULL, 0);
}

static inline void mipid_read(struct mipid_device *md,
			      int reg, u8 *buf, int len)
{
	mipid_transfer(md, reg, NULL, 0, buf, len);
}

static void set_data_lines(struct mipid_device *md, int data_lines)
{
	u16 par;

	switch (data_lines) {
	case 16:
		par = 0x150;
		break;
	case 18:
		par = 0x160;
		break;
	case 24:
		par = 0x170;
		break;
	}
	mipid_write(md, 0x3a, (u8 *)&par, 2);
}

static void send_init_string(struct mipid_device *md)
{
	u16 initpar[] = { 0x0102, 0x0100, 0x0100 };

	mipid_write(md, 0xc2, (u8 *)initpar, sizeof(initpar));
	set_data_lines(md, md->panel.data_lines);
}

static void hw_guard_start(struct mipid_device *md, int guard_msec)
{
	md->hw_guard_wait = msecs_to_jiffies(guard_msec);
	md->hw_guard_end = jiffies + md->hw_guard_wait;
}

static void hw_guard_wait(struct mipid_device *md)
{
	unsigned long wait = md->hw_guard_end - jiffies;

	if ((long)wait > 0 && wait <= md->hw_guard_wait) {
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout(wait);
	}
}

static void set_sleep_mode(struct mipid_device *md, int on)
{
	int cmd, sleep_time = 50;

	if (on)
		cmd = MIPID_CMD_SLEEP_IN;
	else
		cmd = MIPID_CMD_SLEEP_OUT;
	hw_guard_wait(md);
	mipid_cmd(md, cmd);
	hw_guard_start(md, 120);
	/*
	 * When we enable the panel, it seems we _have_ to sleep
	 * 120 ms before sending the init string. When disabling the
	 * panel we'll sleep for the duration of 2 frames, so that the
	 * controller can still provide the PCLK,HS,VS signals. */
	if (!on)
		sleep_time = 120;
	msleep(sleep_time);
}

static void set_display_state(struct mipid_device *md, int enabled)
{
	int cmd = enabled ? MIPID_CMD_DISP_ON : MIPID_CMD_DISP_OFF;

	mipid_cmd(md, cmd);
}

static void enable_backlight_ctrl(struct mipid_device *md, int enable)
{
	u16 ctrl;

	mipid_read(md, MIPID_CMD_READ_CTRL_DISP, (u8 *)&ctrl, 1);
	if (enable) {
		ctrl |= CTRL_DISP_BRIGHTNESS_CTRL_ON |
			CTRL_DISP_BACKLIGHT_ON;
	} else {
		ctrl &= ~(CTRL_DISP_BRIGHTNESS_CTRL_ON |
			  CTRL_DISP_BACKLIGHT_ON);
	}

	ctrl |= 1 << 8;
	mipid_write(md, MIPID_CMD_WRITE_CTRL_DISP, (u8 *)&ctrl, 2);
}

static void mipid_set_brightness(struct mipid_device *md, int level)
{
	int bv;

	bv = level | (1 << 8);
	mipid_write(md, MIPID_CMD_WRITE_DISP_BRIGHTNESS, (u8 *)&bv, 2);

	if (level)
		enable_backlight_ctrl(md, 1);
	else
		enable_backlight_ctrl(md, 0);
}

static int mipid_get_actual_brightness(struct mipid_device *md)
{
	u8 bv;

	mipid_read(md, MIPID_CMD_READ_DISP_BRIGHTNESS, &bv, 1);

	return bv;
}

static int mipid_bl_update_status(struct backlight_device *dev)
{
	struct mipid_device *md = dev_get_drvdata(&dev->dev);
	struct mipid_platform_data *pd = md->spi->dev.platform_data;
	int r;
	int level;

	dev_dbg(&md->spi->dev, "%s\n", __func__);

	mutex_lock(&md->mutex);

	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK)
		level = dev->props.brightness;
	else
		level = 0;

	r = 0;
	if (md->has_bc)
		mipid_set_brightness(md, level);
	else
		if (pd->set_bklight_level != NULL)
			pd->set_bklight_level(pd, level);
	else
		r = -ENODEV;

	mutex_unlock(&md->mutex);

	return r;
}

static int mipid_bl_get_intensity(struct backlight_device *dev)
{
	struct mipid_device *md = dev_get_drvdata(&dev->dev);
	struct mipid_platform_data *pd = md->spi->dev.platform_data;

	dev_dbg(&dev->dev, "%s\n", __func__);

	if (!md->has_bc && pd->set_bklight_level == NULL)
		return -ENODEV;

	if (dev->props.fb_blank == FB_BLANK_UNBLANK &&
			dev->props.power == FB_BLANK_UNBLANK) {
		if (md->has_bc)
			return mipid_get_actual_brightness(md);
		else
			return dev->props.brightness;
	}

	return 0;
}

static struct backlight_ops mipid_bl_ops = {
	.get_brightness = mipid_bl_get_intensity,
	.update_status  = mipid_bl_update_status,
};

/* Old backlight interface, to be removed */
static int mipid_set_bklight_level(struct lcd_panel *panel, unsigned int level)
{
	struct mipid_device *md = to_mipid_device(panel);
	struct backlight_device *bldev = md->bl_dev;

	if (level > bldev->props.max_brightness)
		return -EINVAL;

	bldev->props.brightness = level;
	return mipid_bl_update_status(bldev);
}

static unsigned int mipid_get_bklight_level(struct lcd_panel *panel)
{
	struct mipid_device *md = to_mipid_device(panel);
	struct backlight_device *bldev = md->bl_dev;

	return bldev->props.brightness;
}

static unsigned int mipid_get_bklight_max(struct lcd_panel *panel)
{
	struct mipid_device *md = to_mipid_device(panel);
	struct backlight_device *bldev = md->bl_dev;

	return bldev->props.max_brightness;
}

static unsigned long mipid_get_caps(struct lcd_panel *panel)
{
	return OMAPFB_CAPS_SET_BACKLIGHT;
}

static u16 read_first_pixel(struct mipid_device *md)
{
	u16 pixel;
	u8 red, green, blue;

	mutex_lock(&md->mutex);
	mipid_read(md, MIPID_CMD_READ_RED, &red, 1);
	mipid_read(md, MIPID_CMD_READ_GREEN, &green, 1);
	mipid_read(md, MIPID_CMD_READ_BLUE, &blue, 1);
	mutex_unlock(&md->mutex);

	switch (md->panel.data_lines) {
	case 16:
		pixel = ((red >> 1) << 11) | (green << 5) | (blue >> 1);
		break;
	case 24:
		/* 24 bit -> 16 bit */
		pixel = ((red >> 3) << 11) | ((green >> 2) << 5) |
			(blue >> 3);
		break;
	default:
		pixel = 0;
		BUG();
	}

	return pixel;
}

static int mipid_run_test(struct lcd_panel *panel, int test_num)
{
	struct mipid_device *md = to_mipid_device(panel);
	static const u16 test_values[4] = {
		0x0000, 0xffff, 0xaaaa, 0x5555,
	};
	int i;

	if (test_num != MIPID_TEST_RGB_LINES)
		return MIPID_TEST_INVALID;

	for (i = 0; i < ARRAY_SIZE(test_values); i++) {
		int delay;
		unsigned long tmo;

		omapfb_write_first_pixel(md->fbdev, test_values[i]);
		tmo = jiffies + msecs_to_jiffies(100);
		delay = 25;
		while (1) {
			u16 pixel;

			msleep(delay);
			pixel = read_first_pixel(md);
			if (pixel == test_values[i])
				break;
			if (time_after(jiffies, tmo)) {
				dev_err(&md->spi->dev,
					"MIPI LCD RGB I/F test failed: "
					"expecting %04x, got %04x\n",
					test_values[i], pixel);
				return MIPID_TEST_FAILED;
			}
			delay = 10;
		}
	}

	return 0;
}

static void ls041y3_esd_recover(struct mipid_device *md)
{
	dev_err(&md->spi->dev, "performing LCD ESD recovery\n");
	set_sleep_mode(md, 1);
	set_sleep_mode(md, 0);
}

static void ls041y3_esd_check_mode1(struct mipid_device *md)
{
	u8 state1, state2;

	mipid_read(md, MIPID_CMD_RDDSDR, &state1, 1);
	set_sleep_mode(md, 0);
	mipid_read(md, MIPID_CMD_RDDSDR, &state2, 1);
	dev_dbg(&md->spi->dev, "ESD mode 1 state1 %02x state2 %02x\n",
		state1, state2);
	/* Each sleep out command will trigger a self diagnostic and flip
	* Bit6 if the test passes.
	*/
	if (!((state1 ^ state2) & (1 << 6)))
		ls041y3_esd_recover(md);
}

static void ls041y3_esd_check_mode2(struct mipid_device *md)
{
	int i;
	u8 rbuf[2];
	static const struct {
		int	cmd;
		int	wlen;
		u16	wbuf[3];
	} *rd, rd_ctrl[7] = {
		{ 0xb0, 4, { 0x0101, 0x01fe, } },
		{ 0xb1, 4, { 0x01de, 0x0121, } },
		{ 0xc2, 4, { 0x0100, 0x0100, } },
		{ 0xbd, 2, { 0x0100, } },
		{ 0xc2, 4, { 0x01fc, 0x0103, } },
		{ 0xb4, 0, },
		{ 0x00, 0, },
	};

	rd = rd_ctrl;
	for (i = 0; i < 3; i++, rd++)
		mipid_write(md, rd->cmd, (u8 *)rd->wbuf, rd->wlen);

	udelay(10);
	mipid_read(md, rd->cmd, rbuf, 2);
	rd++;

	for (i = 0; i < 3; i++, rd++) {
		udelay(10);
		mipid_write(md, rd->cmd, (u8 *)rd->wbuf, rd->wlen);
	}

	dev_dbg(&md->spi->dev, "ESD mode 2 state %02x\n", rbuf[1]);
	if (rbuf[1] == 0x00)
		ls041y3_esd_recover(md);
}

static void ls041y3_esd_check(struct mipid_device *md)
{
	ls041y3_esd_check_mode1(md);
	if (md->revision >= 0x88)
		ls041y3_esd_check_mode2(md);
}

static void mipid_esd_start_check(struct mipid_device *md)
{
	if (md->esd_check != NULL)
		queue_delayed_work(md->esd_wq, &md->esd_work,
				   MIPID_ESD_CHECK_PERIOD);
}

static void mipid_esd_stop_check(struct mipid_device *md)
{
	if (md->esd_check != NULL)
		cancel_rearming_delayed_workqueue(md->esd_wq, &md->esd_work);
}

static void mipid_esd_work(struct work_struct *work)
{
	struct mipid_device *md =container_of(work, struct mipid_device, esd_work.work);

	mutex_lock(&md->mutex);
	md->esd_check(md);
	mutex_unlock(&md->mutex);
	mipid_esd_start_check(md);
}

static int mipid_enable(struct lcd_panel *panel)
{
	struct mipid_device *md = to_mipid_device(panel);

	mutex_lock(&md->mutex);

	if (md->enabled) {
		mutex_unlock(&md->mutex);
		return 0;
	}
	set_sleep_mode(md, 0);
	md->enabled = 1;
	send_init_string(md);
	set_display_state(md, 1);
	mipid_esd_start_check(md);

	mutex_unlock(&md->mutex);
	return 0;
}

static void mipid_disable(struct lcd_panel *panel)
{
	struct mipid_device *md = to_mipid_device(panel);

	/*
	 * A final ESD work might be called before returning,
	 * so do this without holding the lock.
	 */
	mipid_esd_stop_check(md);
	mutex_lock(&md->mutex);

	if (!md->enabled) {
		mutex_unlock(&md->mutex);
		return;
	}
	set_display_state(md, 0);
	set_sleep_mode(md, 1);
	md->enabled = 0;

	mutex_unlock(&md->mutex);
}

static int panel_enabled(struct mipid_device *md)
{
	u32 disp_status;
	int enabled;

	mipid_read(md, MIPID_CMD_READ_DISP_STATUS, (u8 *)&disp_status, 4);
	disp_status = __be32_to_cpu(disp_status);
	enabled = (disp_status & (1 << 17)) && (disp_status & (1 << 10));
	dev_dbg(&md->spi->dev,
		"LCD panel %senabled by bootloader (status 0x%04x)\n",
		enabled ? "" : "not ", disp_status);
	return enabled;
}

static void set_cabc_mode(struct mipid_device *md, int mode)
{
	u16 cabc_ctrl;

	cabc_ctrl = 0;
	mipid_read(md, MIPID_CMD_READ_CABC, (u8 *)&cabc_ctrl, 1);
	cabc_ctrl &= ~3;
	cabc_ctrl |= (1 << 8) | (mode & 3);
	mipid_write(md, MIPID_CMD_WRITE_CABC, (u8 *)&cabc_ctrl, 2);
}

static int get_cabc_mode(struct mipid_device *md)
{
	u8 cabc_ctrl;

	mipid_read(md, MIPID_CMD_READ_CABC, &cabc_ctrl, 1);
	return cabc_ctrl & 3;
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
	struct mipid_device *md = dev_get_drvdata(dev);
	const char *mode_str;
	int mode;
	int len;

	if (!md->has_cabc)
		mode = 0;
	else
		mode = get_cabc_mode(md);
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
	struct mipid_device *md = dev_get_drvdata(dev);
	int i;

	for (i = 0; i < ARRAY_SIZE(cabc_modes); i++) {
		const char *mode_str = cabc_modes[i];
		int cmp_len = strlen(mode_str);

		if (count > 0 && buf[count - 1] == '\n')
			count--;
		if (count != cmp_len)
			continue;

		if (strncmp(buf, mode_str, cmp_len) == 0)
			break;
	}

	if (i == ARRAY_SIZE(cabc_modes))
		return -EINVAL;

	if (!md->has_cabc && i != 0)
		return -EINVAL;

	mutex_lock(&md->mutex);
	set_cabc_mode(md, i);
	mutex_unlock(&md->mutex);

	return count;
}

static ssize_t show_cabc_available_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct mipid_device *md = dev_get_drvdata(dev);
	int len;
	int i;

	if (!md->has_cabc)
		return snprintf(buf, PAGE_SIZE, "%s\n", cabc_modes[0]);

	for (i = 0, len = 0;
	     len < PAGE_SIZE && i < ARRAY_SIZE(cabc_modes); i++)
		len += snprintf(&buf[len], PAGE_SIZE - len, "%s%s%s",
			i ? " " : "", cabc_modes[i],
			i == ARRAY_SIZE(cabc_modes) - 1 ? "\n" : "");

	return len < PAGE_SIZE ? len : PAGE_SIZE - 1;
}

static DEVICE_ATTR(cabc_mode, S_IRUGO | S_IWUSR,
		show_cabc_mode, store_cabc_mode);
static DEVICE_ATTR(cabc_available_modes, S_IRUGO,
		show_cabc_available_modes, NULL);

static struct attribute *bldev_attrs[] = {
	&dev_attr_cabc_mode.attr,
	&dev_attr_cabc_available_modes.attr,
	NULL,
};

static struct attribute_group bldev_attr_group = {
	.attrs = bldev_attrs,
};

static int mipid_init(struct lcd_panel *panel,
			    struct omapfb_device *fbdev)
{
	struct mipid_device *md = to_mipid_device(panel);
	struct mipid_platform_data *pd = md->spi->dev.platform_data;
	struct backlight_device *bldev;
	int max_brightness;
	int r;

	md->fbdev = fbdev;
	md->esd_wq = create_singlethread_workqueue("mipid_esd");
	if (md->esd_wq == NULL) {
		dev_err(&md->spi->dev, "can't create ESD workqueue\n");
		return -ENOMEM;
	}
	INIT_DELAYED_WORK(&md->esd_work, mipid_esd_work);
	mutex_init(&md->mutex);

	md->enabled = panel_enabled(md);

	if (!pd->bc_connected) {
		md->has_bc = 0;
		md->has_cabc = 0;
	}

	bldev = backlight_device_register("acx565akm", &md->spi->dev,
			md, &mipid_bl_ops);
	md->bl_dev = bldev;

	if (md->has_cabc) {
		r = sysfs_create_group(&bldev->dev.kobj, &bldev_attr_group);
		if (r) {
			dev_err(&bldev->dev, "failed to create sysfs files\n");
			backlight_device_unregister(bldev);
			return r;
		}
	}

	bldev->props.fb_blank = FB_BLANK_UNBLANK;
	bldev->props.power = FB_BLANK_UNBLANK;

	if (md->has_bc)
		max_brightness = 255;
	else
		max_brightness = pd->get_bklight_max(pd);

	bldev->props.max_brightness = max_brightness;
	bldev->props.brightness = max_brightness;

	mipid_bl_update_status(bldev);

	if (md->has_bc && pd->set_bklight_level != NULL) {
		/*
		 * If both LCD BC and some other board specific BC is present
		 * make sure that only LCD BC is active.
		 */
		msleep(30);
		pd->set_bklight_level(pd, 0);
	}

	if (md->enabled)
		mipid_esd_start_check(md);

	return 0;
}

static void mipid_cleanup(struct lcd_panel *panel)
{
	struct mipid_device *md = to_mipid_device(panel);

	if (md->enabled)
		mipid_esd_stop_check(md);
	destroy_workqueue(md->esd_wq);

	if (md->has_cabc)
		sysfs_remove_group(&md->bl_dev->dev.kobj, &bldev_attr_group);

	backlight_device_unregister(md->bl_dev);
}

static struct lcd_panel mipid_panel = {
	.name		= "mipid",
	.config		= OMAP_LCDC_PANEL_TFT,

	.bpp		= 16,
	.x_res		= 800,
	.y_res		= 480,
	.pixel_clock	= 21940,
	.hsw		= 50,
	.hfp		= 20,
	.hbp		= 15,
	.vsw		= 2,
	.vfp		= 1,
	.vbp		= 3,

	.init		= mipid_init,
	.cleanup	= mipid_cleanup,
	.enable		= mipid_enable,
	.disable	= mipid_disable,
	.get_caps	= mipid_get_caps,
	.set_bklight_level= mipid_set_bklight_level,
	.get_bklight_level= mipid_get_bklight_level,
	.get_bklight_max= mipid_get_bklight_max,
	.run_test	= mipid_run_test,
};

static int mipid_detect(struct mipid_device *md)
{
	struct mipid_platform_data *pdata;

	pdata = md->spi->dev.platform_data;
	if (pdata == NULL) {
		dev_err(&md->spi->dev, "missing platform data\n");
		return -ENOENT;
	}

	mipid_read(md, MIPID_CMD_READ_DISP_ID, md->display_id, 3);
	dev_dbg(&md->spi->dev, "MIPI display ID: %02x%02x%02x\n",
		md->display_id[0], md->display_id[1], md->display_id[2]);

	switch (md->display_id[0]) {
	case 0x10:
		md->model = MIPID_VER_ACX565AKM;
		md->panel.name = "acx565akm";
		md->has_bc = 1;
		md->has_cabc = 1;
		md->panel.pixel_clock = 24360;
		md->panel.hsw = 4;
		md->panel.hfp = 16;
		md->panel.hbp = 12;
		md->panel.vsw = 3;
		md->panel.vfp = 3;
		md->panel.vbp = 3;
		break;
	case 0x29:
		md->model = MIPID_VER_L4F00311;
		md->panel.name = "l4f00311";
		break;
	case 0x45:
		md->model = MIPID_VER_LPH8923;
		md->panel.name = "lph8923";
		break;
	case 0x83:
		md->model = MIPID_VER_LS041Y3;
		md->panel.name = "ls041y3";
		md->esd_check = ls041y3_esd_check;
		break;
	default:
		md->panel.name = "unknown";
		dev_err(&md->spi->dev, "invalid display ID\n");
		return -ENODEV;
	}

	md->revision = md->display_id[1];
	md->panel.data_lines = pdata->data_lines;
	pr_info("omapfb: %s rev %02x LCD detected, %d data lines\n",
			md->panel.name, md->revision, md->panel.data_lines);

	return 0;
}

static int mipid_spi_probe(struct spi_device *spi)
{
	struct mipid_device *md;
	int r;

	md = kzalloc(sizeof(*md), GFP_KERNEL);
	if (md == NULL) {
		dev_err(&spi->dev, "out of memory\n");
		return -ENOMEM;
	}

	spi->mode = SPI_MODE_3;
	md->spi = spi;
	dev_set_drvdata(&spi->dev, md);
	md->panel = mipid_panel;

	r = mipid_detect(md);
	if (r < 0)
		return r;

	omapfb_register_panel(&md->panel);

	return 0;
}

static int mipid_spi_remove(struct spi_device *spi)
{
	struct mipid_device *md = dev_get_drvdata(&spi->dev);

	omapfb_unregister_panel(&md->panel, md->fbdev);
	kfree(md);

	return 0;
}

static struct spi_driver mipid_spi_driver = {
	.driver = {
		.name	= MIPID_MODULE_NAME,
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe	= mipid_spi_probe,
	.remove	= __devexit_p(mipid_spi_remove),
};

static int mipid_drv_init(void)
{
	spi_register_driver(&mipid_spi_driver);

	return 0;
}
module_init(mipid_drv_init);

static void mipid_drv_cleanup(void)
{
	spi_unregister_driver(&mipid_spi_driver);
}
module_exit(mipid_drv_cleanup);

MODULE_DESCRIPTION("MIPI display driver");
MODULE_LICENSE("GPL");
