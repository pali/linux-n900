/*
 * aic34b_dummy.c  --  Dummy driver for AIC34 block B parts used in Nokia RX51
 *
 * Purpose for this driver is to cover few audio connections on Nokia RX51 HW
 * which are connected into block B of TLV320AIC34 dual codec.
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Jarkko Nikula <jarkko.nikula@nokia.com>
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
 * TODO:
 * - Get rid of this driver, at least when ASoC v2 is merged and when
 *   we can support multiple codec instances in tlv320aic3x.c driver.
 *   This driver is hacked only for Nokia RX51 HW.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/i2c.h>
#include <sound/soc.h>

#include "../codecs/tlv320aic3x.h"

struct i2c_client *aic34b_client;
static DEFINE_MUTEX(aic34b_mutex);
static DEFINE_MUTEX(button_press_mutex);
static ktime_t button_press_denial_start;
static int aic34b_volume;
static int button_press_denied;
static int aic34b_bias;


static int aic34b_read(struct i2c_client *client, unsigned int reg,
		       u8 *value)
{
	int err;

	err = i2c_smbus_read_byte_data(client, reg);
	*value = err;
	return (err >= 0) ? 0 : err;
}

static int aic34b_write(struct i2c_client *client, unsigned int reg,
			u8 value)
{
	u8 data[2];

	data[0] = reg & 0xff;
	data[1] = value & 0xff;

	return (i2c_master_send(client, data, 2) == 2) ? 0 : -EIO;
}

/*
 * Introduce a derivative FIR filter to detect unnecessary button
 * presses caused by a change in the MICBIAS. The filter returns
 * TRUE in the event there has not been a change in MICBIAS within
 * the time window (500ms). If the rate of change within the window
 * is >= 1, all button presses are denied. In addition, if bias is
 * zero, then all button presses are also denied explicitly.
 */
int allow_button_press(void)
{
	/* If bias is not on, no chance for button presses */
	if (!aic34b_bias)
		return 0;

	/* If explicitly granted a button press */
	if (!button_press_denied) {
		return 1;
	} else	{
		int64_t delta;
		/* This is the FIR portion with specified time window */
		mutex_lock(&button_press_mutex);
		delta = ktime_to_ns(ktime_sub(ktime_get(),
					button_press_denial_start));

		if (delta < 0) {
			button_press_denied = 0;
			/* If the clock ever wraps */
			button_press_denial_start.tv.sec = 0;
			button_press_denial_start.tv.nsec = 0;
			mutex_unlock(&button_press_mutex);
			return 1;
		}
		do_div(delta, 1000000);
		/* Time window is 500ms */
		if (delta >= 500) {
			button_press_denied = 0;
			mutex_unlock(&button_press_mutex);
			return 1;
		}
		mutex_unlock(&button_press_mutex);
	}

	/* There was a change in MICBIAS within time window */
	return 0;
}
EXPORT_SYMBOL(allow_button_press);

static void deny_button_press(void)
{
	mutex_lock(&button_press_mutex);
	button_press_denied = 1;
	button_press_denial_start = ktime_get();
	mutex_unlock(&button_press_mutex);
}

void aic34b_set_mic_bias(int bias)
{
	if (aic34b_client == NULL)
		return;

	mutex_lock(&aic34b_mutex);
	aic34b_write(aic34b_client, MICBIAS_CTRL, (bias & 0x3) << 6);
	aic34b_bias = bias;
	deny_button_press();
	mutex_unlock(&aic34b_mutex);
}
EXPORT_SYMBOL(aic34b_set_mic_bias);

int aic34b_get_mic_bias(void)
{
	return aic34b_bias;
}
EXPORT_SYMBOL(aic34b_get_mic_bias);

int aic34b_set_volume(u8 volume)
{
	u8 val;

	if (aic34b_client == NULL)
		return 0;

	mutex_lock(&aic34b_mutex);

	/* Volume control for Right PGA to HPLOUT */
	aic34b_read(aic34b_client, 49, &val);
	val &= ~0x7f;
	aic34b_write(aic34b_client, 49, val | (~volume & 0x7f));

	/* Volume control for Right PGA to HPLCOM */
	aic34b_read(aic34b_client, 56, &val);
	val &= ~0x7f;
	aic34b_write(aic34b_client, 56, val | (~volume & 0x7f));

	aic34b_volume = volume;
	mutex_unlock(&aic34b_mutex);

	return 0;
}
EXPORT_SYMBOL(aic34b_set_volume);

void aic34b_ear_enable(int enable)
{
	u8 val;

	if (aic34b_client == NULL)
		return;

	mutex_lock(&aic34b_mutex);
	if (enable) {
		/* Connect LINE2R to RADC */
		aic34b_write(aic34b_client, LINE2R_2_RADC_CTRL, 0x80);
		/* Unmute Right ADC-PGA */
		aic34b_write(aic34b_client, RADC_VOL, 0x00);
		/* Right PGA -> HPLOUT */
		aic34b_read(aic34b_client, 49, &val);
		aic34b_write(aic34b_client, 49, val | 0x80);
		/* Unmute HPLOUT with 1 dB gain */
		aic34b_write(aic34b_client, HPLOUT_CTRL, 0x19);
		/* Right PGA -> HPLCOM */
		aic34b_read(aic34b_client, 56, &val);
		aic34b_write(aic34b_client, 56, val | 0x80);
		/* Unmute HPLCOM with 1 dB gain */
		aic34b_write(aic34b_client, HPLCOM_CTRL, 0x19);
	} else {
		/* Disconnect LINE2R from RADC */
		aic34b_write(aic34b_client, LINE2R_2_RADC_CTRL, 0xF8);
		/* Mute Right ADC-PGA */
		aic34b_write(aic34b_client, RADC_VOL, 0x80);
		/* Detach Right PGA from HPLOUT */
		aic34b_write(aic34b_client, 49, (~aic34b_volume & 0x7f));
		/* Power down HPLOUT */
		aic34b_write(aic34b_client, HPLOUT_CTRL, 0x06);
		/* Detach Right PGA from HPLCOM */
		aic34b_write(aic34b_client, 56, (~aic34b_volume & 0x7f));
		/* Power down HPLCOM */
		aic34b_write(aic34b_client, HPLCOM_CTRL, 0x06);
		/* Deny any possible keypresses for a second */
		deny_button_press();
		/* To regain low power consumption, reset is needed */
		aic34b_write(aic34b_client, AIC3X_RESET, SOFT_RESET);
		/* And need to restore volume level */
		aic34b_write(aic34b_client, 49, (~aic34b_volume & 0x7f));
		aic34b_write(aic34b_client, 56, (~aic34b_volume & 0x7f));
		/* Need to restore MICBIAS if set */
		if (aic34b_bias)
			aic34b_write(aic34b_client, MICBIAS_CTRL,
					(aic34b_bias & 0x3) << 6);
	}
	mutex_unlock(&aic34b_mutex);
}
EXPORT_SYMBOL(aic34b_ear_enable);

static int aic34b_dummy_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	u8 val;

	if (aic34b_read(client, AIC3X_PLL_PROGA_REG, &val) || val != 0x10) {
		/* Chip not present */
		return -ENODEV;
	}
	aic34b_client = client;

	/* Configure LINE2R for differential mode */
	aic34b_read(client, LINE2R_2_RADC_CTRL, &val);
	aic34b_write(client, LINE2R_2_RADC_CTRL, val | 0x80);

	return 0;
}

static int aic34b_dummy_remove(struct i2c_client *client)
{
	aic34b_client = NULL;

	return 0;
}

static const struct i2c_device_id aic34b_dummy_id[] = {
	{ "aic34b_dummy", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aic34b_dummy_id);

static struct i2c_driver aic34b_dummy_driver = {
	.driver = {
		.name	= "aic34b_dummy"
	},
	.probe		= aic34b_dummy_probe,
	.remove		= aic34b_dummy_remove,
	.id_table	= aic34b_dummy_id,
};

static int __init aic34b_dummy_init(void)
{
	return i2c_add_driver(&aic34b_dummy_driver);
}

static void __exit aic34b_dummy_exit(void)
{
	i2c_del_driver(&aic34b_dummy_driver);
}

MODULE_AUTHOR();
MODULE_DESCRIPTION("Dummy driver for AIC34 block B parts used on Nokia RX51");
MODULE_LICENSE("GPL");

module_init(aic34b_dummy_init);
module_exit(aic34b_dummy_exit);
