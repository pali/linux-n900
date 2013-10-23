/*
 * drivers/media/radio/radio-si4713.h
 *
 * Property and commands definitions for Si4713 radio transmitter chip.
 *
 * Copyright (c) 2008 Instituto Nokia de Tecnologia - INdT
 * Author: Eduardo Valentin <eduardo.valentin@indt.org.br>
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

#ifndef RADIO_SI4713_H
#define RADIO_SI4713_H

#define SI4713_NAME "radio-si4713"

/* The SI4713 I2C sensor chip has a fixed slave address of 0xc6. */
#define SI4713_I2C_ADDR_BUSEN_HIGH	0x63
#define SI4713_I2C_ADDR_BUSEN_LOW	0x11

#define LOCK_LOW_POWER		_IOW('v', BASE_VIDIOC_PRIVATE + 0, unsigned int)
#define RELEASE_LOW_POWER	_IOW('v', BASE_VIDIOC_PRIVATE + 1, unsigned int)

/*
 * Platform dependent definition
 */
struct si4713_platform_data {
	/* Set power state, zero is off, non-zero is on. */
	int (*set_power)(int power);
};

#endif /* ifndef RADIO_SI4713_H*/
