/*
 * drivers/i2c/chips/lis302dl.c
 * Driver for STMicroelectronics LIS302DL acceleration sensor
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Written by Henrik Saari <henrik.saari@nokia.com>
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

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/lis302dl.h>
#include <mach/board.h>

#define DRIVER_NAME  "lis302dl"

#define LIS302_WHOAMI			0x0f
#define LIS302_CTRL_1			0x20
#	define LIS302_CTRL1_DR		(1 << 7)
#	define LIS302_CTRL1_PD		(1 << 6)
#	define LIS302_CTRL1_FS		(1 << 5)
#	define LIS302_CTRL1_STP		(1 << 4)
#	define LIS302_CTRL1_STM		(1 << 3)
#	define LIS302_CTRL1_Z		(1 << 2)
#	define LIS302_CTRL1_Y		(1 << 1)
#	define LIS302_CTRL1_X		(1 << 0)
#define LIS302_CTRL_2			0x21
#	define LIS302_CTRL2_BOOT	(1 << 6)
#define LIS302_CTRL_3			0x22
#	define	LIS302_CTRL3_GND	0x00
#	define	LIS302_CTRL3_FF_WU_1	0x01
#	define	LIS302_CTRL3_FF_WU_2	0x02
#	define	LIS302_CTRL3_FF_WU_12	0x03
#	define	LIS302_CTRL3_DATA_RDY	0x04
#	define	LIS302_CTRL3_CLICK	0x07
#define LIS302_HP_FILTER_RESET		0x23
#define LIS302_STATUS_REG		0x27
#define LIS302_X			0x29
#define LIS302_Y			0x2b
#define LIS302_Z			0x2d
#define LIS302_FF_WU_CFG_1		0x30

/* configurable interrupt events */
#define LIS302_X_LOW			(1 << 0)
#define LIS302_X_HIGH			(1 << 1)
#define LIS302_Y_LOW			(1 << 2)
#define LIS302_Y_HIGH			(1 << 3)
#define LIS302_Z_LOW			(1 << 4)
#define LIS302_Z_HIGH			(1 << 5)
#define LIS302_LIR			(1 << 6)
#define LIS302_AOI			(1 << 7)

#define LIS302_FF_WU_SRC_1		0x31
#define LIS302_FF_THS_1			0x32
#define LIS302_FF_WU_DURATION_1		0x33
#define LIS302_FF_WU_CFG_2		0x34
#define LIS302_FF_WU_SRC_2		0x34
#define LIS302_FF_THS_2			0x35
#define LIS302_FF_WU_DURATION_2		0x37

/* Default values */
#define LIS302_THS		810	/* mg    */
#define LIS302_DURATION		500	/* ms    */
#define LIS302_400HZ		1	/* sample rate 400Hz */
#define LIS302_100HZ		0	/* sample rate 100Hz */
#define LIS302_FS		0	/* full scale 0 / 1 */
#define LIS302_SAMPLES		1
#define LIS302_SMALL_UNIT	18	/* Typical value 18 mg/digit */
#define LIS302_BIG_UNIT		72	/* Typical value 72 mg/digit */
#define LIS302_TURN_ON_TIME	3000	/* Turn on time 3000ms / data rate */

#define LIS302_POWEROFF_DELAY	(5 * HZ)

/* A lis302dl chip will contain this value in LIS302_WHOAMI register */
#define LIS302_WHOAMI_VALUE	0x3b
#define LIS302_IRQ_FLAGS (IRQF_TRIGGER_RISING | IRQF_SAMPLE_RANDOM)

struct lis302dl_chip {
	struct mutex		lock;
	struct i2c_client	*client;
	struct work_struct	work1, work2;
	struct delayed_work	poweroff_work;
	int			irq1, irq2;
	uint8_t			power;
	int			threshold;
	int			duration;
	uint8_t			sample_rate;
	uint8_t			fs;
	unsigned int		samples;
};

static inline s32 lis302dl_write(struct i2c_client *c, int reg, u8 value)
{
	return i2c_smbus_write_byte_data(c, reg, value);
}

static inline s32 lis302dl_read(struct i2c_client *c, int reg)
{
	return i2c_smbus_read_byte_data(c, reg);
}

/*
 * Detect LIS302DL chip. Return value is zero if
 * chip detected, otherwise a negative error code.
 */
static int lis302dl_detect(struct i2c_client *c)
{
	int r;

	r = lis302dl_read(c, LIS302_WHOAMI);
	if (r < 0)
		return r;

	if (r != LIS302_WHOAMI_VALUE)
		return -ENODEV;

	return 0;
}

static inline u8 intmode(int pin, u8 mode)
{
	if (pin == 1)
		return mode;
	if (pin == 2)
		return (mode << 3);

	return 0;
}

static int lis302dl_configure(struct i2c_client *c)
{

	struct lis302dl_chip *lis = i2c_get_clientdata(c);
	int ts = 0, ret;
	u8 duration, r = 0;

	/* REG 1*/
	/* Controls power, scale, data rate, and enabled axis */
	r |= lis->sample_rate ? LIS302_CTRL1_DR : 0;
	r |= lis->fs ? LIS302_CTRL1_FS : 0;
	r |= LIS302_CTRL1_PD | LIS302_CTRL1_X | LIS302_CTRL1_Y | LIS302_CTRL1_Z;
	ret = lis302dl_write(c, LIS302_CTRL_1, r);
	if (ret < 0)
		goto out;

	/* REG 2
	 * Boot is used to refresh internal registers
	 * Control High Pass filter selection. not used
	 */
	ret = lis302dl_write(c, LIS302_CTRL_2, LIS302_CTRL2_BOOT);
	if (ret < 0)
		goto out;

	/* REG 3
	 * Interrupt CTRL register. One interrupt pin is used for
	 * inertial wakeup
	*/
	r = intmode(1, LIS302_CTRL3_FF_WU_1) | intmode(2, LIS302_CTRL3_GND);
	ret = lis302dl_write(c, LIS302_CTRL_3, r);
	if (ret < 0)
		goto out;

	/* Configure interrupt pin thresholds */
	ts = lis->threshold / (lis->fs ? LIS302_BIG_UNIT : LIS302_SMALL_UNIT);
	ts &= 0x7f;
	duration = lis->duration / (lis->sample_rate ? 40 : 10);

	ret = lis302dl_write(c, LIS302_FF_THS_1, ts);
	if (ret < 0)
		goto out;
	ret = lis302dl_write(c, LIS302_FF_WU_DURATION_1, duration);
	if (ret < 0)
		goto out;
	/* Enable interrupt wakeup on x and y axis */
	ret = lis302dl_write(c, LIS302_FF_WU_CFG_1,
			     (LIS302_X_HIGH | LIS302_Y_HIGH));
	if (ret < 0)
		goto out;
 out:
	return ret;
}

static inline void lis302dl_print_event(struct device *dev, u8 event)
{
	if (event & 0x01)
		dev_dbg(dev, "X Low event\n");
	if (event & 0x02)
		dev_dbg(dev, "X High event\n");
	if (event & 0x04)
		dev_dbg(dev, "Y Low event\n");
	if (event & 0x08)
		dev_dbg(dev, "Y High event\n");
	if (event & 0x10)
		dev_dbg(dev, "Z Low event\n");
	if (event & 0x20)
		dev_dbg(dev, "Z High event\n");
}

/* Interrupt handler bottom halves. */
static void lis302dl_work1(struct work_struct  *work)
{
	struct lis302dl_chip *chip =
		container_of(work, struct lis302dl_chip, work1);
	u8 reg;

	mutex_lock(&chip->lock);
	/* ack the interrupt */
	reg = lis302dl_read(chip->client, LIS302_FF_WU_SRC_1);
	mutex_unlock(&chip->lock);
	sysfs_notify(&chip->client->dev.kobj, NULL, "coord");
	lis302dl_print_event(&chip->client->dev, reg);
}

static void lis302dl_work2(struct work_struct *work)
{
	struct lis302dl_chip *chip =
		container_of(work, struct lis302dl_chip, work2);
	u8 reg;

	mutex_lock(&chip->lock);
	/* ack the interrupt */
	reg = lis302dl_read(chip->client, LIS302_FF_WU_SRC_2);
	mutex_unlock(&chip->lock);
	lis302dl_print_event(&chip->client->dev, reg);
}

/*
 * We cannot use I2C in interrupt context, so we just schedule work.
 */
static irqreturn_t lis302dl_irq1(int irq, void *_chip)
{
	struct lis302dl_chip *chip = _chip;
	schedule_work(&chip->work1);

	return IRQ_HANDLED;
}

static irqreturn_t lis302dl_irq2(int irq, void *_chip)
{
	return IRQ_HANDLED;
}

/* duration depends on chips data rate */
static void set_duration(struct i2c_client *c, int dr, int msec)
{
	u8 duration;
	if (dr)
		/* 400 Hz data rate max duration is 637.5 ms */
		if (msec > 637)
			duration = 0xff;
		else
			duration = (msec / 10) * 4;
	else
		duration = msec / 10;
	lis302dl_write(c, LIS302_FF_WU_DURATION_1, duration);
}

static void set_ths(struct i2c_client *c, int full_scale, int ths)
{
	u8 threshold;

	if (full_scale)
		threshold = ths / LIS302_BIG_UNIT;
	else
		/* max threshold is 2286 mg when normal scale is used*/
		if (ths > (127 * LIS302_SMALL_UNIT))
			threshold = 0x7f;
		else
			threshold = ths / LIS302_SMALL_UNIT;

	threshold &= 0x7f;
	lis302dl_write(c, LIS302_FF_THS_1, threshold);
}

static int lis302dl_power(struct lis302dl_chip *chip, int on)
{
	u8 reg, regwant;
	int result, delay;

	reg = lis302dl_read(chip->client, LIS302_CTRL_1);
	if (on)
		regwant = reg | LIS302_CTRL1_PD;
	else
		regwant = reg & ~LIS302_CTRL1_PD;

	/* Avoid unnecessary writes */
	if (reg == regwant)
		return 0;

	result = lis302dl_write(chip->client, LIS302_CTRL_1, regwant);

	/* turn on time delay depends on data rate */
	if (on) {
		delay = (chip->sample_rate ? (LIS302_TURN_ON_TIME / 400) :
			 (LIS302_TURN_ON_TIME / 100)) + 1;
		msleep(delay);
	}
	if (!result)
		chip->power = !!on;

	return !!result;
}

static void lis302dl_poweroff_work(struct work_struct *work)
{
	struct lis302dl_chip *chip =
		container_of(work, struct lis302dl_chip, poweroff_work.work);
	mutex_lock(&chip->lock);
	lis302dl_power(chip, 0);
	mutex_unlock(&chip->lock);
}

static int lis302dl_selftest(struct lis302dl_chip *chip)
{
	u8 reg;
	s8 x, y, z;
	s8 powerbit;

	reg = lis302dl_read(chip->client, LIS302_CTRL_1);
	powerbit = reg & LIS302_CTRL1_PD;
	reg |= LIS302_CTRL1_PD;
	lis302dl_write(chip->client, LIS302_CTRL_1, (reg | LIS302_CTRL1_STP));
	msleep(30);
	x = (s8)lis302dl_read(chip->client, LIS302_X);
	y = (s8)lis302dl_read(chip->client, LIS302_Y);
	z = (s8)lis302dl_read(chip->client, LIS302_Z);
	/* back to normal settings */
	lis302dl_write(chip->client, LIS302_CTRL_1, reg);
	msleep(30);
	x -= (s8)lis302dl_read(chip->client, LIS302_X);
	y -= (s8)lis302dl_read(chip->client, LIS302_Y);
	z -= (s8)lis302dl_read(chip->client, LIS302_Z);

	/* Return to passive state if we were in it. */
	if (!powerbit)
		lis302dl_write(chip->client,
			       LIS302_CTRL_1,
			       reg & ~LIS302_CTRL1_PD);

	/* Now check that delta is within specified range for each axis */
	if (x < -32 || x > -3)
		return -1;
	if (y < 3 || y > 32)
		return -1;
	if (z < 3 || z > 32)
		return -1;

	/* test passed */
	return 0;
}

/*******************************************************************************
 * SYSFS                                                                       *
 ******************************************************************************/

static ssize_t lis302dl_show_power(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int val;
	int ret;
	struct lis302dl_chip *chip = dev_get_drvdata(dev);

	mutex_lock(&chip->lock);
	val = lis302dl_read(chip->client, LIS302_CTRL_1);
	if (val >= 0)
		if (val & LIS302_CTRL1_PD)
			ret = snprintf(buf, PAGE_SIZE, "on\n");
		else
			ret = snprintf(buf, PAGE_SIZE, "off\n");
	else
		ret = val;
	mutex_unlock(&chip->lock);
	return ret;
}

static ssize_t lis302dl_set_power(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct lis302dl_chip *chip = dev_get_drvdata(dev);

	mutex_lock(&chip->lock);

	if (!strcmp(buf, "on\n"))
		lis302dl_power(chip, 1);
	else if (!strcmp(buf, "off\n"))
		lis302dl_power(chip, 0);

	mutex_unlock(&chip->lock);

	return len;
}

static ssize_t lis302dl_show_rate(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	u8 val;
	int ret;
	struct lis302dl_chip *chip = dev_get_drvdata(dev);

	mutex_lock(&chip->lock);
	val = lis302dl_read(chip->client, LIS302_CTRL_1);
	ret = snprintf(buf, PAGE_SIZE, "%d\n",
		       (val & LIS302_CTRL1_DR) ? 400 : 100);
	mutex_unlock(&chip->lock);
	return ret;
}

static ssize_t lis302dl_set_rate(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t len)
{
	struct lis302dl_chip *chip = dev_get_drvdata(dev);
	u8 reg;

	mutex_lock(&chip->lock);
	reg = lis302dl_read(chip->client, LIS302_CTRL_1);
	if (!strcmp(buf, "400\n")) {
		reg |= LIS302_CTRL1_DR;
		chip->sample_rate = 1;
		lis302dl_write(chip->client, LIS302_CTRL_1, reg);
		set_duration(chip->client, chip->sample_rate, chip->duration);
	} else if (!strcmp(buf, "100\n")) {
		reg &= ~LIS302_CTRL1_DR;
		chip->sample_rate = 0;
		lis302dl_write(chip->client, LIS302_CTRL_1, reg);
		set_duration(chip->client, chip->sample_rate, chip->duration);

	}
	mutex_unlock(&chip->lock);

	return len;
}

static ssize_t lis302dl_show_scale(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	int val, ret;
	struct lis302dl_chip *chip = dev_get_drvdata(dev);

	mutex_lock(&chip->lock);
	val = lis302dl_read(chip->client, LIS302_CTRL_1);

	if (val >= 0)
		if (val & LIS302_CTRL1_FS)
			ret = snprintf(buf, PAGE_SIZE, "full\n");
		else
			ret = snprintf(buf, PAGE_SIZE, "normal\n");
	else
		ret = val;
	mutex_unlock(&chip->lock);

	return ret;
}

static ssize_t lis302dl_set_scale(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t len)
{
	struct lis302dl_chip *chip = dev_get_drvdata(dev);
	u8 reg;

	mutex_lock(&chip->lock);
	reg = lis302dl_read(chip->client, LIS302_CTRL_1);

	if (!strcmp(buf, "full\n")) {
		reg |= LIS302_CTRL1_FS;
		chip->fs = 1;
		lis302dl_write(chip->client, LIS302_CTRL_1, reg);
		set_ths(chip->client, chip->fs, chip->threshold);
	} else if (!strcmp(buf, "normal\n")) {
		reg &= ~LIS302_CTRL1_FS;
		chip->fs = 0;
		lis302dl_write(chip->client, LIS302_CTRL_1, reg);
		set_ths(chip->client, chip->fs, chip->threshold);
	}
	mutex_unlock(&chip->lock);

	return len;
}

static ssize_t lis302dl_show_duration(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	int val;
	int ret;
	struct lis302dl_chip *chip = dev_get_drvdata(dev);

	mutex_lock(&chip->lock);
	val = lis302dl_read(chip->client, LIS302_FF_WU_DURATION_1);

	if (val >= 0)
		ret = snprintf(buf, PAGE_SIZE, "%d ms\n",
			       chip->sample_rate ? (val * 10 / 4) : (val * 10));
	else
		ret = val;
	mutex_unlock(&chip->lock);
	return ret;
}

static ssize_t lis302dl_set_duration(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t len)
{
	struct lis302dl_chip *chip = dev_get_drvdata(dev);
	unsigned long duration;
	int ret;

	ret = strict_strtoul(buf, 0, &duration);
	if (ret || duration < 0)
		return -EINVAL;
	mutex_lock(&chip->lock);
	/* max duration is 2.55 s when data rate is 100Hz */
	if (duration > 2550)
		duration = 2550;
	set_duration(chip->client, chip->sample_rate, duration);
	chip->duration = duration;
	mutex_unlock(&chip->lock);
	return len;
}

static ssize_t lis302dl_show_ths(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	int val, ret;
	struct lis302dl_chip *chip = dev_get_drvdata(dev);

	mutex_lock(&chip->lock);
	val = lis302dl_read(chip->client, LIS302_FF_THS_1);

	if (val >= 0)
		ret = snprintf(buf, PAGE_SIZE, "%d mg\n", val * (chip->fs ?
				      LIS302_BIG_UNIT : LIS302_SMALL_UNIT));
	else
		ret = val;
	mutex_unlock(&chip->lock);
	return ret;
}

static ssize_t lis302dl_set_ths(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t len)
{
	struct lis302dl_chip *chip = dev_get_drvdata(dev);
	unsigned long ths;
	int ret;

	ret = strict_strtoul(buf, 0, &ths);
	if (ret)
		return -EINVAL;
	mutex_lock(&chip->lock);
	chip->threshold = ths;
	set_ths(chip->client, chip->fs, chip->threshold);
	mutex_unlock(&chip->lock);

	return len;
}

static ssize_t lis302dl_show_samples(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	int ret;
	struct lis302dl_chip *chip = dev_get_drvdata(dev);

	mutex_lock(&chip->lock);
	ret = snprintf(buf, PAGE_SIZE, "%d\n", chip->samples);

	mutex_unlock(&chip->lock);
	return ret;
}

static ssize_t lis302dl_set_samples(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t len)
{
	struct lis302dl_chip *chip = dev_get_drvdata(dev);
	unsigned long samples;
	int ret;

	ret = strict_strtoul(buf, 0, &samples);
	if (ret ||  samples < 1)
		return -EINVAL;

	mutex_lock(&chip->lock);
	chip->samples = samples;
	mutex_unlock(&chip->lock);

	return len;
}

static ssize_t lis302dl_show_coord(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct lis302dl_chip *chip = dev_get_drvdata(dev);
	int ret, i;
	int x, y, z;

	x = y = z = 0;

	/* Cannot cancel synchronously within the mutex */
	cancel_delayed_work_sync(&chip->poweroff_work);

	mutex_lock(&chip->lock);

	if (!chip->power)
		ret = lis302dl_power(chip, 1);

	for (i = 0; i < chip->samples; i++) {
		x += (s8)lis302dl_read(chip->client, LIS302_X);
		y += (s8)lis302dl_read(chip->client, LIS302_Y);
		z += (s8)lis302dl_read(chip->client, LIS302_Z);
	}
	x /= (int)chip->samples;
	y /= (int)chip->samples;
	z /= (int)chip->samples;

	/* convert to mg */
	x *= (chip->fs ?  LIS302_BIG_UNIT : LIS302_SMALL_UNIT);
	y *= (chip->fs ?  LIS302_BIG_UNIT : LIS302_SMALL_UNIT);
	z *= (chip->fs ?  LIS302_BIG_UNIT : LIS302_SMALL_UNIT);
	ret = snprintf(buf, PAGE_SIZE, "%d %d %d\n", x, y, z);
	mutex_unlock(&chip->lock);

	schedule_delayed_work(&chip->poweroff_work, LIS302_POWEROFF_DELAY);

	return ret;
}

static ssize_t lis302dl_show_selftest(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	int ret;

	struct lis302dl_chip *chip = dev_get_drvdata(dev);

	mutex_lock(&chip->lock);
	if (!lis302dl_selftest(chip))
		ret = snprintf(buf, PAGE_SIZE, "OK\n");
	else
		ret = snprintf(buf, PAGE_SIZE, "FAIL\n");
	mutex_unlock(&chip->lock);

	return ret;
}

static struct device_attribute lis302dl_attrs[] = {
	__ATTR(enable, S_IRUGO|S_IWUSR,
	       lis302dl_show_power, lis302dl_set_power),
	__ATTR(rate, S_IRUGO|S_IWUSR,
	       lis302dl_show_rate, lis302dl_set_rate),
	__ATTR(scale, S_IRUGO|S_IWUSR,
	       lis302dl_show_scale, lis302dl_set_scale),
	__ATTR(duration, S_IRUGO|S_IWUSR,
	       lis302dl_show_duration, lis302dl_set_duration),
	__ATTR(ths, S_IRUGO|S_IWUSR,
	       lis302dl_show_ths, lis302dl_set_ths),
	__ATTR(samples, S_IRUGO|S_IWUSR,
	       lis302dl_show_samples, lis302dl_set_samples),
	__ATTR(coord, S_IRUGO|S_IWUSR,
	       lis302dl_show_coord, NULL),
	__ATTR(selftest, S_IRUGO|S_IWUSR,
	       lis302dl_show_selftest, NULL),
};

static int lis302dl_register_sysfs(struct i2c_client *c)
{
	struct device *d = &c->dev;
	int r, i;

	for (i = 0; i < ARRAY_SIZE(lis302dl_attrs); i++) {
		r = device_create_file(d, &lis302dl_attrs[i]);
		if (r)
			goto fail;
	}
	return 0;
fail:
	while (i--)
		device_remove_file(d, &lis302dl_attrs[i]);

	return r;
}

static void lis302dl_unregister_sysfs(struct i2c_client *c)
{
	struct device *d = &c->dev;
	int i;

	for (i = ARRAY_SIZE(lis302dl_attrs) - 1; i >= 0; i--)
		device_remove_file(d, &lis302dl_attrs[i]);
}

/*******************************************************************************
 * INIT
 ******************************************************************************/
static struct i2c_driver lis302dl_i2c_driver;

static int lis302dl_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct lis302dl_chip *lis;
	struct lis302dl_platform_data *pdata = client->dev.platform_data;
	int err = 0;

	if (!pdata) {
		dev_dbg(&client->dev, "no platform data?\n");
		return -EINVAL;
	}
	lis = kzalloc(sizeof(struct lis302dl_chip), GFP_KERNEL);
	if (!lis)
		return -ENOMEM;

	i2c_set_clientdata(client, lis);
	lis->client = client;

	err = lis302dl_detect(client);
	if (err)
		goto fail2;

	/* default startup values */
	lis->power	= 1;
	lis->threshold	= LIS302_THS;
	lis->duration	= LIS302_DURATION;
	lis->fs		= LIS302_FS;
	lis->sample_rate = LIS302_100HZ;
	lis->samples	= LIS302_SAMPLES;

	mutex_init(&lis->lock);

	err = lis302dl_configure(client);
	if (err < 0) {
		dev_err(&client->dev, "lis302dl error configuring chip\n");
		goto fail2;
	}

	err = lis302dl_register_sysfs(client);
	if (err) {
		printk(KERN_ALERT
		       "lis302dl: sysfs registration failed, error %d\n", err);
		goto fail2;
	}

	lis->irq1 = pdata->int1_gpio;
	lis->irq2 = pdata->int2_gpio;

	/* gpio for interrupt pin 1 */
	err = gpio_request(lis->irq1, "lis302dl_irq1");
	if (err) {
		printk(KERN_ALERT "lis302dl: cannot request gpio for int 1\n");
		goto fail2;
	}
	gpio_direction_input(lis->irq1);
	INIT_WORK(&lis->work1, lis302dl_work1);
	INIT_DELAYED_WORK(&lis->poweroff_work, lis302dl_poweroff_work);

	err = request_irq(gpio_to_irq(lis->irq1), lis302dl_irq1,
			  LIS302_IRQ_FLAGS, DRIVER_NAME, lis);
	if (err) {
		dev_err(&client->dev, "could not get IRQ_1 = %d\n",
			gpio_to_irq(lis->irq1));
		goto fail3;
	}
	schedule_delayed_work(&lis->poweroff_work, LIS302_POWEROFF_DELAY);

	return 0;

 fail3:
	free_irq(gpio_to_irq(lis->irq1), lis);
	gpio_free(lis->irq1);
 fail2:

	kfree(lis);
	return err;
}

static int lis302dl_remove(struct i2c_client *client)
{
	struct lis302dl_chip *chip = i2c_get_clientdata(client);

	lis302dl_unregister_sysfs(client);
	free_irq(gpio_to_irq(chip->irq1), chip);
	gpio_free(chip->irq1);

	cancel_delayed_work_sync(&chip->poweroff_work);
	cancel_work_sync(&chip->work1);
	lis302dl_power(chip, 0);

	kfree(chip);

	return 0;
}

static int lis302dl_suspend(struct i2c_client *client, pm_message_t state)
{
	struct lis302dl_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->lock);
	ret = lis302dl_power(chip, 0);
	mutex_unlock(&chip->lock);

	return ret;
}

static int lis302dl_resume(struct i2c_client *client)
{
	struct lis302dl_chip *chip = i2c_get_clientdata(client);
	int ret;

	mutex_lock(&chip->lock);
	ret = lis302dl_power(chip, 1);
	mutex_unlock(&chip->lock);

	return ret;
}

static const struct i2c_device_id lis302dl_id[] = {
	{ "lis302dl", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, lis302dl_id);

static struct i2c_driver lis302dl_i2c_driver = {
	.driver = {
		.name	 = DRIVER_NAME,
	},
	.suspend = lis302dl_suspend,
	.resume	= lis302dl_resume,
	.probe	= lis302dl_probe,
	.remove	= lis302dl_remove,
	.id_table = lis302dl_id,
};

static int __init lis302dl_init(void)
{
	int ret;

	ret = i2c_add_driver(&lis302dl_i2c_driver);
	if (ret < 0)
		printk(KERN_ALERT "lis302dl driver registration failed\n");

	return ret;
}

static void __exit lis302dl_exit(void)
{
	i2c_del_driver(&lis302dl_i2c_driver);
}

MODULE_AUTHOR("Nokia Corporation");
MODULE_DESCRIPTION("LIS302DL acceleration sensor driver");
MODULE_LICENSE("GPL");

module_init(lis302dl_init);
module_exit(lis302dl_exit);
