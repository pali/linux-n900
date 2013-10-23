/**
 * include/linux/as3645a.h
 *
 * Copyright (C) 2010 Nokia Corporation
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
 */

#ifndef __AS3645A_USER_H_
#define __AS3645A_USER_H_

#include <linux/videodev2.h>

/* Flash and privacy (indicator) light controls */
#define V4L2_CID_FLASH_STROBE		(V4L2_CID_CAMERA_CLASS_BASE+1100)
#define V4L2_CID_FLASH_TIMEOUT		(V4L2_CID_CAMERA_CLASS_BASE+1101)
#define V4L2_CID_FLASH_INTENSITY	(V4L2_CID_CAMERA_CLASS_BASE+1102)
#define V4L2_CID_TORCH_INTENSITY	(V4L2_CID_CAMERA_CLASS_BASE+1103)
#define V4L2_CID_INDICATOR_INTENSITY	(V4L2_CID_CAMERA_CLASS_BASE+1104)

#define V4L2_CID_FLASH_FAULT_RFU	(V4L2_CID_CAMERA_CLASS_BASE+1105)

/* Inductor peak current limit fault (1=fault, 0=no fault) */
#define V4L2_CID_FLASH_FAULT_INDUCTOR_PEAK_LIMIT \
					(V4L2_CID_CAMERA_CLASS_BASE+1106)

/* Indicator LED fault (1=fault, 0=no fault);
 * fault is either short circuit or open loop
 */
#define V4L2_CID_FLASH_FAULT_INDICATOR_LED \
					(V4L2_CID_CAMERA_CLASS_BASE+1107)

/* Amount of LEDs (1=two LEDs, 0=only one LED) */
#define V4L2_CID_FLASH_FAULT_LED_AMOUNT \
					(V4L2_CID_CAMERA_CLASS_BASE+1108)

/* Timeout fault (1=fault, 0=no fault) */
#define V4L2_CID_FLASH_FAULT_TIMEOUT 	(V4L2_CID_CAMERA_CLASS_BASE+1109)

/* Over Temperature Protection (OTP) fault (1=fault, 0=no fault) */
#define V4L2_CID_FLASH_FAULT_OVER_TEMPERATURE \
					(V4L2_CID_CAMERA_CLASS_BASE+1110)

/* Short circuit fault (1=fault, 0=no fault) */
#define V4L2_CID_FLASH_FAULT_SHORT_CIRCUIT \
					(V4L2_CID_CAMERA_CLASS_BASE+1111)

/* Over voltage protection (OVP) fault (1=fault, 0=no fault) */
#define V4L2_CID_FLASH_FAULT_OVER_VOLTAGE \
					(V4L2_CID_CAMERA_CLASS_BASE+1112)

/* FLASH_READY flag (1=flash could be used, 0=flash is still cooling down) */
#define V4L2_CID_FLASH_READY		(V4L2_CID_CAMERA_CLASS_BASE+1113)

#define V4L2_CID_FLASH_MODE		(V4L2_CID_CAMERA_CLASS_BASE+1114)

/* Same as STROBE above - this will deprecate the former */
#define V4L2_CID_FLASH_TRIGGER		(V4L2_CID_CAMERA_CLASS_BASE+1115)
/* Same as TIMEOUT above - this will deprecate the former */
#define V4L2_CID_FLASH_DURATION		(V4L2_CID_CAMERA_CLASS_BASE+1116)


enum v4l2_flash_mode {
	V4L2_FLASH_MODE_SOFT = 0,
	V4L2_FLASH_MODE_EXT_STROBE = 1
};

#define V4L2_EVENT_AS3645A_CLASS	(V4L2_EVENT_PRIVATE_START | 0x200)
#define V4L2_EVENT_STATUS_CHANGED	(V4L2_EVENT_AS3645A_CLASS | 1)

enum v4l2_flash_events {
	V4L2_FLASH_READY = 0,
	V4L2_FLASH_COOLDOWN = 1,
};

#endif /* __AS3645A_USER_H_ */
