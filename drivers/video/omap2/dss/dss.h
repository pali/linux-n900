/*
 * linux/drivers/video/omap2/dss/dss.h
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

#ifndef __OMAP2_DSS_H
#define __OMAP2_DSS_H

#ifdef CONFIG_OMAP2_DSS_DEBUG_SUPPORT
#define DEBUG
#endif

#ifdef DEBUG
extern unsigned int dss_debug;
#ifdef DSS_SUBSYS_NAME
#define DSSDBG(format, ...) \
	if (dss_debug) \
		printk(KERN_DEBUG "omapdss " DSS_SUBSYS_NAME ": " format, \
		## __VA_ARGS__)
#else
#define DSSDBG(format, ...) \
	if (dss_debug) \
		printk(KERN_DEBUG "omapdss: " format, ## __VA_ARGS__)
#endif

#ifdef DSS_SUBSYS_NAME
#define DSSDBGF(format, ...) \
	if (dss_debug) \
		printk(KERN_DEBUG "omapdss " DSS_SUBSYS_NAME \
				": %s(" format ")\n", \
				__func__, \
				## __VA_ARGS__)
#else
#define DSSDBGF(format, ...) \
	if (dss_debug) \
		printk(KERN_DEBUG "omapdss: " \
				": %s(" format ")\n", \
				__func__, \
				## __VA_ARGS__)
#endif

#else /* DEBUG */
#define DSSDBG(format, ...)
#define DSSDBGF(format, ...)
#endif


#ifdef DSS_SUBSYS_NAME
#define DSSERR(format, ...) \
	printk(KERN_ERR "omapdss " DSS_SUBSYS_NAME " error: " format, \
	## __VA_ARGS__)
#else
#define DSSERR(format, ...) \
	printk(KERN_ERR "omapdss error: " format, ## __VA_ARGS__)
#endif

#ifdef DSS_SUBSYS_NAME
#define DSSINFO(format, ...) \
	printk(KERN_INFO "omapdss " DSS_SUBSYS_NAME ": " format, \
	## __VA_ARGS__)
#else
#define DSSINFO(format, ...) \
	printk(KERN_INFO "omapdss: " format, ## __VA_ARGS__)
#endif

#ifdef DSS_SUBSYS_NAME
#define DSSWARN(format, ...) \
	printk(KERN_WARNING "omapdss " DSS_SUBSYS_NAME ": " format, \
	## __VA_ARGS__)
#else
#define DSSWARN(format, ...) \
	printk(KERN_WARNING "omapdss: " format, ## __VA_ARGS__)
#endif

/* OMAP TRM gives bitfields as start:end, where start is the higher bit
   number. For example 7:0 */
#define FLD_MASK(start, end)	(((1 << (start - end + 1)) - 1) << (end))
#define FLD_VAL(val, start, end) (((val) << end) & FLD_MASK(start, end))
#define FLD_GET(val, start, end) (((val) & FLD_MASK(start, end)) >> (end))
#define FLD_MOD(orig, val, start, end) \
	(((orig) & ~FLD_MASK(start, end)) | FLD_VAL(val, start, end))

#define DISPC_MAX_FCK 173000000

enum omap_burst_size {
	OMAP_DSS_BURST_4x32 = 0,
	OMAP_DSS_BURST_8x32 = 1,
	OMAP_DSS_BURST_16x32 = 2,
};

enum omap_parallel_interface_mode {
	OMAP_DSS_PARALLELMODE_BYPASS,		/* MIPI DPI */
	OMAP_DSS_PARALLELMODE_RFBI,		/* MIPI DBI */
	OMAP_DSS_PARALLELMODE_DSI,
};

enum dss_clock {
	DSS_CLK_ICK	= 1 << 0,
	DSS_CLK_FCK1	= 1 << 1,
	DSS_CLK_FCK2	= 1 << 2,
	DSS_CLK_54M	= 1 << 3,
	DSS_CLK_96M	= 1 << 4,
};

struct dispc_clock_info {
	/* rates that we get with dividers below */
	unsigned long fck;
	unsigned long lck;
	unsigned long pck;

	/* dividers */
	u16 fck_div;
	u16 lck_div;
	u16 pck_div;
};

struct dsi_clock_info {
	/* rates that we get with dividers below */
	unsigned long fint;
	unsigned long dsiphy;
	unsigned long clkin;
	unsigned long dsi1_pll_fclk;
	unsigned long dsi2_pll_fclk;
	unsigned long lck;
	unsigned long pck;

	/* dividers */
	u16 regn;
	u16 regm;
	u16 regm3;
	u16 regm4;

	u16 lck_div;
	u16 pck_div;

	u8 highfreq;
	bool use_dss2_fck;
};

struct seq_file;
struct platform_device;

/* core */
void dss_clk_enable(enum dss_clock clks);
void dss_clk_disable(enum dss_clock clks);
unsigned long dss_clk_get_rate(enum dss_clock clk);
int dss_need_ctx_restore(void);
void dss_dump_clocks(struct seq_file *s);
const char *dss_get_def_disp_name(void);


int dss_dsi_power_up(void);
void dss_dsi_power_down(void);

void dss_soft_reset(void);

/* display */
void dss_init_displays(struct platform_device *pdev);
void dss_uninit_displays(struct platform_device *pdev);
int dss_suspend_all_displays(void);
int dss_resume_all_displays(void);
struct omap_display *dss_get_display(int no);
bool dss_use_replication(struct omap_display *display,
		enum omap_color_mode mode);

/* manager */
int dss_init_overlay_managers(struct platform_device *pdev);
void dss_uninit_overlay_managers(struct platform_device *pdev);

/* overlay */
void dss_init_overlays(struct platform_device *pdev);
void dss_uninit_overlays(struct platform_device *pdev);
void dss_recheck_connections(struct omap_display *display, bool force);
int dss_check_overlay(struct omap_overlay *ovl, struct omap_display *display);
void dss_overlay_setup_dispc_manager(struct omap_overlay_manager *mgr);

/* DSS */
int dss_init(bool skip_init);
void dss_exit(void);

int dss_check_context(void);
void dss_save_context(void);
void dss_restore_context(void);

int dss_reset(void);

void dss_dump_regs(struct seq_file *s);

void dss_sdi_init(u8 datapairs);
int dss_sdi_enable(void);
void dss_sdi_disable(void);

void dss_select_clk_source(bool dsi, bool dispc);
int dss_get_dsi_clk_source(void);
int dss_get_dispc_clk_source(void);
void dss_set_venc_output(enum omap_dss_venc_type type);
void dss_set_dac_pwrdn_bgz(bool enable);

/* SDI */
int sdi_init(bool skip_init);
void sdi_exit(void);
int sdi_init_display(struct omap_display *display);

/* DSI */
int dsi_init(void);
void dsi_exit(void);

void dsi_dump_clocks(struct seq_file *s);
void dsi_dump_regs(struct seq_file *s);

void dsi_save_context(void);
void dsi_restore_context(void);

int dsi_init_display(struct omap_display *display);
void dsi_irq_handler(void);
unsigned long dsi_get_dsi1_pll_rate(void);
unsigned long dsi_get_dsi2_pll_rate(void);
int dsi_pll_calc_pck(bool is_tft, unsigned long req_pck,
		struct dsi_clock_info *cinfo);
int dsi_pll_program(struct dsi_clock_info *cinfo);
int dsi_pll_init(bool enable_hsclk, bool enable_hsdiv);
void dsi_pll_uninit(void);

/* DPI */
int dpi_init(void);
void dpi_exit(void);
int dpi_init_display(struct omap_display *display);

/* DISPC */
int dispc_init(void);
void dispc_exit(void);
void dispc_dump_clocks(struct seq_file *s);
void dispc_dump_regs(struct seq_file *s);
void dispc_irq_handler(void);
void dispc_fake_vsync_irq(void);

void dispc_save_context(void);
void dispc_restore_context(void);

void dispc_enable_sidle(void);
void dispc_disable_sidle(void);

void dispc_lcd_enable_signal_polarity(bool act_high);
void dispc_lcd_enable_signal(bool enable);
void dispc_pck_free_enable(bool enable);
void dispc_enable_fifohandcheck(bool enable);

void dispc_set_lcd_size(u16 width, u16 height);
void dispc_set_digit_size(u16 width, u16 height);
u32 dispc_get_plane_fifo_size(enum omap_plane plane);
void dispc_setup_plane_fifo(enum omap_plane plane, u32 low, u32 high);
bool dispc_fifomerge_enabled(void);
void dispc_enable_fifomerge(bool enable);
void dispc_set_overlay_optimization(void);
void dispc_set_burst_size(enum omap_plane plane,
		enum omap_burst_size burst_size);

void dispc_set_plane_ba0(enum omap_plane plane, u32 paddr);
void dispc_set_plane_ba1(enum omap_plane plane, u32 paddr);
void dispc_set_plane_pos(enum omap_plane plane, u16 x, u16 y);
void dispc_set_plane_size(enum omap_plane plane, u16 width, u16 height);

int dispc_setup_plane(enum omap_plane plane, enum omap_channel channel_out,
		      u32 paddr, u16 screen_width,
		      u16 pos_x, u16 pos_y,
		      u16 width, u16 height,
		      u16 out_width, u16 out_height,
		      enum omap_color_mode color_mode,
		      bool ilace,
		      enum omap_dss_rotation_type rotation_type,
		      u8 rotation, bool mirror,
		      u8 global_alpha);

enum omap_channel dispc_get_enabled_channel(void);
void dispc_go(enum omap_channel channel);
void dispc_wait_for_go(enum omap_channel channel);
void dispc_enable_lcd_out(bool enable);
void dispc_enable_digit_out(bool enable);
void dispc_enable_digit_errors(int enable);
int dispc_enable_plane(enum omap_plane plane, bool enable);
void dispc_enable_replication(enum omap_plane plane, bool enable);

void dispc_set_parallel_interface_mode(enum omap_parallel_interface_mode mode);
void dispc_set_tft_data_lines(u8 data_lines);
void dispc_set_lcd_display_type(enum omap_lcd_display_type type);
void dispc_set_loadmode(enum omap_dss_load_mode mode);

void dispc_set_default_color(enum omap_channel channel, u32 color);
u32 dispc_get_default_color(enum omap_channel channel);
void dispc_set_trans_key(enum omap_channel ch,
		enum omap_dss_color_key_type type,
		u32 trans_key);
void dispc_get_trans_key(enum omap_channel ch,
		enum omap_dss_color_key_type *type,
		u32 *trans_key);
void dispc_enable_trans_key(enum omap_channel ch, bool enable);
void dispc_enable_alpha_blending(enum omap_channel ch, bool enable);
bool dispc_trans_key_enabled(enum omap_channel ch);
bool dispc_alpha_blending_enabled(enum omap_channel ch);

void dispc_set_lcd_timings(struct omap_video_timings *timings);
unsigned long dispc_fclk_rate(void);
unsigned long dispc_lclk_rate(void);
unsigned long dispc_pclk_rate(void);
void dispc_set_pol_freq(struct omap_panel *panel);
void find_lck_pck_divs(bool is_tft, unsigned long req_pck, unsigned long fck,
		u16 *lck_div, u16 *pck_div);
int dispc_calc_clock_div(bool is_tft, unsigned long req_pck,
		struct dispc_clock_info *cinfo);
int dispc_set_clock_div(struct dispc_clock_info *cinfo);
int dispc_get_clock_div(struct dispc_clock_info *cinfo);
void dispc_set_lcd_divisor(u16 lck_div, u16 pck_div);

void dispc_setup_partial_planes(struct omap_display *display,
				u16 *x, u16 *y, u16 *w, u16 *h);
void dispc_draw_partial_planes(struct omap_display *display);


/* VENC */
int venc_init(struct platform_device *pdev);
void venc_exit(void);
void venc_dump_regs(struct seq_file *s);
int venc_init_display(struct omap_display *display);

/* RFBI */
int rfbi_init(void);
void rfbi_exit(void);
void rfbi_dump_regs(struct seq_file *s);

int rfbi_configure(int rfbi_module, int bpp, int lines);
void rfbi_enable_rfbi(bool enable);
void rfbi_transfer_area(u16 width, u16 height,
			     void (callback)(void *data), void *data);
void rfbi_set_timings(int rfbi_module, struct rfbi_timings *t);
unsigned long rfbi_get_max_tx_rate(void);
int rfbi_init_display(struct omap_display *display);

#endif
