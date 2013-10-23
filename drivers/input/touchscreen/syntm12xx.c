/*
 * syntm12xx.c
 * Synaptic TM12XX touchscreen driver
 *
 * Copyright (C) 2009 Nokia Corporation
 * Author: Mika Kuoppala <mika.kuoppala@nokia.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/firmware.h>
#include <linux/input/syntm12xx-plat.h>
#include <linux/regulator/consumer.h>

#define DRIVER_DESC       "Synaptic TM12xx Touchscreen (MT)"
#define DRIVER_NAME       "syntm12xx"

/* Do validation of firmware image on device */
#define FIRMWARE_VERIFY

/* Number of touch points and input devices supported */
#define MAX_TOUCH_POINTS    2

#define REG_PDT_PROPERTIES  0xef
#define REG_PAGE_SELECT     0xff

#define FUNC_DEVICE_CONTROL 0x01
#define FUNC_BIST           0x08
#define FUNC_2D             0x11
#define FUNC_BUTTONS        0x19
#define FUNC_TIMER          0x32
#define FUNC_FLASH          0x34
#define FUNC_PROXIMITY      0x40

#define MAX_FUNC_DESCS      7

/* Device Control Functionality */
#define DEVICE_CONTROL_DATA_STATUS         0x00
#define DEVICE_CONTROL_DATA_INTR_STATUS    0x01

#define DEVICE_CONTROL_STATUS_SCODE_MASK   0x0f
#define STATUS_CODE_NO_ERROR               0x00
#define STATUS_CODE_RESET                  0x01
#define STATUS_CODE_INVALID_CONF           0x02
#define STATUS_CODE_DEVICE_FAILURE         0x03

#define DEVICE_CONTROL_CTRL                0
#define DEVICE_CONTROL_CONFIGURED          (1 << 7)
#define DEVICE_CONTROL_SLEEP_NORMAL        0x00
#define DEVICE_CONTROL_SLEEP_SENSOR        0x01
#define DEVICE_CONTROL_INTR_ENABLE         1
#define INTR_FLASH                         (1 << 0)
#define INTR_STATUS                        (1 << 1)
#define INTR_BIST                          (1 << 2)
#define INTR_2D                            (1 << 3)
#define INTR_BUTTON                        (1 << 4)
#define INTR_UADC                          (1 << 5)
#define INTR_ALL                           0x3f

#define DEVICE_CONTROL_STATUS_FLASH_PROG   (1 << 6)
#define DEVICE_CONTROL_STATUS_UNCONFIGURED (1 << 7)

#define DEVICE_CONTROL_COMMAND             0x00
#define DEVICE_COMMAND_RESET               (1 << 0)
#define DEVICE_COMMAND_SHUTDOWN            (1 << 1)

#define DEVICE_CONTROL_QUERY_MANID                0
#define DEVICE_CONTROL_QUERY_PROD_PROPERTIES      1
#define DEVICE_CONTROL_QUERY_PROD_FAMILY          2
#define DEVICE_CONTROL_QUERY_FW_VER               3
#define DEVICE_CONTROL_QUERY_PROD_ID              11
#define DEVICE_CONTROL_QUERY_PROD_ID_LAST         20
#define PRODUCT_ID_LEN (DEVICE_CONTROL_QUERY_PROD_ID_LAST - \
			DEVICE_CONTROL_QUERY_PROD_ID + 1)


/* Capasitive Buttons */

#define MAX_BUTTONS                    31

#define BUTTON_QUERY_QUERY0            0
#define BUTTON_QUERY_BUTTON_COUNT      1
#define BUTTON_QUERY_BUTTON_MASK       0x1f

#define BUTTON_CONFIGURABLE            (1 << 0)

/* 2D Functionality */

#define TOUCH_QUERY_NUM_SENSORS        0 /* Zero based */
#define TOUCH_QUERY_LEN                6

#define TOUCH_CONTROL_REPORT_MODE      0
#define TOUCH_CONTROL_SENSOR_MAX_X     6
#define TOUCH_CONTROL_SENSOR_MAX_Y     8
#define TOUCH_CONTROL_SENSOR_MAPPING   10

/* This is for now fixed for 2 touch points */
#define TOUCH_DATA_LEN                 (1 + (MAX_TOUCH_POINTS * 5))

#define FINGER_STATE_NOT_PRESENT       0
#define FINGER_STATE_ACCURATE          1
#define FINGER_STATE_INACCURATE        2
#define FINGER_STATE_RESERVED          3

/* Flashing */
#define FLASH_MAX_SIZE                 (16*1024)
#define FW_MAX_NAME_SIZE               31

#define FLASH_QUERY_BOOTLOADER_ID_0    0
#define FLASH_QUERY_BOOTLOADER_ID_1    1
#define FLASH_QUERY_PROPERTIES         2
#define FLASH_QUERY_BLOCK_SIZE_0       3
#define FLASH_QUERY_BLOCK_SIZE_1       4
#define FLASH_QUERY_FW_BLOCK_COUNT_0   5
#define FLASH_QUERY_FW_BLOCK_COUNT_1   6
#define FLASH_QUERY_CONF_BLOCK_COUNT_0 7
#define FLASH_QUERY_CONF_BLOCK_COUNT_1 8

/* Flash Properties */
#define FLASH_PROPERTY_REGMAP_VERSION  (1 << 0)

/* Commands for FLASH_DATA_COMMAND */
#define FLASH_CMD_IDLE                 0x00
#define FLASH_CMD_FW_CRC_BLOCK         0x01
#define FLASH_CMD_FW_WRITE_BLOCK       0x02
#define FLASH_CMD_ERASE_ALL            0x03
#define FLASH_CMD_CONF_READ_BLOCK      0x05
#define FLASH_CMD_CONF_WRITE_BLOCK     0x06
#define FLASH_CMD_CONF_ERASE_BLOCK     0x07
#define FLASH_CMD_PROGRAM_ENABLE       0x0f

#define FLASH_ERROR_SUCCESS            0
#define FLASH_ERROR_RESERVED           1
#define FLASH_ERROR_NOT_ENABLED        2
#define FLASH_ERROR_INVALID_BLOCK      3
#define FLASH_ERROR_BLOCK_NOT_ERASED   4
#define FLASH_ERROR_ERASE_KEY          5
#define FLASH_ERROR_UNKNOWN            6
#define FLASH_ERROR_RESET              7
#define FLASH_ERROR_COUNT              8

#define DEVICE_STATUS_NO_ERROR         0
#define DEVICE_STATUS_RESET_OCCURRED   1
#define DEVICE_STATUS_INVALID_CONFIG   2
#define DEVICE_STATUS_DEVICE_FAILURE   3
#define DEVICE_STATUS_CFG_CRC_FAILURE  4
#define DEVICE_STATUS_FW_CRC_FAILURE   5
#define DEVICE_STATUS_CRC_IN_PROGRESS  6
#define DEVICE_STATUS_UNKNOWN          7
#define DEVICE_STATUS_COUNT            8

/* Selftest */
#define BIST_QUERY_LIMIT_REG_COUNT     0
#define BIST_DATA_TEST_NUMBER_CTRL     0
#define BIST_DATA_OVERALL_RESULT       1
#define BIST_DATA_TEST_RESULT          2
#define BIST_CONTROL_COMMAND           0

static const char *const device_status_str[DEVICE_STATUS_COUNT] = {
	"no error",
	"reset occurred",
	"invalid configuration",
	"device failure",
	"configuration crc failure",
	"firmware crc failure",
	"crc in progress",
	"unknown",
};

static const char *const flash_error_str[FLASH_ERROR_COUNT] = {
	"success",
	"reserved",
	"programming not enabled",
	"invalid block number",
	"block not erased",
	"erase key incorrect",
	"unknown",
	"device reset",
};

/* Offsets within firmware image */
#define FW_FILE_CHECKSUM  0x0000
#define FW_VERSION        0x0007
#define FW_FW_SIZE        0x0008
#define FW_CONFIG_SIZE    0x000C
#define FW_PRODUCT_ID     0x0010
#define FW_PRODUCT_INFO_0 0x001E
#define FW_PRODUCT_INFO_1 0x001F
#define FW_FW_DATA        0x0100

/* How many times we try to re-initialize chip in row */
#define MAX_FAILED_INITS  4

/* How much data we can put into single write block */
#define MAX_I2C_WRITE_BLOCK_SIZE 32

#define DFLAG_VERBOSE  (1 << 0)
#define DFLAG_I2C_DUMP (1 << 1)

struct syn;

struct func_desc {
	void (*intr_handler)(struct syn *sd, u8 bits);
	u8 num;
	u8 version;
	u8 query;
	u8 command;
	u8 control;
	u8 data;
	u8 exists;

	u8 intr_start_bit;
	u8 intr_sources;
};

struct touch_sensor_caps {
	unsigned   is_configurable:1;
	unsigned   has_gestures:1;
	unsigned   has_abs_mode:1;
	unsigned   has_rel_mode:1;
	unsigned   finger_count:4;
	unsigned   x_electrodes:5;
	unsigned   y_electrodes:5;
	unsigned   max_electrodes:5;
	unsigned   abs_data_size:2;

	/* XXX These are not yet in use
	unsigned   has_pinch:1;
	unsigned   has_press:1;
	unsigned   has_flick:1;
	unsigned   has_early_tap:1;
	unsigned   has_double_tap:1;
	unsigned   had_tap_and_hold:1;
	unsigned   has_single_tap:1;
	unsigned   has_anchored_finger:1;
	unsigned   has_palm_detect:1;
	*/

	u16        max_x;
	u16        max_y;
};

struct button_caps {
	u8 button_count;
};

struct flash_caps {
	u16 bootloader_id;
	u16 block_size;
	u16 fw_block_count;
	u16 conf_block_count;
	u8 properties;
};

struct fw_image {
	u32 file_checksum;
	u8  version;
	u8  product_id[PRODUCT_ID_LEN + 1];
	u8  product_info_0;
	u8  product_info_1;
	const u8 *fw_data;
	u32 fw_size;
	const u8 *config_data;
	u32 config_size;
	u32 config_checksum;
};

struct touch_data {
	u16 x;
	u16 y;
	u8  wx;
	u8  wy;
	u8  z;
	u8  finger_state;
};

struct bist_test_result {
	u8 failed;    /* == 0, if all passed */
	u8 result;    /* specific error for failed test */
};

struct syn {
	struct mutex         lock;
	struct i2c_client    *client;
	struct workqueue_struct *wq;
	struct work_struct   isr_work;
	struct regulator_bulk_data regs[2];

	struct func_desc     *control;
	u8                   device_control_ctrl; /* saved state */

	struct func_desc     *touch;
	struct input_dev     *idev;
	struct touch_data    touch_state[MAX_TOUCH_POINTS];
	struct touch_sensor_caps  touch_caps;

	struct func_desc     *buttons;
	struct button_caps   button_caps;
	u32                  button_state;

	struct func_desc     func_desc[MAX_FUNC_DESCS];
	unsigned             func_desc_num;
	unsigned             interrupt_sources;

	struct func_desc     *flash;
	struct flash_caps    flash_caps;
	struct fw_image      fw_image;
	const struct firmware *fw_entry;

	struct func_desc     *bist;
	struct func_desc     *timer;
	struct func_desc     *prox;

	int                  gpio_intr;
	int                  func_descs_valid;

	int                  failed_inits;
	u32                  debug_flag;

	unsigned long        ts_intr;
	unsigned long        ts_work;
	unsigned long        ts_done;

	unsigned long        t_wakeup_min;
	unsigned long        t_wakeup_max;
	unsigned long        t_wakeup_c;

	unsigned long        t_work_min;
	unsigned long        t_work_max;
	unsigned long        t_work_c;

	unsigned long        t_wakeup;
	unsigned long        t_work;

	unsigned long        t_count;
	unsigned long        f_measure;
};

static const char reg_vdd[] = "Vdd";
static const char reg_vddio[] = "VddIO";

static int syn_initialize(struct syn *sd);
static int syn_reset_device(struct syn *sd);
static int syn_read_func_descs(struct syn *sd);

static int syn_write_block(struct syn *sd, int reg, const u8 *data, int len)
{
	unsigned char wb[MAX_I2C_WRITE_BLOCK_SIZE + 1];
	struct i2c_msg msg;
	int r;
	int i;

	if (len < 1 ||
	    len > MAX_I2C_WRITE_BLOCK_SIZE) {
		dev_info(&sd->client->dev, "too long syn_write_block len %d\n",
			 len);
		return -EIO;
	}

	wb[0] = reg & 0xff;

	for (i = 0; i < len; i++)
		wb[i + 1] = data[i];

	msg.addr = sd->client->addr;
	msg.flags = 0;
	msg.len = len + 1;
	msg.buf = wb;

	r = i2c_transfer(sd->client->adapter, &msg, 1);

	if (sd->debug_flag & DFLAG_I2C_DUMP) {
		if (r == 1) {
			for (i = 0; i < len; i++)
				dev_info(&sd->client->dev,
					 "bw 0x%02x[%d]: 0x%02x\n",
					 reg + i, i, data[i]);
		}
	}

	if (r == 1)
		return 0;

	return r;
}

static int syn_read_block(struct syn *sd, int reg, u8 *data, int len)
{
	unsigned char wb[1];
	struct i2c_msg msg[2];
	int r;

	wb[0] = reg & 0xff;

	msg[0].addr = sd->client->addr;
	msg[0].flags = 0;
	msg[0].len = 1;
	msg[0].buf = wb;

	msg[1].addr = msg[0].addr;
	msg[1].flags = I2C_M_RD;
	msg[1].len = len;
	msg[1].buf = data;

	r = i2c_transfer(sd->client->adapter, msg, 2);

	if (sd->debug_flag & DFLAG_I2C_DUMP) {
		if (r == 2) {
			int i;

			for (i = 0; i < len; i++)
				dev_info(&sd->client->dev,
					 "br 0x%02x[%d]: 0x%02x\n",
					 reg + i, i, data[i]);
		}
	}

	if (r == 2)
		return len;

	return r;
}

static int syn_read_u8(struct syn *sd, int reg)
{
	unsigned char b[1];
	int r;

	r = syn_read_block(sd, reg, b, 1);
	if (r == 1)
		return (int)b[0];

	return r;
}

static int syn_write_u8(struct syn *sd, u8 reg, u8 value)
{
	return syn_write_block(sd, reg, &value, 1);
}

static int syn_read_u16(struct syn *sd, int reg)
{
	int r;
	u8 data[2];

	r = syn_read_block(sd, reg, data, 2);
	if (r < 0)
		return r;

	return (int)data[0] | ((int)(data[1]) << 8);
}

static int syn_write_u16(struct syn *sd, int reg, u16 value)
{
	u8 data[2];

	data[1] = (value & 0xFF00) >> 8;
	data[0] = value & 0x00FF;

	return syn_write_block(sd, reg, data, 2);
}

static int syn_control_query_read(struct syn *sd, int reg)
{
	return syn_read_u8(sd, sd->control->query + reg);
}

static int syn_control_data_read(struct syn *sd, int reg)
{
	return syn_read_u8(sd, sd->control->data + reg);
}

static const char *syn_device_status_str(u8 dev_status)
{
	if (dev_status >= DEVICE_STATUS_COUNT)
		return device_status_str[DEVICE_STATUS_UNKNOWN];

	return device_status_str[dev_status];
}

static int syn_get_nosleep(struct syn *sd)
{
	int r;

	r = syn_read_u8(sd, sd->control->control + DEVICE_CONTROL_CTRL);
	if (r < 0)
		return r;

	return (r & 0x04) >> 2;
}

static int syn_set_nosleep(struct syn *sd, int val)
{
	int r;

	r = syn_read_u8(sd, sd->control->control + DEVICE_CONTROL_CTRL);
	if (r < 0)
		return r;

	val = (r & (~0x04)) | ((!!val) << 2);

	r = syn_write_u8(sd, sd->control->control + DEVICE_CONTROL_CTRL,
			 val);

	return r;
}

static int syn_get_proximity_state(struct syn *sd)
{
	int r;

	if (!sd->prox)
		return -ENODEV;

	r = syn_read_u8(sd, sd->prox->control);
	if (r < 0)
		return r;

	return (r & (1 << 7)) == 0;
}

static int syn_set_proximity_state(struct syn *sd, int val)
{
	int r;

	if (!sd->prox)
		return -ENODEV;

	r = syn_read_u8(sd, sd->prox->control);
	if (r < 0)
		return r;

	/* It is called 'inhibit proximity' in register */
	val = (r & (~0x80)) | ((!val) << 7);

	r = syn_write_u8(sd, sd->prox->control,
			 val);
	if (r < 0)
		return r;

	return r;
}

static void syn_report_device_status(struct syn *sd, u8 dev_status)
{
	struct device *dev = &sd->client->dev;

	if (dev_status & (1 << 7))
		dev_info(dev, "device is unconfigured\n");

	if (dev_status & (1 << 6))
		dev_info(dev, "device is in flashing mode\n");

	if (dev_status)
		dev_info(dev, "device status: 0x%x, %s\n", dev_status,
			 syn_device_status_str(dev_status & 0x0f));
}

static void syn_device_change_state(struct syn *sd, u8 dev_status)
{
	int r;

	/* Unconfigured or reset, we initialize*/
	if ((dev_status & (1 << 7)) ||
	    (dev_status & 0x0f) == DEVICE_STATUS_RESET_OCCURRED) {
		r = syn_initialize(sd);
		if (r)
			dev_err(&sd->client->dev,
				"error %d in syn_initialize\n", r);
	}
}

static void syn_recalculate_latency_data(struct syn *sd)
{
	if (sd->ts_intr <= sd->ts_work && sd->ts_work < sd->ts_done) {
		sd->t_wakeup = sd->ts_work - sd->ts_intr;
		do_div(sd->t_wakeup, 1000);
		sd->t_work = sd->ts_done - sd->ts_work;
		do_div(sd->t_work, 1000);

		if (sd->t_wakeup > sd->t_wakeup_max)
			sd->t_wakeup_max = sd->t_wakeup;

		if (sd->t_wakeup < sd->t_wakeup_min)
			sd->t_wakeup_min = sd->t_wakeup;

		if (sd->t_work > sd->t_work_max)
			sd->t_work_max = sd->t_work;

		if (sd->t_work < sd->t_work_min)
			sd->t_work_min = sd->t_work;

		if ((sd->t_work_c + sd->t_work) < sd->t_work_c ||
		    (sd->t_wakeup_c + sd->t_wakeup) < sd->t_wakeup_c) {

			/* Wrap */
			sd->t_work_c = sd->t_work;
			sd->t_wakeup_c = sd->t_wakeup;
			sd->t_count = 0;
		} else {
			sd->t_work_c += sd->t_work;
			sd->t_wakeup_c += sd->t_wakeup;
			sd->t_count++;
		}
	} else {
		/* Time wrap or something else, discard */
	}
}

static void syn_isr_device_control(struct syn *sd, u8 bits)
{
	int dev_status;

	dev_status = syn_control_data_read(sd, DEVICE_CONTROL_DATA_STATUS);
	if (dev_status < 0) {
		dev_err(&sd->client->dev, "error %d reading device status\n",
			dev_status);
	}

	syn_report_device_status(sd, dev_status);

	syn_device_change_state(sd, dev_status);
}

static void syn_isr_flash(struct syn *sd, u8 bits)
{
	int dev_status;

	dev_info(&sd->client->dev, "flash interrupt\n");

	if (!sd->flash) {
		dev_warn(&sd->client->dev,
			 "flash interrupt without registered functionality\n");
		return;
	}

	dev_status = syn_control_data_read(sd, DEVICE_CONTROL_DATA_STATUS);
	syn_report_device_status(sd, dev_status);

	syn_device_change_state(sd, dev_status);
}

static void syn_isr_timer(struct syn *sd, u8 bits)
{
	if (!sd->timer) {
		dev_warn(&sd->client->dev,
			 "flash interrupt without registered functionality\n");
		return;
	}
}

static void syn_isr_bist(struct syn *sd, u8 bits)
{
	if (!sd->bist) {
		dev_warn(&sd->client->dev,
			 "bist interrupt without registered functionality\n");
		return;
	}
}

static inline int syn_scale(int v, const int in_max, const int out_max)
{
	return v * out_max / in_max;
}

static void syn_report_proximity(struct syn *sd, unsigned char *data)
{
	struct input_dev *idev;
	struct syntm12xx_platform_data *pdata = sd->client->dev.platform_data;

	idev = sd->idev;

	if (data[0] & 0x01) {
		int ry;
		int rx;
		int scale;
		int maxx;
		int maxy;

		input_report_abs(idev, ABS_MT_WIDTH_MAJOR, data[1]);

		if (pdata->swap_xy) {
			maxx = sd->touch_caps.max_y;
			maxy = sd->touch_caps.max_x;
		} else {
			maxx = sd->touch_caps.max_x;
			maxy = sd->touch_caps.max_y;
		}

		if (data[5] == 0 && data[4]) {
			ry = 0;
		} else if (data[5] && data[4] == 0) {
			ry = maxy;
		} else if (data[5] && data[4]) {
			if (data[5] > data[4])
				scale = (data[4] * 100 / data[5]);
			else if (data[4] > data[5])
				scale = -(data[5] * 100 / data[4]);
			else
				scale = 100;

			if (scale > 0)
				ry = (maxy >> 1) +
					(maxy >> 1) * (100 - scale) / 100;
			else
				ry = (maxy >> 1) -
					(maxy >> 1) * (100 + scale) / 100;
		} else {
			ry = 0;
		}

		if (data[2] == 0 && data[3]) {
			rx = maxx;
		} else if (data[3] && data[2] == 0) {
			rx = 0;
		} else if (data[3] && data[2]) {
			if (data[3] > data[2])
				scale = (data[2] * 100 / data[3]);
			else if (data[2] > data[3])
				scale = -(data[3] * 100 / data[2]);
			else
				scale = 100;

			if (scale > 0)
				rx = (maxx >> 1) +
					(maxx >> 1) * (100 - scale) / 100;
			else
				rx = (maxx >> 1) -
					(maxx >> 1) * (100 + scale) / 100;
		} else {
			rx = 0;
		}

		/* dev_info(&sd->client->dev, "rx = %d, ry = %d\n", rx, ry); */

		if (pdata->swap_xy) {
			input_report_abs(idev, ABS_MT_POSITION_X,
					 syn_scale(ry, maxy, pdata->max_x));
			input_report_abs(idev, ABS_MT_POSITION_Y,
					 syn_scale(rx, maxx, pdata->max_y));
		} else {
			input_report_abs(idev, ABS_MT_POSITION_X,
					 syn_scale(rx, maxx, pdata->max_x));
			input_report_abs(idev, ABS_MT_POSITION_Y,
					 syn_scale(ry, maxy, pdata->max_y));
		}
	} else {
		input_report_abs(idev, ABS_MT_WIDTH_MAJOR, 0);
	}
}

static void syn_isr_proximity(struct syn *sd, u8 bits)
{
	unsigned char data[6];
	struct input_dev *idev;
	int r;

	idev = sd->idev;

	if (!sd->prox) {
		dev_warn(&sd->client->dev,
			 "spurious proximity interrupty\n");
		return;
	}

	r = syn_read_block(sd, sd->prox->data, data, 6);
	if (r < 0) {
		dev_warn(&sd->client->dev,
			 "error %d reading proximity data\n", r);
		return;
	}

	syn_report_proximity(sd, data);

	input_mt_sync(idev);
	input_sync(idev);
}

static void syn_touch_parse_one_touch_data(struct syn *sd, struct touch_data *p,
				      u8 finger_state, const u8 *d)
{
	p->finger_state = finger_state;
	p->x = (u16)(d[2] & 0x0f) | (u16)(d[0] << 4);
	p->y = ((u16)(d[2] & 0xf0) >> 4) | (u16)(d[1] << 4);
	p->wx = d[3] & 0x0f;
	p->wy = (d[3] & 0xf0) >> 4;
	p->z = d[4];
}

static void syn_touch_parse_touch_data(struct syn *sd, const u8 *d)
{
	unsigned i;
	u8 fc;
	u8 fs;
	struct touch_data *p;

	fc = sd->touch_caps.finger_count;

	if (fc > MAX_TOUCH_POINTS)
		dev_err(&sd->client->dev, "error in finger count %d\n", fc);

	for (i = 0; i < fc; i++) {
		p = &sd->touch_state[i];

		fs = (d[(i >> 2)] >> ((i % 4) << 1)) & 0x03;

		/* dev_warn(&sd->client->dev, "fs[%d] = %d\n", i, fs); */

		syn_touch_parse_one_touch_data(sd, p, fs,
					       d + ((fc >> 2) + 1) +
					       i * 5);

	}
}

static int syn_touch_get_data(struct syn *sd)
{
	int r;
	u8 data[TOUCH_DATA_LEN];

	r = syn_read_block(sd, sd->touch->data, data, TOUCH_DATA_LEN);
	if (r < 0)
		return r;

	if (r != TOUCH_DATA_LEN)
		return -EIO;

	syn_touch_parse_touch_data(sd, data);

	return 0;
}

static int syn_touch_report_data(struct syn *sd, int tn)
{
	struct syntm12xx_platform_data *pdata = sd->client->dev.platform_data;
	struct touch_data *d;
	struct input_dev *idev;

	idev = sd->idev;

	d = &sd->touch_state[tn];

	switch (d->finger_state) {
	case FINGER_STATE_ACCURATE:
		if (tn == 0)
			input_report_key(idev, BTN_TOUCH, 1);
		input_report_key(idev, BTN_MODE, 0);
		break;

	case FINGER_STATE_INACCURATE:
		if (tn == 0)
			input_report_key(idev, BTN_TOUCH, 1);
		input_report_key(idev, BTN_MODE, 1);
		break;

	case FINGER_STATE_RESERVED:
		/* Intentional fall thru */

	case FINGER_STATE_NOT_PRESENT:
		return 0;
	default:
		dev_err(&sd->client->dev, "unknown finger state 0x%x\n",
			d->finger_state);
		return 0;
	}

	if (pdata->swap_xy) {
		if (tn == 0) {
			input_report_abs(idev, ABS_X,
					 syn_scale(d->y, sd->touch_caps.max_y,
						   pdata->max_x));

			input_report_abs(idev, ABS_Y,
					 syn_scale(d->x, sd->touch_caps.max_x,
						   pdata->max_y));
		}

		input_report_abs(idev, ABS_MT_TOUCH_MAJOR,
				 max(d->wy + 1, d->wx + 1));
		input_report_abs(idev, ABS_MT_TOUCH_MINOR,
				 min(d->wy + 1, d->wx + 1));
		input_report_abs(idev, ABS_MT_ORIENTATION,
				 !!(d->wy > d->wx));

		input_report_abs(idev, ABS_MT_POSITION_X,
				 syn_scale(d->y, sd->touch_caps.max_y,
					   pdata->max_x));
		input_report_abs(idev, ABS_MT_POSITION_Y,
				 syn_scale(d->x, sd->touch_caps.max_x,
					   pdata->max_y));
	} else {
		if (tn == 0) {
			input_report_abs(idev, ABS_X,
					 syn_scale(d->x, sd->touch_caps.max_x,
						   pdata->max_x));
			input_report_abs(idev, ABS_Y,
					 syn_scale(d->y, sd->touch_caps.max_y,
						   pdata->max_y));
		}

		input_report_abs(idev, ABS_MT_TOUCH_MAJOR,
				    max(d->wx + 1, d->wy + 1));
		input_report_abs(idev, ABS_MT_TOUCH_MINOR,
				 min(d->wx + 1, d->wy + 1));
		input_report_abs(idev, ABS_MT_ORIENTATION,
				 !!(d->wx > d->wy));

		input_report_abs(idev, ABS_MT_POSITION_X,
				    syn_scale(d->x, sd->touch_caps.max_x,
					      pdata->max_x));
		input_report_abs(idev, ABS_MT_POSITION_Y,
				 syn_scale(d->y, sd->touch_caps.max_y,
					   pdata->max_y));
	}

	input_report_abs(idev, ABS_MT_TRACKING_ID, tn);
	input_mt_sync(idev);

	return 1;
}

static void syn_isr_2d(struct syn *sd, u8 bits)
{
	int r;
	int i;

	if (!sd->touch) {
		dev_warn(&sd->client->dev,
			 "2d interrupt without registered functionality\n");
		return;
	}

	r = syn_touch_get_data(sd);
	if (r) {
		dev_err(&sd->client->dev, "error getting touch data\n");
		return;
	}

	if (sd->debug_flag & DFLAG_VERBOSE) {
		for (i = 0; i < sd->touch_caps.finger_count; i++) {
			dev_info(&sd->client->dev,
				 "%d: state=0x%x, x=%d, "
				 "y=%d, z=%d, wx=%d, wy=%d\n", i,
				 sd->touch_state[i].finger_state,
				 sd->touch_state[i].x, sd->touch_state[i].y,
				 sd->touch_state[i].z, sd->touch_state[i].wx,
				 sd->touch_state[i].wy);
		}
	}

	r = 0;

	for (i = 0; i < sd->touch_caps.finger_count; i++)
		r += syn_touch_report_data(sd, i);

	if (r == 0)
		input_report_key(sd->idev, BTN_TOUCH, 0);

	input_sync(sd->idev);
}

static int syn_button_report(struct syn *sd, unsigned button_nr, const int val)
{
	struct syntm12xx_platform_data *pdata = sd->client->dev.platform_data;
	unsigned max_buttons;

	max_buttons = pdata->num_buttons;

	if (button_nr >= max_buttons)
		return -EINVAL;

	input_report_key(sd->idev, pdata->button_map[button_nr], val);
	input_sync(sd->idev);

	return 0;
}

static void syn_isr_buttons(struct syn *sd, u8 bits)
{
	int data_reg_count = (sd->button_caps.button_count + 7) / 8;
	int i = 0;
	int r;
	u32 state = 0;
	u32 last_state;

	if (!sd->buttons) {
		dev_warn(&sd->client->dev,
			 "button interrupt without registered functionality\n");
		return;
	}

	if (data_reg_count > MAX_BUTTONS/8 + 1)
		data_reg_count = MAX_BUTTONS/8 + 1;

	while (i < data_reg_count) {
		r = syn_read_u8(sd, sd->buttons->data + i);
		if (r < 0) {
			dev_err(&sd->client->dev,
				"error reading button state\n");
			return;
		}

		state |= (u32)r << (i * 8);
		i++;
	}

	last_state = sd->button_state;

	for (i = 0; i < sd->button_caps.button_count; i++) {
		const u32 mask = (1 << i);
		if ((state & mask) ^ (last_state & mask)) {
			r = syn_button_report(sd, i, state & mask ? 1 : 0);
			if (r) {
				dev_warn(&sd->client->dev,
					 "error reporting button "
					 "(no mapping for %d ?)\n", i);
			}
		}
	}

	sd->button_state = state;
}

static u8 syn_get_status_bits(u8 *int_status, int start, int bits)
{
	const u16 mask = ((1 << bits) - 1) << start;
	u8 val;

	val = (*int_status & mask) >> start;

	*int_status &= ~mask;

	return val;
}

static void syn_isr_call_handlers(struct syn *sd, u8 int_status)
{
	int i;
	u8 bits;

	for (i = 0; i < sd->func_desc_num; i++) {
		struct func_desc * const f = &sd->func_desc[i];

		if (!int_status)
			return;

		bits = syn_get_status_bits(&int_status, f->intr_start_bit,
					   f->intr_sources);

		if (likely(bits)) {
			if (likely(f->intr_handler)) {
				f->intr_handler(sd, bits);
			} else {
				if (printk_ratelimit())
					dev_warn(&sd->client->dev,
						 "no intr handler for %d\n", i);
			}
		}
	}

	if (unlikely(int_status))
		dev_warn(&sd->client->dev,
			 "unhandled attentions: 0x%x\n", int_status);
}

static void syn_clear_device_state(struct syn *sd)
{
	int i;

	sd->control = NULL;

	sd->touch = NULL;
	/*
	 * We don't clear previous exported devices.
	 * If after firmware upgrade there would be different
	 * amount of touchpoints rmmod/insmod cycle is needed.
	 */
	memset(&sd->touch_caps, 0, sizeof(struct touch_sensor_caps));

	sd->buttons = NULL;
	memset(&sd->button_caps, 0, sizeof(struct button_caps));
	sd->button_state = 0;

	for (i = 0; i < MAX_FUNC_DESCS; i++)
		memset(&sd->func_desc[i], 0, sizeof(struct func_desc));

	sd->func_desc_num = 0;
	sd->interrupt_sources = 0;

	sd->flash = NULL;
	memset(&sd->flash_caps, 0, sizeof(struct flash_caps));
	memset(&sd->fw_image, 0, sizeof(struct fw_image));

	sd->bist = NULL;
}

static void syn_isr_work(struct work_struct *work)
{
	int is;
	int r;
	struct syn *sd = container_of(work, struct syn, isr_work);

	mutex_lock(&sd->lock);

	if (sd->f_measure)
		sd->ts_work = cpu_clock(smp_processor_id());

	if (sd->func_descs_valid == 0) {
		if (sd->failed_inits < MAX_FAILED_INITS) {
			r = syn_initialize(sd);
			if (r) {
				dev_err(&sd->client->dev,
					"error initializing\n");
				goto out;
			}
		}
	}

	is = syn_control_data_read(sd, DEVICE_CONTROL_DATA_INTR_STATUS);

	if (unlikely(is < 0)) {
		dev_err(&sd->client->dev,
			"unable to read intr status\n");
		syn_reset_device(sd);
		goto out;
	}

	if (likely(is))
		syn_isr_call_handlers(sd, is);

out:
	if (sd->f_measure) {
		sd->ts_done = cpu_clock(smp_processor_id());
		syn_recalculate_latency_data(sd);
		sd->f_measure = 0;
	}
	enable_irq(gpio_to_irq(sd->gpio_intr));
	mutex_unlock(&sd->lock);
}

static irqreturn_t syn_isr(int irq, void *data)
{
	struct syn *sd = data;

	if (sd->wq != NULL) {
		int r;
		r = queue_work(sd->wq, &sd->isr_work);
		if (r) {
			if (sd->f_measure == 0) {
				sd->ts_intr = cpu_clock(smp_processor_id());
				sd->f_measure = 1;
			}
			disable_irq_nosync(gpio_to_irq(sd->gpio_intr));
		}

	}

	return IRQ_HANDLED;
}

static int syn_set_input_dev_params(struct syn *sd)
{
	struct input_dev *dev;
	struct syntm12xx_platform_data *pdata;
	int i;

	if (!sd)
		return -EINVAL;

	dev = sd->idev;
	if (!dev)
		return -ENODEV;

	pdata = sd->client->dev.platform_data;

	set_bit(EV_KEY, dev->evbit);
	set_bit(EV_ABS, dev->evbit);
	set_bit(BTN_MODE, dev->keybit);
	set_bit(BTN_TOUCH, dev->keybit);

	set_bit(ABS_X, dev->absbit);
	set_bit(ABS_Y, dev->absbit);

	set_bit(ABS_MT_TOUCH_MAJOR, dev->absbit);
	set_bit(ABS_MT_TOUCH_MINOR, dev->absbit);
	set_bit(ABS_MT_ORIENTATION, dev->absbit);
	set_bit(ABS_MT_POSITION_X, dev->absbit);
	set_bit(ABS_MT_POSITION_Y, dev->absbit);
	set_bit(ABS_MT_TRACKING_ID, dev->absbit);

	/* Proximity stuff */
	set_bit(ABS_MT_WIDTH_MAJOR, dev->absbit);

	if (pdata->repeat)
		set_bit(EV_REP, dev->evbit);

	for (i = 0; i < pdata->num_buttons; i++)
		set_bit(pdata->button_map[i], dev->keybit);

	input_set_abs_params(dev, ABS_X,
			     0, pdata->max_x, 0, 0);
	input_set_abs_params(dev, ABS_Y,
			     0, pdata->max_y, 0, 0);

	input_set_abs_params(dev, ABS_MT_POSITION_X,
			     0, pdata->max_x, 0, 0);
	input_set_abs_params(dev, ABS_MT_POSITION_Y,
			     0, pdata->max_y, 0, 0);

	input_set_abs_params(dev, ABS_MT_ORIENTATION,
			     0, 1, 0, 0);

	input_set_abs_params(dev, ABS_MT_TOUCH_MAJOR,
			     0,
			     max(pdata->max_x, pdata->max_y),
			     0, 0);

	input_set_abs_params(dev, ABS_MT_TOUCH_MINOR,
			     0,
			     min(pdata->max_x, pdata->max_y),
			     0, 0);

	input_set_abs_params(dev, ABS_MT_WIDTH_MAJOR,
			     0, 255, 0, 0);

	input_set_abs_params(dev, ABS_MT_TRACKING_ID, 0,
			     sd->touch_caps.finger_count - 1, 0, 0);

	dev->dev.parent = &sd->client->dev;

	return 0;
}

static int syn_register_input_device(struct syn *sd)
{
	struct syntm12xx_platform_data *pdata;
	int r;

	if (!sd)
		return -EINVAL;

	pdata = sd->client->dev.platform_data;

	/* After firmware upgrade no need to reregister */
	if (sd->idev != NULL) {
		if (sd->debug_flag & DFLAG_VERBOSE)
			dev_info(&sd->client->dev,
				 "input device already exists\n");
		return 0;
	}

	sd->idev = input_allocate_device();
	if (!sd->idev) {
		r = -ENOMEM;
		goto err_alloc;
	}

	r = syn_set_input_dev_params(sd);
	if (r != 0)
		goto err_alloc;


	sd->idev->name = DRIVER_DESC;

	r = input_register_device(sd->idev);
	if (r)
		goto err_alloc;

	return 0;

err_alloc:
	if (sd->idev) {
		input_free_device(sd->idev);
		sd->idev = NULL;
	}

	return r;
}

static struct func_desc *syn_get_func_desc(struct syn *sd, int func)
{
	int i;

	for (i = 0; i < sd->func_desc_num; i++) {
		struct func_desc * const f = &sd->func_desc[i];

		if (f->num == func)
			return f;
	}

	return NULL;
}

static int syn_register_intr_handler(struct syn *sd, int func,
				     void (*h)(struct syn *sd, u8 bits))
{
	struct func_desc * const f = syn_get_func_desc(sd, func);

	if (!f)
		return -EINVAL;

	if (f->intr_handler == NULL) {
		f->intr_handler = h;
		return 0;
	}

	return -EINVAL;
}

/*
 * Flashing support
 */
static int syn_flash_query_caps(struct syn *sd)
{
	int r;

	r = syn_read_u16(sd, sd->flash->query + FLASH_QUERY_BOOTLOADER_ID_0);
	if (r < 0)
		return r;

	sd->flash_caps.bootloader_id = r;

	r = syn_read_u8(sd, sd->flash->query + FLASH_QUERY_PROPERTIES);
	if (r < 0)
		return r;

	sd->flash_caps.properties = r;

	r = syn_read_u16(sd, sd->flash->query + FLASH_QUERY_BLOCK_SIZE_0);
	if (r < 0)
		return r;

	sd->flash_caps.block_size = r;

	r = syn_read_u16(sd, sd->flash->query + FLASH_QUERY_FW_BLOCK_COUNT_0);
	if (r < 0)
		return r;

	sd->flash_caps.fw_block_count = r;

	r = syn_read_u16(sd, sd->flash->query + FLASH_QUERY_CONF_BLOCK_COUNT_0);
	if (r < 0)
		return r;

	sd->flash_caps.conf_block_count = r;

	return 0;
}

static u32 syn_crc_fletcher32(const u16 *data, unsigned int len)
{
	u32 sum1 = 0xffff;
	u32 sum2 = 0xffff;

	while (len--) {
		sum1 += *data++;
		sum2 += sum1;

		sum1 = (sum1 & 0xffff) + (sum1 >> 16);
		sum2 = (sum2 & 0xffff) + (sum2 >> 16);
	}

	return sum1 | (sum2 << 16);
}

static int syn_fw_read_bytes(struct syn *sd, u8 *b, int offset, int len)
{
	if (offset + len > sd->fw_entry->size) {
		dev_err(&sd->client->dev,
			"fw_read_bytes overflow on offset %d\n", offset);
		return -EINVAL;
	}

	memcpy(b, sd->fw_entry->data + offset, len);

	return len;
}

static int syn_fw_read_u32(struct syn *sd, u32 *d, int offset)
{
	const u8 *l;

	if (offset + 4 > sd->fw_entry->size) {
		dev_err(&sd->client->dev,
			"fw_read_u32 overflow on offset %d\n", offset);

		return -EINVAL;
	}

	l = sd->fw_entry->data + offset;

	*d = l[0] | (l[1] << 8) | (l[2] << 16) | (l[3] << 24);

	return 4;
}

static int syn_request_firmware(struct syn *sd, const char *filename)
{
	int r;

	r = request_firmware(&sd->fw_entry, filename, &sd->client->dev);

	return r;
}

static int syn_check_firmware(struct syn *sd)
{
	u32 calc_file_crc;
	u32 calc_config_crc;
	struct fw_image *d = &sd->fw_image;
	int r;

	/*
	 * Restrict sizes.
	 * Absolute minimum is zero sized fw data and config
	 * areas.
	 */

	if (sd->fw_entry->size > FLASH_MAX_SIZE ||
	    sd->fw_entry->size < FW_FW_DATA + 4) {
		dev_err(&sd->client->dev, "illegal firmware size\n");
		return -EINVAL;
	}

	r = syn_fw_read_u32(sd, &d->file_checksum, FW_FILE_CHECKSUM);
	if (r < 0)
		return r;

	r = syn_fw_read_bytes(sd, &d->version, FW_VERSION, 1);
	if (r < 0)
		return r;

	r = syn_fw_read_u32(sd, &d->fw_size, FW_FW_SIZE);
	if (r < 0)
		return r;

	r = syn_fw_read_u32(sd, &d->config_size, FW_CONFIG_SIZE);
	if (r < 0)
		return r;

	if ((d->fw_size + d->config_size + FW_FW_DATA) > sd->fw_entry->size) {
		dev_err(&sd->client->dev, "fw size mismatch\n");
		return -EINVAL;
	}

	/* These have to be u32 aligned */
	if ((d->fw_size & 0x0f) || (d->config_size & 0x0f)) {
		dev_err(&sd->client->dev, "fw areas not aligned to 4 bytes\n");
		return -EINVAL;
	}

	r = syn_fw_read_bytes(sd, d->product_id, FW_PRODUCT_ID,
			      PRODUCT_ID_LEN);
	if (r < 10)
		return r;

	r = syn_fw_read_bytes(sd, &d->product_info_0,
			      FW_PRODUCT_INFO_0, 1);
	if (r < 0)
		return r;

	r = syn_fw_read_bytes(sd, &d->product_info_1,
			      FW_PRODUCT_INFO_1, 1);
	if (r < 0)
		return r;

	r = syn_fw_read_u32(sd, &d->config_checksum,
			    d->fw_size + FW_FW_DATA + d->config_size - 4);
	if (r < 0)
		return r;

	d->fw_data = sd->fw_entry->data + FW_FW_DATA;
	d->config_data = sd->fw_entry->data + FW_FW_DATA + d->fw_size;

	calc_file_crc = syn_crc_fletcher32((u16 *)(sd->fw_entry->data + 4),
					   (sd->fw_entry->size - 4) >> 1);

	calc_config_crc = syn_crc_fletcher32((u16 *)(d->config_data),
					     (d->config_size - 4) >> 1);

	if (calc_file_crc != d->file_checksum) {
		dev_err(&sd->client->dev, "fw file crc failed\n");
		return -EINVAL;
	}

	if (calc_config_crc != d->config_checksum) {
		dev_err(&sd->client->dev, "fw config area crc failed\n");
		return -EINVAL;
	}

	return 0;
}

static const char *syn_flash_error_str(u8 flash_error)
{
	if (flash_error >= FLASH_ERROR_COUNT)
		return flash_error_str[FLASH_ERROR_UNKNOWN];

	return flash_error_str[flash_error];
}

static u8 syn_flash_error(int flash_status)
{
	return ((u8)(flash_status) >> 4) & 0x07;
}

/*
 * There are two different ways the flash register are organized
 */
static u8 syn_flash_data_command_offset(struct syn *sd)
{
	if (sd->flash_caps.properties & FLASH_PROPERTY_REGMAP_VERSION)
		return 2 + sd->flash_caps.block_size;
	else
		return 0;
}

static u8 syn_flash_block_data_offset(struct syn *sd)
{
	if (sd->flash_caps.properties & FLASH_PROPERTY_REGMAP_VERSION)
		return 2;
	else
		return 3;
}

static u8 syn_flash_block_num_offset(struct syn *sd)
{
	if (sd->flash_caps.properties & FLASH_PROPERTY_REGMAP_VERSION)
		return 0;
	else
		return 1;
}

static int syn_flash_status(struct syn *sd)
{
	return syn_read_u8(sd, sd->flash->data +
			   syn_flash_data_command_offset(sd));
}

static int syn_wait_for_attn(struct syn *sd, unsigned long timeout_usecs,
			     int state)
{
	const unsigned long one_wait = 20;
	unsigned long waited = 0;
	int r;

	do {
		r = gpio_get_value(sd->gpio_intr);
		if (r == state)
			break;

		udelay(one_wait);
		waited += one_wait;
	} while (waited < timeout_usecs);

	if (waited > 500000 ||
	    waited >= timeout_usecs ||
	    (sd->debug_flag & DFLAG_VERBOSE))
		dev_info(&sd->client->dev, "waited %lu usecs for attn\n",
			 waited);

	return r == state ? 0 : -1;
}

static int syn_flash_command(struct syn *sd, u8 flash_command)
{
	int r;
	int s;

	if (flash_command & 0xf0)
		return -EINVAL;

	r = syn_wait_for_attn(sd, 200 * 1000, 1);
	if (r) {
		dev_err(&sd->client->dev,
			"timeout: attn didn't clear for 200 ms\n");
		return r;
	}

	r  = syn_write_u8(sd,
			  sd->flash->data + syn_flash_data_command_offset(sd),
			  flash_command);

	if (r) {
		dev_err(&sd->client->dev, "flash command error %d\n", r);
		return r;
	}

	r = syn_wait_for_attn(sd, 700 * 1000, 0);
	if (r) {
		dev_err(&sd->client->dev,
			"timeout attn didn't assert for 700 ms\n");
	}

	s = syn_control_data_read(sd, DEVICE_CONTROL_DATA_INTR_STATUS);
	if (s < 0)
		dev_err(&sd->client->dev, "error reading int status\n");

	if (sd->debug_flag & DFLAG_VERBOSE) {
		if (s != 0x01)
			dev_info(&sd->client->dev,
				 "wait for attn intr status 0x%x\n", s);
	}

	return r == 0 ? 0 : -1;
}

static int syn_flash_enable(struct syn *sd)
{
	int r;

	r = syn_control_data_read(sd, DEVICE_CONTROL_DATA_STATUS);
	if (r < 0)
		return r;

	if (r & (1 << 6)) {
		dev_err(&sd->client->dev, "flashing mode already enabled\n");
		return 0;
	}

	r = syn_write_u16(sd, sd->flash->data + syn_flash_block_data_offset(sd),
			  sd->flash_caps.bootloader_id);
	if (r < 0)
		return r;

	r = syn_flash_command(sd, FLASH_CMD_PROGRAM_ENABLE);
	if (r < 0)
		return r;

	r = syn_read_func_descs(sd);
	if (r < 0)
		return r;

	r = syn_flash_status(sd);
	if (r < 0) {
		dev_info(&sd->client->dev, "flash error in enable %s\n",
			 syn_flash_error_str(syn_flash_error(r)));
		return r;
	}

	if (syn_flash_error(r) != FLASH_ERROR_SUCCESS &&
	    syn_flash_error(r) != FLASH_ERROR_RESET) {
		dev_err(&sd->client->dev, "flash error: %s\n",
			syn_flash_error_str(syn_flash_error(r)));

		return -EINVAL;
	}

	if (!(r & 0x80)) {
		dev_err(&sd->client->dev, "flashing not enabled 0x%02x\n", r);
		return -EINVAL;
	}

	return r;
}

static int syn_flash_write_block_cmd_old(struct syn *sd, const u8 *data,
					    int blocks, u8 flash_command)
{
	int block;
	int r;
	const u8 block_num_offset = syn_flash_block_num_offset(sd);
	const u8 block_data_offset = syn_flash_block_data_offset(sd);

	for (block = 0; block < blocks; block++) {
		if (!(block % 10))
			dev_info(&sd->client->dev, "0x%x writing block %d\n",
				 flash_command, block);

		r = syn_write_u16(sd, sd->flash->data + block_num_offset,
				  block);
		if (r < 0)
			return r;

		r = syn_write_block(sd, sd->flash->data + block_data_offset,
				    data + (block * sd->flash_caps.block_size),
				    sd->flash_caps.block_size);
		if (r < 0)
			return r;

		r = syn_flash_command(sd, flash_command);
		if (r < 0)
			return r;

		r = syn_flash_status(sd);
		if (r < 0)
			return r;

		if (r != 0x80) {
			dev_err(&sd->client->dev,
				"flash_write error : %s\n",
				syn_flash_error_str(syn_flash_error(r)));
			return syn_flash_error(r);
		}
	}

	return 0;
}

static int syn_flash_write_block_cmd_fast(struct syn *sd, const u8 *data,
					  int blocks, u8 flash_command)
{
	const u16 block_size = sd->flash_caps.block_size;
	u8 *d;
	u16 block;
	int r;

	d = kmalloc(block_size + 3, GFP_KERNEL);
	if (d == NULL)
		return -ENOMEM;

	for (block = 0; block < blocks; block++) {
		if (!(block % 10))
			dev_info(&sd->client->dev,
				 "0x%x fast writing block %d\n",
				 flash_command, block);
		d[0] = block & 0xff;
		d[1] = (block & 0xff00) >> 8;
		memcpy(&d[2], data + (block * block_size), block_size);
		d[2 + block_size] = flash_command;

		r = syn_wait_for_attn(sd, 200 * 1000, 1);
		if (r) {
			dev_err(&sd->client->dev,
				"timeout: attn didn't clear for 200 ms\n");
			goto out;
		}

		r = syn_write_block(sd, sd->flash->data,
				    d, block_size + 3);


		r = syn_wait_for_attn(sd, 700 * 1000, 0);
		if (r) {
			dev_err(&sd->client->dev,
				"timeout attn didn't assert for 700 ms\n");
			goto out;
		}

		/* We need to do this read to release ATTN */
		syn_control_data_read(sd, DEVICE_CONTROL_DATA_INTR_STATUS);
	}
out:
	kfree(d);

	return r;
}

static int syn_flash_write_block_cmd(struct syn *sd, const u8 *data,
				     int blocks, u8 flash_command)
{
	if (sd->flash_caps.properties & FLASH_PROPERTY_REGMAP_VERSION)
		return syn_flash_write_block_cmd_fast(sd, data,
						      blocks, flash_command);
	else
		return syn_flash_write_block_cmd_old(sd, data,
						     blocks, flash_command);
}

static int syn_flash_read_config(struct syn *sd, u8 *d)
{
	const u8 flash_command = FLASH_CMD_CONF_READ_BLOCK;
	const u8 block_num_offset = syn_flash_block_num_offset(sd);
	const u8 block_data_offset = syn_flash_block_data_offset(sd);
	const int blocks = sd->fw_image.config_size / sd->flash_caps.block_size;
	int i;
	int r;

	for (i = 0; i < blocks; i++) {
		if (!(i % 10))
			dev_info(&sd->client->dev,
				 "0x%x reading conf block %d\n",
				 flash_command, i);

		r = syn_write_u16(sd, sd->flash->data + block_num_offset, i);
		if (r < 0)
			return r;

		r = syn_flash_command(sd, flash_command);
		if (r < 0)
			return r;

		r = syn_flash_status(sd);
		if (r < 0)
			return r;

		if (r != 0x80) {
			dev_err(&sd->client->dev,
				"flash_read error : %s\n",
				syn_flash_error_str(syn_flash_error(r)));
			return syn_flash_error(r);
		}

		r = syn_read_block(sd, sd->flash->data + block_data_offset, d,
				   sd->flash_caps.block_size);
		if (r < 0)
			return r;

		d += sd->flash_caps.block_size;
	}

	return 0;
}

static int syn_flash_write_config(struct syn *sd)
{
	int r;
	const int config_size = sd->fw_image.config_size;
	const int blocks = config_size / sd->flash_caps.block_size;

	r = syn_flash_write_block_cmd(sd, sd->fw_image.config_data, blocks,
				      FLASH_CMD_CONF_WRITE_BLOCK);
	if (r < 0)
		return r;

	if (r) {
		dev_err(&sd->client->dev,
			"flash write fw error : %s\n",
			syn_flash_error_str(r));
		return -EIO;
	}

	return 0;
}

#ifdef FIRMWARE_VERIFY
static int syn_flash_validate(struct syn *sd)
{
	int r;
	const int fw_size = sd->fw_image.fw_size;
	const int blocks = fw_size / sd->flash_caps.block_size;

	if (sd->flash_caps.fw_block_count !=
	    (fw_size / sd->flash_caps.block_size) ||
	    (fw_size % sd->flash_caps.block_size)) {
		dev_err(&sd->client->dev,
			"fw size not aligned to block count\n");
		return -EINVAL;
	}

	r = syn_flash_write_block_cmd(sd, sd->fw_image.fw_data, blocks,
				      FLASH_CMD_FW_CRC_BLOCK);
	if (r < 0)
		return r;

	if (r) {
		dev_err(&sd->client->dev,
			"flash validate error : %s\n",
			syn_flash_error_str(r));
		return -EIO;
	}

	r = syn_read_u8(sd, sd->flash->data + syn_flash_block_data_offset(sd));
	if (r < 0)
		return r;

	if (r == 0x00) {
		dev_info(&sd->client->dev, "flash_validate: image is valid\n");
		return 0;
	} else if (r == 0xff) {
		dev_info(&sd->client->dev,
			 "flash_validate: image is invalid\n");
	} else {
		dev_info(&sd->client->dev,
			 "flash_validate: image status unknown: 0x%02x\n", r);
	}

	return -1;
}

static int syn_flash_compare_config(struct syn *sd)
{
	u32 cfg_crc_c;
	u32 cfg_crc;
	u32 cfg_crc_r;
	u8  *cfg_data_r;
	int r;

	cfg_data_r = kmalloc(sd->fw_image.config_size, GFP_KERNEL);
	if (cfg_data_r == NULL)
		return -ENOMEM;

	r = syn_flash_read_config(sd, cfg_data_r);
	if (r < 0) {
		kfree(cfg_data_r);
		return r;
	}

	dev_info(&sd->client->dev, "Flash read config done\n");

	cfg_crc = sd->fw_image.config_checksum;
	cfg_crc_c = syn_crc_fletcher32((u16 *)(sd->fw_image.config_data),
				       (sd->fw_image.config_size - 4) >> 1);
	cfg_crc_r = syn_crc_fletcher32((u16 *)(cfg_data_r),
				      (sd->fw_image.config_size - 4) >> 1);

	kfree(cfg_data_r);
	cfg_data_r = NULL;

	dev_info(&sd->client->dev, "calculated fw image crc 0x%x\n", cfg_crc_c);
	dev_info(&sd->client->dev, "read fw image crc 0x%x\n", cfg_crc);
	dev_info(&sd->client->dev, "calculated cfg read crc 0x%x\n", cfg_crc_r);

	if (cfg_crc != cfg_crc_c) {
		dev_err(&sd->client->dev,
			"fw crc differs from calculated\n");
		return -EINVAL;
	}

	if (cfg_crc != cfg_crc_r) {
		dev_err(&sd->client->dev,
			"fw crc differs from read from device\n");
		return -EINVAL;
	}

	if (cfg_crc_c != cfg_crc_r) {
		dev_err(&sd->client->dev,
			"fw calculated crc differs from read from device\n");
		return -EINVAL;
	}

	return 0;
}
#else /* FIRMWARE_VERIFY */
static int syn_flash_compare_config(struct syn *sd)
{
	return 0;
}

static int syn_flash_validate(struct syn *sd)
{
	return 0;
}
#endif

static int syn_flash_erase_all(struct syn *sd)
{
	int r;

	r = syn_write_u16(sd, sd->flash->data + syn_flash_block_data_offset(sd),
			  sd->flash_caps.bootloader_id);

	r = syn_flash_command(sd, FLASH_CMD_ERASE_ALL);
	if (r < 0)
		return r;

	r = syn_flash_status(sd);
	if (r < 0)
		return r;

	if (r != 0x80) {
		dev_err(&sd->client->dev,
			"flash erase_all error : %d %s\n", syn_flash_error(r),
			syn_flash_error_str(syn_flash_error(r)));
		return -EIO;
	}

	return 0;
}

static int syn_flash_write_fw(struct syn *sd)
{
	int r;
	const int fw_size = sd->fw_image.fw_size;
	const int blocks = fw_size / sd->flash_caps.block_size;

	r = syn_flash_write_block_cmd(sd, sd->fw_image.fw_data, blocks,
				      FLASH_CMD_FW_WRITE_BLOCK);
	if (r < 0)
		return r;

	r = syn_flash_status(sd);
	if (r < 0)
		return r;

	if (r != 0x80) {
		dev_err(&sd->client->dev,
			"flash write fw :  %d %s\n", syn_flash_error(r),
			syn_flash_error_str(syn_flash_error(r)));
		return -EIO;
	}

	return 0;
}

static int syn_flash_firmware(struct syn *sd)
{
	int r;
	int flash_status;

	/* Restrict interrupts during flashing */
	r = syn_write_u8(sd, sd->control->control + DEVICE_CONTROL_INTR_ENABLE,
			 INTR_FLASH | INTR_STATUS);

	r = syn_flash_enable(sd);
	if (r < 0)
		goto flash_out;

	r = syn_flash_validate(sd);
	if (r < 0)
		goto flash_out;

	r = syn_flash_erase_all(sd);
	if (r < 0)
		goto flash_out;

	r = syn_flash_write_fw(sd);
	if (r < 0)
		goto flash_out;

	r = syn_flash_write_config(sd);
	if (r < 0)
		goto flash_out;

	r = syn_flash_compare_config(sd);
	if (r < 0)
		goto flash_out;

flash_out:
	flash_status = syn_flash_status(sd);
	dev_info(&sd->client->dev,
		 "flash status: %x %s\n", flash_status,
		 syn_flash_error_str(syn_flash_error(flash_status)));

	/*
	 * We reset and interrupt handler should notice that
	 * everything has changed and reread the page descriptor table.
	 */
	syn_reset_device(sd);

	return syn_flash_error(flash_status) == FLASH_ERROR_SUCCESS ? 0 : -1;
}

static ssize_t syn_show_attr_flash(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	ssize_t l = 0;
	int r;

	if (!sd->flash) {
		l += snprintf(buf + l, size - l, "N/A\n");
		return l;
	}

	mutex_lock(&sd->lock);

	r = syn_control_data_read(sd, DEVICE_CONTROL_DATA_STATUS);

	mutex_unlock(&sd->lock);

	if (r < 0)
		dev_err(&sd->client->dev, "read error %d\n", r);
	else
		l += snprintf(buf + l, size - l, "%d\n",
			      (r & (1 << 6)) ? 1 : 0);

	return l;
}

static ssize_t syn_store_attr_flash(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	const struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	int r;
	char name[FW_MAX_NAME_SIZE + 1];

	if (count > FW_MAX_NAME_SIZE || count == 0) {
		dev_err(&sd->client->dev, "firmware name check failure\n");
		return 0;
	}

	memcpy(name, buf, count);
	name[count] = 0;

	mutex_lock(&sd->lock);

	if (name[count - 1] == '\n')
		name[count - 1] = 0;

	if (sd->fw_entry) {
		dev_err(&sd->client->dev, "firmware already in memory\n");
		goto firmware_out;
	}

	r = syn_request_firmware(sd, name);
	if (r < 0) {
		dev_err(&sd->client->dev, "firmware not found\n");
		goto store_out;
	}

	memset(&sd->fw_image, 0, sizeof(struct fw_image));

	r = syn_check_firmware(sd);
	if (r != 0) {
		dev_err(&sd->client->dev,
			"consistency check of firmware failed\n");
		goto firmware_out;
	}

	dev_info(&sd->client->dev, "firmware consistency check ok\n");

	r = syn_flash_firmware(sd);
	if (r != 0) {
		dev_err(&sd->client->dev,
			"flashing of firmware failed\n");
		goto firmware_out;
	}

	dev_info(&sd->client->dev, "flashing done with success\n");

firmware_out:
	memset(&sd->fw_image, 0, sizeof(struct fw_image));
	release_firmware(sd->fw_entry);
	sd->fw_entry = NULL;

store_out:
	mutex_unlock(&sd->lock);

	return count;
}

static ssize_t syn_show_attr_product_id(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	u8 product_id[PRODUCT_ID_LEN + 1];
	ssize_t l = 0;
	int r;

	if (!sd->flash) {
		l += snprintf(buf + l, size - l, "N/A\n");
		return l;
	}

	mutex_lock(&sd->lock);

	r = syn_read_block(sd, sd->control->query +
			   DEVICE_CONTROL_QUERY_PROD_ID,
			   product_id, PRODUCT_ID_LEN);

	mutex_unlock(&sd->lock);

	product_id[PRODUCT_ID_LEN] = 0;

	if (r < 0)
		dev_warn(&sd->client->dev,
			 "error %d reading product id\n", r);
	else
		l += snprintf(buf + l, size - l, "%s\n",
			      product_id);

	return l;
}

static ssize_t syn_show_attr_product_family(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	ssize_t l = 0;
	int r;

	if (!sd->flash) {
		l += snprintf(buf + l, size - l, "N/A\n");
		return l;
	}

	mutex_lock(&sd->lock);

	r = syn_control_query_read(sd,
				   DEVICE_CONTROL_QUERY_PROD_FAMILY);

	mutex_unlock(&sd->lock);

	if (r < 0)
		dev_warn(&sd->client->dev,
			 "error %d reading product family\n", r);
	else
		l += snprintf(buf + l, size - l, "%d\n",
			      r);

	return l;
}

static ssize_t syn_show_attr_firmware_version(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	ssize_t l = 0;
	int r;

	if (!sd->flash) {
		l += snprintf(buf + l, size - l, "N/A\n");
		return l;
	}

	mutex_lock(&sd->lock);

	r = syn_control_query_read(sd,
				   DEVICE_CONTROL_QUERY_FW_VER);

	mutex_unlock(&sd->lock);

	if (r < 0)
		dev_warn(&sd->client->dev,
			 "error %d reading fw version\n", r);
	else
		l += snprintf(buf + l, size - l, "%d\n",
			      r);

	return l;
}

/*
 * BIST (selftest) support
 */

static int syn_bist_run_test(struct syn *sd, struct bist_test_result *tr,
			     u8 test)
{
	int r;
	u8  intr_mask = INTR_ALL;

	if (!sd || !tr || !sd->bist)
		return -EINVAL;

	mutex_lock(&sd->lock);

	r  = syn_read_u8(sd, sd->control->control +
				DEVICE_CONTROL_INTR_ENABLE);
	if (r < 0)
		goto out;

	intr_mask = r & 0xFF;

	/* Restrict interrupt sources*/
	r = syn_write_u8(sd, sd->control->control + DEVICE_CONTROL_INTR_ENABLE,
			 INTR_BIST | INTR_STATUS);

	r = syn_write_u8(sd, sd->bist->data + BIST_DATA_TEST_NUMBER_CTRL,
			 test);
	if (r < 0)
		goto out;

	r = syn_write_u8(sd, sd->bist->command + BIST_CONTROL_COMMAND,
			 0x01);
	if (r < 0)
		goto out;

	r = syn_wait_for_attn(sd, 5 * 1000 * 1000, 0);
	if (r < 0) {
		dev_err(&sd->client->dev, "timeout running bist test\n");
		goto out;
	}

	r = syn_control_data_read(sd, DEVICE_CONTROL_DATA_INTR_STATUS);
	if (r < 0)
		dev_err(&sd->client->dev, "error reading int status\n");

	r = syn_read_u8(sd, sd->bist->command + BIST_CONTROL_COMMAND);
	if (r < 0)
		goto out;

	if (r & (1 << 0)) {
		r = -EBUSY;
		goto out;
	}

	r = syn_read_u8(sd, sd->bist->data + BIST_DATA_OVERALL_RESULT);
	if (r < 0)
		goto out;

	tr->failed = r;

	r = syn_read_u8(sd, sd->bist->data + BIST_DATA_TEST_RESULT);
	if (r < 0)
		goto out;

	tr->result = r;

out:
	r = syn_write_u8(sd, sd->control->control + DEVICE_CONTROL_INTR_ENABLE,
			 intr_mask);
	mutex_unlock(&sd->lock);

	return r;
}

static int syn_test_i2c_wr(struct syn *sd, const u8 value)
{
	int r;

	r = syn_write_u8(sd, sd->bist->data + BIST_DATA_TEST_NUMBER_CTRL,
			 value);
	if (r < 0)
		goto out;

	r = syn_read_u8(sd, sd->bist->data + BIST_DATA_TEST_NUMBER_CTRL);
	if (r < 0)
		goto out;

	if (r != value) {
		dev_err(&sd->client->dev,
			"write verify error: wrote 0x%x got 0x%x\n",
			value, r);
		r = -EINVAL;
		goto out;
	}

	r = 0;
out:

	return r;
}

static int syn_test_i2c(struct syn *sd)
{
	int r;
	int i;

	mutex_lock(&sd->lock);

	for (i = 0; i < 8; i++) {
		r = syn_test_i2c_wr(sd, 1 << i);
		if (r < 0)
			goto out;
	}

	for (i = 0; i < 256; i++) {
		r = syn_test_i2c_wr(sd, i);
		if (r < 0)
			goto out;
	}

out:
	mutex_unlock(&sd->lock);

	return r;
}

static int syn_bist_selftest(struct syn *sd, struct bist_test_result *tr)
{
	int r;

	r = syn_test_i2c(sd);
	if (r != 0)
		return r;

	r = syn_bist_run_test(sd, tr, 0x00);

	return r;
}

static ssize_t syn_show_attr_selftest(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	ssize_t l = 0;
	struct bist_test_result tr = { 0, };
	int r;

	if (!sd->bist) {
		l += snprintf(buf + l, size - l, "Not available\n");
		return l;
	}

	r = syn_bist_selftest(sd, &tr);
	if (r != 0) {
		l += snprintf(buf + l, size - l,
			      "FAIL (io error %d)\n", r);
	} else {
		if (tr.failed == 0)
			l += snprintf(buf + l, size - l,
				      "PASS\n");
		else
			l += snprintf(buf + l, size - l,
				      "FAIL (test %d, result %d)\n",
				      tr.failed, tr.result);
	}

	return l;
}

static ssize_t syn_store_attr_reset(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);

	mutex_lock(&sd->lock);

	syn_reset_device(sd);

	mutex_unlock(&sd->lock);

	return count;
}

static ssize_t syn_show_attr_doze(struct device *dev,
				  struct device_attribute *attr,
				  char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	ssize_t l = 0;
	int r;

	mutex_lock(&sd->lock);
	r = syn_get_nosleep(sd);
	mutex_unlock(&sd->lock);

	if (r < 0)
		goto out;

	l += snprintf(buf + l, size - l, "%d\n", !r);
out:
	return l;
}

static ssize_t syn_store_attr_doze(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	int val;

	if (sscanf(buf, "%i", &val) != 1) {
		dev_info(&sd->client->dev,
			 "error parsing debug\n");
		return count;
	}

	if (val & (~0x01))
		return count;

	mutex_lock(&sd->lock);
	syn_set_nosleep(sd, !val);
	mutex_unlock(&sd->lock);

	return count;
}

static ssize_t syn_show_attr_sleepmode(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	ssize_t l = 0;
	int r;

	mutex_lock(&sd->lock);

	r = syn_read_u8(sd, sd->control->control + DEVICE_CONTROL_CTRL);
	if (r < 0)
		goto out;

	l += snprintf(buf + l, size - l, "%d\n", (r & 0x03));
out:
	mutex_unlock(&sd->lock);
	return l;
}

static ssize_t syn_store_attr_sleepmode(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	int val;
	int r;

	if (sscanf(buf, "%i", &val) != 1) {
		dev_info(&sd->client->dev,
			 "error parsing debug\n");
		return count;
	}

	if (val & (~0x03))
		return count;

	mutex_lock(&sd->lock);

	r = syn_read_u8(sd, sd->control->control + DEVICE_CONTROL_CTRL);
	if (r < 0)
		goto out;

	val = (r & (~0x03)) | (val & 0x03);

	r = syn_write_u8(sd, sd->control->control + DEVICE_CONTROL_CTRL,
			 val);

out:
	mutex_unlock(&sd->lock);

	return count;
}

static ssize_t syn_show_attr_proximity(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	ssize_t l = 0;
	int r;

	mutex_lock(&sd->lock);
	r = syn_get_proximity_state(sd);
	mutex_unlock(&sd->lock);

	if (r < 0)
		r = 0;

	l += snprintf(buf + l, size - l, "%d\n", r);

	return l;
}

static ssize_t syn_store_attr_proximity(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	int val;

	if (sscanf(buf, "%i", &val) != 1) {
		dev_info(&sd->client->dev,
			 "error parsing debug\n");
		return count;
	}

	if (val & (~0x01))
		return count;

	mutex_lock(&sd->lock);
	syn_set_proximity_state(sd, !!val);
	mutex_unlock(&sd->lock);

	return count;
}

static ssize_t syn_show_attr_sensitivity(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	ssize_t l = 0;
	int i;
	u8 addr;
	int r;

	if (!sd->touch) {
		l += snprintf(buf + l, size - l, "Not available\n");
		return l;
	}

	addr = sd->touch->control + TOUCH_CONTROL_SENSOR_MAPPING +
		sd->touch_caps.max_electrodes;

	if (sd->touch_caps.has_gestures)
		addr += 2;

	mutex_lock(&sd->lock);

	for (i = 0; i < sd->touch_caps.max_electrodes; i++) {
		r = syn_read_u8(sd, addr + i);
		if (r < 0) {
			l += snprintf(buf + l, size - l, "Read error %d\n", r);
			goto out;
		} else {
			l += snprintf(buf + l, size - l, "0x%x ", r);
		}
	}

	l += snprintf(buf + l, size - l, "\n");

out:
	mutex_unlock(&sd->lock);

	return l;
}

static ssize_t syn_store_attr_sensitivity(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	int r;
	int addr;
	int base;
	int value;

	if (!sd->touch)
		return count;

	if (sscanf(buf, "%d %i", &addr, &value) != 2) {
		dev_info(&sd->client->dev,
			 "error parsing sensitivity info <addr> <value>\n");
		return count;
	}

	if (addr >= sd->touch_caps.max_electrodes) {
		dev_info(&sd->client->dev,
			 "electorode number out of bounds %d > %d\n", addr,
			 sd->touch_caps.max_electrodes);
		return count;
	}

	base = sd->touch->control + TOUCH_CONTROL_SENSOR_MAPPING +
		sd->touch_caps.max_electrodes;

	if (sd->touch_caps.has_gestures)
		base += 2;

	mutex_lock(&sd->lock);

	r = syn_write_u8(sd, base + addr, value);
	if (r  < 0) {
		dev_err(&sd->client->dev, "write failed with %d\n", r);
		goto out;
	}

	r = syn_read_u8(sd, base + addr);
	if (r < 0) {
		dev_err(&sd->client->dev, "read failed with %d\n", r);
		goto out;
	}

	if (r != value) {
		dev_warn(&sd->client->dev,
			 "value verify error 0x%x != 0x%x\n", r, value);
	}
out:
	mutex_unlock(&sd->lock);

	return count;
}

static ssize_t syn_show_attr_sensormap(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	ssize_t l = 0;
	int i;
	u8 addr;
	int r;

	if (!sd->touch) {
		l += snprintf(buf + l, size - l, "Not available\n");
		return l;
	}

	addr = sd->touch->control + TOUCH_CONTROL_SENSOR_MAPPING;

	if (sd->touch_caps.has_gestures)
		addr += 2;

	mutex_lock(&sd->lock);

	for (i = 0; i < sd->touch_caps.max_electrodes; i++) {
		r = syn_read_u8(sd, addr + i);
		if (r < 0) {
			l += snprintf(buf + l, size - l, "Read error %d\n", r);
			goto out;
		} else {
			l += snprintf(buf + l, size - l, "%s: %d\n",
				      r & 0x80 ? "Y" : "X", r & 0x1F);
		}
	}

	l += snprintf(buf + l, size - l, "\n");

out:
	mutex_unlock(&sd->lock);

	return l;
}

static ssize_t syn_show_attr_reg_dump(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	ssize_t l = 0;
	int i;
	int r;

	if (!sd->touch) {
		l += snprintf(buf + l, size - l, "Not available\n");
		return l;
	}

	mutex_lock(&sd->lock);

	for (i = 0; i < 0xff; i++) {
		r = syn_read_u8(sd, i);
		if (r < 0) {
			l += snprintf(buf + l, size - l,
				      "Read error at 0x%x %d\n", i, r);
			goto out;
		} else {
			l += snprintf(buf + l, size - l,
				      "0x%02x: 0x%02x (%d)\n", i, r, r);
		}
	}

	l += snprintf(buf + l, size - l, "\n");

out:
	mutex_unlock(&sd->lock);
	return l;
}

static ssize_t syn_store_attr_reg_dump(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	int addr;
	int value;

	if (!sd->touch)
		return count;

	if (sscanf(buf, "%i", &addr) != 1) {
		dev_info(&sd->client->dev,
			 "error parsing addr\n");
		return count;
	}

	if (addr < 0 || addr > 0xff) {
		dev_info(&sd->client->dev,
			 "error 0x%x out of bounds\n", addr);
		return count;
	}

	mutex_lock(&sd->lock);

	value = syn_read_u8(sd, addr);
	if (value < 0) {
		dev_info(&sd->client->dev,
			 "error %d reading from 0x%x\n", value, addr);
		goto out;
	}

	dev_info(&sd->client->dev, "0x%x: 0x%x\n", addr, value);

out:
	mutex_unlock(&sd->lock);

	return count;
}

static ssize_t syn_show_attr_debug(struct device *dev,
				   struct device_attribute *attr,
				   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	ssize_t l = 0;

	mutex_lock(&sd->lock);

	l += snprintf(buf + l, size - l, "0x%02x\n", sd->debug_flag);

	mutex_unlock(&sd->lock);

	return l;
}

static ssize_t syn_store_attr_debug(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	int flag = 0;

	if (sscanf(buf, "%i", &flag) != 1) {
		dev_info(&sd->client->dev,
			 "error parsing debug\n");
		return count;
	}

	mutex_lock(&sd->lock);

	sd->debug_flag = flag;

	mutex_unlock(&sd->lock);

	return count;
}

static ssize_t syn_store_attr_latency(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);

	mutex_lock(&sd->lock);

	sd->t_work_min = ULONG_MAX;
	sd->t_work_max = 0;
	sd->t_work = 0;
	sd->t_work_c = 0;

	sd->t_wakeup_min = ULONG_MAX;
	sd->t_wakeup_max = 0;
	sd->t_wakeup = 0;
	sd->t_wakeup_c = 0;

	sd->t_count = 0;

	mutex_unlock(&sd->lock);

	return count;
}

static ssize_t syn_show_attr_latency(struct device *dev,
				     struct device_attribute *attr,
				     char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct syn *sd = platform_get_drvdata(pdev);
	const ssize_t size = PAGE_SIZE;
	ssize_t l = 0;

	mutex_lock(&sd->lock);

	l += snprintf(buf + l, size - l,
		      "count %lu: wakeup %lu (%lu, %lu, %lu), "
		      "work %lu (%lu, %lu, %lu)\n",
		      sd->t_count,
		      sd->t_wakeup, sd->t_wakeup_min, sd->t_wakeup_max,
		      sd->t_count ? (sd->t_wakeup_c / sd->t_count) : 0,
		      sd->t_work, sd->t_work_min, sd->t_work_max,
		      sd->t_count ? (sd->t_work_c / sd->t_count) : 0);

	mutex_unlock(&sd->lock);

	return l;
}

static DEVICE_ATTR(flash, S_IWUSR | S_IRUGO,
		   syn_show_attr_flash, syn_store_attr_flash);

static DEVICE_ATTR(product_id, S_IRUGO,
		   syn_show_attr_product_id, NULL);

static DEVICE_ATTR(product_family, S_IRUGO,
		   syn_show_attr_product_family, NULL);

static DEVICE_ATTR(firmware_version, S_IRUGO,
		   syn_show_attr_firmware_version, NULL);

static DEVICE_ATTR(selftest, S_IRUGO,
		   syn_show_attr_selftest, NULL);

static DEVICE_ATTR(reset, S_IWUSR, NULL, syn_store_attr_reset);

static DEVICE_ATTR(doze, S_IRUGO | S_IWUSR,
		   syn_show_attr_doze, syn_store_attr_doze);

static DEVICE_ATTR(sleepmode, S_IRUGO | S_IWUSR,
		   syn_show_attr_sleepmode, syn_store_attr_sleepmode);

static DEVICE_ATTR(proximity, S_IRUGO | S_IWUSR,
		   syn_show_attr_proximity, syn_store_attr_proximity);

static DEVICE_ATTR(sensitivity, S_IRUGO | S_IWUSR,
		   syn_show_attr_sensitivity, syn_store_attr_sensitivity);

static DEVICE_ATTR(sensormap, S_IRUGO,
		   syn_show_attr_sensormap, NULL);

static DEVICE_ATTR(dump, S_IRUGO | S_IWUSR,
		   syn_show_attr_reg_dump, syn_store_attr_reg_dump);

static DEVICE_ATTR(debug, S_IRUGO | S_IWUSR,
		   syn_show_attr_debug, syn_store_attr_debug);

static DEVICE_ATTR(latency, S_IRUGO | S_IWUSR,
		   syn_show_attr_latency, syn_store_attr_latency);

static struct attribute *syn_attrs[] = {
	&dev_attr_flash.attr,
	&dev_attr_product_id.attr,
	&dev_attr_product_family.attr,
	&dev_attr_firmware_version.attr,
	&dev_attr_selftest.attr,
	&dev_attr_reset.attr,
	&dev_attr_doze.attr,
	&dev_attr_sleepmode.attr,
	&dev_attr_proximity.attr,
	&dev_attr_sensitivity.attr,
	&dev_attr_sensormap.attr,
	&dev_attr_dump.attr,
	&dev_attr_debug.attr,
	&dev_attr_latency.attr,
	NULL,
};

static struct attribute_group syn_attr_group = {
	.attrs = syn_attrs,
};

static void syn_create_sysfs(struct syn *sd)
{
	int r;

	r = sysfs_create_group(&sd->client->dev.kobj, &syn_attr_group);
	if (r) {
		dev_err(&sd->client->dev,
			"failed to create flash sysfs files\n");
	}
}

static void syn_remove_sysfs(struct syn *sd)
{
	sysfs_remove_group(&sd->client->dev.kobj, &syn_attr_group);
}

static int syn_read_func_descs(struct syn *sd)
{
	int properties;
	int fun;
	int r;
	int addr = REG_PDT_PROPERTIES;

	sd->func_descs_valid = 0;
	sd->interrupt_sources = 0;

	properties = syn_read_u8(sd, addr--);
	if (properties < 0)
		return properties;

	/* We don't support non standard page select register addr */
	if (properties != 0x00) {
		dev_err(&sd->client->dev, "unsupported pdt\n");
		return -ENODEV;
	}

	sd->func_desc_num = 0;

	while (sd->func_desc_num < MAX_FUNC_DESCS) {
		struct func_desc * const f = &sd->func_desc[sd->func_desc_num];
		u8 regs[5];

		fun = syn_read_u8(sd, addr);
		if (fun < 0)
			return fun;

		if (fun == 0x00)
			goto out;

		if (sd->debug_flag & DFLAG_VERBOSE)
			dev_info(&sd->client->dev,
				 "found function 0x%x\n", fun);

		f->num = fun;
		addr -= 5;
		r = syn_read_block(sd, addr, regs, 5);
		if (r != 5) {
			dev_err(&sd->client->dev, "error reading pdt\n");
			return -ENODEV;
		}

		f->query = regs[0];
		f->command = regs[1];
		f->control = regs[2];
		f->data = regs[3];
		f->intr_sources = regs[4] & 0x07;
		f->version = (regs[4] >> 5) & 0x3;

		if (f->intr_sources) {
			f->intr_start_bit = sd->interrupt_sources;
			sd->interrupt_sources += f->intr_sources;
		}

		sd->func_desc_num++;
		addr--;
	}

out:
	/* Put shortcuts in place */
	sd->control = syn_get_func_desc(sd, FUNC_DEVICE_CONTROL);
	sd->flash   = syn_get_func_desc(sd, FUNC_FLASH);
	sd->touch   = syn_get_func_desc(sd, FUNC_2D);
	sd->buttons = syn_get_func_desc(sd, FUNC_BUTTONS);
	sd->bist    = syn_get_func_desc(sd, FUNC_BIST);
	sd->prox    = syn_get_func_desc(sd, FUNC_PROXIMITY);

	if (sd->touch == NULL)
		dev_warn(&sd->client->dev,
			 "no 2d functionality found! (in flash mode ?)\n");

	if (sd->flash) {
		r = syn_flash_query_caps(sd);
		if (r)
			return r;
	} else {
		dev_warn(&sd->client->dev, "no flash functionality found!\n");
	}

	if (sd->control) {
		sd->func_descs_valid = 1;
		return 0;
	}

	return -1;
}

static int syn_register_handlers(struct syn *sd)
{
	int r;

	r = syn_register_intr_handler(sd, FUNC_DEVICE_CONTROL,
				      syn_isr_device_control);
	if (r < 0) {
		dev_err(&sd->client->dev,
			"no device control, cant continue\n");
		return r;
	}

	/*
	 * Don't care about return values here.
	 * Install interrupt handlers for every function
	 * (even if they dont exist)
	 */

	syn_register_intr_handler(sd, FUNC_BIST, syn_isr_bist);
	syn_register_intr_handler(sd, FUNC_BUTTONS, syn_isr_buttons);
	syn_register_intr_handler(sd, FUNC_TIMER, syn_isr_timer);
	syn_register_intr_handler(sd, FUNC_2D, syn_isr_2d);
	syn_register_intr_handler(sd, FUNC_FLASH, syn_isr_flash);
	syn_register_intr_handler(sd, FUNC_PROXIMITY, syn_isr_proximity);

	return 0;
}

static int syn_touch_get_max_pos(struct syn *sd, u8 reg)
{
	int data;

	data = syn_read_u16(sd, sd->touch->control + reg);
	if (data < 0)
		return data;

	return data & 0x00000fff;
}

static int syn_touch_get_max_x_pos(struct syn *sd)
{
	return syn_touch_get_max_pos(sd, TOUCH_CONTROL_SENSOR_MAX_X);
}

static int syn_touch_get_max_y_pos(struct syn *sd)
{
	return syn_touch_get_max_pos(sd, TOUCH_CONTROL_SENSOR_MAX_Y);
}

static int syn_button_query_caps(struct syn *sd)
{
	int r;

	r = syn_read_u8(sd, sd->buttons->query + BUTTON_QUERY_BUTTON_COUNT);
	if (r < 0)
		return r;

	if (r > MAX_BUTTONS)
		return -EINVAL;

	sd->button_caps.button_count = r & BUTTON_QUERY_BUTTON_MASK;

	return 0;
}

static int syn_touch_query_caps(struct syn *sd)
{
	int r;
	u8 data[TOUCH_QUERY_LEN];

	if (sd->touch == NULL)
		return -ENODEV;

	r = syn_read_block(sd, sd->touch->query + TOUCH_QUERY_NUM_SENSORS,
			   data, TOUCH_QUERY_LEN);
	if (r < 0)
		return r;

	if (data[0] != 0) {
		dev_err(&sd->client->dev, "no support for multiple sensors\n");
		return -ENODEV;
	}

	sd->touch_caps.is_configurable = (data[1] & (1 << 7)) ? 1 : 0;
	sd->touch_caps.has_gestures    = (data[1] & (1 << 5)) ? 1 : 0;
	sd->touch_caps.has_abs_mode    = (data[1] & (1 << 4)) ? 1 : 0;
	sd->touch_caps.has_rel_mode    = (data[1] & (1 << 3)) ? 1 : 0;
	sd->touch_caps.finger_count    = (data[1] & 0x07) + 1;
	sd->touch_caps.x_electrodes    = (data[2] & 0x1f);
	sd->touch_caps.y_electrodes    = (data[3] & 0x1f);
	sd->touch_caps.max_electrodes  = (data[4] & 0x1f);
	sd->touch_caps.abs_data_size   = (data[5] & 0x03);

	if (sd->touch_caps.finger_count > MAX_TOUCH_POINTS ||
	    sd->touch_caps.has_rel_mode ||
	    /* sd->touch_caps.has_gestures || */
	    sd->touch_caps.abs_data_size) {
		dev_err(&sd->client->dev, "no support for this sensor\n");
		return -ENODEV;
	}

	r = syn_touch_get_max_x_pos(sd);
	if (r < 0)
		return r;

	sd->touch_caps.max_x = r;

	r = syn_touch_get_max_y_pos(sd);
	if (r < 0)
		return r;

	sd->touch_caps.max_y = r;

	return 0;
}

static int syn_reset_device(struct syn *sd)
{
	int r;

	/*
	 * We get called from various contexts, for example after firmware
	 * change. Resetting with state registermap would be disastrous
	 * so we reread everything in order to be sure to write
	 * to correct register.
	 */
	r = syn_read_func_descs(sd);

	if (!sd->control) {
		dev_err(&sd->client->dev, "no control. can't reset!\n");
		return -1;
	}

	r = syn_write_u8(sd, sd->control->control + DEVICE_CONTROL_INTR_ENABLE,
			 0);

	r = syn_control_data_read(sd, DEVICE_CONTROL_DATA_INTR_STATUS);

	sd->func_descs_valid = 0;

	r = syn_write_u8(sd, sd->control->command +
			 DEVICE_CONTROL_COMMAND, DEVICE_COMMAND_RESET);

	r = syn_wait_for_attn(sd, 100 * 1000, 0);
	r = syn_wait_for_attn(sd, 100 * 1000, 1);
	r = syn_wait_for_attn(sd, 500 * 1000, 0);

	return 0;
}

static int syn_regulators_on(struct syn *sd)
{
	int r;

	r = regulator_bulk_enable(ARRAY_SIZE(sd->regs), sd->regs);

	return r;
}

static void syn_regulators_off(struct syn *sd)
{
	regulator_bulk_disable(ARRAY_SIZE(sd->regs), sd->regs);
}

static int syn_initialize(struct syn *sd)
{
	int r;
	char prod_id[PRODUCT_ID_LEN + 1];
	int prod_family;
	int prod_fw_version;

	syn_clear_device_state(sd);

	r = syn_read_u8(sd, REG_PAGE_SELECT);
	if (r < 0) {
		dev_err(&sd->client->dev, "error reading page select\n");
		goto err_out;
	}

	if (r != 0x00) {
		dev_err(&sd->client->dev, "page select non zero\n");
		r = -ENODEV;
		goto err_out;
	}

	r = syn_read_func_descs(sd);
	if (r < 0) {
		dev_err(&sd->client->dev, "error reading func descs\n");
		goto err_out;
	}

	if (!sd->control) {
		dev_err(&sd->client->dev, "no control functions found\n");
		r = -ENODEV;
		goto err_out;
	}

	/* Clear the Unconfigured bit */
	r = syn_write_u8(sd, sd->control->control + DEVICE_CONTROL_CTRL,
			 DEVICE_CONTROL_CONFIGURED);

	if (!sd->bist)
		dev_warn(&sd->client->dev, "no bist capabilities found\n");

	if (sd->touch) {
		r = syn_touch_query_caps(sd);
		if (r < 0) {
			dev_err(&sd->client->dev,
				"error reading touch capabilities\n");
			sd->touch = NULL;
		}
	} else {
		dev_err(&sd->client->dev, "no touch capabilities found\n");
	}

	if (sd->buttons) {
		r = syn_button_query_caps(sd);
		if (r < 0) {
			dev_err(&sd->client->dev,
				"error reading button capabilities\n");
			sd->buttons = NULL;
		}
	}

	r = syn_read_block(sd, sd->control->query +
			   DEVICE_CONTROL_QUERY_PROD_ID,
			   prod_id, PRODUCT_ID_LEN);

	if (r != PRODUCT_ID_LEN) {
		dev_err(&sd->client->dev, "unable to read product id\n");
		r = -ENODEV;
		goto err_out;
	}
	prod_id[r] = 0;

	prod_family = syn_control_query_read(sd,
					     DEVICE_CONTROL_QUERY_PROD_FAMILY);
	if (prod_family < 0) {
		dev_err(&sd->client->dev, "unable to read product family\n");
		goto err_out;
	}

	prod_fw_version = syn_control_query_read(sd,
						 DEVICE_CONTROL_QUERY_FW_VER);
	if (prod_fw_version < 0) {
		dev_err(&sd->client->dev, "unable to read product family\n");
		goto err_out;
	}

	printk(KERN_INFO DRIVER_NAME ": product ID: %s family:%d fw:%d\n",
	       prod_id, prod_family, prod_fw_version);

	r = syn_register_handlers(sd);
	if (r) {
		dev_err(&sd->client->dev, "failed to register_handlers\n");
		r = -ENODEV;
		goto err_out;
	}

	if (sd->touch) {
		r = syn_register_input_device(sd);
		if (r) {
			dev_err(&sd->client->dev,
				"failed to register input devices\n");
			r = -ENODEV;
			goto err_out;
		}
	}

	if (sd->prox) {
		/* By default disable it */
		syn_set_proximity_state(sd, 0);
	}

	r = syn_control_data_read(sd, DEVICE_CONTROL_DATA_STATUS);
	if (r < 0) {
		dev_err(&sd->client->dev, "error %d reading device status\n",
			r);
	}

	/*
	 * If we get reset during initialization, unconfigured should be on
	 */
	if (r & (1 << 7)) {
		dev_warn(&sd->client->dev, "lost config during initialize\n");
		sd->failed_inits++;
		syn_reset_device(sd);
		goto err_out;
	}

	sd->failed_inits = 0;
	return 0;

err_out:
	return r;
}

static int syn_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct syn *sd;
	struct syntm12xx_platform_data *pdata;
	int r;
	int man_id;

	sd = kzalloc(sizeof(struct syn), GFP_KERNEL);
	if (sd == NULL)
		return -ENOMEM;

	INIT_WORK(&sd->isr_work, syn_isr_work);
	i2c_set_clientdata(client, sd);
	sd->client = client;

	mutex_init(&sd->lock);

	sd->t_work_min = ULONG_MAX;
	sd->t_wakeup_min = ULONG_MAX;

	sd->wq = create_singlethread_workqueue("tm12xx_wq");
	if (!sd->wq) {
		r = -ENOMEM;
		goto err_free_dev;
	}

	pdata = sd->client->dev.platform_data;
	if (!pdata) {
		dev_err(&client->dev, "no platform data found\n");
		r = -ENODEV;
		goto err_free_dev;
	}

	sd->gpio_intr = pdata->gpio_intr;

	sd->regs[0].supply = reg_vdd;
	sd->regs[1].supply = reg_vddio;

	r = regulator_bulk_get(&client->dev,
			       ARRAY_SIZE(sd->regs), sd->regs);
	if (r < 0) {
		dev_err(&client->dev, "cannot get regulators\n");
		goto err_free_dev;
	}

	r = syn_regulators_on(sd);
	if (r < 0) {
		dev_err(&client->dev, "cannot enable regulators\n");
		goto err_free_regs;
	}

	/* Wait for regulators to stabilize */
	msleep(50);

	r = syn_read_func_descs(sd);
	if (r < 0) {
		dev_err(&client->dev, "error reading func descs\n");
		goto err_disable_regs;
	}

	r = syn_write_u8(sd, sd->control->control + DEVICE_CONTROL_INTR_ENABLE,
			 0);
	if (r < 0)
		goto err_disable_regs;

	r = syn_control_data_read(sd, DEVICE_CONTROL_DATA_INTR_STATUS);
	if (r < 0)
		goto err_disable_regs;

	man_id = syn_control_query_read(sd, DEVICE_CONTROL_QUERY_MANID);
	if (man_id < 0) {
		dev_dbg(&client->dev, "unable to get manufacturer id\n");
		r = -ENODEV;
		goto err_disable_regs;
	}

	printk(KERN_INFO DRIVER_NAME ": " DRIVER_DESC
	       " found man id %x (%d)\n", man_id, man_id);

	r = gpio_request(sd->gpio_intr, "Synaptic TM12XX Interrupt");
	if (r < 0) {
		dev_dbg(&client->dev, "unable to get INT GPIO\n");
		r = -ENODEV;
		goto err_disable_regs;
	}

	gpio_direction_input(sd->gpio_intr);

	r = syn_register_intr_handler(sd, FUNC_DEVICE_CONTROL,
				      syn_isr_device_control);
	if (r < 0) {
		dev_err(&sd->client->dev,
			"no device control, can't continue\n");
		r = -ENODEV;
		goto err_free_int_gpio;
	}

	mutex_lock(&sd->lock);

	r = request_irq(gpio_to_irq(sd->gpio_intr), syn_isr,
			IRQF_DISABLED | IRQF_TRIGGER_LOW | IRQF_TRIGGER_FALLING,
			DRIVER_NAME, sd);
	if (r) {
		dev_dbg(&client->dev,  "can't get IRQ %d (%d), err %d\n",
			gpio_to_irq(sd->gpio_intr), sd->gpio_intr, r);
		goto err_release_mutex;
	}

	syn_create_sysfs(sd);

	/* Initialize thru reset */
	r = syn_reset_device(sd);
	if (r < 0) {
		dev_err(&client->dev, "error in reset device\n");
		goto err_free_irq;
	}

	mutex_unlock(&sd->lock);

	return 0;

err_free_irq:
	free_irq(gpio_to_irq(sd->gpio_intr), sd);

err_release_mutex:
	mutex_unlock(&sd->lock);

err_free_int_gpio:
	gpio_free(sd->gpio_intr);

err_disable_regs:
	syn_regulators_off(sd);

err_free_regs:
	regulator_bulk_free(ARRAY_SIZE(sd->regs), sd->regs);

err_free_dev:
	kfree(sd);
	sd = NULL;
	return r;
}

static int __exit syn_remove(struct i2c_client *client)
{
	struct syn *sd = i2c_get_clientdata(client);

	mutex_lock(&sd->lock);

	if (sd->control)
		syn_write_u8(sd,
			     sd->control->control + DEVICE_CONTROL_INTR_ENABLE,
			     0);

	free_irq(gpio_to_irq(sd->gpio_intr), sd);

	mutex_unlock(&sd->lock);

	destroy_workqueue(sd->wq);
	sd->wq = NULL;

	syn_remove_sysfs(sd);

	if (sd->idev) {
		input_unregister_device(sd->idev);
		sd->idev = NULL;
	}

	gpio_free(sd->gpio_intr);
	syn_regulators_off(sd);
	regulator_bulk_free(ARRAY_SIZE(sd->regs), sd->regs);

	kfree(sd);
	i2c_set_clientdata(client, NULL);

	return 0;
}

static void syn_shutdown(struct i2c_client *client)
{
	struct syn *sd = i2c_get_clientdata(client);

	mutex_lock(&sd->lock);

	syn_write_u8(sd, sd->control->control + DEVICE_CONTROL_CTRL,
			 DEVICE_CONTROL_SLEEP_SENSOR);

	mutex_unlock(&sd->lock);
}

#ifdef CONFIG_PM
static int syn_suspend(struct i2c_client *client, pm_message_t msg)
{
	struct syn *sd = i2c_get_clientdata(client);
	int r;

	mutex_lock(&sd->lock);
	r = syn_read_u8(sd, sd->control->control + DEVICE_CONTROL_CTRL);
	/* If we fail to get previous finetuned power mode, we don't care */
	mutex_unlock(&sd->lock);
	if (r >= 0)
		sd->device_control_ctrl = r & 0xff;
	else
		sd->device_control_ctrl = DEVICE_CONTROL_SLEEP_NORMAL;

	syn_shutdown(client);

	return 0;
}

static int syn_resume(struct i2c_client *client)
{
	struct syn *sd = i2c_get_clientdata(client);
	int r;

	mutex_lock(&sd->lock);
	r = syn_write_u8(sd, sd->control->control + DEVICE_CONTROL_CTRL,
			 sd->device_control_ctrl);
	if (r < 0)
		dev_err(&sd->client->dev, "error %d restoring device state\n",
			r);
	mutex_unlock(&sd->lock);

	return 0;
}

#endif


static const struct i2c_device_id syn_id[] = {
	{ DRIVER_NAME, 0 },
	{ }
};

static struct i2c_driver syn_i2c_driver = {
	.driver = {
		.name           = DRIVER_NAME,
	},
	.probe          = syn_probe,
	.remove         = __exit_p(syn_remove),
	.id_table       = syn_id,
	.shutdown       = syn_shutdown,

#ifdef CONFIG_PM
	.suspend        = syn_suspend,
	.resume         = syn_resume,
#endif
};

static int __init syn_init(void)
{
	int r;

	r = i2c_add_driver(&syn_i2c_driver);
	if (r < 0) {
		printk(KERN_WARNING DRIVER_NAME
		       " driver registration failed\n");
		return r;
	}

	return 0;
}

static void __exit syn_exit(void)
{
	i2c_del_driver(&syn_i2c_driver);
}

module_init(syn_init);
module_exit(syn_exit);

MODULE_AUTHOR("Mika Kuoppala <mika.kuoppala@nokia.com>");
MODULE_DESCRIPTION("Synaptic TM12xx Touch controller driver");
MODULE_LICENSE("GPL");
