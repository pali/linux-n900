/**
 * drivers/media/video/ad58xx.c
 *
 * AD58xx SMIA++ VCM lens driver module.
 *
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2007 Texas Instruments
 *
 * Contact: Atanas Filipov <afilipov@mm-sol.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/regulator/consumer.h>
#include <linux/ad58xx.h>
#include <linux/delay.h>

#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/smiapp-power.h>
#include <media/ad58xx.h>

/*
 * High level register definitions
 */
#define AD58XX_REG_INFO			0x00	/* Model identification */
#define AD58XX_REG_VERSION		0x01	/* Version Control register */
#define AD58XX_REG_ADDR_CTRL		0x02	/* I2C Address Register */
#define AD58XX_REG_FC_MSB		0x03	/* VCM DAC or FCPNx update */
#define AD58XX_REG_FC_LSB		0x04	/* VCM DAC or FCPNx update */
#define AD58XX_REG_FCC_MSB		0x05	/* Focus control - MSB */
#define AD58XX_REG_FCC_LSB		0x06	/* Focus control - LSB */
#define AD58XX_REG_FCNP1		0x08	/* VCM DAC code in phase 1 */
#define AD58XX_REG_FCNP2		0x0a	/* VCM DAC code in phase 2 */
#define AD58XX_REG_SCP1			0x0b	/* Number of strobes for p1 */
#define AD58XX_REG_SCP2			0x0c	/* Number of strobes for p2 */

/*
 * Low level register definitions
 */
#define AD5836_REG_CONTROL		0x0d	/* The Control register */
#define AD5836_REG_VCM_MOVE_TIME	0x0e	/* VCM's resonant frequency */
#define AD5836_REG_VCM_THRESHOLD	0x0f	/* VCM threshold value */
#define AD5836_REG_VCM_MODE		0x10	/* VCM Mode bits */
#define AD5836_REG_PROTECT_MODE		0x11	/* Enable/disable shutdown */

/*
 * AD5836 Lens Specific registers
 */
#define AD5836_REG_STATUS		0x12	/* Displays the state */

/*
 * AD5817 Lens Specific registers
 */
#define AD5817_REG_DRV_MODE		0x12	/* Displays the state */
#define AD5817_REG_SETTINGS		0x13	/* Displays the state */
#define AD5817_REG_STATUS		0x14	/* Displays the state */

/*
 * BU8051GWZ Lens Specific registers
 */
#define BU8051GWZ_REG_OP1		0x0d	/* Resonance frequency */
#define BU8051GWZ_REG_OP2		0x0e	/* VCM un-control current 1 */
#define BU8051GWZ_REG_OP3		0x0f	/* VCM un-control current 2 */
#define BU8051GWZ_REG_OP4		0x10	/* VCM un-control region */
#define BU8051GWZ_REG_SETTINGS		0x11
#define BU8051GWZ_REG_PWM		0x12	/* PWM mode enable/disable */

/*
 * AD58XX_REG_FCC - Register bit definitions
 */
#define AD58XX_FCC_RINGCTRL		0x0200	/* Enabled the VCM_MODE bits */
#define AD58XX_FCC_SW_RESET		0x0100	/* Reset to default value */
#define AD58XX_FCC_MOVE_DIR		0x0020	/* Direction Bit */
#define AD58XX_FCC_PHASE		0x0010	/* Movement phase mode */
#define AD58XX_FCC_STROBES		0x0008	/* Movement strobe mode */
#define AD58XX_FCC_FRAMES		0x0004	/* Movement frames mode */
#define AD58XX_FCC_SYNC			0x0002	/* Focus change mode */
#define AD58XX_FCC_ENABLE		0x0001	/* Enables/disable move */

#define AD58XX_FCC_MODE_MASK		(AD58XX_FCC_PHASE \
	| AD58XX_FCC_STROBES | AD58XX_FCC_FRAMES | AD58XX_FCC_SYNC)

/*
 * AD5836_REG_STATUS Register bits definitions
 */
#define AD5836_STATUS_OVER_CURR		(1 << 3)
#define AD5836_STATUS_LOW_VBATT		(1 << 2)
#define AD5836_STATUS_OVER_TEMP		(1 << 1)
#define AD5836_STATUS_BUSY		(1 << 0)

/*
 * IC_INFO register bit definitions
 */
#define AD58XX_INFO_MAN_ID_SHIFT	4
#define AD58XX_INFO_MAN_ID_MASK		(0xf << AD58XX_INFO_MAN_ID_SHIFT)
#define AD58XX_INFO_DEV_ID_SHIFT	0
#define AD58XX_INFO_DEV_ID_MASK		(0xf << AD58XX_INFO_DEV_ID_SHIFT)

/*
 * IC_VERSION register bit definitions
 */
#define AD58XX_VERSION_SHIFT		0
#define AD58XX_VERSION_MASK		(0xf << AD58XX_VERSION_SHIFT)

/*
 * AD5836_REG_VCM_MODE Register bits definitions
 */
#define AD5836_VCMMODE_ARC_SHIFT	0
#define AD5836_VCMMODE_ARC_MASK		(0x03 << AD5836_VCMMODE_ARC_SHIFT)
#define AD5836_VCMMODE_ADJ_SHIFT	2
#define AD5836_VCMMODE_ADJ_MASK		(0x07 << AD5836_VCMMODE_ADJ_SHIFT)
#define AD5836_VCMMODE_CLK_SHIFT	5
#define AD5836_VCMMODE_CLK_MASK		(0x01 << AD5836_VCMMODE_CLK_SHIFT)

/*
 * AD5817_REG_DRV_MODE Register bits definitions
 */
#define AD5817_DRMODE_LIN_MODE_SHIFT	0
#define AD5817_DRMODE_LIN_MODE_MASK	(0x03 << AD5817_DRMODE_LIN_MODE_SHIFT)
#define AD5817_DRMODE_CLK_DIV_SHIFT	2
#define AD5817_DRMODE_CLK_DIV_MASK	(0x07 << AD5817_DRMODE_CLK_DIV_SHIFT)
#define AD5817_DRMODE_FORCE_DISK_SHIFT	5
#define AD5817_DRMODE_FORCE_DISK_MASK	(0x01 << AD5817_DRMODE_FORCE_DISK_SHIFT)

/*
 * AD5817_REG_SETTINGS Register bits definitions
 */
#define AD5817_S_LDO_VANA_DSBL_SHIFT	0
#define AD5817_S_LDO_VANA_DSBL_MASK	(0x01 << AD5817_S_LDO_VANA_DSBL_SHIFT)
#define AD5817_S_CP_DISABLE_SHIFT	1
#define AD5817_S_CP_DISABLE_MASK	(0x01 << AD5817_S_CP_DISABLE_SHIFT)
#define AD5817_S_LDO_VANA_SHIFT		2
#define AD5817_S_LDO_VANA_MASK		(0x07 << AD5817_S_LDO_VANA_SHIFT)
#define AD5817_S_SET_CLK_SHIFT		5
#define AD5817_S_SET_CLK_MASK		(0x03 << AD5817_S_SET_CLK_SHIFT)

/*
 * BU8051GWZ_REG_SETTINGS Register bits definitions
 */
#define BU8051GWZ_SETTINGS_SF_SHIFT		0
#define BU8051GWZ_SETTINGS_SF_MASK		\
				(0x07 << BU8051GWZ_SETTINGS_SF_SHIFT)
#define BU8051GWZ_SETTINGS_SCO_SHIFT		3
#define BU8051GWZ_SETTINGS_SCO_MASK		\
				(0x03 << BU8051GWZ_SETTINGS_SCO_SHIFT)
#define BU8051GWZ_SETTINGS_GAINSEL_SHIFT	5
#define BU8051GWZ_SETTINGS_GAINSEL_MASK		\
				(0x03 << BU8051GWZ_SETTINGS_GAINSEL_SHIFT)
#define BU8051GWZ_SETTINGS_GAINSELEN_SHIFT	7
#define BU8051GWZ_SETTINGS_GAINSELEN_MASK	\
				(0x01 << BU8051GWZ_SETTINGS_GAINSELEN_SHIFT)

#define BU8051GWZ_PWM_SHIFT		0
#define BU8051GWZ_PWM_MASK		(0x01 << BU8051GWZ_PWM_SHIFT)

/*
 * Types and definitions for power state.
 */
enum ad58xx_ext_controls {
	AD58XX_EXT_SET,
	AD58XX_EXT_GET,
	AD58XX_EXT_TRY
};

/*
 * Controls the Focus_change mode
 */
static const u8 ad58xx_fc_mode[] = {
	0x0 << 1,	/* Immediate Move */
	0x2 << 1,	/* Single Frame Single Strobe, SFSS */
	0x5 << 1,	/* Single Frame Multiple Strobe Phase1, SFMSP1 */
	0x7 << 1,	/* Single Frame Multiple Strobe Phase 1-2, SFMSP2 */
	0xd << 1,	/* Multiple Frame Multiple Strobe Phase1, MFMSP1 */
	0xf << 1	/* Multiple Frame Multiple Strobe Phase 1-2, MFMSP2 */
};

/*
 * Controls the ringing control modes
 */
static const u8 ad58xx_vcm_arc[] = {
	0x0,		/* ARC Trade mark - RES1 */
	0x1,		/* ARC Trade mark - ESRC */
	0x2,		/* ARC Trade mark - RES0.5 */
	0x3		/* ARC Trade mark - RES2 */
};

struct ad58xx_device {
	struct v4l2_subdev subdev;
	struct ad58xx_platform_data *platform_data;
	struct regulator *vana;

	struct v4l2_ctrl_handler ctrls;

	u16 focus_ctrl;		/* Current Focus control config */
	u16 focus_cpos;		/* Current lens position */
	u8 vcm_mode;		/* Current VCM mode */

	u8 drive_mode;		/* Current Drive mode */
	u8 settings;		/* Current Settings config */

	u8 bu8051gwz_settings;	/* bu8051gwz frequency and output voltage */
	u8 bu8051gwz_pwm_mode;	/* bu8051gwz PWM mode */

	const struct ad58xx_module_ident *ident; /* Specific module info */

	struct mutex power_lock;
	int power_count;
};

#define to_ad58xx_device(sd) container_of(sd, struct ad58xx_device, subdev)

#define MK_MODULE_ID(manuf, dev)					\
	((((manuf) << AD58XX_INFO_MAN_ID_SHIFT) & AD58XX_INFO_MAN_ID_MASK) | \
	 ((dev) & AD58XX_INFO_DEV_ID_MASK) << AD58XX_INFO_DEV_ID_SHIFT)

#define MODULE_ID_MANUFACTURER(id)					\
	(((id) & AD58XX_INFO_MAN_ID_MASK) >> AD58XX_INFO_MAN_ID_SHIFT)

#define MODULE_ID_DEV(id)						\
	(((id) & AD58XX_INFO_DEV_ID_MASK) >> AD58XX_INFO_DEV_ID_SHIFT)

enum {
	AD5836_ID = MK_MODULE_ID(0x2, 0x2),
	AD5817_ID = MK_MODULE_ID(0x2, 0x3),
	BU8051GWZ_ID = MK_MODULE_ID(0x5, 0x1),
};

/*
 * ad58xx_module_idents - supported lens chips
 */
static const struct ad58xx_module_ident ad58xx_module_idents[] = {
	{ AD5836_ID,	"ad5836 2-000e" },
	{ AD5817_ID,	"ad5817 2-000e" },
	{ BU8051GWZ_ID,	"bu8051gwz" },
};

static int ad58xx_write(struct ad58xx_device *coil, u8 command, u8 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&coil->subdev);

	return i2c_smbus_write_byte_data(client, command, val);
}

static int ad58xx_read(struct ad58xx_device *coil, u8 command)
{
	struct i2c_client *client = v4l2_get_subdevdata(&coil->subdev);

	return i2c_smbus_read_byte_data(client, command);
}

static int ad58xx_is_busy(struct ad58xx_device *coil)
{
	int rval;

	switch (coil->ident->id) {
	case AD5836_ID:
		rval = ad58xx_read(coil, AD5836_REG_STATUS);
		break;
	case AD5817_ID:
		rval = ad58xx_read(coil, AD5817_REG_STATUS);
		break;
	case BU8051GWZ_ID:
		rval = 0;
		break;
	default:
		rval = -ENODEV;
	}

	if (rval < 0)
		return rval;

	if (rval & AD5836_STATUS_BUSY)
		return -EBUSY;

	rval = ad58xx_read(coil, AD58XX_REG_FCC_LSB);
	if (rval < 0)
		return rval;

	if (rval & AD58XX_FCC_ENABLE)
		return -EBUSY;

	return 0;
}

static int ad58xx_set_reg_bits(struct ad58xx_device *coil, u8 reg, u8 mask,
			       u8 shift, u8 *regptr, u8 value)
{
	int new_val;
	int rval;

	new_val = *regptr & ~mask;
	new_val |= value << shift;

	rval = ad58xx_write(coil, reg, new_val);
	if (!rval)
		*regptr = new_val;

	return rval;
}

/* -----------------------------------------------------------------------------
 * V4L2 controls
 */

/*
 * ad58xx_focus_mode - Specifies the focus movement mode
 *
 * In Immediate Mode and Single Strobe Mode, the FC register
 * directly controls the VCM DAC code. In Multiple Strobe mode
 * the FC register is updated by the FCPN1 and FCPN2 registers.
 */
static int ad58xx_focus_mode(struct ad58xx_device *coil, s32 value)
{
	int new_val;
	int rval;

	new_val = coil->focus_ctrl & ~AD58XX_FCC_MODE_MASK;
	new_val |= ad58xx_fc_mode[value];

	rval = ad58xx_write(coil, AD58XX_REG_FCC_LSB, new_val);
	if (!rval)
		coil->focus_ctrl = new_val;

	return rval;
}

/*
 * ad58xx_focus_ringctrl - Ringing control enable / disable
 *
 * RINGCTRL = 0, Direct Load Mode is used.
 * RINGCTRL = 1, The AD58xx allows the user to overcome the mechanical
 * limitations associated with reductions in VCM form factor.
 */
static int ad58xx_focus_ringctrl(struct ad58xx_device *coil, bool ring)
{
	int new_val;
	int rval;

	if (ring)
		new_val = coil->focus_ctrl | AD58XX_FCC_RINGCTRL;
	else
		new_val = coil->focus_ctrl & ~AD58XX_FCC_RINGCTRL;

	rval = ad58xx_write(coil, AD58XX_REG_FCC_MSB, new_val >> 8);
	if (!rval)
		coil->focus_ctrl = new_val;

	return rval;
}

/*
 * ad58xx_focus_swreset - Reset the Focus Logic registers
 *
 * An Immediate Reset of all the Focus Logic registers to their default values.
 * This bit resets to zero automatically. This bit is fully asynchronous to all
 * bits in this register. Any Active sequence will be interrupted and then all
 * registers are reset. This has the highest priority of all the bits in the
 * register.
 */
static int ad58xx_focus_swreset(struct ad58xx_device *coil)
{
	return ad58xx_write(coil, AD58XX_REG_FCC_MSB,
			    (coil->focus_ctrl | AD58XX_FCC_SW_RESET) >> 8);
}

/*
 * ad58xx_focus_change - Specifies the output current source for VCM
 *
 * The AD58xx is a 10-bit DAC with 100 mA output current source capability.
 */
static int ad58xx_focus_change(struct ad58xx_device *coil, s32 value)
{
	int rval;

	rval = ad58xx_write(coil, AD58XX_REG_FC_LSB, value);
	if (!rval)
		rval = ad58xx_write(coil, AD58XX_REG_FC_MSB, value >> 8);
	if (!rval)
		rval = ad58xx_write(coil, AD58XX_REG_FCC_LSB,
				    coil->focus_ctrl | AD58XX_FCC_ENABLE);
	if (!rval)
		coil->focus_cpos = value;

	return rval;
}

/*
 * ad58xx_focus_number_p1 - Specifies the change in VCM DAC code
 *
 * Specifies the change in VCM DAC code in every focus
 * change in phase1. Address location matches FCNP1 LSB register
 */
static int ad58xx_focus_number_p1(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_write(coil, AD58XX_REG_FCNP1, value);
}

/*
 * ad58xx_focus_number_p2 - Specifies the change in VCM DAC code
 *
 * Specifies the change in VCM DAC code in every focus
 * change in phase2. Address location matches FCNP2 LSB register
 */
static int ad58xx_focus_number_p2(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_write(coil, AD58XX_REG_FCNP2, value);
}

/*
 * ad58xx_focus_count_p1 - Specifies the number of strobes for phase1
 */
static int ad58xx_focus_count_p1(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_write(coil, AD58XX_REG_SCP1, value);
}

/*
 * ad58xx_focus_count_p2 - Specifies the number of strobes for phase2
 */
static int ad58xx_focus_count_p2(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_write(coil, AD58XX_REG_SCP2, value);
}

/*
 * ad58xx_vcm_move_time - Specifies the VCM actuator resonant frequency
 *
 * Move time is used to indicate the VCM actuator resonant frequency
 * with the below formulas:
 *
 * When bit high_freq_clk = 0 Tres = 1024 x MCLK_PERIOD x (VCM_MOVE_TIME + 1)
 * When bit high_freq_clk = 1 Tres = 1024 x MCLK_PERIOD x (VCM_MOVE_TIME + 128)
 */
static int ad58xx_vcm_move_time(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_write(coil, AD5836_REG_VCM_MOVE_TIME, value);
}

/*
 * ad58xx_vcm_threshold - Specifies VCM threshold value
 *
 * The threshold is programmed using 8 bits.
 * The real threshold is 9 bits wide and is obtained from the register value:
 *
 * Reg Value (8 bits) = D[7..0] -> threshold value (9 bits) = {D[7..0], 0},
 * so the real threshold is double the value contained in this register.
 */
static int ad58xx_vcm_threshold(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_write(coil, AD5836_REG_VCM_THRESHOLD, value);
}

/*
 * ad58xx_vcm_mode_arc - VCM ringing control mode
 *
 *  00 = Mode 0: ARC3
 *  01 = Mode 1: Linear Ramp Mode
 *  10 = Mode 2: ARC1++
 *  11 = Mode 3: ARC
 */
static int ad58xx_vcm_mode_arc(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_set_reg_bits(coil, AD5836_REG_VCM_MODE,
				   AD5836_VCMMODE_ARC_MASK,
				   AD5836_VCMMODE_ARC_SHIFT,
				   &(coil->vcm_mode), value);
}

/*
 * ad58xx_vcm_mode2_adj - VCM ringing control adjust
 *
 * Programs the uneven steps ration in the case of the ARC filter
 */
static int ad58xx_vcm_mode2_adj(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_set_reg_bits(coil, AD5836_REG_VCM_MODE,
				   AD5836_VCMMODE_ADJ_MASK,
				   AD5836_VCMMODE_ADJ_SHIFT,
				   &(coil->vcm_mode), value);
}

/*
 * ad5817_force_disk - Force disk enable/disable
 *
 * 1 = VCM driver can operate in discontinuous mode
 * 0 = VCM driver always switches
 */
static int ad5817_force_disk(struct ad58xx_device *coil, bool value)
{
	return ad58xx_set_reg_bits(coil, AD5817_REG_DRV_MODE,
				   AD5817_DRMODE_FORCE_DISK_MASK,
				   AD5817_DRMODE_FORCE_DISK_SHIFT,
				   &(coil->drive_mode), value);
}

/*
 * ad5817_clk_div - Set the clock frequency of the switched driver
 *
 * 111 = 4.8Mhz (2.4MHz)
 * 110 = 3.2Mhz (1.6MHz)
 * 101 = 2.4Mhz (1.2MHz)
 * 100 = 1.92Mhz (1.2MHz)
 * 011 = 1.6MHz (1.2MHz)
 * 010 = 1.37MHz (1.2MHz)
 * 001 = 1.2MHz (1.2MHz)
 * 000 = 1.06MHz (1.2MHz)
 * Values in parenthesis applicable for Fext = 4.8MHz (SEL_CLK [1,0] = 01)
 */
static int ad5817_clk_div(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_set_reg_bits(coil, AD5817_REG_DRV_MODE,
				   AD5817_DRMODE_CLK_DIV_MASK,
				   AD5817_DRMODE_CLK_DIV_SHIFT,
				   &(coil->drive_mode), value);
}

/*
 * ad5817_lin_mode - Output Drive Mode select
 *
 * 0 0 = Full PWM Mode
 * 0 1 = Full Linear Mode
 * 1 0 = Auto Mode 0 PWM during each strobe, linear at the end of strobe
 * 1 1 = Auto Mode 1 PWM during each frame, linear at the end of frame
 */
static int ad5817_lin_mode(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_set_reg_bits(coil, AD5817_REG_DRV_MODE,
				   AD5817_DRMODE_LIN_MODE_MASK,
				   AD5817_DRMODE_LIN_MODE_SHIFT,
				   &(coil->drive_mode), value);
}


/*
 * ad5817_ldo_vana - Controls LDO_VANA output voltage level:
 *
 * 000 = 2.5V
 * 001 = 2.6V
 * 010 = 2.7V
 * 011 = 2.8V
 * 100 = 3.0V
 * 101 = 3.2V
 * 110 = 3.3V
 * 111 = 3.6V
 */
static int ad5817_ldo_vana(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_set_reg_bits(coil, AD5817_REG_SETTINGS,
				   AD5817_S_LDO_VANA_MASK,
				   AD5817_S_LDO_VANA_SHIFT,
				   &(coil->settings), value);
}

/*
 * ad5817_cp_disable - Charge Pump enable/disable
 *
 * 1 = Charge Pump disable (Charge Pump and LDO_Vana will remain powered down)
 * 0 = Charge Pump and LDO_Vana powered up
 */
static int ad5817_cp_disable(struct ad58xx_device *coil, bool value)
{
	return ad58xx_set_reg_bits(coil, AD5817_REG_SETTINGS,
				   AD5817_S_CP_DISABLE_MASK,
				   AD5817_S_CP_DISABLE_SHIFT,
				   &(coil->settings), value);
}

/*
 * ad5817_ldo_vana_disable - LDO_Vana enable/disable
 *
 * 1 = LDO_Vana will remain powered down
 * 0 = LDO_Vana powered up
 */
static int ad5817_ldo_vana_disable(struct ad58xx_device *coil, bool value)
{
	return ad58xx_set_reg_bits(coil, AD5817_REG_SETTINGS,
				   AD5817_S_LDO_VANA_DSBL_MASK,
				   AD5817_S_LDO_VANA_DSBL_SHIFT,
				   &(coil->settings), value);
}

/*
 * bu8051gwz_op1 - Resonance frequency setting, Slew rate speed setting
 */
static int bu8051gwz_op1(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_write(coil, BU8051GWZ_REG_OP1, value);
}

/*
 * bu8051gwz_op2 - VCM un-control current setting 1
 */
static int bu8051gwz_op2(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_write(coil, BU8051GWZ_REG_OP2, value);
}

/*
 * bu8051gwz_op3 - VCM un-control current setting 2
 */
static int bu8051gwz_op3(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_write(coil, BU8051GWZ_REG_OP3, value);
}

/*
 * bu8051gwz_op4 - VCM un-control region operating parameter
 */
static int bu8051gwz_op4(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_write(coil, BU8051GWZ_REG_OP4, value);
}

/*
 * bu8051gwz_gainsel - Setting for output voltage
 */
static int bu8051gwz_gainsel(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_set_reg_bits(coil, BU8051GWZ_REG_SETTINGS,
				   BU8051GWZ_SETTINGS_GAINSEL_MASK,
				   BU8051GWZ_SETTINGS_GAINSEL_SHIFT,
				   &(coil->bu8051gwz_settings), value);
}

/*
 * bu8051gwz_gainselen - Select enable/disable Gainsel1,Gainsel0
 */
static int bu8051gwz_gainselen(struct ad58xx_device *coil, bool value)
{
	return ad58xx_set_reg_bits(coil, BU8051GWZ_REG_SETTINGS,
				   BU8051GWZ_SETTINGS_GAINSELEN_MASK,
				   BU8051GWZ_SETTINGS_GAINSELEN_SHIFT,
				   &(coil->bu8051gwz_settings), value);
}

/*
 * bu8051gwz_sco - Slew rate of Output Waveform
 */
static int bu8051gwz_sco(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_set_reg_bits(coil, BU8051GWZ_REG_SETTINGS,
				   BU8051GWZ_SETTINGS_SCO_MASK,
				   BU8051GWZ_SETTINGS_SCO_SHIFT,
				   &(coil->bu8051gwz_settings), value);
}

/*
 * bu8051gwz_sf - Switching frequency setting
 */
static int bu8051gwz_sf(struct ad58xx_device *coil, s32 value)
{
	return ad58xx_set_reg_bits(coil, BU8051GWZ_REG_SETTINGS,
				   BU8051GWZ_SETTINGS_SF_MASK,
				   BU8051GWZ_SETTINGS_SF_SHIFT,
				   &(coil->bu8051gwz_settings), value);
}

/*
 * bu8051gwz_pwm - PWM mode enable/disable
 */
static int bu8051gwz_pwm(struct ad58xx_device *coil, bool value)
{
	return ad58xx_set_reg_bits(coil, BU8051GWZ_REG_PWM,
				   BU8051GWZ_PWM_MASK,
				   BU8051GWZ_PWM_SHIFT,
				   &(coil->bu8051gwz_pwm_mode), value);
}

static int ad58xx_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ad58xx_device *coil =
		container_of(ctrl->handler, struct ad58xx_device, ctrls);

	if (ctrl->id == V4L2_CID_AD58XX_FOCUS_SW_RESET)
		return ad58xx_focus_swreset(coil);

	if (ad58xx_is_busy(coil))
		return -EBUSY;

	switch (ctrl->id) {
	case V4L2_CID_FOCUS_ABSOLUTE:
		return ad58xx_focus_change(coil, ctrl->val);
	case V4L2_CID_AD58XX_FOCUS_MODE:
		return ad58xx_focus_mode(coil, ctrl->val);
	case V4L2_CID_AD58XX_FOCUS_RINGCTRL:
		return ad58xx_focus_ringctrl(coil, ctrl->val);
	case V4L2_CID_AD58XX_CHANGE_NUM_P1:
		return ad58xx_focus_number_p1(coil, ctrl->val);
	case V4L2_CID_AD58XX_CHANGE_NUM_P2:
		return ad58xx_focus_number_p2(coil, ctrl->val);
	case V4L2_CID_AD58XX_STROBE_CNT_P1:
		return ad58xx_focus_count_p1(coil, ctrl->val);
	case V4L2_CID_AD58XX_STROBE_CNT_P2:
		return ad58xx_focus_count_p2(coil, ctrl->val);
	}

	if (coil->ident->id == AD5817_ID || coil->ident->id == AD5836_ID)
		switch (ctrl->id) {
		case V4L2_CID_AD58XX_VCM_MOVE_TIME:
			return ad58xx_vcm_move_time(coil, ctrl->val);
		case V4L2_CID_AD58XX_VCM_THRESHOLD:
			return ad58xx_vcm_threshold(coil, ctrl->val);
		case V4L2_CID_AD58XX_VCM_MODE_ARC:
			return ad58xx_vcm_mode_arc(coil, ctrl->val);
		case V4L2_CID_AD58XX_VCM_MODE_ADJ:
			return ad58xx_vcm_mode2_adj(coil, ctrl->val);
		}
	if (coil->ident->id == AD5817_ID)
		switch (ctrl->id) {
		case V4L2_CID_AD5817_LIN_MODE:
			return ad5817_lin_mode(coil, ctrl->val);
		case V4L2_CID_AD5817_CLK_DIV:
			return ad5817_clk_div(coil, ctrl->val);
		case V4L2_CID_AD5817_FORCE_DISK:
			return ad5817_force_disk(coil, ctrl->val);
		case V4L2_CID_AD5817_LDO_VANA_DSBL:
			return ad5817_ldo_vana_disable(coil, ctrl->val);
		case V4L2_CID_AD5817_CP_DISABLE:
			return ad5817_cp_disable(coil, ctrl->val);
		case V4L2_CID_AD5817_LDO_VANA:
			return ad5817_ldo_vana(coil, ctrl->val);
		}
	else if (coil->ident->id == BU8051GWZ_ID)
		switch (ctrl->id) {
		case V4L2_CID_BU8051GWZ_OP1:
			return bu8051gwz_op1(coil, ctrl->val);
		case V4L2_CID_BU8051GWZ_OP2:
			return bu8051gwz_op2(coil, ctrl->val);
		case V4L2_CID_BU8051GWZ_OP3:
			return bu8051gwz_op3(coil, ctrl->val);
		case V4L2_CID_BU8051GWZ_OP4:
			return bu8051gwz_op4(coil, ctrl->val);
		case V4L2_CID_BU8051GWZ_GAINSEL:
			return bu8051gwz_gainsel(coil, ctrl->val);
		case V4L2_CID_BU8051GWZ_GAINSELEN:
			return bu8051gwz_gainselen(coil, ctrl->val);
		case V4L2_CID_BU8051GWZ_SCO:
			return bu8051gwz_sco(coil, ctrl->val);
		case V4L2_CID_BU8051GWZ_SF:
			return bu8051gwz_sf(coil, ctrl->val);
		case V4L2_CID_BU8051GWZ_PWM:
			return bu8051gwz_pwm(coil, ctrl->val);
		}

	return -EINVAL;
}

static const struct v4l2_ctrl_ops ad58xx_ctrl_ops = {
	.s_ctrl = ad58xx_set_ctrl,
};

static const char * const ad58xx_fc_mode_menu[] = {
	"Immediate Move",
	"Single Frame Single Strobe",
	"Single Frame Multi Strobe P1",
	"Multiple Frame Multi Strobe P1",
	"Single Frame Multi Strobe P1,2",
	"Multiple Frame Multi Strobe P1,2",
};

static const char * const ad58xx_vcm_arc_menu[] = {
	"ARC Trademark RES1",
	"ARC Trademark ESRC",
	"ARC Trademark RES0.5",
	"ARC Trademark RES2",
};

static const char * const ad5817_lin_mode_menu[] = {
	"Full PWM Mode",
	"Full linear Mode",
	"Auto Mode 0 PWM during each strobe, linear at the end of strobe",
	"Auto Mode 1 PWM during each frame, linear at the end of frame",
};


static const char * const bu8051gwz_gainsel_menu[] = {
	"Output voltage 2.70V",
	"Output voltage 2.75V",
	"Output voltage 2.80V",
	"Output voltage 2.85V",
};

/* Common control configuration strunture for all lenses */
static const struct v4l2_ctrl_config ad58xx_ctrls[] = {
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_FOCUS_MODE,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "Focus mode",
		.min		= AD58XX_FOCUS_MODE_IM,
		.max		= AD58XX_FOCUS_MODE_MFMSP2,
		.def		= AD58XX_FOCUS_MODE_IM,
		.qmenu		= ad58xx_fc_mode_menu,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_FOCUS_RINGCTRL,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Ringing control enable / disable",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_FOCUS_SW_RESET,
		.type		= V4L2_CTRL_TYPE_BUTTON,
		.name		= "Reset the focus logic registers",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_CHANGE_NUM_P1,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Focus change number phase 1",
		.min		= 0,
		.max		= 255,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_CHANGE_NUM_P2,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Focus change number phase 2",
		.min		= 0,
		.max		= 255,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_STROBE_CNT_P1,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Strobe count phase 1",
		.min		= 0,
		.max		= 255,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_STROBE_CNT_P2,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Strobe count phase 2",
		.min		= 0,
		.max		= 255,
		.step		= 1,
		.def		= 0,
	},
};

/* Control configuration strunture for AD5817 */
static const struct v4l2_ctrl_config ad5817_ctrls[] = {
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_VCM_MOVE_TIME,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "VCM actuator resonant frequency",
		.min		= 0,
		.max		= 255,
		.step		= 1,
		.def		= 128,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_VCM_THRESHOLD,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "VCM threshold value",
		.min		= 0,
		.max		= 255,
		.step		= 1,
		.def		= 8,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_VCM_MODE_ARC,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "VCM ARC mode",
		.min		= AD58XX_VCM_MODE_ARC_RES01,
		.max		= AD58XX_VCM_MODE_ARC_RES02,
		.def		= AD58XX_VCM_MODE_ARC_RES01,
		.qmenu		= ad58xx_vcm_arc_menu,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_VCM_MODE_ADJ,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "VCM ringing control adjust",
		.min		= 0,
		.max		= 7,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD5817_LIN_MODE,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "Output Drive Mode",
		.min		= AD5817_LIN_MODE_FULL_PWM,
		.max		= AD5817_LIN_MODE_AUTO_MODE1,
		.def		= AD5817_LIN_MODE_FULL_PWM,
		.qmenu		= ad5817_lin_mode_menu,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD5817_CLK_DIV,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Clock frequency of the switched driver",
		.min		= 0,
		.max		= 7,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD5817_FORCE_DISK,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Force disk enable/disable",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD5817_LDO_VANA_DSBL,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "LDO_Vana enable/diable",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD5817_CP_DISABLE,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Charge Pump enable/disable",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD5817_LDO_VANA,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Controls LDO_VANA output voltage level",
		.min		= 0,
		.max		= 7,
		.step		= 1,
		.def		= 2,
	},
};

/* Control configuration strunture for AD5836 */
static const struct v4l2_ctrl_config ad5836_ctrls[] = {
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_VCM_MOVE_TIME,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "VCM actuator resonant frequency",
		.min		= 0,
		.max		= 255,
		.step		= 1,
		.def		= 128,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_VCM_THRESHOLD,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "VCM threshold value",
		.min		= 0,
		.max		= 255,
		.step		= 1,
		.def		= 8,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_VCM_MODE_ARC,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "VCM ARC mode",
		.min		= AD58XX_VCM_MODE_ARC_RES01,
		.max		= AD58XX_VCM_MODE_ARC_RES02,
		.def		= AD58XX_VCM_MODE_ARC_RES01,
		.qmenu		= ad58xx_vcm_arc_menu,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_AD58XX_VCM_MODE_ADJ,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "VCM ringing control adjust",
		.min		= 0,
		.max		= 7,
		.step		= 1,
		.def		= 0,
	},
};

/* Control configuration strunture for BU8051GWZ */
static const struct v4l2_ctrl_config bu8051gwz_ctrls[] = {
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_BU8051GWZ_OP1,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Resonance frequency, Slew rate speed",
		.min		= 0,
		.max		= 255,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_BU8051GWZ_OP2,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "VCM un-control current 1",
		.min		= 0,
		.max		= 255,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_BU8051GWZ_OP3,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "VCM un-control current 2",
		.min		= 0,
		.max		= 255,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_BU8051GWZ_OP4,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "VCM un-control region operating parameter",
		.min		= 0,
		.max		= 255,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_BU8051GWZ_GAINSEL,
		.type		= V4L2_CTRL_TYPE_MENU,
		.name		= "Setting for output voltage",
		.min		= BU8051GWZ_GAINSEL_2_70,
		.max		= BU8051GWZ_GAINSEL_2_85,
		.def		= BU8051GWZ_GAINSEL_2_70,
		.qmenu		= bu8051gwz_gainsel_menu,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_BU8051GWZ_GAINSELEN,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Select enable/disable Gainsel1,Gainsel0",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_BU8051GWZ_SCO,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Slew rate of Output Waveform",
		.min		= 0,
		.max		= 3,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_BU8051GWZ_SF,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Switching frequency setting",
		.min		= 0,
		.max		= 7,
		.step		= 1,
		.def		= 0,
	},
	{
		.ops		= &ad58xx_ctrl_ops,
		.id		= V4L2_CID_BU8051GWZ_PWM,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "PWM mode / Liner mode change",
		.min		= 0,
		.max		= 1,
		.step		= 1,
		.def		= 0,
	},
};

/* -----------------------------------------------------------------------------
 * V4L2 subdev operations
 */

static int __ad58xx_set_power(struct ad58xx_device *coil, bool on)
{
	int ret;

	if (on) {
		ret = smiapp_power_set_power(coil->vana, 1);
		if (ret < 0)
			return ret;

		if (coil->platform_data->set_xclk != NULL)
			ret = coil->platform_data->set_xclk(&coil->subdev,
						coil->platform_data->ext_clk);

		if (ret < 0) {
			smiapp_power_set_power(coil->vana, 0);
			return ret;
		}

		ret = 0;
	} else {
		if (coil->platform_data->set_xclk != NULL)
			coil->platform_data->set_xclk(&coil->subdev, 0);

		ret = smiapp_power_set_power(coil->vana, 0);
	}

	return ret;
}

static int ad58xx_set_power(struct v4l2_subdev *subdev, int on)
{
	struct ad58xx_device *coil = to_ad58xx_device(subdev);
	int ret = 0;

	if (coil->platform_data == NULL)
		return -ENODEV;

	mutex_lock(&coil->power_lock);

	/* If the power count is modified from 0 to != 0 or from != 0 to 0,
	 * update the power state.
	 */
	if (coil->power_count == !on) {
		ret = __ad58xx_set_power(coil, !!on);
		if (ret < 0)
			goto done;
	}

	/* Update the power count. */
	coil->power_count += on ? 1 : -1;
	WARN_ON(coil->power_count < 0);

	/* Controls set while the power to the sensor is turned off are saved
	 * but not applied to the hardware. Now that we're about to start
	 * streaming apply all the current values to the hardware.
	 */
	if (on)
		v4l2_ctrl_handler_setup(&coil->ctrls);

done:
	mutex_unlock(&coil->power_lock);
	return ret;
}

static int ad58xx_identify_module(struct v4l2_subdev *subdev)
{
	struct ad58xx_device *coil = to_ad58xx_device(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	int i;
	int id;

	id = ad58xx_read(coil, AD58XX_REG_INFO) &
		   (AD58XX_INFO_MAN_ID_MASK | AD58XX_INFO_DEV_ID_MASK);

	if (id < 0) {
		dev_err(&client->dev, "sensor detection failed\n");
		return -ENODEV;
	}

	for (i = 0; i < ARRAY_SIZE(ad58xx_module_idents); i++) {
		if (ad58xx_module_idents[i].id != id)
			continue;

		coil->ident = &ad58xx_module_idents[i];
		strlcpy(subdev->name, coil->ident->name, sizeof(subdev->name));

		return 0;
	}

	dev_err(&client->dev, "unknown module: %01x-%01x\n",
		MODULE_ID_MANUFACTURER(id), MODULE_ID_DEV(id));

	return -ENODEV;
}

static int ad58xx_registered(struct v4l2_subdev *subdev)
{
	struct ad58xx_device *coil = to_ad58xx_device(subdev);
	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	const struct v4l2_ctrl_config *specific_cfg;
	unsigned int i, spec_ctrl_size;
	int rval;

	coil->vana = regulator_get(&client->dev, "VANA");
	if (IS_ERR(coil->vana)) {
		dev_err(&client->dev, "could not get regulator for vana\n");
		return -ENODEV;
	}

	rval = __ad58xx_set_power(coil, true);
	if (rval) {
		dev_err(&client->dev, "unable to power on AD58XX\n");
		return -ENODEV;
	}

	mdelay(2);

	rval = ad58xx_identify_module(subdev);
	if (rval) {
		rval = -ENODEV;
		goto done;
	}

	if (coil->ident->id == AD5817_ID) {
		switch (coil->platform_data->ext_clk) {
		case 4800000:
			ad58xx_set_reg_bits(coil, AD5817_REG_SETTINGS,
					    AD5817_S_SET_CLK_MASK,
					    AD5817_S_SET_CLK_SHIFT,
					    &(coil->settings),
					    AD5817_SET_CLK_EXT_CLK_4_8);
			break;
		case 9600000:
			ad58xx_set_reg_bits(coil, AD5817_REG_SETTINGS,
					    AD5817_S_SET_CLK_MASK,
					    AD5817_S_SET_CLK_SHIFT,
					    &(coil->settings),
					    AD5817_SET_CLK_EXT_CLK_9_6);
			break;
		case 19200000:
			ad58xx_set_reg_bits(coil, AD5817_REG_SETTINGS,
					    AD5817_S_SET_CLK_MASK,
					    AD5817_S_SET_CLK_SHIFT,
					    &(coil->settings),
					    AD5817_SET_CLK_EXT_CLK_19_2);
			break;
		default:
			ad58xx_set_reg_bits(coil, AD5817_REG_SETTINGS,
					    AD5817_S_SET_CLK_MASK,
					    AD5817_S_SET_CLK_SHIFT,
					    &(coil->settings),
					    AD5817_SET_CLK_INT_CLK);
		};
	};

	switch (coil->ident->id) {
	case AD5817_ID:
		spec_ctrl_size = ARRAY_SIZE(ad5817_ctrls);
		specific_cfg = &ad5817_ctrls[0];
		break;
	case AD5836_ID:
		spec_ctrl_size = ARRAY_SIZE(ad5836_ctrls);
		specific_cfg = &ad5836_ctrls[0];
		break;
	case BU8051GWZ_ID:
		spec_ctrl_size = ARRAY_SIZE(bu8051gwz_ctrls);
		specific_cfg = &bu8051gwz_ctrls[0];
		break;
	default:
		spec_ctrl_size = 0;
		specific_cfg = NULL;
	}

	v4l2_ctrl_handler_init(&coil->ctrls, ARRAY_SIZE(ad58xx_ctrls) +
							spec_ctrl_size);

	v4l2_ctrl_new_std(&coil->ctrls, &ad58xx_ctrl_ops,
			  V4L2_CID_FOCUS_ABSOLUTE, 0, 1023, 1, 0);
	for (i = 0; i < ARRAY_SIZE(ad58xx_ctrls); ++i)
		v4l2_ctrl_new_custom(&coil->ctrls, &ad58xx_ctrls[i], NULL);
	for (i = 0; i < spec_ctrl_size; ++i)
		v4l2_ctrl_new_custom(&coil->ctrls, &specific_cfg[i], NULL);

	coil->subdev.ctrl_handler = &coil->ctrls;

done:
	if (__ad58xx_set_power(coil, false)) {
		dev_err(&client->dev, "unable to power OFF AD58XX\n");
		return -ENODEV;
	}

	return rval;
}

static int ad58xx_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return ad58xx_set_power(sd, 1);
}

static int ad58xx_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	return ad58xx_set_power(sd, 0);
}

static const struct v4l2_subdev_core_ops ad58xx_core_ops = {
	.s_power	= ad58xx_set_power,
};

static const struct v4l2_subdev_ops ad58xx_ops = {
	.core = &ad58xx_core_ops,
};

static const struct v4l2_subdev_internal_ops ad58xx_internal_ops = {
	.registered = ad58xx_registered,
	.open = ad58xx_open,
	.close = ad58xx_close,
};

/*
 * I2C driver
 */
static int ad58xx_probe(struct i2c_client *client,
			const struct i2c_device_id *devid)
{
	struct ad58xx_device *coil;
	int ret;

	if (client->dev.platform_data == NULL)
		return -ENODEV;

	coil = kzalloc(sizeof(*coil), GFP_KERNEL);
	if (coil == NULL)
		return -ENOMEM;

	coil->platform_data = client->dev.platform_data;
	mutex_init(&coil->power_lock);

	v4l2_i2c_subdev_init(&coil->subdev, client, &ad58xx_ops);
	coil->subdev.internal_ops = &ad58xx_internal_ops;
	coil->subdev.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	ret = media_entity_init(&coil->subdev.entity, 0, NULL, 0);
	if (ret < 0)
		kfree(coil);

	return ret;
}

static int __exit ad58xx_remove(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ad58xx_device *coil = to_ad58xx_device(subdev);

	if (coil->power_count &&
	    coil->platform_data && coil->platform_data->set_xclk)
			coil->platform_data->set_xclk(&coil->subdev, 0);

	v4l2_device_unregister_subdev(subdev);
	v4l2_ctrl_handler_free(&coil->ctrls);
	media_entity_cleanup(&coil->subdev.entity);

	if (coil->vana)
		regulator_put(coil->vana);

	kfree(coil);

	return 0;
}

#ifdef CONFIG_PM
static int ad58xx_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ad58xx_device *coil = to_ad58xx_device(subdev);

	if (!coil->power_count)
		return 0;

	return __ad58xx_set_power(coil, false);
}

static int ad58xx_resume(struct i2c_client *client)
{
	struct v4l2_subdev *subdev = i2c_get_clientdata(client);
	struct ad58xx_device *coil = to_ad58xx_device(subdev);
	int ret;

	if (!coil->power_count)
		return 0;

	ret = __ad58xx_set_power(coil, true);
	if (ret < 0)
		return ret;

	return ad58xx_focus_change(coil, coil->focus_cpos);
}
#endif /* CONFIG_PM */

static const struct i2c_device_id ad58xx_id_table[] = {
	{ AD58XX_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ad58xx_id_table);

static struct i2c_driver ad58xx_i2c_driver = {
	.driver		= {
		.name	= AD58XX_NAME,
	},
	.probe		= ad58xx_probe,
	.remove		= __exit_p(ad58xx_remove),
#ifdef CONFIG_PM
	.suspend	= ad58xx_suspend,
	.resume		= ad58xx_resume,
#endif
	.id_table	= ad58xx_id_table
};

static int __init ad58xx_init(void)
{
	int rval;

	rval = i2c_add_driver(&ad58xx_i2c_driver);
	if (rval)
		printk(KERN_INFO "%s: failed registering " AD58XX_NAME "\n",
		       __func__);

	return rval;
}

static void __exit ad58xx_exit(void)
{
	i2c_del_driver(&ad58xx_i2c_driver);
}


module_init(ad58xx_init);
module_exit(ad58xx_exit);

MODULE_AUTHOR("Atanas Filipov <afilipov@mm-sol.com>");
MODULE_DESCRIPTION("AD58xx lens driver");
MODULE_LICENSE("GPL");
