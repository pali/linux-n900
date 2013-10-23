/*
 * board-gpio-export.c
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

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>

#include "board-gpio-export.h"

#if defined(CONFIG_KEYBOARD_GPIO) || defined(CONFIG_KEYBOARD_GPIO_MODULE)

#define GPIO_DEBOUNCE_TIMEOUT     10

static struct gpio_keys_button gpio_keys[] = {
	{
		.desc                   = "keypad_slide",
		.type                   = EV_SW,
		.code                   = SW_KEYPAD_SLIDE,
		.debounce_interval      = GPIO_DEBOUNCE_TIMEOUT,
		.can_disable            = true,
	}, {
		.desc                   = "cam_focus",
		.type                   = EV_KEY,
		.code                   = KEY_CAMERA_FOCUS,
		.debounce_interval      = GPIO_DEBOUNCE_TIMEOUT,
		.can_disable            = true,
	}, {
		.desc                   = "cam_capture",
		.type                   = EV_KEY,
		.code                   = KEY_CAMERA,
		.debounce_interval      = GPIO_DEBOUNCE_TIMEOUT,
		.can_disable            = true,
	},
};

static struct gpio_keys_platform_data gpio_keys_data = {
	.buttons        = gpio_keys,
	.nbuttons       = 0,
};

static struct platform_device gpio_keys_device = {
	.name   = "gpio-keys",
	.id     = -1,
	.dev    = {
		.platform_data  = &gpio_keys_data,
	},
};

int board_gpio_config_key(const struct board_gpio_export_data *gpio_data)
{
	int i;
	int key_index = gpio_keys_data.nbuttons;

	for (i = key_index; i < ARRAY_SIZE(gpio_keys); i++) {
		if (!strcmp(gpio_data->name, gpio_keys[i].desc)) {
			/* put matched key next in gpio_keys[] */
			if (key_index != i) {
				struct gpio_keys_button tmp;

				tmp = gpio_keys[key_index];
				gpio_keys[key_index] = gpio_keys[i];
				gpio_keys[i] = tmp;
			}
			gpio_keys[key_index].gpio = gpio_data->gpio;
			gpio_keys[key_index].active_low =
				!!(gpio_data->flags &
				   BOARD_GPIO_EXPORT_FLAG_INVERTED);
			gpio_keys_data.nbuttons++;
			return 0;
		}
	}
	return -EINVAL;
}

void board_gpio_export_keys(void)
{
	platform_device_register(&gpio_keys_device);
}
#else
void board_gpio_export_keys(void)
{
}

int board_gpio_config_key(const struct board_gpio_export_data *gpio_data)
{
	return 0;
}
#endif  /* CONFIG_KEYBOARD_GPIO || CONFIG_KEYBOARD_GPIO_MODULE */

int board_gpio_export_one(const struct board_gpio_export_data *d)
{
	int s;

	s = gpio_request(d->gpio, d->name);
	if (s)
		goto err;

	if (d->flags & BOARD_GPIO_EXPORT_FLAG_OUTPUT) {
		int value = d->flags & BOARD_GPIO_EXPORT_FLAG_INIT_ACTIVE;

		if (d->flags & BOARD_GPIO_EXPORT_FLAG_INVERTED)
			value = !value;

		s = gpio_direction_output(d->gpio, value);
	} else {
		s = gpio_direction_input(d->gpio);
	}

	if (s)
		goto err_free;

	s = gpio_sysfs_set_active_low(d->gpio,
				d->flags & BOARD_GPIO_EXPORT_FLAG_INVERTED);
	if (s)
		goto err_free;

	s = gpio_export(d->gpio, 0);
	if (s)
		goto err_free;

	return 0;

err_free:
	gpio_free(d->gpio);

err:
	printk(KERN_ERR "gpio %d (%s) export failed (%d)\n",
		d->gpio, d->name, s);

	return s;
}

int board_gpio_export(const struct board_gpio_export_data *gpio_data, int n)
{
	int i, s;

	for (i = 0; i < n; i++) {
		s = board_gpio_export_one(&gpio_data[i]);
		if (s)
			goto err;
	}

	return 0;

err:
	while (--i >= 0)
		gpio_free(gpio_data[i].gpio);

	return s;
}
