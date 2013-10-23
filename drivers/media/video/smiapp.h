/*
 * drivers/media/video/smiapp.h
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

#ifndef __SMIAPP_PRIV_H_
#define __SMIAPP_PRIV_H_

#include <linux/mutex.h>
#include <media/v4l2-ctrls.h>

/*
 * SMIA/SMIA++ Registers
 */
#define SMIAPP_REG_U16_MODEL_ID			0x0000
#define SMIAPP_REG_U8_REV_NO_MAJOR		0x0002
#define SMIAPP_REG_U8_MANU_ID			0x0003
#define SMIAPP_REG_U8_SMIA_VER			0x0004
#define SMIAPP_REG_U8_REV_NO_MINOR		0x0010
#define SMIAPP_REG_U8_SMIAPP_VER		0x0011

#define SMIAPP_REG_U16_SENSOR_MODEL_ID		0x0016
#define SMIAPP_REG_U8_SENSOR_REV_NO		0x0018
#define SMIAPP_REG_U8_SENSOR_MANU_ID		0x0019

#define SMIAPP_REG_U8_FFMT_MODEL_TYPE		0x0040
#define SMIAPP_REG_U8_FFMT_MODEL_STYPE		0x0041
#define SMIAPP_REG_U8_FFMT_TYPE1_DESC_ZERO	0x0042
#define SMIAPP_REG_U8_FFMT_TYPE2_DESC_ZERO	0x0060

#define SMIAPP_REG_U16_ANALOG_GAIN_MIN		0x0084
#define SMIAPP_REG_U16_ANALOG_GAIN_MAX		0x0086
#define SMIAPP_REG_U16_ANALOG_GAIN_STEP		0x0088

#define SMIAPP_REG_U8_MODE			0x0100

#define SMIAPP_REG_U8_IMAGE_ORIENT		0x0101
#define SMIAPP_IMAGE_ORIENT_HFLIP		(1 << 0)
#define SMIAPP_IMAGE_ORIENT_VFLIP		(1 << 1)

#define SMIAPP_REG_U8_SOFT_RESET		0x0103
#define SMIAPP_REG_U8_GP_HOLD			0x0104
#define SMIAPP_REG_U8_FAST_STANDBY		0x0106
#define SMIAPP_REG_U8_CCI_ADDR			0x0107

#define SMIAPP_REG_U16_COARSE_INTEG_TIME	0x0202
#define SMIAPP_REG_U16_ANALOG_GAIN		0x0204

#define SMIAPP_DATATX_IFX_CTRL_EN		(1 << 0)
#define SMIAPP_DATATX_IFX_CTRL_RD_EN		(0 << 1)
#define SMIAPP_DATATX_IFX_CTRL_WR_EN		(1 << 1)
#define SMIAPP_DATATX_IFX_CTRL_ERR_CLEAR	(1 << 2)
#define SMIAPP_DATATX_IFX_STATUS_RD_READY	(1 << 0)
#define SMIAPP_DATATX_IFX_STATUS_WR_READY	(1 << 1)
#define SMIAPP_DATATX_IFX_STATUS_EDATA		(1 << 2)
#define SMIAPP_DATATX_IFX_STATUS_EUSAGE		(1 << 3)
#define SMIAPP_REG_U8_DATATX_IF1_CTRL		0x0a00
#define SMIAPP_REG_U8_DATATX_IF1_STATUS		0x0a01
#define SMIAPP_REG_U8_DATATX_IF1_PAGESEL	0x0a02
#define SMIAPP_REG_U8_DATATX_IF1_DATA_BASE	0x0a04
#define SMIAPP_REG_U8_DATATX_IF2_CTRL		0x0a44
#define SMIAPP_REG_U8_DATATX_IF2_STATUS		0x0a45
#define SMIAPP_REG_U8_DATATX_IF2_PAGESEL	0x0a46
#define SMIAPP_REG_U8_DATATX_IF2_DATA_BASE	0x0a48

/* Timer configuration registers */
#define SMIAPP_FLASH_STROBE_ADJUSTMENT		0x0c12
#define SMIAPP_FLASH_STROBE_START_POINT		0x0c14
#define SMIAPP_FLASH_STROBE_DELAY_RS_CTRL	0x0c16
#define SMIAPP_FLASH_STROBE_WIDTH_HIGH_RS_CTRL	0x0c18
#define SMIAPP_FLASH_MODE_RS			0x0c1a
#define SMIAPP_FLASH_TRIGGER_RS			0x0c1b
#define SMIAPP_FLASH_STATUS			0x0c1c
#define SMIAPP_FLASH_STROBE_WIDTH2_HIGH_RS_CTRL	0x0c26
#define SMIAPP_FLASH_STROBE_WIDTH_LOW_RS_CTRL	0x0c28
#define SMIAPP_FLASH_STROBE_COUNT_RS_CTRL	0x0c2a

/* Timer capability */
#define SMIAPP_FLASH_MODE_CAPABILITY		0x1a02
#define SMIAPP_FLASH_CAP_SINGLE_STROBE		(1 << 0)
#define SMIAPP_FLASH_CAP_MULTIPLE_STROBE	(1 << 1)

/*
 * Standard SMIA++ constants
 */
#define SMIA_STD_VERSION_1		10
#define SMIAPP_STD_VERSION_D_08		08 /* Draft 0.8 */
#define SMIAPP_STD_VERSION_D_09		09 /* Draft 0.9 */
#define SMIAPP_STD_VERSION_1		10

#define SMIAPP_FFMT_MODEL_TYPE1		1
#define SMIAPP_FFMT_MODEL_TYPE2		2
#define SMIAPP_NVM_PAGE_SIZE		64	/* bytes */

#define SMIAPP_FIRMWARE_NAME_MAX 	30

#define SMIAPP_CTRL_ANALOG_GAIN		0
#define SMIAPP_CTRL_EXPOSURE		1
#define SMIAPP_CTRL_HFLIP		2
#define SMIAPP_CTRL_VFLIP		3
#define SMIAPP_NCTRLS			4

/*
 * struct smiapp_sensor - Main device structure
 */
struct smiapp_sensor {
	struct v4l2_subdev subdev;
	struct media_pad pad;
	struct v4l2_mbus_framefmt format;
	struct smiapp_platform_data *platform_data;
	struct regulator *vana;

	u8 smia_version;
	u8 smiapp_version;
	u8 rev_major;
	u8 sof_rows;
	u8 eof_rows;
	u8 hvflip_inv_mask; /* H/VFLIP inversion due to sensor orientation */
	u8 flash_capability;
	u8 frame_skip;

	struct mutex power_lock;
	int power_count;

	unsigned int streaming:1;
	unsigned int dev_init_done:1;

	u8 *nvm;		/* nvm memory buffer */
	unsigned int nvm_size;	/* bytes */

	const struct smiapp_module_ident *ident;
	struct v4l2_ctrl_handler ctrl_handler;
	struct v4l2_ctrl *ctrls[SMIAPP_NCTRLS];

	struct smia_reglist *current_reglist;
	struct smia_meta_reglist *meta_reglist;

	const struct firmware *fw;

	unsigned int sysfs_mode:1;
	unsigned int sysfs_ident:1;
};
#define to_smiapp_sensor(sd)	container_of(sd, struct smiapp_sensor, subdev)

#endif /* __SMIAPP_PRIV_H_ */
