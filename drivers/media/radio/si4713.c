/*
 * drivers/media/radio/si4713.c
 *
 * Silicon Labs Si4713 FM Radio Transmitter I2C commands.
 *
 * Copyright (c) 2008 Instituto Nokia de Tecnologia - INdT
 * Author: Eduardo Valentin <eduardo.valentin@indt.org.br>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <linux/mutex.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/i2c.h>

#include "si4713.h"

#define MAX_ARGS 7

#define RDS_BLOCK			8
#define RDS_BLOCK_CLEAR			0x03
#define RDS_BLOCK_LOAD			0x04
#define RDS_RADIOTEXT_2A		0x20
#define RDS_RADIOTEXT_BLK_SIZE		4
#define RDS_RADIOTEXT_INDEX_MAX		0x0F
#define RDS_CARRIAGE_RETURN		0x0D

#define rds_ps_nblocks(len)	((len / RDS_BLOCK) + (len % RDS_BLOCK ? 1 : 0))
#define enable_rds(p)		(p | (1 << 2))
#define disable_rds(p)		(p & ~(1 << 2))
#define get_rds_status(p)	((p >> 2) & 0x01)

#define enable_stereo(p)	(p | (1 << 1))
#define disable_stereo(p)	(p & ~(1 << 1))
#define get_stereo_status(p)	((p >> 1) & 0x01)

#define enable_limiter(p)	(p | (1 << 1))
#define disable_limiter(p)	(p & ~(1 << 1))
#define get_limiter_status(p)	((p >> 1) & 0x01)

#define enable_pilot(p)		(p | (1 << 0))
#define disable_pilot(p)	(p & ~(1 << 0))
#define get_pilot_status(p)	((p >> 0) & 0x01)

#define enable_acomp(p)		(p | (1 << 0))
#define disable_acomp(p)	(p & ~(1 << 0))
#define get_acomp_status(p)	((p >> 0) & 0x01)
#define ATTACK_TIME_UNIT	500

#define DEFAULT_RDS_PI			0x00
#define DEFAULT_RDS_PTY			0x00
#define DEFAULT_RDS_PS_NAME		"Si4713  "
#define DEFAULT_RDS_RADIO_TEXT		DEFAULT_RDS_PS_NAME
#define DEFAULT_RDS_DEVIATION		0x00C8
#define DEFAULT_RDS_PS_REPEAT_COUNT	0x0003
#define DEFAULT_LIMITER_RTIME		0x1392
#define DEFAULT_LIMITER_DEV		0x102CA
#define DEFAULT_PILOT_FREQUENCY 	0x4A38
#define DEFAULT_PILOT_DEVIATION		0x1A5E
#define DEFAULT_ACOMP_ATIME		0x0000
#define DEFAULT_ACOMP_RTIME		0xF4240L
#define DEFAULT_ACOMP_GAIN		0x0F
#define DEFAULT_ACOMP_THRESHOLD 	(-0x28)
#define DEFAULT_REGION_SETTINGS		0x02
#define DEFAULT_MUTE			0x00
#define DEFAULT_POWER_LEVEL		88
#define DEFAULT_TUNE_RSSI		0xFF
#define DEFAULT_TONE_FREQUENCY	 	0x00
#define DEFAULT_TONE_DEVIATION		0x00
#define DEFAULT_TONE_ON_TIME 		0x00
#define DEFAULT_TONE_OFF_TIME		0x00

#define POWER_OFF			0x00
#define POWER_ON			0x01

#define msb(x)                  ((u8)((u16) x >> 8))
#define lsb(x)                  ((u8)((u16) x &  0x00FF))
#define compose_u16(msb, lsb)	(((u16)msb << 8) | lsb)
#define check_command_failed(status)	(!(status & SI4713_CTS) || \
					(status & SI4713_ERR))
/* mute definition */
#define set_mute(p)	((p & 1) | ((p & 1) << 1));
#define get_mute(p)	(p & 0x01)
#define set_pty(v, pty)	((v & 0xFC1F) | (pty << 5))
#define get_pty(v)	((v >> 5) & 0x1F)


#ifdef DEBUG
#define DBG_BUFFER(device, message, buffer, size)			\
	{								\
		int i;							\
		char str[(size)*5];					\
		for (i = 0; i < size; i++)				\
			sprintf(str + i * 5, " 0x%02x", buffer[i]);	\
		dev_dbg(device, "%s:%s\n", message, str);		\
	}
#else
#define DBG_BUFFER(device, message, buffer, size)
#endif

/*
 * Values for limiter release time
 *	device	release
 *	value	time (us)
 */
static unsigned long const limiter_times[] = {
	2000,	250,
	1000,	500,
	510,	1000,
	255,	2000,
	170,	3000,
	127,	4020,
	102,	5010,
	85,	6020,
	73,	7010,
	64,	7990,
	57,	8970,
	51,	10030,
	25,	20470,
	17,	30110,
	13,	39380,
	10,	51190,
	8,	63690,
	7,	73140,
	6,	85330,
	5,	102390,
};

/*
 * Values for audio compression release time
 *	device	release
 *	value	time (us)
 */
static unsigned long const acomp_rtimes[] = {
	0,	100000,
	1,	200000,
	2,	350000,
	3,	525000,
	4,	1000000,
};

/*
 * Values for region specific configurations
 * (spacing, bottom and top frequencies, preemphasis)
 */
static struct region_info region_configs[] = {
	/* USA */
	{
		.channel_spacing	= 20,
		.bottom_frequency	= 8750,
		.top_frequency		= 10800,
		.preemphasis		= 0,
		.region			= 0,
	},
	/* Australia */
	{
		.channel_spacing	= 20,
		.bottom_frequency	= 8750,
		.top_frequency		= 10800,
		.preemphasis		= 1,
		.region			= 1,
	},
	/* Europe */
	{
		.channel_spacing	= 10,
		.bottom_frequency	= 8750,
		.top_frequency		= 10800,
		.preemphasis		= 1,
		.region			= 2,
	},
	/* Japan */
	{
		.channel_spacing	= 10,
		.bottom_frequency	= 7600,
		.top_frequency		= 9000,
		.preemphasis		= 1,
		.region			= 3,
	},
	/* Japan wide band */
	{
		.channel_spacing	= 10,
		.bottom_frequency	= 7600,
		.top_frequency		= 10800,
		.preemphasis		= 1,
		.region			= 4,
	},
};

static int usecs_to_dev(unsigned long usecs, unsigned long const array[],
			int size)
{
	int i;
	int rval = -EINVAL;

	for (i = 0; i < size / 2; i++)
		if (array[(i * 2) + 1] >= usecs) {
			rval = array[i * 2];
			break;
		}

	return rval;
}

static unsigned long dev_to_usecs(int value, unsigned long const array[],
			int size)
{
	int i;
	int rval = -EINVAL;

	for (i = 0; i < size / 2; i++)
		if (array[i * 2] == value) {
			rval = array[(i * 2) + 1];
			break;
		}

	return rval;
}

/*
 * si4713_send_command - sends a command to si4713 and waits its response
 * @sdev: si4713_device structure for the device we are communicating
 * @command: command id
 * @args: command arguments we are sending (up to 7)
 * @argn: actual size of @args
 * @response: buffer to place the expected response from the device (up to 15)
 * @respn: actual size of @response
 * @usecs: amount of time to wait before reading the response (in usecs)
 */
static int si4713_send_command(struct si4713_device *sdev, const u8 command,
				const u8 args[], const int argn,
				u8 response[], const int respn, const int usecs)
{
	struct i2c_client *client = sdev->client;
	u8 data1[MAX_ARGS + 1];
	int err;

	if (!client->adapter)
		return -ENODEV;

	/* First send the command and its arguments */
	data1[0] = command;
	memcpy(data1 + 1, args, argn);
	DBG_BUFFER(&client->dev, "Parameters", data1, argn + 1);

	err = i2c_master_send(client, data1, argn + 1);
	if (err != argn + 1) {
		dev_err(&client->dev, "Error while sending command 0x%02x\n",
			command);
		return (err > 0) ? -EIO : err;
	}

	/* Wait response from interrupt */
	if (!wait_for_completion_timeout(&sdev->work,
				usecs_to_jiffies(usecs) + 1))
		dev_dbg(&client->dev, "Device took too much time.\n");

	/* Then get the response */
	err = i2c_master_recv(client, response, respn);
	if (err != respn) {
		dev_err(&client->dev,
			"Error while reading response for command 0x%02x\n",
			command);
		return (err > 0) ? -EIO : err;
	}

	DBG_BUFFER(&client->dev, "Response", response, respn);
	if (check_command_failed(response[0]))
		return -EBUSY;

	return 0;
}

/*
 * si4713_read_property - reads a si4713 property
 * @sdev: si4713_device structure for the device we are communicating
 * @prop: property identification number
 */
static int si4713_read_property(struct si4713_device *sdev, u16 prop)
{
	int err;
	u8 val[SI4713_GET_PROP_NRESP];
	/*
	 * REVISIT: From Programming Manual
	 * 	.First byte = 0
	 * 	.Second byte = property's MSB
	 * 	.Third byte = property's LSB
	 */
	const u8 args[SI4713_GET_PROP_NARGS] = {
		0x00,
		msb(prop),
		lsb(prop),
	};

	err = si4713_send_command(sdev, SI4713_CMD_GET_PROPERTY,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (err < 0)
		return err;

	dev_dbg(&sdev->client->dev, "Status from read prop: 0x%02x\n", val[0]);

	return compose_u16(val[2], val[3]);
}

/*
 * si4713_write_property - modifies a si4713 property
 * @sdev: si4713_device structure for the device we are communicating
 * @prop: property identification number
 * @val: new value for that property
 */
static int si4713_write_property(struct si4713_device *sdev, u16 prop, u16 val)
{
	int rval;
	u8 resp[SI4713_SET_PROP_NRESP];
	/*
	 * REVISIT: From Programming Manual
	 * 	.First byte = 0
	 * 	.Second byte = property's MSB
	 * 	.Third byte = property's LSB
	 * 	.Fourth byte = value's MSB
	 * 	.Fifth byte = value's LSB
	 */
	const u8 args[SI4713_SET_PROP_NARGS] = {
		0x00,
		msb(prop),
		lsb(prop),
		msb(val),
		lsb(val),
	};

	rval = si4713_send_command(sdev, SI4713_CMD_SET_PROPERTY,
					args, ARRAY_SIZE(args),
					resp, ARRAY_SIZE(resp),
					DEFAULT_TIMEOUT);

	if (rval < 0)
		return rval;

	dev_dbg(&sdev->client->dev, "Status from write prop: 0x%02x\n",
		resp[0]);

	/*
	 * As there is no command response for SET_PROPERTY,
	 * wait Tcomp time to finish before proceed, in order
	 * to have property properly set.
	 */
	msleep(TIMEOUT_SET_PROPERTY);

	return rval;
}

/*
 * si4713_powerup - Powers the device up
 * @sdev: si4713_device structure for the device we are communicating
 */
static int si4713_powerup(struct si4713_device *sdev)
{
	int err;
	u8 resp[SI4713_PWUP_NRESP];
	/*
	 * REVISIT: From Programming Manual
	 * 	.First byte = Enabled interrupts and boot function
	 * 	.Second byte = Input operation mode
	 */
	const u8 args[SI4713_PWUP_NARGS] = {
		SI4713_PWUP_CTSIEN | SI4713_PWUP_GPO2OEN | SI4713_PWUP_FUNC_TX,
		SI4713_PWUP_OPMOD_ANALOG,
	};

	if (sdev->power_state)
		return 0;

	sdev->platform_data->set_power(1);
	err = si4713_send_command(sdev, SI4713_CMD_POWER_UP,
					args, ARRAY_SIZE(args),
					resp, ARRAY_SIZE(resp),
					TIMEOUT_POWER_UP);

	if (!err) {
		dev_dbg(&sdev->client->dev, "Powerup response: 0x%02x\n",
			resp[0]);
		dev_dbg(&sdev->client->dev, "Device in power up mode\n");
		sdev->power_state = POWER_ON;

		err = si4713_write_property(sdev, SI4713_GPO_IEN,
						SI4713_STC_INT | SI4713_CTS);
	} else {
		sdev->platform_data->set_power(0);
	}

	return err;
}

/*
 * si4713_powerdown - Powers the device down
 * @sdev: si4713_device structure for the device we are communicating
 */
static int si4713_powerdown(struct si4713_device *sdev)
{
	int err;
	u8 resp[SI4713_PWDN_NRESP];

	if (!sdev->power_state)
		return 0;

	err = si4713_send_command(sdev, SI4713_CMD_POWER_DOWN,
					NULL, 0,
					resp, ARRAY_SIZE(resp),
					DEFAULT_TIMEOUT);

	if (!err) {
		dev_dbg(&sdev->client->dev, "Power down response: 0x%02x\n",
			resp[0]);
		dev_dbg(&sdev->client->dev, "Device in reset mode\n");
		sdev->platform_data->set_power(0);
		sdev->power_state = POWER_OFF;
	}

	return err;
}

/*
 * si4713_checkrev - Checks if we are treating a device with the correct rev.
 * @sdev: si4713_device structure for the device we are communicating
 */
#define pr_revision(devicep, buffer)					\
	dev_info(devicep, "Detected %s (0x%02x) Firmware: %d.%d"	\
			  " Patch ID: %02x:%02x Component: %d.%d"	\
			  " Chip Rev.: %s\n",				\
			buffer[1] == SI4713_PRODUCT_NUMBER ? "Si4713" : "",\
			buffer[1],					\
			buffer[2] & 0xF, buffer[3] & 0xF,		\
			buffer[4], buffer[5],				\
			buffer[6] & 0xF, buffer[7] & 0xF,		\
			buffer[8] == 0x41 ? "revA" : "unknown")
static int si4713_checkrev(struct si4713_device *sdev)
{
	int rval;
	u8 resp[SI4713_GETREV_NRESP];

	mutex_lock(&sdev->mutex);

	rval = si4713_send_command(sdev, SI4713_CMD_GET_REV,
					NULL, 0,
					resp, ARRAY_SIZE(resp),
					DEFAULT_TIMEOUT);

	if (rval < 0)
		goto unlock;

	if (resp[1] == SI4713_PRODUCT_NUMBER) {
		pr_revision(&sdev->client->dev, resp);
	} else {
		dev_err(&sdev->client->dev, "Invalid product number\n");
		rval = -EINVAL;
	}

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

/*
 * si4713_wait_stc - Waits STC interrupt and clears status bits. Usefull
 *		     for TX_TUNE_POWER, TX_TUNE_FREQ and TX_TUNE_MEAS
 * @sdev: si4713_device structure for the device we are communicating
 * @usecs: timeout to wait for STC interrupt signal
 */
static int si4713_wait_stc(struct si4713_device *sdev, const int usecs)
{
	int err;
	u8 resp[SI4713_GET_STATUS_NRESP];

	/* Wait response from STC interrupt */
	if (!wait_for_completion_timeout(&sdev->work,
			usecs_to_jiffies(TIMEOUT_TX_TUNE) + 1))
		dev_dbg(&sdev->client->dev, "Device took too much time.\n");

	/* Clear status bits */
	err = si4713_send_command(sdev, SI4713_CMD_GET_INT_STATUS,
					NULL, 0,
					resp, ARRAY_SIZE(resp),
					DEFAULT_TIMEOUT);

	if (err < 0)
		goto exit;

	dev_dbg(&sdev->client->dev, "Status bits: 0x%02x\n", resp[0]);

	if (!(resp[0] & SI4713_STC_INT))
		err = -EIO;

exit:
	return err;
}

/*
 * si4713_tx_tune_freq - Sets the state of the RF carrier and sets the tuning
 * 			frequency between 76 and 108 MHz in 10 kHz units and
 * 			steps of 50 kHz.
 * @sdev: si4713_device structure for the device we are communicating
 * @frequency: desired frequency (76 - 108 MHz, unit 10 KHz, step 50 kHz)
 */
static int si4713_tx_tune_freq(struct si4713_device *sdev, u16 frequency)
{
	int err;
	u8 val[SI4713_TXFREQ_NRESP];
	/*
	 * REVISIT: From Programming Manual
	 * 	.First byte = 0
	 * 	.Second byte = frequency's MSB
	 * 	.Third byte = frequency's LSB
	 */
	const u8 args[SI4713_TXFREQ_NARGS] = {
		0x00,
		msb(frequency),
		lsb((frequency -
			(frequency % sdev->region_info.channel_spacing))),
	};

	if (frequency < sdev->region_info.bottom_frequency ||
		frequency > sdev->region_info.top_frequency)
		return -EDOM;

	err = si4713_send_command(sdev, SI4713_CMD_TX_TUNE_FREQ,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (err < 0)
		return err;

	dev_dbg(&sdev->client->dev, "Status from tx tune freq: 0x%02x\n",
		val[0]);

	err = si4713_wait_stc(sdev, TIMEOUT_TX_TUNE);
	if (err < 0)
		return err;

	return compose_u16(args[1], args[2]);
}

/*
 * si4713_tx_tune_power - Sets the RF voltage level between 88 and 115 dBuV in
 * 			1 dB units. A value of 0x00 indicates off. The command
 * 			also sets the antenna tuning capacitance. A value of 0
 * 			indicates autotuning, and a value of 1 - 191 indicates
 * 			a manual override, which results in a tuning
 * 			capacitance of 0.25 pF x @antcap.
 * @sdev: si4713_device structure for the device we are communicating
 * @power: tuning power (88 - 115 dBuV, unit/step 1 dB)
 * @antcap: value of antenna tuning capacitor (0 - 191)
 */
static int si4713_tx_tune_power(struct si4713_device *sdev, u8 power,
				u8 antcap)
{
	int err;
	u8 val[SI4713_TXPWR_NRESP];
	/*
	 * REVISIT: From Programming Manual
	 * 	.First byte = 0
	 * 	.Second byte = 0
	 * 	.Third byte = power
	 * 	.Fourth byte = antcap
	 */
	const u8 args[SI4713_TXPWR_NARGS] = {
		0x00,
		0x00,
		power,
		antcap,
	};

	if (((power > 0) && (power < SI4713_MIN_POWER)) ||
		power > SI4713_MAX_POWER || antcap > SI4713_MAX_ANTCAP)
		return -EDOM;

	err = si4713_send_command(sdev, SI4713_CMD_TX_TUNE_POWER,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (err < 0)
		return err;

	dev_dbg(&sdev->client->dev, "Status from tx tune power: 0x%02x\n",
		val[0]);

	return si4713_wait_stc(sdev, TIMEOUT_TX_TUNE_POWER);
}

/*
 * si4713_tx_tune_measure - Enters receive mode and measures the received noise
 * 			level in units of dBuV on the selected frequency.
 * 			The Frequency must be between 76 and 108 MHz in 10 kHz
 * 			units and steps of 50 kHz. The command also sets the
 * 			antenna	tuning capacitance. A value of 0 means
 * 			autotuning, and a value of 1 to 191 indicates manual
 * 			override.
 * @sdev: si4713_device structure for the device we are communicating
 * @frequency: desired frequency (76 - 108 MHz, unit 10 KHz, step 50 kHz)
 * @antcap: value of antenna tuning capacitor (0 - 191)
 */
static int si4713_tx_tune_measure(struct si4713_device *sdev, u16 frequency,
					u8 antcap)
{
	int err;
	u8 val[SI4713_TXMEA_NRESP];
	/*
	 * REVISIT: From Programming Manual
	 * 	.First byte = 0
	 * 	.Second byte = frequency's MSB
	 * 	.Third byte = frequency's LSB
	 * 	.Fourth byte = antcap
	 */
	const u8 args[SI4713_TXMEA_NARGS] = {
		0x00,
		msb(frequency),
		lsb((frequency -
			(frequency % sdev->region_info.channel_spacing))),
		antcap,
	};

	sdev->tune_rssi = DEFAULT_TUNE_RSSI;

	if (frequency < sdev->region_info.bottom_frequency ||
		frequency > sdev->region_info.top_frequency ||
		antcap > SI4713_MAX_ANTCAP)
		return -EDOM;

	err = si4713_send_command(sdev, SI4713_CMD_TX_TUNE_MEASURE,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (err < 0)
		return err;

	dev_dbg(&sdev->client->dev, "Status from tx tune measure: 0x%02x\n",
		val[0]);

	return si4713_wait_stc(sdev, TIMEOUT_TX_TUNE);
}

/*
 * si4713_tx_tune_status- Returns the status of the tx_tune_freq, tx_tune_mea or
 * 			tx_tune_power commands. This command return the current
 * 			frequency, output voltage in dBuV, the antenna tunning
 * 			capacitance value and the received noise level. The
 * 			command also clears the stcint interrupt bit when the
 * 			first bit of its arguments is high.
 * @sdev: si4713_device structure for the device we are communicating
 * @intack: 0x01 to clear the seek/tune complete interrupt status indicator.
 * @frequency: returned frequency
 * @power: returned power
 * @antcap: returned antenna capacitance
 * @noise: returned noise level
 */
static int si4713_tx_tune_status(struct si4713_device *sdev, u8 intack,
					u16 *frequency,	u8 *power,
					u8 *antcap, u8 *noise)
{
	int err;
	u8 val[SI4713_TXSTATUS_NRESP];
	/*
	 * REVISIT: From Programming Manual
	 * 	.First byte = intack bit
	 */
	const u8 args[SI4713_TXSTATUS_NARGS] = {
		intack & SI4713_INTACK_MASK,
	};

	err = si4713_send_command(sdev, SI4713_CMD_TX_TUNE_STATUS,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (!err) {
		dev_dbg(&sdev->client->dev,
			"Status from tx tune status: 0x%02x\n", val[0]);
		*frequency = compose_u16(val[2], val[3]);
		sdev->frequency = *frequency;
		*power = val[5];
		*antcap = val[6];
		*noise = val[7];
		dev_dbg(&sdev->client->dev, "Tune status: %d x 10 kHz "
				"(power %d, antcap %d, rnl %d)\n",
				*frequency, *power, *antcap, *noise);
	}

	return err;
}

/*
 * si4713_tx_rds_buff - Loads the RDS group buffer FIFO or circular buffer.
 * @sdev: si4713_device structure for the device we are communicating
 * @mode: the buffer operation mode.
 * @rdsb: RDS Block B
 * @rdsc: RDS Block C
 * @rdsd: RDS Block D
 * @intstatus: returns current interrupt status
 * @cbavail: returns the number of available circular buffer blocks.
 * @cbused: returns the number of used circular buffer blocks.
 * @fifoavail: returns the number of available fifo buffer blocks.
 * @fifoused: returns the number of used fifo buffer blocks.
 */
static int si4713_tx_rds_buff(struct si4713_device *sdev, u8 mode, u16 rdsb,
				u16 rdsc, u16 rdsd, u8 *intstatus, u8 *cbavail,
				u8 *cbused, u8 *fifoavail, u8 *fifoused)
{
	int err;
	u8 val[SI4713_RDSBUFF_NRESP];

	const u8 args[SI4713_RDSBUFF_NARGS] = {
		mode & SI4713_RDSBUFF_MODE_MASK,
		msb(rdsb),
		lsb(rdsb),
		msb(rdsc),
		lsb(rdsc),
		msb(rdsd),
		lsb(rdsd),
	};

	err = si4713_send_command(sdev, SI4713_CMD_TX_RDS_BUFF,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (!err) {
		dev_dbg(&sdev->client->dev,
			"Status from tx rds buff: 0x%02x\n", val[0]);
		*intstatus = val[1];
		*cbavail = val[2];
		*cbused = val[3];
		*fifoavail = val[4];
		*fifoused = val[5];
		dev_dbg(&sdev->client->dev, "rds buffer status: interrupts"
				" 0x%02x cb avail: %d cb used %d fifo avail"
				" %d fifo used %d\n", *intstatus, *cbavail,
				*cbused, *fifoavail, *fifoused);
	}

	return err;
}

/*
 * si4713_tx_rds_ps - Loads the program service buffer.
 * @sdev: si4713_device structure for the device we are communicating
 * @psid: program service id to be loaded.
 * @pschar: assumed 4 size char array to be loaded into the program service
 */
static int si4713_tx_rds_ps(struct si4713_device *sdev, u8 psid,
				unsigned char *pschar)
{
	int err;
	u8 val[SI4713_RDSPS_NRESP];

	const u8 args[SI4713_RDSPS_NARGS] = {
		psid & SI4713_RDSPS_PSID_MASK,
		pschar[0],
		pschar[1],
		pschar[2],
		pschar[3],
	};

	err = si4713_send_command(sdev, SI4713_CMD_TX_RDS_PS,
				  args, ARRAY_SIZE(args), val,
				  ARRAY_SIZE(val), DEFAULT_TIMEOUT);

	if (err < 0)
		return err;

	dev_dbg(&sdev->client->dev, "Status from tx rds ps: 0x%02x\n",
		val[0]);

	return err;
}

/*
 * si4713_init - Sets the device up with default configuration.
 * @sdev: si4713_device structure for the device we are communicating
 */
int si4713_init(struct si4713_device *sdev)
{
	int rval;

	rval = si4713_set_rds_pi(sdev, DEFAULT_RDS_PI);
	if (rval < 0)
		goto exit;

	rval = si4713_set_rds_pty(sdev, DEFAULT_RDS_PTY);
	if (rval < 0)
		goto exit;

	rval = si4713_set_rds_ps_name(sdev, DEFAULT_RDS_PS_NAME);
	if (rval < 0)
		goto exit;

	rval = si4713_set_rds_radio_text(sdev, DEFAULT_RDS_RADIO_TEXT);
	if (rval < 0)
		goto exit;

	rval = si4713_set_rds_enabled(sdev, 1);
	if (rval < 0)
		goto exit;

	rval = si4713_set_limiter_release_time(sdev, DEFAULT_LIMITER_RTIME);
	if (rval < 0)
		goto exit;

	rval = si4713_set_limiter_deviation(sdev, DEFAULT_LIMITER_DEV);
	if (rval < 0)
		goto exit;

	rval = si4713_set_limiter_enabled(sdev, 1);
	if (rval < 0)
		goto exit;

	rval = si4713_set_pilot_frequency(sdev, DEFAULT_PILOT_FREQUENCY);
	if (rval < 0)
		goto exit;

	rval = si4713_set_pilot_deviation(sdev, DEFAULT_PILOT_DEVIATION);
	if (rval < 0)
		goto exit;

	rval = si4713_set_pilot_enabled(sdev, 1);
	if (rval < 0)
		goto exit;

	rval = si4713_set_stereo_enabled(sdev, 1);
	if (rval < 0)
		goto exit;

	rval = si4713_set_acomp_attack_time(sdev, DEFAULT_ACOMP_ATIME);
	if (rval < 0)
		goto exit;

	rval = si4713_set_acomp_release_time(sdev, DEFAULT_ACOMP_RTIME);
	if (rval < 0)
		goto exit;

	rval = si4713_set_acomp_gain(sdev, DEFAULT_ACOMP_GAIN);
	if (rval < 0)
		goto exit;

	rval = si4713_set_acomp_threshold(sdev, DEFAULT_ACOMP_THRESHOLD);
	if (rval < 0)
		goto exit;

	rval = si4713_set_acomp_enabled(sdev, 1);
	if (rval < 0)
		goto exit;

	rval = si4713_set_region(sdev, DEFAULT_REGION_SETTINGS);
	if (rval < 0)
		goto exit;

	rval = si4713_set_mute(sdev, DEFAULT_MUTE);
	if (rval < 0)
		goto exit;

	rval = si4713_set_tone_frequency(sdev, DEFAULT_TONE_FREQUENCY);
	if (rval < 0)
		goto exit;

	rval = si4713_set_tone_deviation(sdev, DEFAULT_TONE_DEVIATION);
	if (rval < 0)
		goto exit;

	rval = si4713_set_tone_on_time(sdev, DEFAULT_TONE_ON_TIME);
	if (rval < 0)
		goto exit;

	rval = si4713_set_tone_off_time(sdev, DEFAULT_TONE_OFF_TIME);
	if (rval < 0)
		goto exit;

exit:
	return rval;
}

/*
 * si4713_setup - Sets the device up with current configuration.
 * @sdev: si4713_device structure for the device we are communicating
 */
int si4713_setup(struct si4713_device *sdev)
{
	struct si4713_device *tmp;
	int rval;

	tmp = kmalloc(sizeof(*tmp), GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	/* Get a local copy to avoid race */
	mutex_lock(&sdev->mutex);
	memcpy(tmp, sdev, sizeof(*sdev));
	mutex_unlock(&sdev->mutex);

	rval = si4713_set_rds_pi(sdev, tmp->rds_info.pi);
	if (rval < 0)
		goto exit;

	rval = si4713_set_rds_pty(sdev, tmp->rds_info.pty);
	if (rval < 0)
		goto exit;

	rval = si4713_set_rds_ps_name(sdev, tmp->rds_info.ps_name);
	if (rval < 0)
		goto exit;

	rval = si4713_set_rds_radio_text(sdev, tmp->rds_info.radio_text);
	if (rval < 0)
		goto exit;

	rval = si4713_set_rds_enabled(sdev, tmp->rds_info.enabled);
	if (rval < 0)
		goto exit;

	rval = si4713_set_limiter_release_time(sdev,
				tmp->limiter_info.release_time);
	if (rval < 0)
		goto exit;

	rval = si4713_set_limiter_deviation(sdev, tmp->limiter_info.deviation);
	if (rval < 0)
		goto exit;

	rval = si4713_set_limiter_enabled(sdev, tmp->limiter_info.enabled);
	if (rval < 0)
		goto exit;

	rval = si4713_set_region(sdev, tmp->region_info.region);
	if (rval < 0)
		goto exit;

	rval = si4713_set_pilot_frequency(sdev, tmp->pilot_info.frequency);
	if (rval < 0)
		goto exit;

	rval = si4713_set_pilot_deviation(sdev, tmp->pilot_info.deviation);
	if (rval < 0)
		goto exit;

	rval = si4713_set_pilot_enabled(sdev, tmp->pilot_info.enabled);
	if (rval < 0)
		goto exit;

	rval = si4713_set_stereo_enabled(sdev, tmp->stereo);
	if (rval < 0)
		goto exit;

	rval = si4713_set_acomp_attack_time(sdev, tmp->acomp_info.attack_time);
	if (rval < 0)
		goto exit;

	rval = si4713_set_acomp_release_time(sdev,
						tmp->acomp_info.release_time);
	if (rval < 0)
		goto exit;

	rval = si4713_set_acomp_gain(sdev, tmp->acomp_info.gain);
	if (rval < 0)
		goto exit;

	rval = si4713_set_acomp_threshold(sdev, tmp->acomp_info.threshold);
	if (rval < 0)
		goto exit;

	rval = si4713_set_acomp_enabled(sdev, tmp->acomp_info.enabled);
	if (rval < 0)
		goto exit;

	rval = si4713_set_mute(sdev, tmp->mute);
	if (rval < 0)
		goto exit;

	rval = si4713_set_tone_frequency(sdev, tmp->tone_info.frequency);
	if (rval < 0)
		goto exit;

	rval = si4713_set_tone_deviation(sdev, tmp->tone_info.deviation);
	if (rval < 0)
		goto exit;

	rval = si4713_set_tone_on_time(sdev, tmp->tone_info.on_time);
	if (rval < 0)
		goto exit;

	rval = si4713_set_tone_off_time(sdev, tmp->tone_info.off_time);
	if (rval < 0)
		goto exit;

	rval = si4713_set_frequency(sdev, tmp->frequency ? tmp->frequency :
					tmp->region_info.bottom_frequency);
	if (rval < 0)
		goto exit;

	rval = si4713_set_power_level(sdev, tmp->power_level ?
					tmp->power_level :
					DEFAULT_POWER_LEVEL);
	if (rval < 0)
		goto exit;

	rval = si4713_set_antenna_capacitor(sdev, tmp->antenna_capacitor);

exit:
	kfree(tmp);
	return rval;
}

int si4713_set_power_level(struct si4713_device *sdev, u8 power_level)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_tx_tune_power(sdev, power_level,
						sdev->antenna_capacitor);

		if (rval < 0)
			goto unlock;
	}

	sdev->power_level = power_level;
	rval = 0;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_get_power_level(struct si4713_device *sdev)
{
	int rval;
	u16 f = 0;
	u8 p, a, n;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_tx_tune_status(sdev, 0x00, &f, &p, &a, &n);

		if (rval < 0)
			goto unlock;

		sdev->power_level = p;
	}

	rval = sdev->power_level;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_antenna_capacitor(struct si4713_device *sdev, u8 value)
{
	int rval = 0;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_tx_tune_power(sdev, sdev->power_level, value);

	if (!rval)
		sdev->antenna_capacitor = value;

	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_get_antenna_capacitor(struct si4713_device *sdev)
{
	int rval = -EINVAL;
	u16 f = 0;
	u8 p, a, n;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_tx_tune_status(sdev, 0x00, &f, &p, &a, &n);

		if (rval < 0)
			goto unlock;

		rval = a;
	}

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_power_state(struct si4713_device *sdev, u8 value)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (value)
		rval = si4713_powerup(sdev);
	else
		rval = si4713_powerdown(sdev);

	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_probe(struct si4713_device *sdev)
{
	int rval;

	rval = si4713_set_power_state(sdev, POWER_ON);
	if (rval < 0)
		goto exit;

	rval = si4713_checkrev(sdev);
	if (rval < 0)
		goto exit;

	rval = si4713_set_power_state(sdev, POWER_OFF);
	if (rval < 0)
		goto exit;

	rval = si4713_init(sdev);

exit:
	return rval;
}

int si4713_set_frequency(struct si4713_device *sdev, u16 frequency)
{
	int rval = 0;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_tx_tune_freq(sdev, frequency);
		if (rval < 0)
			goto unlock;
		sdev->frequency = rval;
	} else {
		rval = -ENODEV;
	}

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_get_frequency(struct si4713_device *sdev)
{
	int rval;
	u16 f = 0;
	u8 p, a, n;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_tx_tune_status(sdev, 0x00, &f, &p, &a, &n);

		if (rval < 0)
			goto unlock;

		sdev->frequency = f;
	}

	rval = sdev->frequency;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_mute(struct si4713_device *sdev, u16 mute)
{
	int rval = 0;

	mute = set_mute(mute);

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev,
				SI4713_TX_LINE_INPUT_MUTE, mute);

	if (rval >= 0)
		sdev->mute = get_mute(mute);

	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_get_mute(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_LINE_INPUT_MUTE);

		if (rval < 0)
			goto unlock;

		sdev->mute = rval;
	}

	rval = get_mute(sdev->mute);

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_rds_pi(struct si4713_device *sdev, u16 pi)
{
	int rval = 0;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev, SI4713_TX_RDS_PI, pi);

	if (rval >= 0)
		sdev->rds_info.pi = pi;

	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_get_rds_pi(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_RDS_PI);

		if (rval < 0)
			goto unlock;

		sdev->rds_info.pi = rval;
	}

	rval = sdev->rds_info.pi;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_rds_pty(struct si4713_device *sdev, u8 pty)
{
	int rval = 0;
	u16 p;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_RDS_PS_MISC);
		if (rval < 0)
			goto unlock;

		p = set_pty(rval, pty);

		rval = si4713_write_property(sdev, SI4713_TX_RDS_PS_MISC, p);
		if (rval < 0)
			goto unlock;
	}

	sdev->rds_info.pty = pty;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_get_rds_pty(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_RDS_PS_MISC);

		if (rval < 0)
			goto unlock;

		sdev->rds_info.pty = get_pty(rval);
	}

	rval = sdev->rds_info.pty;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_rds_ps_name(struct si4713_device *sdev, char *ps_name)
{
	int rval = 0, i;
	u8 len = 0;
	u8 *tmp;

	if (!strlen(ps_name))
		return -EINVAL;

	tmp = kzalloc(MAX_RDS_PS_NAME + 1, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	strncpy(tmp, ps_name, MAX_RDS_PS_NAME);

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		/* Write the new ps name and clear the padding */
		for (i = 0; i < MAX_RDS_PS_NAME; i += (RDS_BLOCK / 2)) {
			rval = si4713_tx_rds_ps(sdev, (i / (RDS_BLOCK / 2)),
						tmp + i);
			if (rval < 0)
				goto unlock;
		}

		/* Setup the size to be sent */
		len = strlen(tmp) - 1;

		rval = si4713_write_property(sdev,
				SI4713_TX_RDS_PS_MESSAGE_COUNT,
				rds_ps_nblocks(len));
		if (rval < 0)
			goto unlock;

		rval = si4713_write_property(sdev,
				SI4713_TX_RDS_PS_REPEAT_COUNT,
				DEFAULT_RDS_PS_REPEAT_COUNT * 2);
		if (rval < 0)
			goto unlock;
	}

	strncpy(sdev->rds_info.ps_name, tmp, MAX_RDS_PS_NAME);

unlock:
	mutex_unlock(&sdev->mutex);
	kfree(tmp);
	return rval;
}

int si4713_get_rds_ps_name(struct si4713_device *sdev, char *ps_name)
{
	mutex_lock(&sdev->mutex);
	strncpy(ps_name, sdev->rds_info.ps_name, MAX_RDS_PS_NAME);
	mutex_unlock(&sdev->mutex);

	return 0;
}

int si4713_set_rds_radio_text(struct si4713_device *sdev, char *radio_text)
{
	int rval = 0, i;
	u16 t_index = 0;
	u8 s, a, u, fa, fu, b_index = 0, cr_inserted = 0;
	u8 *tmp;

	if (!strlen(radio_text))
		return -EINVAL;

	tmp = kzalloc(MAX_RDS_RADIO_TEXT + 1, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	strncpy(tmp, radio_text, MAX_RDS_RADIO_TEXT);

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_tx_rds_buff(sdev, RDS_BLOCK_CLEAR, 0, 0, 0,
						&s, &a, &u, &fa, &fu);
		if (rval < 0)
			goto unlock;
		do {
			/* RDS spec says that if the last block isn't used,
			 * then apply a carriage return
			 */
			if (t_index < (RDS_RADIOTEXT_INDEX_MAX * \
				RDS_RADIOTEXT_BLK_SIZE)) {
				for (i = 0; i < RDS_RADIOTEXT_BLK_SIZE; i++) {
					if (!tmp[t_index + i] ||
						tmp[t_index + i] == \
						RDS_CARRIAGE_RETURN) {
						tmp[t_index + i] =
							RDS_CARRIAGE_RETURN;
						cr_inserted = 1;
						break;
					}
				}
			}

			rval = si4713_tx_rds_buff(sdev, RDS_BLOCK_LOAD,
					compose_u16(RDS_RADIOTEXT_2A,
						b_index++),
					compose_u16(tmp[t_index],
						tmp[t_index + 1]),
					compose_u16(tmp[t_index + 2],
						tmp[t_index + 3]),
					&s, &a, &u, &fa, &fu);
			if (rval < 0)
				goto unlock;

			t_index += RDS_RADIOTEXT_BLK_SIZE;

			if (cr_inserted)
				break;
		} while (u < a);
	}

	strncpy(sdev->rds_info.radio_text, tmp, MAX_RDS_RADIO_TEXT);

unlock:
	mutex_unlock(&sdev->mutex);
	kfree(tmp);
	return rval;
}

int si4713_get_rds_radio_text(struct si4713_device *sdev, char *radio_text)
{
	mutex_lock(&sdev->mutex);
	strncpy(radio_text, sdev->rds_info.radio_text, MAX_RDS_RADIO_TEXT);
	mutex_unlock(&sdev->mutex);

	return 0;
}

int si4713_set_rds_enabled(struct si4713_device *sdev, u8 enabled)
{
	int rval = 0;
	u16 p;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_COMPONENT_ENABLE);
		if (rval < 0)
			goto unlock;

		p = rval;
		if (enabled)
			p = enable_rds(p);
		else
			p = disable_rds(p);

		rval = si4713_write_property(sdev, SI4713_TX_COMPONENT_ENABLE,
				p);
		if (rval < 0)
			goto unlock;

		if (enabled) {
			rval = si4713_write_property(sdev,
				SI4713_TX_RDS_DEVIATION,
				DEFAULT_RDS_DEVIATION);
			if (rval < 0)
				goto unlock;
		}
	}

	sdev->rds_info.enabled = enabled & 0x01;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_get_rds_enabled(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_COMPONENT_ENABLE);
		if (rval < 0)
			goto unlock;

		sdev->rds_info.enabled = get_rds_status(rval);
	}

	rval = sdev->rds_info.enabled;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_get_region_bottom_frequency(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);
	/* Device works in 10kHz units */
	rval = sdev->region_info.bottom_frequency * 10;
	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_get_region_top_frequency(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);
	/* Device works in 10kHz units */
	rval = sdev->region_info.top_frequency * 10;
	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_set_region_preemphasis(struct si4713_device *sdev, u8 preemphasis)
{
	int rval = 0;

	switch (preemphasis) {
	case PREEMPHASIS_USA:
		preemphasis = FMPE_USA;
		break;
	case PREEMPHASIS_EU:
		preemphasis = FMPE_EU;
		break;
	case PREEMPHASIS_DISABLED:
		preemphasis = FMPE_DISABLED;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev, SI4713_TX_PREEMPHASIS,
								preemphasis);

	if (rval >= 0)
		sdev->region_info.preemphasis = preemphasis;

	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_get_region_preemphasis(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_PREEMPHASIS);

		if (rval < 0)
			goto unlock;

		sdev->region_info.preemphasis = rval;
	}

	switch (sdev->region_info.preemphasis) {
	case FMPE_USA:
		rval = PREEMPHASIS_USA;
		break;
	case FMPE_EU:
		rval = PREEMPHASIS_EU;
		break;
	case FMPE_DISABLED:
		rval = PREEMPHASIS_DISABLED;
		break;
	default:
		rval = -EINVAL;
		goto unlock;
	}

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_get_region_channel_spacing(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);
	/* Device works in 10kHz units */
	rval = sdev->region_info.channel_spacing * 10;
	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_set_region(struct si4713_device *sdev, u8 region)
{
	int rval = 0;
	u16 new_frequency = 0;

	if (region >= ARRAY_SIZE(region_configs))
		return -EINVAL;

	mutex_lock(&sdev->mutex);

	if (sdev->frequency < region_configs[region].bottom_frequency ||
		sdev->frequency > region_configs[region].top_frequency)
		new_frequency = region_configs[region].bottom_frequency;

	memcpy(&sdev->region_info, &region_configs[region],
		sizeof(sdev->region_info));

	if (sdev->power_state) {
		if (new_frequency > 0) {
			rval = si4713_tx_tune_freq(sdev, new_frequency);

			if (rval < 0)
				goto unlock;

			new_frequency = rval;
		}

		rval = si4713_write_property(sdev, SI4713_TX_PREEMPHASIS,
					region_configs[region].preemphasis);
		if (rval < 0)
			goto unlock;
	}

	sdev->frequency = new_frequency;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_get_region(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);
	rval = sdev->region_info.region;
	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_set_limiter_enabled(struct si4713_device *sdev, u8 enabled)
{
	int rval = 0;
	u16 p;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_ACOMP_ENABLE);
		if (rval < 0)
			goto unlock;

		p = rval;
		if (enabled)
			p = enable_limiter(p);
		else
			p = disable_limiter(p);

		rval = si4713_write_property(sdev, SI4713_TX_ACOMP_ENABLE,
				p);

		if (rval < 0)
			goto unlock;
	}

	sdev->limiter_info.enabled = enabled & 0x01;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_get_limiter_enabled(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_ACOMP_ENABLE);
		if (rval < 0)
			goto unlock;

		sdev->limiter_info.enabled = get_limiter_status(rval);
	}

	rval = sdev->limiter_info.enabled;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_limiter_deviation(struct si4713_device *sdev,
					unsigned long deviation)
{
	int rval = 0;

	/* Device receives in 10Hz units */
	deviation /= 10;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev, SI4713_TX_AUDIO_DEVIATION,
						deviation);

	/* Device returns in 10Hz units */
	if (rval >= 0)
		sdev->limiter_info.deviation = deviation * 10;

	mutex_unlock(&sdev->mutex);

	return rval;
}

long si4713_get_limiter_deviation(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_AUDIO_DEVIATION);

		if (rval < 0)
			goto unlock;

		/* Device returns in 10Hz units */
		sdev->limiter_info.deviation = rval * 10;
	}

	rval = sdev->limiter_info.deviation;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_limiter_release_time(struct si4713_device *sdev,
					unsigned long rtime)
{
	int rval;

	rval = usecs_to_dev(rtime, limiter_times, ARRAY_SIZE(limiter_times));
	if (rval < 0)
		goto exit;

	rtime = rval;
	rval = 0;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev,
				SI4713_TX_LIMITER_RELEASE_TIME,	rtime);

	if (rval >= 0)
		sdev->limiter_info.release_time = dev_to_usecs(rtime,
						limiter_times,
						ARRAY_SIZE(limiter_times));

	mutex_unlock(&sdev->mutex);

exit:
	return rval;
}

long si4713_get_limiter_release_time(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev,
				SI4713_TX_LIMITER_RELEASE_TIME);

		if (rval < 0)
			goto unlock;

		sdev->limiter_info.release_time = dev_to_usecs(rval,
						limiter_times,
						ARRAY_SIZE(limiter_times));
	}

	rval = sdev->limiter_info.release_time;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_stereo_enabled(struct si4713_device *sdev, u8 enabled)
{
	int rval = 0;
	u16 p;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_COMPONENT_ENABLE);
		if (rval < 0)
			goto unlock;

		p = rval;
		if (enabled)
			p = enable_stereo(p);
		else
			p = disable_stereo(p);

		rval = si4713_write_property(sdev, SI4713_TX_COMPONENT_ENABLE,
				p);

		if (rval < 0)
			goto unlock;
	}

	sdev->stereo = enabled & 0x01;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_get_stereo_enabled(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_COMPONENT_ENABLE);
		if (rval < 0)
			goto unlock;

		sdev->stereo = get_stereo_status(rval);
	}

	rval = sdev->stereo;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_pilot_enabled(struct si4713_device *sdev, u8 enabled)
{
	int rval = 0;
	u16 p;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_COMPONENT_ENABLE);
		if (rval < 0)
			goto unlock;

		p = rval;
		if (enabled)
			p = enable_pilot(p);
		else
			p = disable_pilot(p);

		rval = si4713_write_property(sdev, SI4713_TX_COMPONENT_ENABLE,
				p);

		if (rval < 0)
			goto unlock;
	}

	sdev->pilot_info.enabled = enabled & 0x01;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_get_pilot_enabled(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_COMPONENT_ENABLE);
		if (rval < 0)
			goto unlock;

		sdev->pilot_info.enabled = get_pilot_status(rval);
	}

	rval = sdev->pilot_info.enabled;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_pilot_deviation(struct si4713_device *sdev,
					unsigned long deviation)
{
	int rval = 0;

	/* Device receives in 10Hz units */
	deviation /= 10;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev, SI4713_TX_PILOT_DEVIATION,
						deviation);

	/* Device returns in 10Hz units */
	if (rval >= 0)
		sdev->pilot_info.deviation = deviation * 10;

	mutex_unlock(&sdev->mutex);

	return rval;
}

long si4713_get_pilot_deviation(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_PILOT_DEVIATION);

		if (rval < 0)
			goto unlock;

		/* Device returns in 10Hz units */
		sdev->pilot_info.deviation = rval * 10;
	}

	rval = sdev->pilot_info.deviation;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_pilot_frequency(struct si4713_device *sdev, u16 freq)
{
	int rval = 0;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev, SI4713_TX_PILOT_FREQUENCY,
						freq);

	if (rval >= 0)
		sdev->pilot_info.frequency = freq;

	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_get_pilot_frequency(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_PILOT_FREQUENCY);

		if (rval < 0)
			goto unlock;

		sdev->pilot_info.frequency = rval;
	}

	rval = sdev->pilot_info.frequency;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_tone_deviation(struct si4713_device *sdev,
					unsigned long deviation)
{
	int rval = 0;

	/* Device receives in 10Hz units */
	deviation /= 10;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev, SI4713_TX_TONE_DEVIATION,
						deviation);

	/* Device returns in 10Hz units */
	if (rval >= 0)
		sdev->tone_info.deviation = deviation * 10;

	mutex_unlock(&sdev->mutex);

	return rval;
}

long si4713_get_tone_deviation(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_TONE_DEVIATION);

		if (rval < 0)
			goto unlock;

		/* Device returns in 10Hz units */
		sdev->tone_info.deviation = rval * 10;
	}

	rval = sdev->tone_info.deviation;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_tone_frequency(struct si4713_device *sdev, u16 freq)
{
	int rval = 0;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev, SI4713_TX_TONE_FREQUENCY,
						freq);

	if (rval >= 0)
		sdev->tone_info.frequency = freq;

	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_get_tone_frequency(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_TONE_FREQUENCY);

		if (rval < 0)
			goto unlock;

		sdev->tone_info.frequency = rval;
	}

	rval = sdev->tone_info.frequency;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_tone_on_time(struct si4713_device *sdev, u16 on_time)
{
	int rval = 0;

	/* Device receives in 2ms units */
	on_time >>= 1;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev, SI4713_TX_TONE_ON_TIME,
						on_time);

	/* Device returns in 2ms units */
	if (rval >= 0)
		sdev->tone_info.on_time = on_time << 1;

	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_get_tone_on_time(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_TONE_ON_TIME);

		if (rval < 0)
			goto unlock;

		/* Device returns in 2ms units */
		sdev->tone_info.on_time = rval << 1;
	}

	rval = sdev->tone_info.on_time;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_tone_off_time(struct si4713_device *sdev, u16 off_time)
{
	int rval = 0;

	/* Device receives in 2ms units */
	off_time >>= 1;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev, SI4713_TX_TONE_OFF_TIME,
						off_time);

	/* Device returns in 2ms units */
	if (rval >= 0)
		sdev->tone_info.off_time = off_time << 1;

	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_get_tone_off_time(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_TONE_OFF_TIME);

		if (rval < 0)
			goto unlock;

		/* Device returns in 2ms units */
		sdev->tone_info.off_time = rval << 1;
	}

	rval = sdev->tone_info.off_time;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_acomp_enabled(struct si4713_device *sdev, u8 enabled)
{
	int rval = 0;
	u16 p;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_ACOMP_ENABLE);
		if (rval < 0)
			goto unlock;

		p = rval;
		if (enabled)
			p = enable_acomp(p);
		else
			p = disable_acomp(p);

		rval = si4713_write_property(sdev, SI4713_TX_ACOMP_ENABLE, p);

		if (rval < 0)
			goto unlock;
	}

	sdev->acomp_info.enabled = enabled & 0x01;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_get_acomp_enabled(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_ACOMP_ENABLE);
		if (rval < 0)
			goto unlock;

		sdev->acomp_info.enabled = get_acomp_status(rval);
	}

	rval = sdev->acomp_info.enabled;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_acomp_gain(struct si4713_device *sdev, u8 gain)
{
	int rval = 0;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev, SI4713_TX_ACOMP_GAIN, gain);

	if (rval >= 0)
		sdev->acomp_info.gain = gain;

	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_get_acomp_gain(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_ACOMP_GAIN);

		if (rval < 0)
			goto unlock;

		sdev->acomp_info.gain = rval;
	}

	rval = sdev->acomp_info.gain;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_acomp_threshold(struct si4713_device *sdev, s8 threshold)
{
	int rval = 0;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev, SI4713_TX_ACOMP_THRESHOLD,
						threshold);

	if (rval >= 0)
		sdev->acomp_info.threshold = threshold;

	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_get_acomp_threshold(struct si4713_device *sdev, s8 *threshold)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev, SI4713_TX_ACOMP_THRESHOLD);

		if (rval < 0)
			goto unlock;

		sdev->acomp_info.threshold = rval;
	}

	*threshold = sdev->acomp_info.threshold;
	rval = 0;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_acomp_release_time(struct si4713_device *sdev,
					unsigned long rtime)
{
	int rval;

	rval = usecs_to_dev(rtime, acomp_rtimes, ARRAY_SIZE(acomp_rtimes));
	if (rval < 0)
		goto exit;

	rtime = rval;
	rval = 0;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev,
				SI4713_TX_ACOMP_RELEASE_TIME, rtime);

	if (rval >= 0)
		sdev->acomp_info.release_time = dev_to_usecs(rtime,
						acomp_rtimes,
						ARRAY_SIZE(acomp_rtimes));

	mutex_unlock(&sdev->mutex);

exit:
	return rval;
}

long si4713_get_acomp_release_time(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev,
				SI4713_TX_ACOMP_RELEASE_TIME);

		if (rval < 0)
			goto unlock;

		sdev->acomp_info.release_time = dev_to_usecs(rval,
						acomp_rtimes,
						ARRAY_SIZE(acomp_rtimes));
	}

	rval = sdev->acomp_info.release_time;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}


int si4713_set_acomp_attack_time(struct si4713_device *sdev, u16 atime)
{
	int rval = 0;

	/* Device receives in 0.5 ms units */
	atime /= ATTACK_TIME_UNIT;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state)
		rval = si4713_write_property(sdev,
				SI4713_TX_ACOMP_ATTACK_TIME, atime);

	if (rval >= 0)
		sdev->acomp_info.attack_time = atime * ATTACK_TIME_UNIT;

	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_get_acomp_attack_time(struct si4713_device *sdev)
{
	int rval;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_read_property(sdev,
				SI4713_TX_ACOMP_RELEASE_TIME);

		if (rval < 0)
			goto unlock;

		sdev->acomp_info.release_time = rval * ATTACK_TIME_UNIT;
	}

	rval = sdev->acomp_info.attack_time;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}

int si4713_set_tune_measure(struct si4713_device *sdev, u32 frequency)
{
	int rval = -ENODEV;

	mutex_lock(&sdev->mutex);
	if (sdev->power_state)
		rval = si4713_tx_tune_measure(sdev, frequency / 10, 0);
	mutex_unlock(&sdev->mutex);

	return rval;
}

int si4713_get_tune_measure(struct si4713_device *sdev)
{
	int rval;
	u16 f = 0;
	u8 p, a, n;

	mutex_lock(&sdev->mutex);

	if (sdev->power_state) {
		rval = si4713_tx_tune_status(sdev, 0x00, &f, &p, &a, &n);

		if (rval < 0)
			goto unlock;

		sdev->tune_rssi = n;
	}

	rval = sdev->tune_rssi;

unlock:
	mutex_unlock(&sdev->mutex);
	return rval;
}
