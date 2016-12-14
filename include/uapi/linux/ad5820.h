/*
 * AD5820 DAC driver for camera voice coil focus.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 */

#ifndef __LINUX_AD5820_H
#define __LINUX_AD5820_H

#include <linux/v4l2-controls.h>

/* Control IDs specific to the AD5820 driver as defined by V4L2 */
#define V4L2_CID_AD5820_RAMP_TIME    (V4L2_CID_USER_AD5820_BASE + 0)
#define V4L2_CID_AD5820_RAMP_MODE    (V4L2_CID_USER_AD5820_BASE + 1)

#endif
