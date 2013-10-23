#ifndef __ARCH_ARM_PLAT_OMAP_NOKIA_DSI_PANEL_H
#define __ARCH_ARM_PLAT_OMAP_NOKIA_DSI_PANEL_H

#include "display.h"

/**
 * struct nokia_dsi_panel_data - Nokia DSI panel driver configuration
 * @name: panel name
 * @use_ext_te: use external TE
 * @ext_te_gpio: external TE GPIO
 * @esd_timeout: interval of ESD checks, 0 = disabled (ms)
 * @ulps_timeout: time to wait before entering ULPS, 0 = disabled (ms)
 * @max_backlight_level: maximum backlight level
 * @set_backlight: pointer to backlight set function
 * @get_backlight: pointer to backlight get function
 * @partial_area: reduced vertical display area to use, .height 0 = disabled
 * @rotate: initial rotation
 */
struct nokia_dsi_panel_data {
	const char *name;

	int reset_gpio;

	bool use_ext_te;
	int ext_te_gpio;

	unsigned esd_timeout;
	unsigned ulps_timeout;

	int max_backlight_level;
	int (*set_backlight)(struct omap_dss_device *dssdev, int level);
	int (*get_backlight)(struct omap_dss_device *dssdev);

	struct {
		u16 offset;
		u16 height;
	} partial_area;

	u8 rotate;
};

#endif /* __ARCH_ARM_PLAT_OMAP_NOKIA_DSI_PANEL_H */
