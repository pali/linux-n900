/*
 * board-gpio-export.h
 *
 * Copyright (C) 2009 Nokia Corporation
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

#ifndef BOARD_GPIO_EXPORT_H
#define BOARD_GPIO_EXPORT_H

#define BOARD_GPIO_EXPORT_FLAG_OUTPUT		(1 << 0)
#define BOARD_GPIO_EXPORT_FLAG_INVERTED		(1 << 1)
#define BOARD_GPIO_EXPORT_FLAG_INIT_ACTIVE	(1 << 2)

struct board_gpio_export_data {
	unsigned int	gpio;
	unsigned	flags;
	char 		name[0];
};

int board_gpio_export(const struct board_gpio_export_data *gpio_data, int n);
int board_gpio_export_one(const struct board_gpio_export_data *gpio_data);
int board_gpio_config_key(const struct board_gpio_export_data *gpio_data);
void board_gpio_export_keys(void);

#endif /* BOARD_GPIO_EXPORT_H */
