/*
 * This file is part of the ROHM BH1770GLC / OSRAM SFH7770 sensor driver.
 * Chip is combined proximity and ambient light sensor.
 *
 * Copyright (C) 2010 Nokia Corporation and/or its subsidiary(-ies).
 *
 * Contact: Samu Onkalo <samu.p.onkalo@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __BHSFH_HEADER__
#define __BHSFH_HEADER__

struct bhsfh_chip {
	struct bhsfh_platform_data	*pdata;
	char				chipname[10];
	u8				revision;
	struct i2c_client		*client;
	struct regulator_bulk_data	regs[2];

	/* proximity misc device - depreciated */
	void			*ps_dev_private;
	wait_queue_head_t	*ps_dev_wait;
	loff_t			ps_offset;

	/* Als misc device  - depreciated */
	void			*als_dev_private;
	wait_queue_head_t	*als_dev_wait;
	loff_t			als_offset;

	struct mutex		mutex; /* avoid parallel access */

	/* For buggy chip version  - depreciated */
	struct delayed_work	work;
	int			broken_chip;

	bool			powered;
	bool			int_mode_ps;
	bool			int_mode_als;
	struct delayed_work	ps_work;

	u32	als_cf; /* Chip specific factor */
	u32	als_ga;
	u32	als_calib;
	int	als_rate;
	int	als_users;
	u32     als_corr;
	u16	als_data_raw;
	u16	als_data;
	u16	als_threshold_hi;
	u16	als_threshold_lo;
	u16	als_thres_hi_onchip;
	u16	als_thres_lo_onchip;
	bool    als_wait_interrupt;

	u16	ps_coef;
	u16	ps_const;
	int	ps_rate;
	int	ps_rate_threshold;
	int	ps_users;
	u8	ps_persistence;
	u8	ps_persistence_counter;
	u8	ps_data_raw;
	u8	ps_data;
	u8	ps_threshold;
	u8	ps_threshold_hw;
	bool	ps_force_update;
	u8	ps_abs_thres;
	u8	ps_led;
	u8	ps_channels; /* nbr of leds */
};

extern int bhsfh_ps_init(struct bhsfh_chip *chip);
extern int bhsfh_ps_destroy(struct bhsfh_chip *chip);
extern int bhsfh_proximity_on(struct bhsfh_chip *chip);
extern void bhsfh_proximity_off(struct bhsfh_chip *chip);

extern int bhsfh_als_init(struct bhsfh_chip *chip);
extern int bhsfh_als_destroy(struct bhsfh_chip *chip);
extern int bhsfh_als_on(struct bhsfh_chip *chip);
extern void bhsfh_als_off(struct bhsfh_chip *chip);

extern void bhsfh_handle_buggy_version(struct bhsfh_chip *chip);
#endif
