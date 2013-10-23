/*
 * linux/arch/arm/mach-omap2/dss.c
 *
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
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

#include <linux/kernel.h>
#include <linux/platform_device.h>

#include <plat/display.h>
#include <plat/omap-pm.h>
#include <plat/control.h>
#include <plat/mux.h>

#include "dss.h"

#define OMAP343X_PADCONF_DSS(i)		(OMAP2_CONTROL_PADCONFS + 0xA4 + (i)*2)

#if defined(CONFIG_OMAP2_DSS_DSI) || defined(CONFIG_OMAP2_DSS_DSI_MODULE)
static void omap3_dss_dsi_mux_pads(bool enable)
{
	u16 val;
	int i;

	if (enable)
		val = OMAP34XX_MUX_MODE1;
	else
		val = OMAP34XX_MUX_MODE7 | OMAP2_PULL_ENA | OMAP3_INPUT_EN;

	/* 6 DSI pins, starting from fourth DSS pin */
	for (i = 0; i < 6; ++i)
		omap_ctrl_writew(val, OMAP343X_PADCONF_DSS(i + 4));
}
#endif

void omap_setup_dss_device(struct platform_device *pdev)
{
	struct omap_dss_board_info *board_info = pdev->dev.platform_data;

	board_info->get_last_off_on_transaction_id =
		omap_pm_get_dev_context_loss_count;
	board_info->set_max_mpu_wakeup_lat =
		omap_pm_set_max_mpu_wakeup_lat;
	board_info->set_min_bus_tput =
		omap_pm_set_min_bus_tput;
	board_info->set_max_dev_wakeup_lat =
		omap_pm_set_max_dev_wakeup_lat;

#if defined(CONFIG_OMAP2_DSS_DSI) || defined(CONFIG_OMAP2_DSS_DSI_MODULE)
	board_info->dsi_mux_pads = omap3_dss_dsi_mux_pads;
#endif
}
