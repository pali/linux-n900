/**
 * drivers/media/video/ad58xx.h
 *
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2007 Texas Instruments
 *
 * Contact: Atanas Filipov <atanasf@mm-sol.com>
 *
 * Based on ad5807.c by
 *          Vimarsh Zutshi <vimarsh.zutshi@nokia.com>
 *
 * Based on ad5820.c by
 *          Tuukka Toivonen <tuukka.o.toivonen@nokia.com>
 *          Sakari Ailus <sakari.ailus@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

#ifndef __AD58XX_H_
#define __AD58XX_H_

#include <media/v4l2-subdev.h>

#define AD5836_NAME     "ad5836"
#define AD5836_I2C_ADDR (0x1c >> 1)

#define AD58XX_NAME     "ad58xx"
#define AD58XX_I2C_ADDR (0x1c >> 1)

/*
 * struct ad58xx_module_ident - Lens module identification
 */
struct ad58xx_module_ident {
	u8 id;			/* module manufacturer / device id */
	char *name;
};

/* When no activity on EXTCLK, the AD58XX enters power-down mode */
struct ad58xx_platform_data {
	unsigned int ext_clk;
	int (*set_xclk)(struct v4l2_subdev *sd, u32 hz);
};

#endif /* __AD58XX_H_ */
