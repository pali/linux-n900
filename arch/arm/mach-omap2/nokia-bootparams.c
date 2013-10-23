/*
 * linux/arch/arm/mach-omap2/nokia-bootparams.c
 *
 * Copyright (C) 2009 Nokia Corporation
 *
 * Author: Roger Quadros <roger.quadros@nokia.com>
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

#include <linux/proc_fs.h>
#include <linux/errno.h>
#include <linux/string.h>

#include <asm/mach-types.h>
#include "board-gpio-export.h"

static char * __initdata gpio_cmdline_data;

/**
 * gpio_init - parse GPIO settings from cmdline
 *
 * The format is "gpio=<name>:<nr>:<type>:[<flag>:<flag>:...] , ..."
 *
 * <name>: GPIO name
 * <nr>: GPIO number
 * <type>: key, sysfs (key are registered as gpio-keys, sysfs through gpiolib)
 * <flag>: inverted, output, init_active
 *
 * GPIOs are comma separated, their parameters colon separated
 */

static int __init gpio_init(void)
{
	struct board_gpio_export_data *gpio_data;
	char *cmdline_local;
	char *cmdline_local_start;
	char *line;
	char *token;
	size_t token_len = 0;
	int is_key = 0;

	if (!gpio_cmdline_data)
		return -EINVAL;

	cmdline_local = kstrdup(gpio_cmdline_data, GFP_KERNEL);
	if (cmdline_local == NULL)
		return -ENOMEM;

	cmdline_local_start = cmdline_local;

	while ((line = strsep(&cmdline_local, ",")) != NULL) {

		token = strsep(&line, ":");
		if (!*token) {
			kfree(cmdline_local_start);
			return -EINVAL;
		}
		token_len = strlen(token);

		gpio_data = kzalloc(sizeof(*gpio_data) + token_len + 1,
				    GFP_KERNEL);
		if (gpio_data == NULL) {
			printk(KERN_ERR "gpio_data: kmalloc failed\n");
			kfree(cmdline_local_start);
			return -ENOMEM;
		}
		strlcpy(gpio_data->name, token, token_len + 1);

		/* get gpio number */
		token = strsep(&line, ":");
		if (!*token) {
			kfree(gpio_data);
			continue;
		}
		if (strict_strtoul(token, 0,
				   (unsigned long *)&gpio_data->gpio)) {
			kfree(gpio_data);
			continue;
		}
		/* get gpio type */
		token = strsep(&line, ":");
		if (!strcmp(token, "key")) {
			is_key = 1;
		} else if (!strcmp(token, "sysfs")) {
			is_key = 0;
		} else {
			printk(KERN_ERR "Unsupported %s gpio type: %s\n",
			       gpio_data->name, token);
			kfree(gpio_data);
			continue;
		}
		/* check flags */
		while ((token = strsep(&line, ":")) != NULL) {
			if (!strcmp(token, "output"))
				gpio_data->flags |=
					BOARD_GPIO_EXPORT_FLAG_OUTPUT;
			if (!strcmp(token, "inverted"))
				gpio_data->flags |=
					BOARD_GPIO_EXPORT_FLAG_INVERTED;
			if (!strcmp(token, "init_active"))
				gpio_data->flags |=
					BOARD_GPIO_EXPORT_FLAG_INIT_ACTIVE;
		}

		if (is_key) {
			if (board_gpio_config_key(gpio_data))
				printk(KERN_ERR "gpio-key %s config failed\n",
				       gpio_data->name);
			kfree(gpio_data);
		} else {
			board_gpio_export_one(gpio_data);
		}
	}
	board_gpio_export_keys();
	kfree(cmdline_local_start);
	return 0;
}

static int __init gpio_setup(char *str)
{
	gpio_cmdline_data = str;
	return 1;
}

__setup("gpio=", gpio_setup);

late_initcall(gpio_init);
