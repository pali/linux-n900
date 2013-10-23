/*
 * @file include/linux/i2c/bcm4751-gps.h
 *
 * Hardware interface to BCM4751 GPS chip.
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Written by Andrei Emeltchenko <andrei.emeltchenko@nokia.com>
 * Modified by Yuri Zaporozhets <ext-yuri.zaporozhets@nokia.com>
 */

#include <linux/miscdevice.h>

#ifndef _LINUX_I2C_BCM4751_GPS_H
#define _LINUX_I2C_BCM4751_GPS_H

/* Max packet sizes for RX and TX */
#define BCM4751_MAX_BINPKT_RX_LEN	64
#define BCM4751_MAX_BINPKT_TX_LEN	64

/* Plaform data, used by the board support file */
struct bcm4751_gps_platform_data {
	int gps_gpio_irq;
	int gps_gpio_enable;
	int gps_gpio_wakeup;
	int	(*setup)(struct i2c_client *client);
	void	(*cleanup)(struct i2c_client *client);
	void	(*enable)(struct i2c_client *client);
	void	(*disable)(struct i2c_client *client);
	void	(*wakeup_ctrl)(struct i2c_client *client, int value);
	int	(*show_irq)(struct i2c_client *client);
};

/* Used internally by the driver */
struct bcm4751_gps_data {
	struct miscdevice		miscdev;
	struct i2c_client		*client;
	struct bcm4751_gps_platform_data *pdata;
	struct mutex                    mutex; /* Serialize things */
	struct regulator_bulk_data	regs[2];
	unsigned int			gpio_irq;
	unsigned int			gpio_enable;
	unsigned int			gpio_wakeup;
	int				enable;
	int				wakeup;
};

#endif
