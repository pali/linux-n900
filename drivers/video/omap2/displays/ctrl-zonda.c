/*
 * TC358731XBG, eDisco
 */

/*#define DEBUG*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/err.h>

#include <mach/display.h>
#include <mach/dma.h>

/* DSI Virtual channel. Hardcoded for now. */
#define ZCH 0

#define DCS_GET_POWER_MODE	0x0a
#define DCS_GET_ADDR_MODE	0x0b
#define DCS_GET_PIX_FORMAT	0x0c
#define DCS_ENTER_SLEEP_MODE	0x10
#define DCS_EXIT_SLEEP_MODE	0x11
#define DCS_DISP_OFF		0x28
#define DCS_DISP_ON		0x29
#define DCS_WRITE_MEM_START	0x2c
#define DCS_SET_TEAR_OFF	0x34
#define DCS_SET_TEAR_ON		0x35
#define DCS_SET_ADDR_MODE	0x36
#define DCS_SET_PIX_FORMAT	0x3a
#define DCS_THSSI_OFF		0x80
#define DCS_THSSI_ON		0x81
#define DCS_SET_IOCTRL		0x82
#define DCS_GET_IOCTRL		0x83
#define DCS_SET_TE_TIMING	0x84
#define DCS_SET_VTIMING		0x8b
#define DCS_SET_HTIMING		0x92
#define DCS_SET_PIX_CLOCK	0x9e
#define DCS_GET_ID1		0xda
#define DCS_GET_ID2		0xdb
#define DCS_GET_ID3		0xdc

#define DCS_WRITE_IDX		0xfb
#define DCS_READ_EDISCO		0xfc
#define DCS_WRITE_EDISCO	0xfd
#define DCS_ENABLE_EDISCO	0x9d

#ifdef DEBUG
#define DBG(format, ...) printk(KERN_DEBUG "Zonda: " format, ## __VA_ARGS__)
#else
#define DBG(format, ...)
#endif

static struct {
	bool enabled;
	u8 rotate;
	bool mirror;
} zonda;

static int zonda_dcs_read_1(u8 dcs_cmd, u8 *data)
{
	int r;
	u8 buf[1];

	r = dsi_vc_dcs_read(ZCH, dcs_cmd, buf, 1);

	if (r < 0) {
		printk(KERN_ERR "Zonda read error\n");
		return r;
	}

	*data = buf[0];

	return 0;
}

static int zonda_dcs_write_0(u8 dcs_cmd)
{
	return dsi_vc_dcs_write(ZCH, &dcs_cmd, 1);
}

static int zonda_dcs_write_1(u8 dcs_cmd, u8 param)
{
	u8 buf[2];
	buf[0] = dcs_cmd;
	buf[1] = param;
	return dsi_vc_dcs_write(ZCH, buf, 2);
}

#if 0
static void zonda_edisco_toggle(void)
{
	u8 buf[5];
	buf[0] = DCS_ENABLE_EDISCO;
	buf[1] = 0x03;
	buf[2] = 0x7f;
	buf[3] = 0x5c;
	buf[4] = 0x33;
	dsi_vc_dcs_write(ZCH, buf, sizeof(buf));
}

static u8 zonda_edisco_read(u8 reg)
{
	int r;
	u8 buf[2];

	buf[0] = DCS_WRITE_IDX;
	buf[1] = reg;
	dsi_vc_dcs_write(ZCH, buf, 2);

	r = dsi_vc_dcs_read(ZCH, DCS_READ_EDISCO, buf, 1);
	if (r != 1)
		printk(KERN_ERR "ERRor reading edisco\n");

	return buf[0];
}

static void zonda_edisco_write(u8 reg, u32 data)
{
	int val;
	u8 buf[5];
	buf[0] = DCS_WRITE_EDISCO;
	buf[1] = reg;
	buf[2] = (data >> 16) & 0xf;
	buf[3] = (data >> 8) & 0xf;
	buf[4] = (data >> 0) & 0xf;
	val = dsi_vc_dcs_write(ZCH, buf, sizeof(buf));
}

static void zonda_enable_colorbar(bool enable)
{
	zonda_edisco_toggle();

	/* enable colorbar (10-8 bits) */
	zonda_edisco_write(0x20,
			(1<<2) | ((enable ? 2 : 0)<<8));
	zonda_edisco_write(0x28, 1); /* confirm lcdc settings */

	zonda_edisco_toggle();
}
#endif

static void zonda_set_timings(struct omap_display *display,
			struct omap_video_timings *timings)
{
	display->panel->timings = *timings;
}

static int zonda_check_timings(struct omap_display *display,
			struct omap_video_timings *timings)
{
	return 0;
}

static void zonda_get_timings(struct omap_display *display,
			struct omap_video_timings *timings)
{
	*timings = display->panel->timings;
}

static void zonda_get_resolution(struct omap_display *display,
		u16 *xres, u16 *yres)
{
	if (zonda.rotate == 0 || zonda.rotate == 2) {
		*xres = display->panel->timings.x_res;
		*yres = display->panel->timings.y_res;
	} else {
		*yres = display->panel->timings.x_res;
		*xres = display->panel->timings.y_res;
	}
}

static int zonda_ctrl_init(struct omap_display *display)
{
	DBG("zonda_ctrl_init\n");

	display->set_timings = zonda_set_timings;
	display->check_timings = zonda_check_timings;
	display->get_timings = zonda_get_timings;

	display->get_resolution = zonda_get_resolution;

	return 0;
}

static int zonda_sleep_enable(bool enable)
{
	u8 cmd;
	int r;

	if (enable) {
		cmd = DCS_ENTER_SLEEP_MODE;
		r = dsi_vc_dcs_write_nosync(ZCH, &cmd, 1);
	} else {
		cmd = DCS_EXIT_SLEEP_MODE;
		r = dsi_vc_dcs_write(ZCH, &cmd, 1);
	}

	if (r)
		return r;

	if (!enable)
		msleep(5);

	r = dsi_vc_send_null(ZCH);
	if (r)
		return r;

	return 0;
}

static int zonda_display_enable(bool enable)
{
	u8 cmd;

	if (enable)
		cmd = DCS_DISP_ON;
	else
		cmd = DCS_DISP_OFF;

	return zonda_dcs_write_0(cmd);
}

static int zonda_sdi_enable(bool enable)
{
	u8 cmd;

	if (enable)
		cmd = DCS_THSSI_ON;
	else
		cmd = DCS_THSSI_OFF;

	return zonda_dcs_write_0(cmd);
}

static int zonda_get_id(void)
{
	u8 id1, id2, id3;
	int r;

	r = zonda_dcs_read_1(DCS_GET_ID1, &id1);
	if (r)
		return r;
	r = zonda_dcs_read_1(DCS_GET_ID2, &id2);
	if (r)
		return r;
	r = zonda_dcs_read_1(DCS_GET_ID3, &id3);
	if (r)
		return r;

	printk(KERN_INFO "Zonda version %d.%d.%d\n", id1, id2, id3);
	return 0;
}

static void zonda_set_te_timing(void)
{
	u8 buf[7];
	int start = 0, end = 0;

	buf[0] = DCS_SET_TE_TIMING;
	buf[1] = (start >> 16) & 0xff;
	buf[2] = (start >> 8) & 0xff;
	buf[3] = start & 0xff;
	buf[4] = (end >> 16) & 0xff;
	buf[5] = (end >> 8) & 0xff;
	buf[6] = end & 0xff;
	dsi_vc_dcs_write(ZCH, buf, sizeof(buf));
}

static void zonda_set_video_timings(struct omap_display *display)
{
	u8 buf[7];
	int res;

	res = display->panel->timings.y_res;
	buf[0] = DCS_SET_VTIMING;
	buf[1] = display->panel->timings.vfp;
	buf[2] = display->panel->timings.vsw;
	buf[3] = display->panel->timings.vbp;
	buf[4] = (res >> 16) & 0xff;
	buf[5] = (res >> 8) & 0xff;
	buf[6] = res & 0xff;
	dsi_vc_dcs_write(ZCH, buf, sizeof(buf));

	res = display->panel->timings.x_res;
	buf[0] = DCS_SET_HTIMING;
	buf[1] = display->panel->timings.hfp;
	buf[2] = display->panel->timings.hsw;
	buf[3] = display->panel->timings.hbp;
	buf[4] = (res >> 16) & 0xff;
	buf[5] = (res >> 8) & 0xff;
	buf[6] = res & 0xff;
	dsi_vc_dcs_write(ZCH, buf, sizeof(buf));
}

static void zonda_set_pixel_clock(void)
{
	/* nevada supports only 10MHz pclk for now. broken.*/
	/*zonda_dcs_write_1(DCS_SET_PIX_CLOCK, 0x41);*/
	zonda_dcs_write_1(DCS_SET_PIX_CLOCK, 0x1b);
	/*zonda_dcs_write_1(DCS_SET_PIX_CLOCK, 0x3);*/
}

static void zonda_set_sdi_channels(int numchannels)
{
	BUG_ON(numchannels != 1 && numchannels != 2);

	zonda_dcs_write_1(DCS_SET_IOCTRL, numchannels == 1 ? 0 : 1);
}

static void zonda_dump_debug(void)
{
	u8 val;

	dsi_vc_dcs_read(ZCH, DCS_GET_IOCTRL, &val, 1);
	DBG("ioctrl %#x, thssi %s, thssi channels %d\n", val,
			val & (1<<7) ? "on" : "off",
			(val & 3) + 1);

	dsi_vc_dcs_read(ZCH, DCS_GET_POWER_MODE, &val, 1);
	DBG("powermode %#x: sleep %s, display %s\n", val,
			val & (1<<4) ? "off" : "on",
			val & (1<<2) ? "on" : "off");

}

static void zonda_dump_video_timings(void)
{
	u8 zonda_dcs_read(u8 dcs_cmd)
	{
		u8 tmp = 0;
		int r;
		r = zonda_dcs_read_1(dcs_cmd, &tmp);
		if (r)
			printk(KERN_ERR "Zonda read error\n");
		return tmp;
	}

	u8 fp = zonda_dcs_read(0x8c);
	u8 sp = zonda_dcs_read(0x8d);
	u8 bp = zonda_dcs_read(0x8e);
	u8 amsb = zonda_dcs_read(0x8f);
	u8 acsb = zonda_dcs_read(0x90);
	u8 alsb = zonda_dcs_read(0x91);

	DBG("vfp %d, vsp %d, vbp %d, lines %d\n",
			fp, sp, bp,
			(amsb << 16) | (acsb << 8) | (alsb << 0));

	fp = zonda_dcs_read(0x93);
	sp = zonda_dcs_read(0x94);
	bp = zonda_dcs_read(0x95);
	amsb = zonda_dcs_read(0x96);
	acsb = zonda_dcs_read(0x97);
	alsb = zonda_dcs_read(0x98);

	DBG("hfp %d, hsp %d, hbp %d, cols %d\n",
			fp, sp, bp,
			(amsb << 16) | (acsb << 8) | (alsb << 0));
}

static int zonda_set_addr_mode(int rotate, int mirror)
{
	int r;
	u8 mode;
	int b5, b6, b7;

	r = zonda_dcs_read_1(DCS_GET_ADDR_MODE, &mode);
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

	return zonda_dcs_write_1(DCS_SET_ADDR_MODE, mode);
}

static int zonda_ctrl_rotate(struct omap_display *display, u8 rotate)
{
	int r;

	if (zonda.enabled) {
		r = zonda_set_addr_mode(rotate, zonda.mirror);

		if (r)
			return r;
	}

	zonda.rotate = rotate;

	return 0;
}

static u8 zonda_ctrl_get_rotate(struct omap_display *display)
{
	return zonda.rotate;
}

static int zonda_ctrl_mirror(struct omap_display *display, bool enable)
{
	int r;

	if (zonda.enabled) {
		r = zonda_set_addr_mode(zonda.rotate, enable);

		if (r)
			return r;
	}

	zonda.mirror = enable;

	return 0;
}

static bool zonda_ctrl_get_mirror(struct omap_display *display)
{
	return zonda.mirror;
}


static int zonda_ctrl_enable(struct omap_display *display)
{
	int r;

	DBG("zonda_ctrl_enable\n");

	if (display->hw_config.ctrl_enable) {
		r = display->hw_config.ctrl_enable(display);
		if (r)
			return r;
	}

	/* it seems we have to wait a bit until zonda is ready */
	msleep(5);

	r = zonda_sleep_enable(0);
	if (r)
		return r;

	/*dsi_vc_set_max_rx_packet_size(0, 64);*/

	r = zonda_get_id();
	if (r)
		return r;

	/*dsi_vc_enable_hs(0, 1);*/

	zonda_set_video_timings(display);

	zonda_set_pixel_clock();

	zonda_set_sdi_channels(2);

	zonda_set_te_timing();

	/*zonda_enable_colorbar(1);*/

	zonda_dcs_write_1(DCS_SET_PIX_FORMAT, 0x77);

	zonda_set_addr_mode(zonda.rotate, zonda.mirror);

	zonda_display_enable(1);
	zonda_sdi_enable(1);

	zonda_dump_debug();

	zonda_dump_video_timings();

	zonda.enabled = 1;

	return 0;
}

static void zonda_ctrl_disable(struct omap_display *display)
{
	zonda_sdi_enable(0);
	zonda_display_enable(0);
	zonda_sleep_enable(1);

	/* wait a bit so that the message goes through */
	msleep(10);

	if (display->hw_config.ctrl_disable)
		display->hw_config.ctrl_disable(display);

	zonda.enabled = 0;
}

static void zonda_set_update_area(int x, int y, int w, int h)
{
	int x1 = x;
	int x2 = x + w - 1;
	int y1 = y;
	int y2 = y + h - 1;

	u8 buf[7];
	buf[0] = 0x2a;	/* 0x2a == set_column_address */
	buf[1] = (x1 >> 16) & 0xff;
	buf[2] = (x1 >> 8) & 0xff;
	buf[3] = (x1 >> 0) & 0xff;
	buf[4] = (x2 >> 16) & 0xff;
	buf[5] = (x2 >> 8) & 0xff;
	buf[6] = (x2 >> 0) & 0xff;

	dsi_vc_dcs_write(ZCH, buf, sizeof(buf));

	buf[0] = 0x2b;	/* 0x2b == set_column_address */
	buf[1] = (y1 >> 16) & 0xff;
	buf[2] = (y1 >> 8) & 0xff;
	buf[3] = (y1 >> 0) & 0xff;
	buf[4] = (y2 >> 16) & 0xff;
	buf[5] = (y2 >> 8) & 0xff;
	buf[6] = (y2 >> 0) & 0xff;

	dsi_vc_dcs_write(ZCH, buf, sizeof(buf));
}

static void zonda_ctrl_setup_update(struct omap_display *display,
				    u16 x, u16 y, u16 w, u16 h)
{
	u8 tmpbuf[3+1];

	zonda_set_update_area(x, y, w, h);

	/* zonda errata: in high-speed mode, with 2 lanes,
	 * start_mem_write has to be sent twice, first is ignored */
	tmpbuf[0] = 0x2c; /* start mem write */
	dsi_vc_dcs_write(ZCH, tmpbuf, sizeof(tmpbuf));
}

static int zonda_ctrl_enable_te(struct omap_display *display, bool enable)
{
	u8 buf[2];

	if (enable) {
		buf[0] = DCS_SET_TEAR_ON;
		buf[1] = 0;	/* only vertical sync */
		dsi_vc_dcs_write(ZCH, buf, 2);
	} else {
		buf[0] = DCS_SET_TEAR_OFF;
		dsi_vc_dcs_write(ZCH, buf, 1);
	}

	return 0;
}

static int zonda_run_test(struct omap_display *display, int test_num)
{
	u8 id1, id2, id3;
	int r;

	r = zonda_dcs_read_1(DCS_GET_ID1, &id1);
	if (r)
		return r;
	r = zonda_dcs_read_1(DCS_GET_ID2, &id2);
	if (r)
		return r;
	r = zonda_dcs_read_1(DCS_GET_ID3, &id3);
	if (r)
		return r;

	if (id1 != 41 || id2 != 129 || id3 != 1)
		return -EINVAL;

	return 0;
}

static struct omap_ctrl zonda_ctrl = {
	.owner = THIS_MODULE,
	.name = "ctrl-zonda",
	.init = zonda_ctrl_init,
	.enable = zonda_ctrl_enable,
	.disable = zonda_ctrl_disable,
	.setup_update = zonda_ctrl_setup_update,
	.enable_te = zonda_ctrl_enable_te,
	.set_rotate = zonda_ctrl_rotate,
	.get_rotate = zonda_ctrl_get_rotate,
	.set_mirror = zonda_ctrl_mirror,
	.get_mirror = zonda_ctrl_get_mirror,
	.run_test = zonda_run_test,
	.pixel_size = 24,
};


static int __init zonda_init(void)
{
	DBG("zonda_init\n");
	omap_dss_register_ctrl(&zonda_ctrl);
	return 0;
}

static void __exit zonda_exit(void)
{
	DBG("zonda_exit\n");

	omap_dss_unregister_ctrl(&zonda_ctrl);
}

module_init(zonda_init);
module_exit(zonda_exit);

MODULE_AUTHOR("Tomi Valkeinen <tomi.valkeinen@nokia.com>");
MODULE_DESCRIPTION("Zonda Driver");
MODULE_LICENSE("GPL");
