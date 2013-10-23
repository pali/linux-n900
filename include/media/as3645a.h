/*
 * include/media/as3645a.h
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Ivan T. Ivanov <iivanov@mm-sol.com>
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

#ifndef __AS3645A_H__
#define __AS3645A_H__

#include <media/v4l2-subdev.h>

#define AS3645A_NAME				"as3645a"
#define AS3645A_I2C_ADDR			(0x60 >> 1) /* W:0x60, R:0x61 */

struct as3645a_flash_torch_parms {
	/* Min flash current allowed by LEDs */
	u8 flash_min_current;
	/* Max flash current allowed by LEDs*/
	u8 flash_max_current;
	/* Flash current step */
	u8 flash_step;
	/* Min torch current allowed by LEDs*/
	u8 torch_min_current;
	/* Max torch current allowed by LEDs*/
	u8 torch_max_current;
	/* Torch current step */
	u8 torch_step;
	/* Min flash timeout in us allowed by LEDs*/
	u32 timeout_min_us;
	/* Max flash timeout in us allowed by LEDs*/
	u32 timeout_max_us;
	/* Flash timeout step in us */
	u32 timeout_step;
};

struct as3645a_platform_data {
	int (*set_power)(struct v4l2_subdev *subdev, int on);
	/* used to notify the entity which trigger external storbe signal */
	void (*setup_ext_strobe)(int enable);
	/* Sends the strobe width to the sensor strobe configuration */
	void (*set_strobe_width)(u32 width_in_us);
	/* positive value if Torch pin is used */
	int ext_torch;
	/* positive value if Flash Strobe pin is used for triggering
	 * the Flash light (no matter where is connected to, host processor or
	 * image sensor)
	 */
	int use_ext_flash_strobe;
	/* Number of attached LEDs, 1 or 2 */
	int num_leds;
	/* LED limitations with this flash chip */
	struct as3645a_flash_torch_parms *flash_torch_limits;
};


#endif /* __AS3645A_H__ */
