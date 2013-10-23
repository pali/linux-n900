/*
 * lp5523.c - LP5523 LED Driver
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Mathias Nyman <mathias.nyman@nokia.com>
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
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/ctype.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/leds.h>
#include <linux/leds-lp5523.h>

#define LP5523_DRIVER_NAME		"lp5523"
#define LP5523_REG_ENABLE		0x00
#define LP5523_REG_OP_MODE		0x01
#define LP5523_REG_RATIOMETRIC_MSB	0x02
#define LP5523_REG_RATIOMETRIC_LSB	0x03
#define LP5523_REG_ENABLE_LEDS_MSB	0x04
#define LP5523_REG_ENABLE_LEDS_LSB	0x05
#define LP5523_REG_LED_CNTRL_BASE	0x06
#define LP5523_REG_LED_PWM_BASE		0x16
#define LP5523_REG_LED_CURRENT_BASE	0x26
#define LP5523_REG_CONFIG		0x36
#define LP5523_REG_CHANNEL1_PC		0x37
#define LP5523_REG_CHANNEL2_PC		0x38
#define LP5523_REG_CHANNEL3_PC		0x39
#define LP5523_REG_STATUS		0x3a
#define LP5523_REG_GPO			0x3b
#define LP5523_REG_VARIABLE		0x3c
#define LP5523_REG_RESET		0x3d
#define LP5523_REG_TEMP_CTRL		0x3e
#define LP5523_REG_TEMP_READ		0x3f
#define LP5523_REG_TEMP_WRITE		0x40
#define LP5523_REG_LED_TEST_CTRL	0x41
#define LP5523_REG_LED_TEST_ADC		0x42
#define LP5523_REG_ENG1_VARIABLE	0x45
#define LP5523_REG_ENG2_VARIABLE	0x46
#define LP5523_REG_ENG3_VARIABLE	0x47
#define LP5523_REG_MASTER_FADER1	0x48
#define LP5523_REG_MASTER_FADER2	0x49
#define LP5523_REG_MASTER_FADER3	0x4a
#define LP5523_REG_CH1_PROG_START	0x4c
#define LP5523_REG_CH2_PROG_START	0x4d
#define LP5523_REG_CH3_PROG_START	0x4e
#define LP5523_REG_PROG_PAGE_SEL	0x4f
#define LP5523_REG_PROG_MEM		0x50

#define LP5523_CMD_LOAD			0x15 /* 00010101 */
#define LP5523_CMD_RUN			0x2a /* 00101010 */
#define LP5523_CMD_DISABLED		0x00 /* 00000000 */

#define LP5523_ENABLE			0x40
#define LP5523_AUTO_INC			0x40
#define LP5523_PWR_SAVE			0x20
#define LP5523_PWM_PWR_SAVE		0x04
#define LP5523_CP_1			0x08
#define LP5523_CP_1_5			0x10
#define LP5523_CP_AUTO			0x18
#define LP5523_INT_CLK			0x01
#define LP5523_AUTO_CLK			0x02
#define LP5523_EN_LEDTEST		0x80
#define LP5523_LEDTEST_DONE		0x80

#define LP5523_DEFAULT_CURRENT		50 /* microAmps */
#define LP5523_PROGRAM_LENGTH		32 /* in bytes */
#define LP5523_PROGRAM_PAGES		6
#define LP5523_ADC_SHORTCIRC_LIM	80
#define LP5523_ADC_OPEN_LIM		180

#define LP5523_LEDS			9
#define LP5523_CHANNELS			3

#define LP5523_ENG_MASK_BASE		0x30 /* 00110000 */

#define LP5523_ENG_STATUS_MASK          0x07 /* 00000111 */

#define LP5523_IRQ_FLAGS                IRQF_TRIGGER_FALLING


#define LED_ACTIVE(mux, led)		(!!(mux & (0x0001 << led)))
#define SHIFT_MASK(id)			(((id) - 1) * 2)



struct lp5523_engine {
	const struct attribute_group *attributes;
	int		id;
	u8		mode;
	u8		prog_page;
	u8		mux_page;
	u16		led_mux;
	u8		engine_mask;
};

struct lp5523_led {
	int			id;
	u8			led_nr;
	u8			led_current;
	struct led_classdev     cdev;
};

struct lp5523_chip {
	struct mutex		lock;
	struct i2c_client	*client;
	struct work_struct	work;
	u8			active_led;
	struct lp5523_engine	engines[LP5523_CHANNELS];
	struct lp5523_led	leds[LP5523_LEDS];
	u8			num_leds;
	int                     irq;
	int			chip_en;
	/* for initialisation */
	wait_queue_head_t       configured;
	u8                      engine_config;
};

#define cdev_to_led(c)          container_of(c, struct lp5523_led, cdev)


static struct lp5523_chip *engine_to_lp5523(struct lp5523_engine *engine)
{
	return container_of(engine, struct lp5523_chip,
			    engines[engine->id - 1]);
}

static struct lp5523_chip *led_to_lp5523(struct lp5523_led *led)
{
	return container_of(led, struct lp5523_chip,
			    leds[led->id]);
}


static int lp5523_set_mode(struct lp5523_engine *engine, u8 mode);
static int lp5523_set_engine_mode(struct lp5523_engine *engine, u8 mode);
static int lp5523_load_program(struct lp5523_engine *engine, u8 *pattern);


static void lp5523_work(struct work_struct  *work);
static irqreturn_t lp5523_irq(int irq, void *_chip);


static int lp5523_write(struct i2c_client *client, u8 reg, u8 value)
{
	return i2c_smbus_write_byte_data(client, reg, value);
}

static int lp5523_read(struct i2c_client *client, u8 reg, u8 *buf)
{
	s32 ret = i2c_smbus_read_byte_data(client, reg);

	if (ret < 0)
		return -EIO;

	*buf = ret;
	return 0;
}

static int lp5523_detect(struct i2c_client *client)
{
	int ret;
	u8 buf;

	if ((ret = lp5523_write(client, LP5523_REG_ENABLE, 0x40)))
		return ret;
	if ((ret = lp5523_read(client, LP5523_REG_ENABLE, &buf)))
		return ret;
	if (buf == 0x40)
		return 0;
	else
		return -ENODEV;
}

static int lp5523_configure(struct i2c_client *client)
{
	struct lp5523_chip *chip = i2c_get_clientdata(client);
	int i, ret = 0;

	/* one pattern per engine setting led mux start and stop addresses */
	u8 pattern[][LP5523_PROGRAM_LENGTH] =  {
		{ 0x9c, 0x30, 0x9c, 0xb0, 0x9d, 0x80, 0xd8, 0x00, 0},
		{ 0x9c, 0x40, 0x9c, 0xc0, 0x9d, 0x80, 0xd8, 0x00, 0},
		{ 0x9c, 0x50, 0x9c, 0xd0, 0x9d, 0x80, 0xd8, 0x00, 0},
	};

	INIT_WORK(&chip->work, lp5523_work);
	ret |= request_irq(chip->irq, lp5523_irq,
			  LP5523_IRQ_FLAGS, LP5523_DRIVER_NAME, chip);
	if (ret) {
		dev_err(&client->dev, "could not get IRQ = %d\n",
			chip->irq);
		goto fail1;
	}


	lp5523_write(client, LP5523_REG_RESET, 0xff);

	msleep(10);

	ret |= lp5523_write(client, LP5523_REG_ENABLE, LP5523_ENABLE);
	/* Chip startup time after reset is 500 us */
	msleep(1);

	ret |= lp5523_write(client, LP5523_REG_CONFIG,
			    LP5523_AUTO_INC | LP5523_PWR_SAVE |
			    LP5523_CP_AUTO | LP5523_AUTO_CLK |
			    LP5523_PWM_PWR_SAVE);

	/* turn on all leds */
	ret |= lp5523_write(client, LP5523_REG_ENABLE_LEDS_MSB, 0x01);
	ret |= lp5523_write(client, LP5523_REG_ENABLE_LEDS_LSB, 0xff);

	/* set current for all leds */
	for (i = 0; i < chip->num_leds; i++)
		lp5523_write(client,
			     LP5523_REG_LED_CURRENT_BASE + chip->leds[i].led_nr,
			     chip->leds[i].led_current);

	/* hardcode 32 bytes of memory for each engine from program memory */
	ret |= lp5523_write(client, LP5523_REG_CH1_PROG_START, 0x00);
	ret |= lp5523_write(client, LP5523_REG_CH2_PROG_START, 0x10);
	ret |= lp5523_write(client, LP5523_REG_CH3_PROG_START, 0x20);

	/* write led mux address space for each channel */
	ret |= lp5523_load_program(&chip->engines[0], pattern[0]);
	ret |= lp5523_load_program(&chip->engines[1], pattern[1]);
	ret |= lp5523_load_program(&chip->engines[2], pattern[2]);

	if (ret) {
		dev_err(&client->dev, "could not load mux programs\n");
		goto fail2;
	}

	init_waitqueue_head(&chip->configured);

	chip->engine_config = 0;

	/* set all engines exec state and mode to run 00101010 */
	ret |= lp5523_write(client, LP5523_REG_ENABLE,
			    (LP5523_CMD_RUN | LP5523_ENABLE));

	ret |= lp5523_write(client, LP5523_REG_OP_MODE, LP5523_CMD_RUN);

	if (ret) {
		dev_err(&client->dev, "could not start mux programs\n");
		goto fail2;
	}

	ret |= wait_event_interruptible(chip->configured,
			(chip->engine_config == LP5523_ENG_STATUS_MASK));

	if (ret) {
		dev_err(&client->dev,
			"got signal while waiting for interrupt\n");
		goto fail2;
	}

	dev_info(&client->dev, "disabling engines\n");

	ret |= lp5523_write(client, LP5523_REG_OP_MODE, LP5523_CMD_DISABLED);

fail2:
	free_irq(chip->irq, chip);
fail1:
	return ret;
}

static int lp5523_set_engine_mode(struct lp5523_engine *engine, u8 mode)
{
	struct lp5523_chip *chip = engine_to_lp5523(engine);
	struct i2c_client *client = chip->client;
	int ret;
	u8 engine_state;

	ret = lp5523_read(client, LP5523_REG_OP_MODE, &engine_state);
	if (ret)
		goto fail;

	engine_state &= ~(engine->engine_mask);

	/* set mode only for this engine */
	mode &= engine->engine_mask;

	engine_state |= mode;

	ret |= lp5523_write(client, LP5523_REG_OP_MODE, engine_state);
fail:
	return ret;
}

static int lp5523_load_mux(struct lp5523_engine *engine, u16 mux)
{
	struct lp5523_chip *chip = engine_to_lp5523(engine);
	struct i2c_client *client = chip->client;
	int ret = 0;

	ret |= lp5523_set_engine_mode(engine, LP5523_CMD_LOAD);

	ret |= lp5523_write(client, LP5523_REG_PROG_PAGE_SEL, engine->mux_page);
	ret |= lp5523_write(client, LP5523_REG_PROG_MEM,
			    (u8)(mux >> 8));
	ret |= lp5523_write(client, LP5523_REG_PROG_MEM + 1, (u8)(mux));
	engine->led_mux = mux;

	return ret;
}

static int lp5523_load_program(struct lp5523_engine *engine, u8 *pattern)
{
	struct lp5523_chip *chip = engine_to_lp5523(engine);
	struct i2c_client *client = chip->client;

	int ret = 0;

	ret |= lp5523_set_engine_mode(engine, LP5523_CMD_LOAD);

	ret |= lp5523_write(client, LP5523_REG_PROG_PAGE_SEL,
			    engine->prog_page);
	ret |= i2c_smbus_write_i2c_block_data(client, LP5523_REG_PROG_MEM,
					      LP5523_PROGRAM_LENGTH, pattern);

	return ret;
}

static int lp5523_run_program(struct lp5523_engine *engine)
{
	struct lp5523_chip *chip = engine_to_lp5523(engine);
	struct i2c_client *client = chip->client;
	int ret;

	ret = lp5523_write(client, LP5523_REG_ENABLE, LP5523_CMD_RUN | LP5523_ENABLE);
	if (ret)
		goto fail;

	ret = lp5523_set_engine_mode(engine, LP5523_CMD_RUN);
fail:
	return ret;
}

static int lp5523_mux_parse(const char *buf, u16 *mux, size_t len)
{
	int i;
	u16 tmp_mux = 0;
	len = len < LP5523_LEDS ? len : LP5523_LEDS;
	for (i = 0; i < len; i++) {
		switch (buf[i]) {
		case '1':
			tmp_mux |= (1 << i);
			break;
		case '0':
			break;
		case '\n':
			i = len;
			break;
		default:
			return -1;
		}
	}
	*mux = tmp_mux;

	return 0;
}

static void lp5523_mux_to_array(u16 led_mux, char *array)
{
	int i, pos = 0;
	for (i = 0; i < LP5523_LEDS; i++)
		pos += sprintf(array + pos, "%x", LED_ACTIVE(led_mux, i));

	array[pos] = '\0';
}

/*--------------------------------------------------------------*/
/*			Sysfs interface				*/
/*--------------------------------------------------------------*/

#define show_leds(nr)							\
static ssize_t show_engine##nr##_leds(struct device *dev,		\
			    struct device_attribute *attr,		\
			    char *buf)					\
{									\
	struct i2c_client *client = to_i2c_client(dev);			\
	struct lp5523_chip *chip = i2c_get_clientdata(client);		\
	char mux[LP5523_LEDS + 1];					\
									\
	lp5523_mux_to_array(chip->engines[nr - 1].led_mux, mux);	\
									\
	return sprintf(buf, "%s\n", mux);				\
}
show_leds(1)
show_leds(2)
show_leds(3)

#define store_leds(nr)						\
static ssize_t store_engine##nr##_leds(struct device *dev,	\
			     struct device_attribute *attr,	\
			     const char *buf, size_t len)	\
{								\
	struct i2c_client *client = to_i2c_client(dev);		\
	struct lp5523_chip *chip = i2c_get_clientdata(client);	\
	u16 mux = 0;						\
								\
	if (lp5523_mux_parse(buf, &mux, len))			\
		return -EINVAL;					\
								\
	if (lp5523_load_mux(&chip->engines[nr - 1], mux))	\
		return -EINVAL;					\
								\
	return len;						\
}
store_leds(1)
store_leds(2)
store_leds(3)

static ssize_t lp5523_selftest(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct lp5523_chip *chip = i2c_get_clientdata(client);
	int i, ret, pos = 0;
	u8 status, adc;

	mutex_lock(&chip->lock);

	for (i = 0; i < LP5523_LEDS; i++) {
		lp5523_write(chip->client, LP5523_REG_LED_PWM_BASE + i, 0xff);
		/* let current stabilize 2ms before measurements start */
		msleep(2);
		lp5523_write(chip->client,
			     LP5523_REG_LED_TEST_CTRL,
			     LP5523_EN_LEDTEST | i);
		/* ledtest takes 2.7ms  */
		msleep(3);
		ret = lp5523_read(chip->client, LP5523_REG_STATUS, &status);
		if (!(status & LP5523_LEDTEST_DONE))
			msleep(3);
		ret |= lp5523_read(chip->client, LP5523_REG_LED_TEST_ADC, &adc);

		if (adc > LP5523_ADC_OPEN_LIM || adc < LP5523_ADC_SHORTCIRC_LIM)
			pos += sprintf(buf + pos, "LED %d FAIL\n", i);

		lp5523_write(chip->client, LP5523_REG_LED_PWM_BASE + i, 0x00);
	}
	if (pos == 0)
		pos = sprintf(buf, "OK\n");

	mutex_unlock(&chip->lock);

	return pos;
}

static void lp5523_set_brightness(struct led_classdev *cdev,
			     enum led_brightness brightness)
{
	struct lp5523_led *led = cdev_to_led(cdev);
	struct lp5523_chip *chip = led_to_lp5523(led);
	struct i2c_client *client = chip->client;

	mutex_lock(&chip->lock);

	lp5523_write(client,
		     LP5523_REG_LED_PWM_BASE + led->led_nr,
		     (u8)brightness);

	mutex_unlock(&chip->lock);
}

static int lp5523_do_store_load(struct lp5523_engine *engine,
				const char *buf, size_t len)
{
	struct lp5523_chip *chip = engine_to_lp5523(engine);
	struct i2c_client *client = chip->client;
	int  ret, nrchars, offset = 0, i = 0;
	char c[3];
	unsigned cmd;
	u8 pattern[LP5523_PROGRAM_LENGTH] = {0};

	while ((offset < len - 1) && (i < LP5523_PROGRAM_LENGTH)) {
		/* separate sscanfs because length is working only for %s */
		ret = sscanf(buf + offset, "%2s%n ", c, &nrchars);
		ret = sscanf(c, "%2x", &cmd);
		if (ret != 1)
			goto fail;
		pattern[i] = (u8)cmd;

		offset += nrchars;
		i++;
	}

	/* pattern commands are always two bytes long */
	if (i % 2)
		goto fail;

	mutex_lock(&chip->lock);

	ret = lp5523_load_program(engine, pattern);
	mutex_unlock(&chip->lock);

	if (ret) {
		dev_err(&client->dev, "failed loading pattern\n");
		return ret;
	}

	return len;
fail:
	dev_err(&client->dev, "wrong pattern format\n");
	return -EINVAL;
}

#define store_load(nr)							\
static ssize_t store_engine##nr##_load(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t len)	\
{									\
	struct i2c_client *client = to_i2c_client(dev);			\
	struct lp5523_chip *chip = i2c_get_clientdata(client);		\
	int ret;							\
	ret = lp5523_do_store_load(&chip->engines[nr - 1], buf, len);	\
	return ret;							\
}
store_load(1)
store_load(2)
store_load(3)

#define show_mode(nr)							\
static ssize_t show_engine##nr##_mode(struct device *dev,		\
				    struct device_attribute *attr,	\
				    char *buf)				\
{									\
	struct i2c_client *client = to_i2c_client(dev);			\
	struct lp5523_chip *chip = i2c_get_clientdata(client);		\
	switch (chip->engines[nr - 1].mode) {				\
	case LP5523_CMD_RUN:						\
		return sprintf(buf, "run\n");				\
	case LP5523_CMD_LOAD:						\
		return sprintf(buf, "load\n");				\
	case LP5523_CMD_DISABLED:					\
		return sprintf(buf, "disabled\n");			\
	default:							\
	return sprintf(buf, "disabled\n");				\
	}								\
}
show_mode(1)
show_mode(2)
show_mode(3)

#define store_mode(nr)							\
static ssize_t store_engine##nr##_mode(struct device *dev,		\
				     struct device_attribute *attr,	\
				     const char *buf, size_t len)	\
{									\
	struct i2c_client *client = to_i2c_client(dev);			\
	struct lp5523_chip *chip = i2c_get_clientdata(client);		\
	struct lp5523_engine *engine = &chip->engines[nr - 1];		\
	mutex_lock(&chip->lock);					\
									\
	if (!strncmp(buf, "run", 3))					\
		lp5523_set_mode(engine, LP5523_CMD_RUN);		\
	else if (!strncmp(buf, "load", 4))				\
		lp5523_set_mode(engine, LP5523_CMD_LOAD);		\
	else if (!strncmp(buf, "disabled", 8))				\
		lp5523_set_mode(engine, LP5523_CMD_DISABLED);		\
									\
	mutex_unlock(&chip->lock);					\
	return len;							\
}
store_mode(1)
store_mode(2)
store_mode(3)

static ssize_t show_current(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct lp5523_led *led = cdev_to_led(led_cdev);

	return sprintf(buf, "%d\n", led->led_current);
}

static ssize_t store_current(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t len)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct lp5523_led *led = cdev_to_led(led_cdev);
	struct lp5523_chip *chip = led_to_lp5523(led);
	ssize_t ret = -EINVAL;
	char *after;
	unsigned long curr = simple_strtoul(buf, &after, 10);
	size_t count = after - buf;

	if (*after && isspace(*after))
		count++;

	if (count == len) {
		ret = count;

		mutex_lock(&chip->lock);
		lp5523_write(chip->client,
			     LP5523_REG_LED_CURRENT_BASE + led->led_nr,
			     (u8)curr);
		mutex_unlock(&chip->lock);

		led->led_current = (u8)curr;
	}
	return ret;
}

/* led class device attributes */
static DEVICE_ATTR(led_current, S_IRUGO | S_IWUGO, show_current, store_current);

/* device attributes */
static DEVICE_ATTR(engine1_mode, S_IRUGO | S_IWUGO,
		   show_engine1_mode, store_engine1_mode);
static DEVICE_ATTR(engine2_mode, S_IRUGO | S_IWUGO,
		   show_engine2_mode, store_engine2_mode);
static DEVICE_ATTR(engine3_mode, S_IRUGO | S_IWUGO,
		   show_engine3_mode, store_engine3_mode);
static DEVICE_ATTR(engine1_leds, S_IRUGO | S_IWUGO,
		   show_engine1_leds, store_engine1_leds);
static DEVICE_ATTR(engine2_leds, S_IRUGO | S_IWUGO,
		   show_engine2_leds, store_engine2_leds);
static DEVICE_ATTR(engine3_leds, S_IRUGO | S_IWUGO,
		   show_engine3_leds, store_engine3_leds);
static DEVICE_ATTR(engine1_load, S_IWUGO, NULL, store_engine1_load);
static DEVICE_ATTR(engine2_load, S_IWUGO, NULL, store_engine2_load);
static DEVICE_ATTR(engine3_load, S_IWUGO, NULL, store_engine3_load);
static DEVICE_ATTR(selftest, S_IRUGO, lp5523_selftest, NULL);

static struct attribute *lp5523_attributes[] = {
	&dev_attr_engine1_mode.attr,
	&dev_attr_engine2_mode.attr,
	&dev_attr_engine3_mode.attr,
	&dev_attr_selftest.attr,
	NULL
};

static struct attribute *lp5523_engine1_attributes[] = {
	&dev_attr_engine1_load.attr,
	&dev_attr_engine1_leds.attr,
	NULL
};

static struct attribute *lp5523_engine2_attributes[] = {
	&dev_attr_engine2_load.attr,
	&dev_attr_engine2_leds.attr,
	NULL
};

static struct attribute *lp5523_engine3_attributes[] = {
	&dev_attr_engine3_load.attr,
	&dev_attr_engine3_leds.attr,
	NULL
};

static const struct attribute_group lp5523_group = {
	.attrs = lp5523_attributes,
};

static const struct attribute_group lp5523_engine_group[] = {
	{.attrs = lp5523_engine1_attributes },
	{.attrs = lp5523_engine2_attributes },
	{.attrs = lp5523_engine3_attributes },
};

static int lp5523_register_sysfs(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int ret;

	ret = sysfs_create_group(&dev->kobj, &lp5523_group);
	if (ret < 0)
		return ret;

	return 0;
}

static void lp5523_unregister_sysfs(struct i2c_client *client)
{
	struct lp5523_chip *chip = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	int i;

	sysfs_remove_group(&dev->kobj, &lp5523_group);

	for (i = 0; i < LP5523_CHANNELS; i++) {
		if (chip->engines[i].mode == LP5523_CMD_LOAD)
			sysfs_remove_group(&dev->kobj, &lp5523_engine_group[i]);
	}

	for (i = 0; i < chip->num_leds; i++)
		device_remove_file(chip->leds[i].cdev.dev,
				   &dev_attr_led_current);
}

/*--------------------------------------------------------------*/
/*			Set chip operating mode			*/
/*--------------------------------------------------------------*/
static int lp5523_set_mode(struct lp5523_engine *engine, u8 mode)
{
	/*  engine to chip */
	struct lp5523_chip *chip = engine_to_lp5523(engine);
	struct i2c_client *client = chip->client;
	struct device *dev = &client->dev;
	int ret = 0;

	/* if in that mode already do nothing, except for run */
	if (mode == engine->mode && mode != LP5523_CMD_RUN)
		return 0;

	if (mode == LP5523_CMD_RUN)
		ret = lp5523_run_program(engine);

	else if (mode == LP5523_CMD_LOAD) {

		lp5523_set_engine_mode(engine, LP5523_CMD_DISABLED);
		lp5523_set_engine_mode(engine, LP5523_CMD_LOAD);

		if ((ret = sysfs_create_group(&dev->kobj, engine->attributes)))
			return ret;
	}

	else if (mode == LP5523_CMD_DISABLED)
		lp5523_set_engine_mode(engine, LP5523_CMD_DISABLED);

	/* remove load attribute from sysfs if not in load mode */
	if (engine->mode == LP5523_CMD_LOAD && mode != LP5523_CMD_LOAD)
		sysfs_remove_group(&dev->kobj, engine->attributes);

	engine->mode = mode;

	return ret;
}

/*--------------------------------------------------------------*/
/*			Probe, Attach, Remove			*/
/*--------------------------------------------------------------*/
static int __init lp5523_init_engine(struct lp5523_engine *engine, int id)
{
	if (id < 1 || id > LP5523_CHANNELS)
		return -1;
	engine->id = id;
	engine->engine_mask = LP5523_ENG_MASK_BASE >> SHIFT_MASK(id);
	engine->prog_page = id - 1;
	engine->mux_page = id + 2;
	engine->attributes = &lp5523_engine_group[id - 1];

	return 0;
}

static int __init lp5523_init_led(struct lp5523_led *led, struct device *dev,
			   int id, struct lp5523_platform_data *pdata)
{
	char name[32];
	if (id >= LP5523_LEDS)
		return -1;
	led->led_current = LP5523_DEFAULT_CURRENT;
	led->id = id;
	led->led_nr = pdata->led_config[id].led_nr;

	if (pdata->led_config[id].led_current)
		led->led_current = pdata->led_config[id].led_current;
	if (pdata->led_config[id].name)
		snprintf(name, 32, "lp5523:%s",
			 pdata->led_config[id].name);
	else
		snprintf(name, 32, "lp5523:led%d", id);

	led->cdev.name = name;
	led->cdev.brightness_set = lp5523_set_brightness;
	if (led_classdev_register(dev, &led->cdev) < 0) {
		dev_err(dev, "couldn't register led %d\n", id);
		return -1;
	}
	if (device_create_file(led->cdev.dev, &dev_attr_led_current) < 0) {
		dev_err(dev, "couldn't register current attribute\n");
		led_classdev_unregister(&led->cdev);
		return -1;
	}
	return 0;
}

static struct i2c_driver lp5523_driver;

/* Interrupt handler bottom half. */
static void lp5523_work(struct work_struct  *work)
{
	struct lp5523_chip *chip =
		container_of(work, struct lp5523_chip, work);
	u8 reg;

	dev_info(&chip->client->dev, "got interrupt from led chip\n");

	mutex_lock(&chip->lock);

	if (chip->engine_config != LP5523_ENG_STATUS_MASK) {

		/* ack the interrupt */
		lp5523_read(chip->client, LP5523_REG_STATUS, &reg);

		dev_info(&chip->client->dev,
			"interrupt from led chip %x\n", reg);

		chip->engine_config |= (reg & LP5523_ENG_STATUS_MASK);

		if (chip->engine_config == LP5523_ENG_STATUS_MASK) {

			dev_info(&chip->client->dev,
				"all engines configured\n");
			wake_up(&chip->configured);
		} else {
			dev_info(&chip->client->dev,
				"engine_config == %x\n", chip->engine_config);
		}
	}

	mutex_unlock(&chip->lock);
}

/*
 * We cannot use I2C in interrupt context, so we just schedule work.
 */
static irqreturn_t lp5523_irq(int irq, void *_chip)
{
	struct lp5523_chip *chip = _chip;
	schedule_work(&chip->work);

	return IRQ_HANDLED;
}


static int lp5523_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct lp5523_chip		*chip;
	struct lp5523_platform_data	*pdata;
	int ret, i;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	i2c_set_clientdata(client, chip);
	chip->client = client;

	pdata = client->dev.platform_data;

	if (!pdata) {
		dev_err(&client->dev, "no platform data\n");
		ret = -EINVAL;
		goto fail1;
	}

	mutex_init(&chip->lock);

	if ((ret = lp5523_detect(client)))
		goto fail1;

	dev_info(&client->dev, "LP5523 Programmable led chip found\n");

	chip->irq = pdata->irq;
	chip->chip_en = pdata->chip_en;

	/* Initialize engines */
	for (i = 0; i < LP5523_CHANNELS; i++) {
		ret = lp5523_init_engine(&chip->engines[i], i + 1);
		if (ret) {
			dev_err(&client->dev, "error initializing engine\n");
			goto fail1;
		}
	}
	ret = lp5523_configure(client);
	if (ret < 0) {
		dev_err(&client->dev, "error configuring chip \n");
		goto fail1;
	}

	/* Initialize leds */
	chip->num_leds = pdata->num_leds;
	for (i = 0; i < pdata->num_leds; i++) {
		ret = lp5523_init_led(&chip->leds[i], &client->dev, i, pdata);
		if (ret) {
			dev_err(&client->dev, "error initializing leds\n");
			goto fail2;
		}
	}

	ret = lp5523_register_sysfs(client);
	if (ret) {
		dev_err(&client->dev, "registering sysfs failed \n");
		goto fail2;
	}
	return ret;
fail2:
	for (i = 0; i < pdata->num_leds; i++)
		led_classdev_unregister(&chip->leds[i].cdev);

fail1:
	kfree(chip);
	return ret;
}

static int lp5523_remove(struct i2c_client *client)
{
	struct lp5523_chip *chip = i2c_get_clientdata(client);
	int i;

	lp5523_unregister_sysfs(client);

	for (i = 0; i < chip->num_leds; i++)
		led_classdev_unregister(&chip->leds[i].cdev);

	kfree(chip);

	return 0;
}

static const struct i2c_device_id lp5523_id[] = {
	{ "lp5523", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, lp5523_id);

static struct i2c_driver lp5523_driver = {
	.driver = {
		.name	= LP5523_DRIVER_NAME,
	},
	.probe		= lp5523_probe,
	.remove		= lp5523_remove,
	.id_table	= lp5523_id,
};

static int __init lp5523_init(void)
{
	int ret;

	ret = i2c_add_driver(&lp5523_driver);

	if (ret < 0)
		printk(KERN_ALERT "Adding lp5523 driver failed \n");

	return ret;
}

static void __exit lp5523_exit(void)
{
	i2c_del_driver(&lp5523_driver);
}

MODULE_AUTHOR("Mathias Nyman <mathias.nyman@nokia.com>");
MODULE_DESCRIPTION("lp5523 LED driver");
MODULE_LICENSE("GPL");

module_init(lp5523_init);
module_exit(lp5523_exit);
