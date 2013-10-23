/*
 * This file is part of twl5031 ACI (Accessory Control Interface) driver
 *
 * Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Tapio Vihuri <tapio.vihuri@nokia.com>
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
#ifndef __ACI_H__
#define __ACI_H__

/*
 * Accessory block register offsets (use TWL5031_MODULE_ACCESSORY)
 */
#define TWL5031_ACIID			0x0
#define TWL5031_ACICOMR_LSB		0x1
#define TWL5031_ACICOMR_MSB		0x2
#define TWL5031_ACITXDAR		0x3
#define TWL5031_ACIRXDAR		0x4
#define TWL5031_ACISPLR			0x9
#define TWL5031_ACIITR			0xA
#define TWL5031_ECI_DBI_CTRL		0xB
#define TWL5031_ACI_AV_CTRL		0xC
#define TWL5031_ACI_AUDIO_CTRL		0xD
#define TWL5031_ACI_BCIA_CTRL		0xE
#define TWL5031_ACCSIR			0x13

/* TWL5031_ACIIxR_xSB bits, ie. ACI IDR/IMR types */
#define ACI_ACCINT	0x001
#define ACI_DREC	0x002
#define ACI_DSENT	0x004
#define ACI_SPDSET	0x008
#define ACI_COMMERR	0x010
#define ACI_FRAERR	0x020
#define ACI_RESERR	0x040
#define ACI_COLL	0x080
#define ACI_NOPINT	0x100
#define ACI_INTERNAL	(ACI_DREC | ACI_DSENT | ACI_SPDSET | ACI_COMMERR | \
			ACI_FRAERR | ACI_RESERR | ACI_COLL | ACI_NOPINT)

/* TWL5031_ACI_AV_CTRL bits */
#define AV_COMP1_EN		0x01
#define AV_COMP2_EN		0x02
#define ADC_SW_EN		0x04
#define STATUS_A1_COMP		0x08
#define STATUS_A2_COMP		0x10
#define HOOK_DET_EN		0x20
#define HOOK_DET_EN_SLEEP_MODE	0x40
#define HSM_GROUNDED_EN		0x80

/* TWL5031_ACI_AV_CTRL bits */
#define HOOK_DEB_BYPASS		0x10

/*----------------------------------------------------------------------*/

/* TWL4030_REG_MICBIAS_CTL bits */
#define HSMICBIAS_EN	0x04

/* TWL5031_ECI_DBI_CTRL bit */
#define ACI_DBI_MODE		(0 << 0)
#define ACI_ACCESSORY_MODE	(1 << 0)
#define ACI_ENABLE		(0 << 1)
#define ACI_DISABLE		(1 << 1)

/* fixed in ECI HW, do not change */
enum {
	ECICMD_HWID,
	ECICMD_SWID,
	ECICMD_ECI_BUS_SPEED,
	ECICMD_MIC_CTRL,
	ECICMD_MASTER_INT_REG,
	ECICMD_HW_CONF_MEM_ACCESS,
	ECICMD_EXTENDED_MEM_ACCESS,
	ECICMD_INDIRECT_MEM_ACCESS,
	ECICMD_PORT_DATA_0,
	ECICMD_PORT_DATA_1,
	ECICMD_PORT_DATA_2,
	ECICMD_PORT_DATA_3,
	ECICMD_LATCHED_PORT_DATA_0,
	ECICMD_LATCHED_PORT_DATA_1,
	ECICMD_LATCHED_PORT_DATA_2,
	ECICMD_LATCHED_PORT_DATA_3,
	ECICMD_DATA_DIR_0,
	ECICMD_DATA_DIR_1,
	ECICMD_DATA_DIR_2,
	ECICMD_DATA_DIR_3,
	ECICMD_INT_CONFIG_0_LOW,
	ECICMD_INT_CONFIG_0_HIGH,
	ECICMD_INT_CONFIG_1_LOW,
	ECICMD_INT_CONFIG_1_HIGH,
	ECICMD_INT_CONFIG_2_LOW,
	ECICMD_INT_CONFIG_2_HIGH,
	ECICMD_INT_CONFIG_3_LOW,
	ECICMD_INT_CONFIG_3_HIGH,
	/* 0x1c-0x2f reserved for future */
	ECICMD_EEPROM_LOCK = 0x3e,
	ECICMD_DUMMY = 0xff,
	ECICMD_PARAM1,
};

/* ECI accessory register's bits */
#define ECI_MIC_AUTO	0x00
#define ECI_MIC_OFF	0x5a
#define ECI_MIC_ON	0xff

#define ECI_INT_ENABLE		1
#define ECI_INT_DELAY_ENABLE	(1 << 1)
#define ECI_INT_LEN_76MS	0
#define ECI_INT_LEN_82MS	(1 << 5)
#define ECI_INT_LEN_37MS	(2 << 5)
#define ECI_INT_LEN_19MS	(3 << 5)
#define ECI_INT_LEN_10MS	(4 << 5)
#define ECI_INT_LEN_5MS		(5 << 5)
#define ECI_INT_LEN_2MS		(6 << 5)
#define ECI_INT_LEN_120US	(7 << 5)

enum {
	ECI_EVENT_PLUG_IN,
	ECI_EVENT_PLUG_OUT,
	ECI_EVENT_BUTTON,
	ECI_EVENT_NO,
};

enum{
	ECI_DISABLED,
	ECI_ENABLED,
};

/* misc ACI defines */
#define ACI_MADC_CHANNEL	10

void twl5031_aci_register(void (*aci_cb)(bool button_down, void *priv),
		void *priv);
void twl5031_eci_register(void (*eci_cb)(int event, void *priv), void *priv);
#endif
