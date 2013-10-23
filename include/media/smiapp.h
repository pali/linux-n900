/*
 * include/media/smiapp.h
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2010 Nokia Corporation
 * Contact: Vimarsh Zutshi <vimarsh.zutshi@nokia.com>
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
 *
 */

#ifndef __SMIAPP_H_
#define __SMIAPP_H_

#include <media/smiapp-regs.h>
#include <media/v4l2-subdev.h>

#define SMIAPP_NAME		"smiapp"
#define SMIA_NAME		"smia"

#define SMIAPP_DFL_I2C_ADDR	(0x20 >> 1) /* Default I2C Address */
#define SMIAPP_ALT_I2C_ADDR	(0x6e >> 1) /* Alternate I2C Address */

/*
 * Sometimes due to board layout considerations the camera module can be
 * mounted rotated. The typical rotation used is 180 degrees which can be
 * corrected by giving a default H-FLIP and V-FLIP in the sensor readout.
 * FIXME: rotation also changes the bayer pattern.
 */
enum smiapp_module_board_orient {
	SMIAPP_MODULE_BOARD_ORIENT_0 = 0,
	SMIAPP_MODULE_BOARD_ORIENT_180,
};

/*
 * struct smiapp_module_ident - SMIA/SMIA++ module identification
 */
struct smiapp_module_ident {
	/* mandatory info */
	u32 manu_id;		/* module manufacturer id */
	u32 model_id;		/* module model id */
	char *name;

	/* non mandatory info */
	u32 sensor_manu_id;	/* sensor manufacturer id */
	u32 sensor_model_id;	/* sensor model id */
};

struct smiapp_flash_strobe_parms {
	u8 mode;
	u32 strobe_width_high_us;
	u16 strobe_delay;
	u16 stobe_start_point;
	u8 trigger;
};

struct smiapp_platform_data {
	/*
	 * Change the cci address if i2c_addr_alt is set.
	 * Both default and alternate cci addr need to be present
	 */
	unsigned short i2c_addr_dfl;	/* Default i2c addr */
	unsigned short i2c_addr_alt;	/* Alternate i2c addr */

	unsigned int nvm_size;			/* bytes */
	unsigned int ext_clk;			/* sensor external clk */

	enum smiapp_module_board_orient module_board_orient;

	struct smiapp_flash_strobe_parms *strobe_setup;

	/*
	 * Allow the platform to provide a module identification
	 * function to cover the possibility of having to do a
	 * non-standard module identification
	 */
	const struct smiapp_module_ident *
	(*identify_module)(const struct smiapp_module_ident *ident_in);

	void (*csi_configure)(struct v4l2_subdev *sd, struct smia_mode *mode);
	int (*set_xclk)(struct v4l2_subdev *sd, int hz);
	int (*set_xshutdown)(struct v4l2_subdev *sd, u8 set);
	int (*get_analog_gain_limits)(const struct smiapp_module_ident *ident,
				      u32 *min, u32 *max, u32 *step);
};

#endif /* __SMIAPP_H_  */
